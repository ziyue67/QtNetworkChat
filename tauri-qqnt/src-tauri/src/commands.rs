use std::sync::Arc;

use serde::Serialize;
use serde_json::{json, Value};
use tauri::State;

use crate::bridge;
use crate::error::QQNTError;
use crate::protocol;
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
pub async fn engine_ready(
    state: State<'_, Arc<AppState>>,
    req_id: String,
) -> Result<Value, QQNTError> {
    let payload = call_engine_payload(state.inner(), "ready", req_id, json!({})).await?;
    protocol::validate_ready_payload(&payload)?;
    Ok(payload)
}

#[tauri::command]
pub async fn connect_server(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    host: String,
    port: u16,
) -> Result<ConnectServerResponse, QQNTError> {
    let payload = call_engine_payload(
        state.inner(),
        "connect",
        req_id,
        json!({
            "host": host,
            "port": port
        }),
    )
    .await?;

    Ok(ConnectServerResponse {
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
    login_like(state.inner(), "login", req_id, account, password, None).await
}

#[tauri::command]
pub async fn register_account(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    account: String,
    password: String,
    user_name: String,
) -> Result<LoginResponse, QQNTError> {
    login_like(
        state.inner(),
        "register",
        req_id,
        account,
        password,
        Some(user_name),
    )
    .await
}

#[tauri::command]
pub async fn disconnect_server(
    state: State<'_, Arc<AppState>>,
    req_id: String,
) -> Result<Value, QQNTError> {
    call_engine_payload(state.inner(), "disconnect", req_id, json!({})).await
}

#[tauri::command]
pub async fn logout(state: State<'_, Arc<AppState>>, req_id: String) -> Result<Value, QQNTError> {
    call_engine_payload(state.inner(), "logout", req_id, json!({})).await
}

#[tauri::command]
pub async fn set_user_info(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    user_id: String,
    user_name: String,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "set_user_info",
        req_id,
        json!({
            "userId": user_id,
            "userName": user_name
        }),
    )
    .await
}

#[tauri::command]
pub async fn get_user_list(
    state: State<'_, Arc<AppState>>,
    req_id: String,
) -> Result<Value, QQNTError> {
    call_engine_payload(state.inner(), "get_user_list", req_id, json!({})).await
}

#[tauri::command]
pub async fn get_friend_list(
    state: State<'_, Arc<AppState>>,
    req_id: String,
) -> Result<Value, QQNTError> {
    call_engine_payload(state.inner(), "get_friend_list", req_id, json!({})).await
}

#[tauri::command]
pub async fn get_group_list(
    state: State<'_, Arc<AppState>>,
    req_id: String,
) -> Result<Value, QQNTError> {
    call_engine_payload(state.inner(), "get_group_list", req_id, json!({})).await
}

#[tauri::command]
pub async fn search_friend(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    account: String,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "search_friend",
        req_id,
        json!({ "account": account }),
    )
    .await
}

#[tauri::command]
pub async fn send_friend_request(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    receiver_id: String,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "send_friend_request",
        req_id,
        json!({ "receiverId": receiver_id }),
    )
    .await
}

#[tauri::command]
pub async fn respond_friend_request(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    sender_id: String,
    accepted: bool,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "respond_friend_request",
        req_id,
        json!({
            "senderId": sender_id,
            "accepted": accepted
        }),
    )
    .await
}

#[tauri::command]
pub async fn send_private_message(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    receiver_id: String,
    content: String,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "send_private_message",
        req_id,
        json!({
            "receiverId": receiver_id,
            "content": content
        }),
    )
    .await
}

#[tauri::command]
pub async fn send_group_message(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    group_id: String,
    content: String,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "send_group_message",
        req_id,
        json!({
            "groupId": group_id,
            "content": content
        }),
    )
    .await
}

#[tauri::command]
pub async fn create_group(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    group_name: String,
    members: Option<Vec<String>>,
    announcement: Option<String>,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "create_group",
        req_id,
        json!({
            "groupName": group_name,
            "members": members,
            "announcement": announcement
        }),
    )
    .await
}

