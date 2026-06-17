use std::sync::Arc;
use std::time::Duration;

use serde_json::{json, Value};
use tauri::{AppHandle, Emitter};
use tauri_plugin_shell::process::CommandEvent;
use tokio::sync::oneshot;

use crate::error::{QQNTError, QQNTResult};
use crate::state::AppState;

const ENGINE_TIMEOUT_SECONDS: u64 = 30;

pub async fn call_engine(state: &Arc<AppState>, command: Value) -> QQNTResult<Value> {
    let req_id = command
        .get("reqId")
        .and_then(Value::as_str)
        .filter(|value| !value.trim().is_empty())
        .ok_or_else(|| QQNTError::rust("missing_req_id", "Command reqId is required."))?
        .to_string();

    let mut line = serde_json::to_vec(&command)
        .map_err(|error| QQNTError::rust("invalid_command", error.to_string()))?;
    line.push(b'\n');

    let (sender, receiver) = oneshot::channel();
    state
        .engine
        .pending
        .lock()
        .await
        .insert(req_id.clone(), sender);

    let write_result = {
        let mut child_guard = state.engine.child.lock().await;
        match child_guard.as_mut() {
            Some(child) => child
                .write(&line)
                .map_err(|error| QQNTError::rust("engine_write_failed", error.to_string())),
            None => Err(QQNTError::rust(
                "engine_not_running",
                "QQNTEngine sidecar is not running.",
            )),
        }
    };

    if let Err(error) = write_result {
        state.engine.pending.lock().await.remove(&req_id);
        return Err(error);
    }

    tokio::time::timeout(Duration::from_secs(ENGINE_TIMEOUT_SECONDS), receiver)
        .await
        .map_err(|_| QQNTError::rust("engine_timeout", "QQNTEngine did not ack before timeout."))?
        .map_err(|_| QQNTError::rust("engine_channel_closed", "QQNTEngine ack channel closed."))
}

pub async fn handle_engine_event(app: &AppHandle, state: &Arc<AppState>, event: CommandEvent) {
    match event {
        CommandEvent::Stdout(line) => handle_stdout_line(app, state, line).await,
        CommandEvent::Stderr(line) => {
            let message = String::from_utf8_lossy(&line).trim().to_string();
            if !message.is_empty() {
                let _ = app.emit("qqnt://engine/log", json!({ "message": message }));
            }
        }
        CommandEvent::Error(message) => {
            let error = QQNTError::rust("engine_process_error", message);
            state.engine.set_last_error(error.clone()).await;
            let _ = app.emit("qqnt://engine/error", error);
        }
        CommandEvent::Terminated(payload) => {
            *state.engine.child.lock().await = None;
            let _ = app.emit(
                "qqnt://engine/error",
                json!({
                    "code": "engine_terminated",
                    "message": "QQNTEngine sidecar terminated.",
                    "source": "rust",
                    "codeValue": payload.code,
                    "signal": payload.signal
                }),
            );
        }
        _ => {}
    }
}

async fn handle_stdout_line(app: &AppHandle, state: &Arc<AppState>, line: Vec<u8>) {
    let trimmed = String::from_utf8_lossy(&line).trim().to_string();
    if trimmed.is_empty() {
        return;
    }

    let packet: Value = match serde_json::from_str(&trimmed) {
        Ok(value) => value,
        Err(error) => {
            let _ = app.emit(
                "qqnt://engine/error",
                json!({
                    "code": "invalid_engine_json",
                    "message": error.to_string(),
                    "source": "rust"
                }),
            );
            return;
        }
    };

    match packet.get("type").and_then(Value::as_str) {
        Some("ack") => handle_ack(state, packet).await,
        Some("event") => emit_engine_event(app, packet),
        _ => {
            let _ = app.emit(
                "qqnt://engine/error",
                json!({
                    "code": "unknown_engine_packet",
                    "message": "QQNTEngine emitted an unsupported packet envelope.",
                    "source": "rust"
                }),
            );
        }
    }
}

async fn handle_ack(state: &Arc<AppState>, packet: Value) {
    if let Some(req_id) = packet.get("reqId").and_then(Value::as_str) {
        if let Some(sender) = state.engine.pending.lock().await.remove(req_id) {
            let _ = sender.send(packet);
        }
    }
}

fn emit_engine_event(app: &AppHandle, packet: Value) {
    let Some(event_name) = packet.get("event").and_then(Value::as_str) else {
        let _ = app.emit(
            "qqnt://engine/error",
            json!({
                "code": "missing_event_name",
                "message": "QQNTEngine event packet did not include event.",
                "source": "rust"
            }),
        );
        return;
    };

    let payload = packet.get("payload").cloned().unwrap_or_else(|| json!({}));
    let topic = format!("qqnt://engine/{event_name}");
    let _ = app.emit(topic.as_str(), payload);
}
