use std::sync::Arc;

use serde::Serialize;
use serde_json::{json, Value};
use tauri::State;

use crate::bridge;
use crate::error::QQNTError;
use crate::state::AppState;

#[tauri::command]
pub async fn qqnt_command(
    state: State<'_, Arc<AppState>>,
    payload: Value,
) -> Result<Value, QQNTError> {
    bridge::call_engine(state.inner(), payload).await
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ConnectServerResponse {
    pub connected: bool,
    pub host: String,
    pub port: u16,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LoginResponse {
    pub accepted: bool,
    pub requires_connect: bool,
    pub mode: String,
}

#[tauri::command]
pub async fn connect_server(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    host: String,
    port: u16,
) -> Result<ConnectServerResponse, QQNTError> {
    let packet = bridge::call_engine(
        state.inner(),
        json!({
            "op": "connect",
            "reqId": req_id,
            "payload": {
                "host": host,
                "port": port
            }
        }),
    )
    .await?;
    ack_payload(packet, "connect").map(|payload| ConnectServerResponse {
        connected: payload
            .get("connected")
            .and_then(Value::as_bool)
            .unwrap_or(false),
        host: payload
            .get("host")
            .and_then(Value::as_str)
            .unwrap_or_default()
            .to_string(),
        port: payload
            .get("port")
            .and_then(Value::as_u64)
            .and_then(|value| u16::try_from(value).ok())
            .unwrap_or(0),
    })
}

#[tauri::command]
pub async fn login(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    account: String,
    password: String,
) -> Result<LoginResponse, QQNTError> {
    let packet = bridge::call_engine(
        state.inner(),
        json!({
            "op": "login",
            "reqId": req_id,
            "payload": {
                "account": account,
                "password": password
            }
        }),
    )
    .await?;
    ack_payload(packet, "login").map(|payload| LoginResponse {
        accepted: payload
            .get("accepted")
            .and_then(Value::as_bool)
            .unwrap_or(false),
        requires_connect: payload
            .get("requiresConnect")
            .and_then(Value::as_bool)
            .unwrap_or(true),
        mode: payload
            .get("mode")
            .and_then(Value::as_str)
            .unwrap_or("login")
            .to_string(),
    })
}

fn ack_payload(packet: Value, op: &str) -> Result<Value, QQNTError> {
    if packet.get("status").and_then(Value::as_str) == Some("error") {
        let error = packet.get("error").cloned().unwrap_or_else(|| json!({}));
        return Err(QQNTError::new(
            error
                .get("code")
                .and_then(Value::as_str)
                .unwrap_or("engine_error"),
            error
                .get("message")
                .and_then(Value::as_str)
                .unwrap_or("QQNTEngine command failed."),
            error
                .get("source")
                .and_then(Value::as_str)
                .unwrap_or("engine"),
        ));
    }

    if packet.get("type").and_then(Value::as_str) != Some("ack")
        || packet.get("op").and_then(Value::as_str) != Some(op)
    {
        return Err(QQNTError::rust(
            "unexpected_ack",
            format!("QQNTEngine returned an unexpected ack for {op}."),
        ));
    }

    Ok(packet.get("payload").cloned().unwrap_or_else(|| json!({})))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ack_payload_returns_payload_for_matching_ok_ack() {
        let payload = ack_payload(
            json!({
                "type": "ack",
                "op": "connect",
                "reqId": "req-1",
                "status": "ok",
                "payload": { "connected": true, "host": "127.0.0.1", "port": 8888 }
            }),
            "connect",
        )
        .expect("matching ack should return payload");

        assert_eq!(payload["connected"], true);
        assert_eq!(payload["port"], 8888);
    }

    #[test]
    fn ack_payload_maps_engine_error_ack() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "login",
                "reqId": "req-2",
                "status": "error",
                "error": { "code": "login_failed", "message": "bad password", "source": "engine" }
            }),
            "login",
        )
        .expect_err("engine error ack should map to QQNTError");

        assert_eq!(error.code, "login_failed");
        assert_eq!(error.source, "engine");
    }
}