#[tauri::command]
pub async fn update_group_announcement(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    group_id: String,
    announcement: String,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "update_group_announcement",
        req_id,
        json!({
            "groupId": group_id,
            "announcement": announcement
        }),
    )
    .await
}

#[tauri::command]
pub async fn update_group_member(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    group_id: String,
    member_id: String,
    action: String,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "update_group_member",
        req_id,
        json!({
            "groupId": group_id,
            "memberId": member_id,
            "action": action
        }),
    )
    .await
}

#[tauri::command]
pub async fn send_file(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    file_path: String,
    receiver_id: Option<String>,
    group_id: Option<String>,
) -> Result<Value, QQNTError> {
    send_file_like(
        state.inner(),
        "send_file",
        req_id,
        file_path,
        receiver_id,
        group_id,
    )
    .await
}

#[tauri::command]
pub async fn send_image(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    file_path: String,
    receiver_id: Option<String>,
    group_id: Option<String>,
) -> Result<Value, QQNTError> {
    send_file_like(
        state.inner(),
        "send_image",
        req_id,
        file_path,
        receiver_id,
        group_id,
    )
    .await
}

#[tauri::command]
pub async fn cancel_transfer(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    transfer_id: String,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "cancel_transfer",
        req_id,
        cancel_transfer_payload(transfer_id),
    )
    .await
}

#[tauri::command]
pub async fn query_resume(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    transfer_id: String,
    file_path: Option<String>,
    receiver_id: Option<String>,
    content_type: Option<String>,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "query_resume",
        req_id,
        query_resume_payload(transfer_id, file_path, receiver_id, content_type),
    )
    .await
}

#[tauri::command]
pub async fn e2e_status(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    peer_id: Option<String>,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "e2e_status",
        req_id,
        json!({ "peerId": peer_id }),
    )
    .await
}

#[tauri::command]
pub async fn e2e_announce_identity(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    peer_id: String,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "e2e_announce_identity",
        req_id,
        json!({ "peerId": peer_id }),
    )
    .await
}

#[tauri::command]
pub async fn e2e_pin_identity(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    peer_id: String,
    fingerprint: Option<String>,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "e2e_pin_identity",
        req_id,
        json!({
            "peerId": peer_id,
            "fingerprint": fingerprint
        }),
    )
    .await
}

#[tauri::command]
pub async fn e2e_request_rotation(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    peer_id: String,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "e2e_request_rotation",
        req_id,
        json!({ "peerId": peer_id }),
    )
    .await
}

#[tauri::command]
pub async fn profile_update(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    user_name: Option<String>,
    avatar_base64: Option<String>,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "profile_update",
        req_id,
        json!({
            "userName": user_name,
            "avatarBase64": avatar_base64
        }),
    )
    .await
}

#[tauri::command]
pub async fn settings_sync(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    settings: Value,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "settings_sync",
        req_id,
        json!({ "settings": settings }),
    )
    .await
}

async fn login_like(
    state: &Arc<AppState>,
    op: &str,
    req_id: String,
    account: String,
    password: String,
    user_name: Option<String>,
) -> Result<LoginResponse, QQNTError> {
    let payload = call_engine_payload(
        state,
        op,
        req_id,
        json!({
            "account": account,
            "password": password,
            "userName": user_name
        }),
    )
    .await?;

    Ok(LoginResponse {
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
            .unwrap_or(op)
            .to_string(),
    })
}

async fn send_file_like(
    state: &Arc<AppState>,
    op: &str,
    req_id: String,
    file_path: String,
    receiver_id: Option<String>,
    group_id: Option<String>,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state,
        op,
        req_id,
        json!({
            "filePath": file_path,
            "receiverId": receiver_id,
            "groupId": group_id
        }),
    )
    .await
}

fn cancel_transfer_payload(transfer_id: String) -> Value {
    json!({ "transferId": transfer_id })
}

fn query_resume_payload(
    transfer_id: String,
    file_path: Option<String>,
    receiver_id: Option<String>,
    content_type: Option<String>,
) -> Value {
    json!({
        "transferId": transfer_id,
        "filePath": file_path,
        "receiverId": receiver_id,
        "contentType": content_type
    })
}

async fn call_engine_payload(
    state: &Arc<AppState>,
    op: &str,
    req_id: String,
    payload: Value,
) -> Result<Value, QQNTError> {
    let packet = bridge::call_engine(state, command_packet(op, req_id, payload)).await?;
    ack_payload(packet, op)
}

fn command_packet(op: &str, req_id: String, payload: Value) -> Value {
    json!({
        "op": op,
        "reqId": req_id,
        "payload": compact_payload(payload)
    })
}

fn compact_payload(mut payload: Value) -> Value {
    if let Value::Object(map) = &mut payload {
        map.retain(|_, value| !value.is_null());
    }
    payload
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

    const TYPED_COMMAND_OPS: &[&str] = &[
        "ready",
        "connect",
        "disconnect",
        "login",
        "register",
        "logout",
        "set_user_info",
        "get_user_list",
        "get_friend_list",
        "get_group_list",
        "search_friend",
        "send_friend_request",
        "respond_friend_request",
        "send_private_message",
        "send_group_message",
        "create_group",
        "update_group_announcement",
        "update_group_member",
        "send_file",
        "send_image",
        "cancel_transfer",
        "query_resume",
        "e2e_status",
        "e2e_announce_identity",
        "e2e_pin_identity",
        "e2e_request_rotation",
        "profile_update",
        "settings_sync",
    ];

    fn protocol_contract() -> Value {
        serde_json::from_str(include_str!(
            "../../../tests/fixtures/protocol_contract.json"
        ))
        .expect("protocol contract fixture should parse")
    }

    fn string_array<'a>(value: &'a Value, key: &str) -> Vec<&'a str> {
        value[key]
            .as_array()
            .expect("protocol contract key should be an array")
            .iter()
            .map(|item| {
                item.as_str()
                    .expect("protocol contract value should be a string")
            })
            .collect()
    }

    #[test]
    fn typed_command_wrappers_cover_protocol_contract() {
        let contract = protocol_contract();
        let mut expected = string_array(&contract, "commands");
        let mut actual = TYPED_COMMAND_OPS.to_vec();
        expected.sort_unstable();
        actual.sort_unstable();

        assert_eq!(actual, expected);
    }

    #[test]
    fn command_packet_keeps_protocol_field_names() {
        let packet = command_packet(
            "send_private_message",
            "req-1".to_string(),
            json!({
                "receiverId": "10001",
                "content": "hello"
            }),
        );

        assert_eq!(packet["op"], "send_private_message");
        assert_eq!(packet["reqId"], "req-1");
        assert_eq!(packet["payload"]["receiverId"], "10001");
        assert_eq!(packet["payload"]["content"], "hello");
    }

    #[test]
    fn command_packet_omits_null_optional_fields() {
        let packet = command_packet(
            "send_file",
            "req-2".to_string(),
            json!({
                "filePath": "C:/tmp/a.txt",
                "receiverId": "10001",
                "groupId": null
            }),
        );

        assert_eq!(packet["payload"]["filePath"], "C:/tmp/a.txt");
        assert_eq!(packet["payload"]["receiverId"], "10001");
        assert!(packet["payload"].get("groupId").is_none());
    }

    #[test]
    fn cancel_transfer_payload_requires_transfer_id() {
        let payload = cancel_transfer_payload("transfer-1".to_string());

        assert_eq!(payload["transferId"], "transfer-1");
    }

    #[test]
    fn query_resume_payload_preserves_content_type() {
        let packet = command_packet(
            "query_resume",
            "req-3".to_string(),
            query_resume_payload(
                "transfer-1".to_string(),
                Some("C:/tmp/a.png".to_string()),
                None,
                Some("image".to_string()),
            ),
        );

        assert_eq!(packet["payload"]["transferId"], "transfer-1");
        assert_eq!(packet["payload"]["filePath"], "C:/tmp/a.png");
        assert_eq!(packet["payload"]["contentType"], "image");
        assert!(packet["payload"].get("receiverId").is_none());
    }

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

    #[test]
    fn ack_payload_rejects_mismatched_op() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "login",
                "reqId": "req-3",
                "status": "ok",
                "payload": {}
            }),
            "connect",
        )
        .expect_err("mismatched ack op should fail");

        assert_eq!(error.code, "unexpected_ack");
    }
}
