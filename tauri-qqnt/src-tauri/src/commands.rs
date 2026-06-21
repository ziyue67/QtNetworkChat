use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::{SystemTime, UNIX_EPOCH};

use base64::{engine::general_purpose, Engine as _};

use serde::{Deserialize, Serialize};
use serde_json::{json, Value};
use tauri::{AppHandle, Manager, State};

use crate::bridge;
use crate::error::QQNTError;
use crate::protocol;
use crate::state::AppState;

#[tauri::command]
pub async fn qqnt_command(
    state: State<'_, Arc<AppState>>,
    payload: Value,
) -> Result<Value, QQNTError> {
    validate_generic_command(&payload)?;
    let op = payload
        .get("op")
        .and_then(Value::as_str)
        .unwrap_or_default()
        .to_string();
    let req_id = payload
        .get("reqId")
        .and_then(Value::as_str)
        .unwrap_or_default()
        .to_string();

    let packet = bridge::call_engine(state.inner(), payload).await?;
    validate_generic_ack_packet(&packet, &op, &req_id)?;

    Ok(packet)
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

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ImageBase64Response {
    pub base64: String,
    pub data_url: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SavedLocalFileResponse {
    pub file_path: String,
    pub file_name: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RestoreMainWindowResponse {
    pub restored: bool,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HideMainWindowResponse {
    pub hidden: bool,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ScreenshotResponse {
    pub file_path: String,
    pub file_name: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub capture_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub x: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub y: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub width: Option<u32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub height: Option<u32>,
}

#[tauri::command]
pub async fn restore_main_window(
    app_handle: AppHandle,
) -> Result<RestoreMainWindowResponse, QQNTError> {
    let Some(main_window) = app_handle.get_webview_window("main") else {
        return Ok(RestoreMainWindowResponse { restored: false });
    };

    let _ = set_window_capture_exclusion(main_window.clone(), false);
    let _ = set_window_alpha(main_window.clone(), 255);
    let _ = main_window.show();
    let _ = main_window.unminimize();
    let _ = main_window.set_focus();
    Ok(RestoreMainWindowResponse { restored: true })
}

#[tauri::command]
pub async fn hide_main_window(app_handle: AppHandle) -> Result<HideMainWindowResponse, QQNTError> {
    let Some(main_window) = app_handle.get_webview_window("main") else {
        return Ok(HideMainWindowResponse { hidden: false });
    };

    let _ = set_window_capture_exclusion(main_window.clone(), true);
    let _ = set_window_alpha(main_window.clone(), 1);
    let _ = main_window.minimize();
    Ok(HideMainWindowResponse { hidden: true })
}

#[derive(Debug, Serialize, Clone, Copy)]
#[serde(rename_all = "camelCase")]
pub struct ScreenshotMonitorInfo {
    pub x: i32,
    pub y: i32,
    pub width: u32,
    pub height: u32,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LocalActionResponse {
    pub saved: bool,
    pub id: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ClearSessionHistoryResponse {
    pub cleared: bool,
    pub session_id: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeleteLocalMessageResponse {
    pub deleted: bool,
    pub session_id: String,
    pub message_id: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LocalChatActionsResponse {
    pub cleared_sessions: Vec<ClearedLocalSession>,
    pub deleted_messages: Vec<DeletedLocalMessage>,
    pub favorite_messages: Vec<LocalMessageAction>,
    pub emoji_messages: Vec<LocalMessageAction>,
    pub selected_messages: Vec<LocalMessageAction>,
    pub quote_messages: Vec<LocalMessageAction>,
    pub essence_messages: Vec<LocalMessageAction>,
    pub recalled_messages: Vec<LocalMessageAction>,
    pub member_actions: Vec<LocalMemberAction>,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ClearedLocalSession {
    pub session_id: String,
    pub cleared_at: u128,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeletedLocalMessage {
    pub session_id: String,
    pub message_id: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LocalMessageAction {
    pub session_id: String,
    pub message_id: String,
    pub saved_at: u128,
    pub value: Value,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LocalMemberAction {
    pub kind: String,
    pub member_id: String,
    pub session_id: Option<String>,
    pub saved_at: u128,
    pub value: Value,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ScreenshotCropPayload {
    #[serde(default)]
    pub source_path: Option<String>,
    #[serde(default)]
    pub capture_id: Option<String>,
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
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
        connect_payload(host, json!(port))?,
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
    let payload = set_user_info_payload(user_id, user_name)?;

    call_engine_payload(state.inner(), "set_user_info", req_id, payload).await
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
    let payload = search_friend_payload(account)?;

    call_engine_payload(state.inner(), "search_friend", req_id, payload).await
}

#[tauri::command]
pub async fn send_friend_request(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    receiver_id: String,
) -> Result<Value, QQNTError> {
    let payload = send_friend_request_payload(receiver_id)?;

    call_engine_payload(state.inner(), "send_friend_request", req_id, payload).await
}

#[tauri::command]
pub async fn respond_friend_request(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    sender_id: String,
    accepted: bool,
) -> Result<Value, QQNTError> {
    let payload = respond_friend_request_payload(sender_id, json!(accepted))?;

    call_engine_payload(state.inner(), "respond_friend_request", req_id, payload).await
}

#[tauri::command]
pub async fn send_private_message(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    receiver_id: String,
    content: String,
    client_message_id: Option<String>,
) -> Result<Value, QQNTError> {
    let payload = send_private_message_payload(receiver_id, content, client_message_id)?;

    call_engine_payload(state.inner(), "send_private_message", req_id, payload).await
}

#[tauri::command]
pub async fn send_group_message(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    group_id: String,
    content: String,
    client_message_id: Option<String>,
) -> Result<Value, QQNTError> {
    let payload = send_group_message_payload(group_id, content, client_message_id)?;

    call_engine_payload(state.inner(), "send_group_message", req_id, payload).await
}

#[tauri::command]
pub async fn create_group(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    group_name: String,
    members: Option<Vec<Value>>,
    announcement: Option<String>,
) -> Result<Value, QQNTError> {
    let payload = create_group_payload(group_name, members, announcement)?;

    call_engine_payload(state.inner(), "create_group", req_id, payload).await
}

#[tauri::command]
pub async fn update_group_announcement(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    group_id: String,
    announcement: String,
) -> Result<Value, QQNTError> {
    let payload = update_group_announcement_payload(group_id, announcement)?;

    call_engine_payload(state.inner(), "update_group_announcement", req_id, payload).await
}

#[tauri::command]
pub async fn update_group_member(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    group_id: String,
    member_id: String,
    action: String,
) -> Result<Value, QQNTError> {
    let payload = update_group_member_payload(group_id, member_id, action)?;

    call_engine_payload(state.inner(), "update_group_member", req_id, payload).await
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
    let payload = cancel_transfer_payload(transfer_id)?;

    call_engine_payload(state.inner(), "cancel_transfer", req_id, payload).await
}

#[tauri::command]
pub async fn query_resume(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    transfer_id: String,
    file_path: Option<String>,
    receiver_id: Option<String>,
    group_id: Option<String>,
    content_type: Option<String>,
) -> Result<Value, QQNTError> {
    call_engine_payload(
        state.inner(),
        "query_resume",
        req_id,
        query_resume_payload(transfer_id, file_path, receiver_id, group_id, content_type)?,
    )
    .await
}

#[tauri::command]
pub async fn e2e_status(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    peer_id: Option<String>,
) -> Result<Value, QQNTError> {
    let payload = e2e_status_payload(peer_id)?;

    call_engine_payload(state.inner(), "e2e_status", req_id, payload).await
}

#[tauri::command]
pub async fn e2e_announce_identity(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    peer_id: String,
) -> Result<Value, QQNTError> {
    let payload = e2e_peer_payload("e2e_announce_identity", peer_id)?;

    call_engine_payload(state.inner(), "e2e_announce_identity", req_id, payload).await
}

#[tauri::command]
pub async fn e2e_pin_identity(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    peer_id: String,
    fingerprint: Option<String>,
) -> Result<Value, QQNTError> {
    let payload = e2e_pin_identity_payload(peer_id, fingerprint)?;

    call_engine_payload(state.inner(), "e2e_pin_identity", req_id, payload).await
}

#[tauri::command]
pub async fn e2e_request_rotation(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    peer_id: String,
) -> Result<Value, QQNTError> {
    let payload = e2e_peer_payload("e2e_request_rotation", peer_id)?;

    call_engine_payload(state.inner(), "e2e_request_rotation", req_id, payload).await
}

#[tauri::command]
pub async fn profile_update(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    user_name: Option<String>,
    avatar_base64: Option<String>,
) -> Result<Value, QQNTError> {
    let payload = profile_update_payload(user_name, avatar_base64)?;

    call_engine_payload(state.inner(), "profile_update", req_id, payload).await
}

#[tauri::command]
pub async fn settings_sync(
    state: State<'_, Arc<AppState>>,
    req_id: String,
    settings: Value,
) -> Result<Value, QQNTError> {
    let payload = settings_sync_payload(settings)?;

    call_engine_payload(state.inner(), "settings_sync", req_id, payload).await
}

#[tauri::command]
pub async fn clear_session_history(
    session_id: String,
) -> Result<ClearSessionHistoryResponse, QQNTError> {
    let session_id = require_trimmed(session_id, "sessionId")?;
    persist_local_action_value("clear_session", json!({ "sessionId": session_id }))?;
    Ok(ClearSessionHistoryResponse {
        cleared: true,
        session_id,
    })
}

#[tauri::command]
pub async fn delete_local_message(
    session_id: String,
    message_id: String,
) -> Result<DeleteLocalMessageResponse, QQNTError> {
    let session_id = require_trimmed(session_id, "sessionId")?;
    let message_id = require_trimmed(message_id, "messageId")?;
    persist_local_action_value(
        "delete_message",
        json!({ "sessionId": session_id, "messageId": message_id }),
    )?;
    Ok(DeleteLocalMessageResponse {
        deleted: true,
        session_id,
        message_id,
    })
}

#[tauri::command]
pub async fn favorite_local_message(message: Value) -> Result<LocalActionResponse, QQNTError> {
    persist_local_action("favorite", message)
}

#[tauri::command]
pub async fn add_local_emoji(message: Value) -> Result<LocalActionResponse, QQNTError> {
    persist_local_action("emoji", message)
}

#[tauri::command]
pub async fn multi_select_local_message(message: Value) -> Result<LocalActionResponse, QQNTError> {
    persist_local_action("multi_select", message)
}

#[tauri::command]
pub async fn quote_local_message(message: Value) -> Result<LocalActionResponse, QQNTError> {
    persist_local_action("quote", message)
}

#[tauri::command]
pub async fn set_essence_local_message(message: Value) -> Result<LocalActionResponse, QQNTError> {
    persist_local_action("essence", message)
}

#[tauri::command]
pub async fn recall_local_message(message: Value) -> Result<LocalActionResponse, QQNTError> {
    persist_local_action("recall", message)
}

#[tauri::command]
pub async fn forward_local_message(
    message: Value,
    target: Value,
    note: Option<String>,
) -> Result<LocalActionResponse, QQNTError> {
    let target_id = target
        .get("id")
        .and_then(Value::as_str)
        .unwrap_or_default()
        .trim();
    if target_id.is_empty() {
        return Err(QQNTError::rust("missing_field", "target.id is required."));
    }
    persist_local_action_value(
        "forward",
        json!({
            "id": message
                .get("id")
                .and_then(Value::as_str)
                .or_else(|| message.get("messageId").and_then(Value::as_str))
                .unwrap_or("local-message"),
            "message": message,
            "target": target,
            "note": note.unwrap_or_default()
        }),
    )
}

#[tauri::command]
pub async fn view_local_profile(member: Value) -> Result<LocalActionResponse, QQNTError> {
    persist_member_action("view_profile", member, None, None)
}

#[tauri::command]
pub async fn add_local_friend(member: Value) -> Result<LocalActionResponse, QQNTError> {
    persist_member_action("add_friend", member, None, None)
}

#[tauri::command]
pub async fn report_local_user(
    member: Value,
    session_id: Option<String>,
) -> Result<LocalActionResponse, QQNTError> {
    persist_member_action("report_user", member, session_id, None)
}

#[tauri::command]
pub async fn block_local_user(
    member: Value,
    session_id: Option<String>,
) -> Result<LocalActionResponse, QQNTError> {
    persist_member_action("block_user", member, session_id, None)
}

#[tauri::command]
pub async fn edit_local_group_nickname(
    member: Value,
    nickname: String,
    session_id: Option<String>,
) -> Result<LocalActionResponse, QQNTError> {
    let nickname = require_trimmed(nickname, "nickname")?;
    persist_member_action("edit_group_nickname", member, session_id, Some(json!({ "nickname": nickname })))
}

#[tauri::command]
pub async fn get_local_chat_actions() -> Result<LocalChatActionsResponse, QQNTError> {
    let mut cleared_sessions: Vec<ClearedLocalSession> = Vec::new();
    let mut deleted_messages = Vec::new();
    let mut favorite_messages = Vec::new();
    let mut emoji_messages = Vec::new();
    let mut selected_messages = Vec::new();
    let mut quote_messages = Vec::new();
    let mut essence_messages = Vec::new();
    let mut recalled_messages = Vec::new();
    let mut member_actions = Vec::new();
    let directory = local_action_directory();
    if !directory.is_dir() {
        return Ok(LocalChatActionsResponse {
            cleared_sessions,
            deleted_messages,
            favorite_messages,
            emoji_messages,
            selected_messages,
            quote_messages,
            essence_messages,
            recalled_messages,
            member_actions,
        });
    }

    let entries = fs::read_dir(&directory).map_err(|err| {
        QQNTError::rust(
            "local_action_read_failed",
            format!("Unable to read local action directory: {err}"),
        )
    })?;
    for entry in entries.flatten() {
        let path = entry.path();
        if path.extension().and_then(|ext| ext.to_str()) != Some("json") {
            continue;
        }
        let Ok(data) = fs::read_to_string(&path) else {
            continue;
        };
        let Ok(payload) = serde_json::from_str::<Value>(&data) else {
            continue;
        };
        let kind = payload.get("kind").and_then(Value::as_str).unwrap_or_default();
        let saved_at = payload.get("savedAt").and_then(Value::as_u64).unwrap_or(0) as u128;
        let message = payload.get("message").unwrap_or(&Value::Null);
        match kind {
            "clear_session" => {
                if let Some(session_id) = message.get("sessionId").and_then(Value::as_str) {
                    let session_id = session_id.trim();
                    if !session_id.is_empty() {
                        if let Some(existing) = cleared_sessions.iter_mut().find(|existing| existing.session_id == session_id) {
                            existing.cleared_at = existing.cleared_at.max(saved_at);
                        } else {
                            cleared_sessions.push(ClearedLocalSession {
                                session_id: session_id.to_string(),
                                cleared_at: saved_at,
                            });
                        }
                    }
                }
            }
            "delete_message" => {
                let session_id = message
                    .get("sessionId")
                    .and_then(Value::as_str)
                    .unwrap_or_default()
                    .trim();
                let message_id = message
                    .get("messageId")
                    .and_then(Value::as_str)
                    .unwrap_or_default()
                    .trim();
                if !session_id.is_empty() && !message_id.is_empty() {
                    deleted_messages.push(DeletedLocalMessage {
                        session_id: session_id.to_string(),
                        message_id: message_id.to_string(),
                    });
                }
            }
            "favorite" => push_message_action(&mut favorite_messages, message, saved_at),
            "emoji" => push_message_action(&mut emoji_messages, message, saved_at),
            "multi_select" => push_message_action(&mut selected_messages, message, saved_at),
            "quote" => push_message_action(&mut quote_messages, message, saved_at),
            "essence" => push_message_action(&mut essence_messages, message, saved_at),
            "recall" => push_message_action(&mut recalled_messages, message, saved_at),
            "view_profile" | "add_friend" | "report_user" | "block_user" | "edit_group_nickname" => {
                let member_id = message
                    .get("member")
                    .and_then(|member| member.get("id"))
                    .and_then(Value::as_str)
                    .unwrap_or_default()
                    .trim();
                if !member_id.is_empty() {
                    member_actions.push(LocalMemberAction {
                        kind: kind.to_string(),
                        member_id: member_id.to_string(),
                        session_id: message
                            .get("sessionId")
                            .and_then(Value::as_str)
                            .map(str::trim)
                            .filter(|value| !value.is_empty())
                            .map(ToString::to_string),
                        saved_at,
                        value: message.clone(),
                    });
                }
            }
            _ => {}
        }
    }

    Ok(LocalChatActionsResponse {
        cleared_sessions,
        deleted_messages,
        favorite_messages,
        emoji_messages,
        selected_messages,
        quote_messages,
        essence_messages,
        recalled_messages,
        member_actions,
    })
}

#[tauri::command]
pub async fn read_image_base64(file_path: String) -> Result<ImageBase64Response, QQNTError> {
    let path = Path::new(&file_path);
    if !path.is_file() {
        return Err(QQNTError::rust(
            "invalid_file_path",
            "Selected avatar path is not a readable image file.",
        ));
    }

    let bytes = fs::read(path).map_err(|err| {
        QQNTError::rust(
            "read_image_failed",
            format!("Unable to read selected image: {err}"),
        )
    })?;

    if bytes.is_empty() {
        return Err(QQNTError::rust(
            "empty_image_file",
            "Selected image is empty.",
        ));
    }

    let mime = image_mime_from_path_or_bytes(path, &bytes)?;
    let base64 = general_purpose::STANDARD.encode(bytes);
    Ok(ImageBase64Response {
        data_url: format!("data:{mime};base64,{base64}"),
        base64,
    })
}

async fn login_like(
    state: &Arc<AppState>,
    op: &str,
    req_id: String,
    account: String,
    password: String,
    user_name: Option<String>,
) -> Result<LoginResponse, QQNTError> {
    let command_payload = login_like_payload(account, password, user_name)?;
    let payload = call_engine_payload(state, op, req_id, command_payload).await?;

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

fn require_trimmed(value: String, field: &str) -> Result<String, QQNTError> {
    let trimmed = value.trim();
    if trimmed.is_empty() {
        return Err(QQNTError::rust("missing_field", format!("{field} is required.")));
    }
    Ok(trimmed.to_string())
}

fn persist_local_action(kind: &str, message: Value) -> Result<LocalActionResponse, QQNTError> {
    persist_local_action_value(kind, message)
}

fn persist_member_action(
    kind: &str,
    member: Value,
    session_id: Option<String>,
    extra: Option<Value>,
) -> Result<LocalActionResponse, QQNTError> {
    let member_id = member
        .get("id")
        .and_then(Value::as_str)
        .unwrap_or_default()
        .trim();
    if member_id.is_empty() {
        return Err(QQNTError::rust("missing_field", "member.id is required."));
    }

    let mut payload = json!({
        "id": member_id,
        "member": member,
    });
    if let Some(session_id) = session_id.map(|value| value.trim().to_string()).filter(|value| !value.is_empty()) {
        payload["sessionId"] = json!(session_id);
    }
    if let Some(extra) = extra {
        payload["extra"] = extra;
    }

    persist_local_action_value(kind, payload)
}

fn local_action_directory() -> PathBuf {
    std::env::temp_dir().join("tauri-qqnt-local-actions")
}

fn safe_local_action_component(value: &str) -> String {
    let mut safe = String::with_capacity(value.len());
    for character in value.chars() {
        if character.is_ascii_alphanumeric() || matches!(character, '-' | '_' | '.') {
            safe.push(character);
        } else {
            safe.push('_');
        }
    }
    if safe.is_empty() {
        "local-message".to_string()
    } else {
        safe
    }
}

fn persist_local_action_value(kind: &str, message: Value) -> Result<LocalActionResponse, QQNTError> {
    let message_id = message
        .get("id")
        .and_then(Value::as_str)
        .or_else(|| message.get("messageId").and_then(Value::as_str))
        .unwrap_or("local-message")
        .trim();
    if message_id.is_empty() {
        return Err(QQNTError::rust("missing_field", "message.id is required."));
    }

    let directory = local_action_directory();
    fs::create_dir_all(&directory).map_err(|err| {
        QQNTError::rust(
            "local_action_write_failed",
            format!("Unable to create local action directory: {err}"),
        )
    })?;
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(|err| QQNTError::rust("clock_error", format!("System clock error: {err}")))?
        .as_millis();
    let id = format!("{}-{}-{now}", safe_local_action_component(kind), safe_local_action_component(message_id));
    let path = directory.join(format!("{id}.json"));
    let payload = json!({
        "kind": kind,
        "id": id,
        "savedAt": now,
        "message": message
    });
    let data = serde_json::to_vec_pretty(&payload).map_err(|err| {
        QQNTError::rust(
            "local_action_serialize_failed",
            format!("Unable to serialize local action: {err}"),
        )
    })?;
    fs::write(path, data).map_err(|err| {
        QQNTError::rust(
            "local_action_write_failed",
            format!("Unable to write local action: {err}"),
        )
    })?;

    Ok(LocalActionResponse { saved: true, id })
}

fn push_message_action(target: &mut Vec<LocalMessageAction>, message: &Value, saved_at: u128) {
    let message_id = message
        .get("id")
        .and_then(Value::as_str)
        .or_else(|| message.get("messageId").and_then(Value::as_str))
        .or_else(|| message.get("clientMessageId").and_then(Value::as_str))
        .unwrap_or_default()
        .trim();
    let session_id = message
        .get("sessionId")
        .and_then(Value::as_str)
        .unwrap_or_default()
        .trim();
    if message_id.is_empty() || session_id.is_empty() {
        return;
    }
    target.push(LocalMessageAction {
        session_id: session_id.to_string(),
        message_id: message_id.to_string(),
        saved_at,
        value: message.clone(),
    });
}
fn connect_payload(host: String, port: Value) -> Result<Value, QQNTError> {
    let payload = json!({
        "host": host,
        "port": port
    });
    validate_connect_command_payload(&payload)?;
    Ok(payload)
}

fn login_like_payload(
    account: String,
    password: String,
    user_name: Option<String>,
) -> Result<Value, QQNTError> {
    let register_mode = user_name.is_some();
    let payload = compact_payload(json!({
        "account": account,
        "password": password,
        "userName": user_name
    }));
    validate_login_like_command_payload(&payload, register_mode)?;
    Ok(payload)
}

fn set_user_info_payload(user_id: String, user_name: String) -> Result<Value, QQNTError> {
    let payload = json!({
        "userId": user_id,
        "userName": user_name
    });
    validate_set_user_info_command_payload(&payload)?;
    Ok(payload)
}

fn search_friend_payload(account: String) -> Result<Value, QQNTError> {
    let payload = json!({ "account": account });
    validate_search_friend_command_payload(&payload)?;
    Ok(payload)
}

fn send_friend_request_payload(receiver_id: String) -> Result<Value, QQNTError> {
    let payload = json!({ "receiverId": receiver_id });
    validate_send_friend_request_command_payload(&payload)?;
    Ok(payload)
}

fn respond_friend_request_payload(sender_id: String, accepted: Value) -> Result<Value, QQNTError> {
    let payload = json!({
        "senderId": sender_id,
        "accepted": accepted
    });
    validate_respond_friend_request_command_payload(&payload)?;
    Ok(payload)
}

fn send_private_message_payload(
    receiver_id: String,
    content: String,
    client_message_id: Option<String>,
) -> Result<Value, QQNTError> {
    let payload = compact_payload(json!({
        "receiverId": receiver_id,
        "content": content,
        "clientMessageId": client_message_id
    }));
    validate_send_private_message_command_payload(&payload)?;
    Ok(payload)
}

fn e2e_status_payload(peer_id: Option<String>) -> Result<Value, QQNTError> {
    let payload = compact_payload(json!({ "peerId": peer_id }));
    validate_e2e_status_command_payload(&payload)?;
    Ok(payload)
}

fn e2e_peer_payload(op: &str, peer_id: String) -> Result<Value, QQNTError> {
    let payload = json!({ "peerId": peer_id });
    validate_e2e_peer_command_payload(op, &payload)?;
    Ok(payload)
}

fn e2e_pin_identity_payload(
    peer_id: String,
    fingerprint: Option<String>,
) -> Result<Value, QQNTError> {
    let payload = compact_payload(json!({
        "peerId": peer_id,
        "fingerprint": fingerprint
    }));
    validate_e2e_pin_identity_command_payload(&payload)?;
    Ok(payload)
}

fn profile_update_payload(
    user_name: Option<String>,
    avatar_base64: Option<String>,
) -> Result<Value, QQNTError> {
    let payload = compact_payload(json!({
        "userName": user_name,
        "avatarBase64": avatar_base64
    }));
    validate_profile_update_command_payload(&payload)?;
    Ok(payload)
}

fn settings_sync_payload(settings: Value) -> Result<Value, QQNTError> {
    let payload = json!({ "settings": settings });
    validate_settings_sync_command_payload(&payload)?;
    Ok(payload)
}

fn image_mime_from_path(path: &Path) -> Result<&'static str, QQNTError> {
    let extension = path
        .extension()
        .and_then(|value| value.to_str())
        .unwrap_or_default()
        .to_ascii_lowercase();

    match extension.as_str() {
        "png" => Ok("image/png"),
        "jpg" | "jpeg" => Ok("image/jpeg"),
        "gif" => Ok("image/gif"),
        "webp" => Ok("image/webp"),
        "bmp" => Ok("image/bmp"),
        "svg" => Ok("image/svg+xml"),
        "avif" => Ok("image/avif"),
        "apng" => Ok("image/apng"),
        _ => Err(QQNTError::rust(
            "unsupported_image_type",
            "Avatar image must be png, jpg, jpeg, gif, webp, bmp, svg, avif, or apng.",
        )),
    }
}

#[tauri::command]
pub async fn get_screenshot_monitor_info() -> Result<ScreenshotMonitorInfo, QQNTError> {
    get_screenshot_monitor_info_impl()
}

#[tauri::command]
pub async fn get_screenshot_virtual_screen_info() -> Result<ScreenshotMonitorInfo, QQNTError> {
    get_screenshot_virtual_screen_info_impl()
}

#[tauri::command]
pub async fn capture_screenshot() -> Result<ScreenshotResponse, QQNTError> {
    capture_screenshot_impl()
}

#[tauri::command]
pub async fn capture_screenshot_shared_buffer(
    webview: tauri::Webview,
    request_id: Option<String>,
) -> Result<ScreenshotResponse, QQNTError> {
    capture_screenshot_shared_buffer_impl(webview, request_id).await
}

#[tauri::command]
pub async fn crop_screenshot(
    payload: ScreenshotCropPayload,
) -> Result<ScreenshotResponse, QQNTError> {
    crop_screenshot_impl(payload)
}

#[tauri::command]
pub async fn release_screenshot_capture(capture_id: String) -> Result<(), QQNTError> {
    release_screenshot_capture_impl(&capture_id);
    Ok(())
}

#[tauri::command]
pub async fn set_screenshot_window_exclude_from_capture(
    window: tauri::Window,
    enable: bool,
) -> Result<(), QQNTError> {
    set_screenshot_window_exclude_from_capture_impl(window, enable)
}

#[tauri::command]
pub async fn prepare_screenshot_window(window: tauri::Window) -> Result<(), QQNTError> {
    prepare_screenshot_window_impl(window)
}

#[tauri::command]
pub async fn save_file_to_directory(
    source_path: String,
    directory_path: String,
    file_name: Option<String>,
) -> Result<SavedLocalFileResponse, QQNTError> {
    save_file_to_directory_impl(&source_path, &directory_path, file_name.as_deref())
}

#[tauri::command]
pub async fn save_base64_file_to_directory(
    base64: String,
    directory_path: String,
    file_name: String,
) -> Result<SavedLocalFileResponse, QQNTError> {
    save_base64_file_to_directory_impl(&base64, &directory_path, &file_name)
}

fn image_mime_from_path_or_bytes(path: &Path, bytes: &[u8]) -> Result<&'static str, QQNTError> {
    if let Ok(mime) = image_mime_from_path(path) {
        return Ok(mime);
    }
    if bytes.starts_with(b"\x89PNG\r\n\x1A\n") {
        return Ok("image/png");
    }
    if bytes.starts_with(b"\xFF\xD8\xFF") {
        return Ok("image/jpeg");
    }
    if bytes.starts_with(b"GIF87a") || bytes.starts_with(b"GIF89a") {
        return Ok("image/gif");
    }
    if bytes.starts_with(b"BM") {
        return Ok("image/bmp");
    }
    if bytes.len() >= 12 && bytes.starts_with(b"RIFF") && &bytes[8..12] == b"WEBP" {
        return Ok("image/webp");
    }
    if bytes.len() >= 12 && &bytes[4..8] == b"ftyp" && matches!(&bytes[8..12], b"avif" | b"avis") {
        return Ok("image/avif");
    }

    let prefix_len = bytes.len().min(256);
    let prefix = String::from_utf8_lossy(&bytes[..prefix_len])
        .trim_start_matches('\u{feff}')
        .trim_start()
        .to_ascii_lowercase();
    if prefix.starts_with("<svg") || prefix.starts_with("<?xml") && prefix.contains("<svg") {
        return Ok("image/svg+xml");
    }

    Err(QQNTError::rust(
        "unsupported_image_type",
        "Image must be png, jpg, jpeg, gif, webp, bmp, svg, avif, or apng.",
    ))
}

fn safe_leaf_name(candidate: Option<&str>, source_path: &Path) -> Result<String, QQNTError> {
    let selected = candidate
        .and_then(|value| Path::new(value).file_name())
        .or_else(|| source_path.file_name())
        .and_then(|value| value.to_str())
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .ok_or_else(|| QQNTError::rust("invalid_file_name", "File name is required."))?;

    Ok(selected
        .chars()
        .map(|ch| match ch {
            '<' | '>' | ':' | '"' | '/' | '\\' | '|' | '?' | '*' => '_',
            _ => ch,
        })
        .collect())
}

fn unique_destination_path(directory: &Path, file_name: &str) -> PathBuf {
    let initial = directory.join(file_name);
    if !initial.exists() {
        return initial;
    }

    let path = Path::new(file_name);
    let stem = path
        .file_stem()
        .and_then(|value| value.to_str())
        .unwrap_or("file");
    let extension = path
        .extension()
        .and_then(|value| value.to_str())
        .unwrap_or_default();
    for index in 2..1000 {
        let candidate_name = if extension.is_empty() {
            format!("{stem}_{index}")
        } else {
            format!("{stem}_{index}.{extension}")
        };
        let candidate = directory.join(candidate_name);
        if !candidate.exists() {
            return candidate;
        }
    }

    directory.join(format!("{stem}_{}", chrono_like_timestamp()))
}

fn paths_refer_to_same_file(first: &Path, second: &Path) -> bool {
    match (first.canonicalize(), second.canonicalize()) {
        (Ok(first), Ok(second)) => first == second,
        _ => false,
    }
}

fn chrono_like_timestamp() -> u128 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|duration| duration.as_millis())
        .unwrap_or_default()
}

fn screenshot_temp_destination(prefix: &str) -> Result<(PathBuf, String), QQNTError> {
    let directory = std::env::temp_dir().join("tauri-qqnt-screenshots");
    fs::create_dir_all(&directory).map_err(|err| {
        QQNTError::rust(
            "screenshot_save_failed",
            format!("Unable to create screenshot directory: {err}"),
        )
    })?;

    let file_name = format!("{prefix}-{}.png", chrono_like_timestamp());
    Ok((unique_destination_path(&directory, &file_name), file_name))
}

fn screenshot_response(
    destination: &Path,
    fallback_name: &str,
    capture: ScreenshotCaptureInfo,
) -> ScreenshotResponse {
    ScreenshotResponse {
        file_name: destination
            .file_name()
            .and_then(|value| value.to_str())
            .unwrap_or(fallback_name)
            .to_string(),
        file_path: destination.to_string_lossy().to_string(),
        capture_id: None,
        x: Some(capture.x),
        y: Some(capture.y),
        width: Some(capture.width),
        height: Some(capture.height),
    }
}

fn capture_screenshot_impl() -> Result<ScreenshotResponse, QQNTError> {
    let (destination, file_name) = screenshot_temp_destination("screenshot")?;

    let capture = capture_screenshot_to_path(&destination)?;
    if !destination.is_file() {
        return Err(QQNTError::rust(
            "screenshot_save_failed",
            "Screenshot command finished but no image file was created.",
        ));
    }

    Ok(screenshot_response(&destination, &file_name, capture))
}

async fn capture_screenshot_shared_buffer_impl(
    webview: tauri::Webview,
    request_id: Option<String>,
) -> Result<ScreenshotResponse, QQNTError> {
    let capture = capture_screenshot_data()?;
    let info = capture.info;
    let capture_id = format!("screenshot-{}", chrono_like_timestamp());
    let (destination, file_name) = screenshot_temp_destination("screenshot")?;
    save_screenshot_capture_data(&capture, &destination)?;
    let transfer_type = request_id
        .as_deref()
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(|value| format!("screenshot:{value}"))
        .unwrap_or_else(|| "screenshot".to_string());
    let extra = screenshot_shared_buffer_extra(info.width, info.height);

    crate::shared_buffer::create_shared_buffer(
        webview,
        capture.rgba.as_raw(),
        &extra,
        transfer_type,
    )
    .await
    .ok();

    cache_screenshot_capture(capture_id.clone(), capture)?;

    Ok(ScreenshotResponse {
        file_path: destination.to_string_lossy().into_owned(),
        file_name,
        capture_id: Some(capture_id),
        x: Some(info.x),
        y: Some(info.y),
        width: Some(info.width),
        height: Some(info.height),
    })
}

fn screenshot_shared_buffer_extra(width: u32, height: u32) -> [u8; 8] {
    let mut extra = [0; 8];
    extra[..4].copy_from_slice(&width.to_le_bytes());
    extra[4..].copy_from_slice(&height.to_le_bytes());
    extra
}

fn get_screenshot_monitor_info_impl() -> Result<ScreenshotMonitorInfo, QQNTError> {
    get_active_screenshot_monitor_info()
}

fn get_screenshot_virtual_screen_info_impl() -> Result<ScreenshotMonitorInfo, QQNTError> {
    get_virtual_screenshot_monitor_info()
}

#[derive(Debug, Clone, Copy)]
struct ScreenshotCaptureInfo {
    x: i32,
    y: i32,
    width: u32,
    height: u32,
}

struct ScreenshotCaptureData {
    info: ScreenshotCaptureInfo,
    rgba: image::RgbaImage,
}

static SCREENSHOT_CAPTURE_CACHE: OnceLock<Mutex<HashMap<String, ScreenshotCaptureData>>> =
    OnceLock::new();

fn screenshot_capture_cache() -> &'static Mutex<HashMap<String, ScreenshotCaptureData>> {
    SCREENSHOT_CAPTURE_CACHE.get_or_init(|| Mutex::new(HashMap::new()))
}

fn cache_screenshot_capture(
    capture_id: String,
    capture: ScreenshotCaptureData,
) -> Result<(), QQNTError> {
    screenshot_capture_cache()
        .lock()
        .map_err(|_| QQNTError::rust("screenshot_cache_failed", "Screenshot cache is poisoned."))?
        .insert(capture_id, capture);
    Ok(())
}

fn take_screenshot_capture(capture_id: &str) -> Result<ScreenshotCaptureData, QQNTError> {
    screenshot_capture_cache()
        .lock()
        .map_err(|_| QQNTError::rust("screenshot_cache_failed", "Screenshot cache is poisoned."))?
        .remove(capture_id)
        .ok_or_else(|| {
            QQNTError::rust(
                "screenshot_cache_miss",
                "Screenshot cache has expired. Please capture again.",
            )
        })
}

fn release_screenshot_capture_impl(capture_id: &str) {
    if capture_id.trim().is_empty() {
        return;
    }
    if let Some(cache) = SCREENSHOT_CAPTURE_CACHE.get() {
        if let Ok(mut captures) = cache.lock() {
            captures.remove(capture_id);
        }
    }
}

#[cfg(target_os = "windows")]
fn set_screenshot_window_exclude_from_capture_impl(
    window: tauri::Window,
    enable: bool,
) -> Result<(), QQNTError> {
    let hwnd = window.hwnd().map_err(|err| {
        QQNTError::rust(
            "screenshot_window_handle_failed",
            format!("Unable to get screenshot window handle: {err}"),
        )
    })?;
    set_hwnd_capture_exclusion(hwnd, enable)
}

#[cfg(target_os = "windows")]
fn set_window_capture_exclusion(window: tauri::WebviewWindow, enable: bool) -> Result<(), QQNTError> {
    let hwnd = window.hwnd().map_err(|err| {
        QQNTError::rust(
            "screenshot_window_handle_failed",
            format!("Unable to get screenshot window handle: {err}"),
        )
    })?;
    set_hwnd_capture_exclusion(hwnd, enable)
}

#[cfg(target_os = "windows")]
fn set_hwnd_capture_exclusion(
    hwnd: windows::Win32::Foundation::HWND,
    enable: bool,
) -> Result<(), QQNTError> {
    use windows::Win32::UI::WindowsAndMessaging::{
        SetWindowDisplayAffinity, WDA_EXCLUDEFROMCAPTURE, WDA_NONE,
    };

    unsafe {
        SetWindowDisplayAffinity(
            hwnd,
            if enable {
                WDA_EXCLUDEFROMCAPTURE
            } else {
                WDA_NONE
            },
        )
    }
    .map_err(|err| {
        QQNTError::rust(
            "screenshot_exclude_from_capture_failed",
            format!("Unable to update screenshot window capture exclusion: {err}"),
        )
    })
}

#[cfg(not(target_os = "windows"))]
fn set_screenshot_window_exclude_from_capture_impl(
    _window: tauri::Window,
    _enable: bool,
) -> Result<(), QQNTError> {
    Ok(())
}

#[cfg(not(target_os = "windows"))]
fn set_window_capture_exclusion(_window: tauri::WebviewWindow, _enable: bool) -> Result<(), QQNTError> {
    Ok(())
}

#[cfg(target_os = "windows")]
fn set_window_alpha(window: tauri::WebviewWindow, alpha: u8) -> Result<(), QQNTError> {
    use windows::Win32::Foundation::COLORREF;
    use windows::Win32::UI::WindowsAndMessaging::{
        GetWindowLongW, SetLayeredWindowAttributes, SetWindowLongW, GWL_EXSTYLE, LWA_ALPHA,
        WS_EX_LAYERED,
    };

    let hwnd = window.hwnd().map_err(|err| {
        QQNTError::rust(
            "screenshot_window_handle_failed",
            format!("Unable to get main window handle: {err}"),
        )
    })?;

    unsafe {
        let style = GetWindowLongW(hwnd, GWL_EXSTYLE);
        let layered_style = style | WS_EX_LAYERED.0 as i32;
        let _ = SetWindowLongW(hwnd, GWL_EXSTYLE, layered_style);
        SetLayeredWindowAttributes(hwnd, COLORREF(0), alpha, LWA_ALPHA).map_err(|err| {
            QQNTError::rust(
                "screenshot_window_alpha_failed",
                format!("Unable to update main window alpha: {err}"),
            )
        })?;
    }

    Ok(())
}

#[cfg(not(target_os = "windows"))]
fn set_window_alpha(_window: tauri::WebviewWindow, _alpha: u8) -> Result<(), QQNTError> {
    Ok(())
}

#[cfg(target_os = "windows")]
fn prepare_screenshot_window_impl(window: tauri::Window) -> Result<(), QQNTError> {
    use windows::Win32::Foundation::HWND;
    use windows::Win32::Graphics::Dwm::{
        DwmSetWindowAttribute, DWMWA_TRANSITIONS_FORCEDISABLED,
    };

    let hwnd = window.hwnd().map_err(|err| {
        QQNTError::rust(
            "screenshot_window_handle_failed",
            format!("Unable to get screenshot window handle: {err}"),
        )
    })?;

    // Match Snow Shot: disable DWM show/hide animation so the overlay appears immediately.
    let disable_transitions: i32 = 1;
    unsafe {
        DwmSetWindowAttribute(
            HWND(hwnd.0),
            DWMWA_TRANSITIONS_FORCEDISABLED,
            &disable_transitions as *const _ as *const _,
            std::mem::size_of::<i32>() as u32,
        )
    }
    .map_err(|err| {
        QQNTError::rust(
            "screenshot_window_style_failed",
            format!("Unable to prepare screenshot window style: {err}"),
        )
    })
}

#[cfg(not(target_os = "windows"))]
fn prepare_screenshot_window_impl(_window: tauri::Window) -> Result<(), QQNTError> {
    Ok(())
}

#[cfg(target_os = "windows")]
fn get_active_screenshot_monitor_info() -> Result<ScreenshotMonitorInfo, QQNTError> {
    use xcap::Monitor;

    let (cursor_x, cursor_y) = cursor_position()?;
    let monitor = Monitor::from_point(cursor_x, cursor_y)
        .or_else(|_| {
            let mut monitors = Monitor::all()?;
            if let Some(primary_index) = monitors
                .iter()
                .position(|monitor| monitor.is_primary().unwrap_or(false))
            {
                return Ok(monitors.remove(primary_index));
            }

            monitors.into_iter().next().ok_or_else(|| {
                xcap::XCapError::new("No display is available for screenshot capture.")
            })
        })
        .map_err(|err| {
            QQNTError::rust(
                "screenshot_failed",
                format!("Unable to find active monitor with xcap: {err}"),
            )
        })?;

    let x = monitor.x().unwrap_or(0);
    let y = monitor.y().unwrap_or(0);
    let width = monitor.width().unwrap_or(960).max(1);
    let height = monitor.height().unwrap_or(640).max(1);

    Ok(ScreenshotMonitorInfo {
        x,
        y,
        width,
        height,
    })
}

#[cfg(target_os = "windows")]
fn get_virtual_screenshot_monitor_info() -> Result<ScreenshotMonitorInfo, QQNTError> {
    use xcap::Monitor;

    let monitors = Monitor::all().map_err(|err| {
        QQNTError::rust(
            "screenshot_failed",
            format!("Unable to enumerate monitors with xcap: {err}"),
        )
    })?;

    let mut bounds: Option<(i64, i64, i64, i64)> = None;
    for monitor in monitors {
        let x = monitor.x().unwrap_or(0) as i64;
        let y = monitor.y().unwrap_or(0) as i64;
        let width = monitor.width().unwrap_or(1).max(1) as i64;
        let height = monitor.height().unwrap_or(1).max(1) as i64;
        bounds = Some(match bounds {
            Some((min_x, min_y, max_x, max_y)) => (
                min_x.min(x),
                min_y.min(y),
                max_x.max(x + width),
                max_y.max(y + height),
            ),
            None => (x, y, x + width, y + height),
        });
    }

    let (min_x, min_y, max_x, max_y) = bounds.ok_or_else(|| {
        QQNTError::rust(
            "screenshot_failed",
            "No display is available for screenshot capture.",
        )
    })?;

    Ok(ScreenshotMonitorInfo {
        x: i32::try_from(min_x).unwrap_or(if min_x.is_negative() {
            i32::MIN
        } else {
            i32::MAX
        }),
        y: i32::try_from(min_y).unwrap_or(if min_y.is_negative() {
            i32::MIN
        } else {
            i32::MAX
        }),
        width: u32::try_from(max_x - min_x).unwrap_or(u32::MAX).max(1),
        height: u32::try_from(max_y - min_y).unwrap_or(u32::MAX).max(1),
    })
}

#[cfg(target_os = "windows")]
fn capture_screenshot_data() -> Result<ScreenshotCaptureData, QQNTError> {
    use image::RgbaImage;
    use xcap::Monitor;

    let monitors = Monitor::all().map_err(|err| {
        QQNTError::rust(
            "screenshot_failed",
            format!("Unable to enumerate monitors with xcap: {err}"),
        )
    })?;
    if monitors.is_empty() {
        return Err(QQNTError::rust(
            "screenshot_failed",
            "No display is available for screenshot capture.",
        ));
    }

    let mut bounds: Option<(i64, i64, i64, i64)> = None;
    let mut monitor_infos = Vec::with_capacity(monitors.len());
    for monitor in monitors {
        let x = monitor.x().unwrap_or(0) as i64;
        let y = monitor.y().unwrap_or(0) as i64;
        let width = monitor.width().unwrap_or(1).max(1) as i64;
        let height = monitor.height().unwrap_or(1).max(1) as i64;
        let right = x + width;
        let bottom = y + height;
        bounds = Some(match bounds {
            Some((min_x, min_y, max_x, max_y)) => (
                min_x.min(x),
                min_y.min(y),
                max_x.max(right),
                max_y.max(bottom),
            ),
            None => (x, y, right, bottom),
        });
        monitor_infos.push((monitor, x, y));
    }

    let (min_x, min_y, max_x, max_y) = bounds.ok_or_else(|| {
        QQNTError::rust(
            "screenshot_failed",
            "No display is available for screenshot capture.",
        )
    })?;
    let virtual_width = u32::try_from(max_x - min_x).unwrap_or(u32::MAX).max(1);
    let virtual_height = u32::try_from(max_y - min_y).unwrap_or(u32::MAX).max(1);
    let mut rgba = RgbaImage::new(virtual_width, virtual_height);

    for (monitor, monitor_x, monitor_y) in monitor_infos {
        let image = monitor.capture_image().map_err(|err| {
            QQNTError::rust(
                "screenshot_failed",
                format!("Unable to capture monitor image with xcap: {err}"),
            )
        })?;
        image::imageops::overlay(&mut rgba, &image, monitor_x - min_x, monitor_y - min_y);
    }

    let info = ScreenshotCaptureInfo {
        x: i32::try_from(min_x).unwrap_or(if min_x.is_negative() {
            i32::MIN
        } else {
            i32::MAX
        }),
        y: i32::try_from(min_y).unwrap_or(if min_y.is_negative() {
            i32::MIN
        } else {
            i32::MAX
        }),
        width: rgba.width(),
        height: rgba.height(),
    };

    Ok(ScreenshotCaptureData { info, rgba })
}

#[cfg(target_os = "windows")]
fn save_screenshot_capture_data(
    capture: &ScreenshotCaptureData,
    destination: &Path,
) -> Result<(), QQNTError> {
    image::save_buffer_with_format(
        destination,
        capture.rgba.as_raw(),
        capture.info.width,
        capture.info.height,
        image::ColorType::Rgba8,
        image::ImageFormat::Png,
    )
    .map_err(|err| {
        QQNTError::rust(
            "screenshot_save_failed",
            format!("Unable to save xcap screenshot image: {err}"),
        )
    })
}

#[cfg(target_os = "windows")]
fn capture_screenshot_to_path(destination: &Path) -> Result<ScreenshotCaptureInfo, QQNTError> {
    let capture = capture_screenshot_data()?;
    save_screenshot_capture_data(&capture, destination)?;
    Ok(capture.info)
}

#[cfg(target_os = "windows")]
fn cursor_position() -> Result<(i32, i32), QQNTError> {
    use windows::Win32::Foundation::POINT;
    use windows::Win32::UI::WindowsAndMessaging::GetCursorPos;

    let mut point = POINT { x: 0, y: 0 };
    unsafe { GetCursorPos(&mut point) }.map_err(|err| {
        QQNTError::rust(
            "screenshot_failed",
            format!("Unable to read cursor position for screenshot: {err}"),
        )
    })?;

    Ok((point.x, point.y))
}

#[cfg(not(target_os = "windows"))]
fn get_active_screenshot_monitor_info() -> Result<ScreenshotMonitorInfo, QQNTError> {
    Err(QQNTError::rust(
        "screenshot_unsupported",
        "System screenshot capture is currently implemented for Windows.",
    ))
}

#[cfg(not(target_os = "windows"))]
fn get_virtual_screenshot_monitor_info() -> Result<ScreenshotMonitorInfo, QQNTError> {
    Err(QQNTError::rust(
        "screenshot_unsupported",
        "System screenshot capture is currently implemented for Windows.",
    ))
}

#[cfg(not(target_os = "windows"))]
fn capture_screenshot_to_path(_destination: &Path) -> Result<ScreenshotCaptureInfo, QQNTError> {
    Err(QQNTError::rust(
        "screenshot_unsupported",
        "System screenshot capture is currently implemented for Windows.",
    ))
}

#[cfg(not(target_os = "windows"))]
fn capture_screenshot_data() -> Result<ScreenshotCaptureData, QQNTError> {
    Err(QQNTError::rust(
        "screenshot_unsupported",
        "System screenshot capture is currently implemented for Windows.",
    ))
}

fn crop_screenshot_impl(payload: ScreenshotCropPayload) -> Result<ScreenshotResponse, QQNTError> {
    if payload.width == 0 || payload.height == 0 {
        return Err(QQNTError::rust(
            "invalid_screenshot_selection",
            "Screenshot selection is empty.",
        ));
    }

    if let Some(capture_id) = payload
        .capture_id
        .as_deref()
        .map(str::trim)
        .filter(|value| !value.is_empty())
    {
        return crop_screenshot_from_capture(capture_id, &payload);
    }

    let Some(source_path) = payload
        .source_path
        .as_deref()
        .map(str::trim)
        .filter(|value| !value.is_empty())
    else {
        return Err(QQNTError::rust(
            "invalid_screenshot_source",
            "Screenshot source cache or file path is required.",
        ));
    };

    let source = Path::new(source_path);
    if !source.is_file() {
        return Err(QQNTError::rust(
            "invalid_screenshot_path",
            "Screenshot source file is not readable.",
        ));
    }

    let image = image::open(source).map_err(|err| {
        QQNTError::rust(
            "screenshot_crop_failed",
            format!("Unable to open screenshot image: {err}"),
        )
    })?;

    if payload.x >= image.width() || payload.y >= image.height() {
        return Err(QQNTError::rust(
            "invalid_screenshot_selection",
            "Screenshot selection starts outside the image.",
        ));
    }

    let width = payload.width.min(image.width() - payload.x);
    let height = payload.height.min(image.height() - payload.y);
    let cropped = image.crop_imm(payload.x, payload.y, width, height);

    let directory = source
        .parent()
        .map(Path::to_path_buf)
        .unwrap_or_else(std::env::temp_dir);
    let file_name = format!("screenshot-crop-{}.png", chrono_like_timestamp());
    let destination = unique_destination_path(&directory, &file_name);
    cropped.save(&destination).map_err(|err| {
        QQNTError::rust(
            "screenshot_save_failed",
            format!("Unable to save cropped screenshot image: {err}"),
        )
    })?;

    Ok(ScreenshotResponse {
        file_name: destination
            .file_name()
            .and_then(|value| value.to_str())
            .unwrap_or(&file_name)
            .to_string(),
        file_path: destination.to_string_lossy().to_string(),
        capture_id: None,
        x: None,
        y: None,
        width: Some(width),
        height: Some(height),
    })
}

fn crop_screenshot_from_capture(
    capture_id: &str,
    payload: &ScreenshotCropPayload,
) -> Result<ScreenshotResponse, QQNTError> {
    let capture = take_screenshot_capture(capture_id)?;
    let image_width = capture.rgba.width();
    let image_height = capture.rgba.height();

    if payload.x >= image_width || payload.y >= image_height {
        return Err(QQNTError::rust(
            "invalid_screenshot_selection",
            "Screenshot selection starts outside the image.",
        ));
    }

    let width = payload.width.min(image_width - payload.x);
    let height = payload.height.min(image_height - payload.y);
    let cropped =
        image::imageops::crop_imm(&capture.rgba, payload.x, payload.y, width, height).to_image();
    let (destination, file_name) = screenshot_temp_destination("screenshot-crop")?;

    image::save_buffer_with_format(
        &destination,
        cropped.as_raw(),
        width,
        height,
        image::ColorType::Rgba8,
        image::ImageFormat::Png,
    )
    .map_err(|err| {
        QQNTError::rust(
            "screenshot_save_failed",
            format!("Unable to save cropped screenshot image: {err}"),
        )
    })?;

    Ok(ScreenshotResponse {
        file_name: destination
            .file_name()
            .and_then(|value| value.to_str())
            .unwrap_or(&file_name)
            .to_string(),
        file_path: destination.to_string_lossy().to_string(),
        capture_id: None,
        x: None,
        y: None,
        width: Some(width),
        height: Some(height),
    })
}

fn save_file_to_directory_impl(
    source_path: &str,
    directory_path: &str,
    file_name: Option<&str>,
) -> Result<SavedLocalFileResponse, QQNTError> {
    let source = Path::new(source_path);
    if !source.is_file() {
        return Err(QQNTError::rust(
            "invalid_file_path",
            "Source file is not readable.",
        ));
    }

    let directory = Path::new(directory_path);
    if directory_path.trim().is_empty() {
        return Err(QQNTError::rust(
            "invalid_download_directory",
            "Download directory is required.",
        ));
    }
    fs::create_dir_all(directory).map_err(|err| {
        QQNTError::rust(
            "create_download_directory_failed",
            format!("Unable to create download directory: {err}"),
        )
    })?;

    let safe_name = safe_leaf_name(file_name, source)?;
    let preferred_destination = directory.join(&safe_name);
    if paths_refer_to_same_file(source, &preferred_destination) {
        return Ok(SavedLocalFileResponse {
            file_path: preferred_destination.to_string_lossy().to_string(),
            file_name: safe_name,
        });
    }

    if source
        .parent()
        .map(|parent| paths_refer_to_same_file(parent, directory))
        .unwrap_or(false)
    {
        let source_name = source
            .file_name()
            .and_then(|value| value.to_str())
            .unwrap_or(&safe_name)
            .to_string();
        return Ok(SavedLocalFileResponse {
            file_path: source.to_string_lossy().to_string(),
            file_name: source_name,
        });
    }

    let destination = unique_destination_path(directory, &safe_name);
    fs::copy(source, &destination).map_err(|err| {
        QQNTError::rust(
            "save_file_failed",
            format!("Unable to save file to download directory: {err}"),
        )
    })?;

    Ok(SavedLocalFileResponse {
        file_name: destination
            .file_name()
            .and_then(|value| value.to_str())
            .unwrap_or(&safe_name)
            .to_string(),
        file_path: destination.to_string_lossy().to_string(),
    })
}

fn save_base64_file_to_directory_impl(
    base64: &str,
    directory_path: &str,
    file_name: &str,
) -> Result<SavedLocalFileResponse, QQNTError> {
    let bytes = general_purpose::STANDARD
        .decode(base64.trim())
        .map_err(|err| {
            QQNTError::rust(
                "invalid_file_data",
                format!("File data is not valid base64: {err}"),
            )
        })?;
    if bytes.is_empty() {
        return Err(QQNTError::rust("empty_file_data", "File data is empty."));
    }

    let directory = Path::new(directory_path);
    if directory_path.trim().is_empty() {
        return Err(QQNTError::rust(
            "invalid_download_directory",
            "Download directory is required.",
        ));
    }
    fs::create_dir_all(directory).map_err(|err| {
        QQNTError::rust(
            "create_download_directory_failed",
            format!("Unable to create download directory: {err}"),
        )
    })?;

    let safe_name = safe_leaf_name(Some(file_name), Path::new("file.bin"))?;
    let destination = unique_destination_path(directory, &safe_name);
    fs::write(&destination, bytes).map_err(|err| {
        QQNTError::rust(
            "save_file_failed",
            format!("Unable to save file to download directory: {err}"),
        )
    })?;

    Ok(SavedLocalFileResponse {
        file_name: destination
            .file_name()
            .and_then(|value| value.to_str())
            .unwrap_or(&safe_name)
            .to_string(),
        file_path: destination.to_string_lossy().to_string(),
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
    let payload = send_file_like_payload(op, file_path, receiver_id, group_id)?;

    call_engine_payload(state, op, req_id, payload).await
}

fn send_file_like_payload(
    op: &str,
    file_path: String,
    receiver_id: Option<String>,
    group_id: Option<String>,
) -> Result<Value, QQNTError> {
    let payload = json!({
        "filePath": file_path,
        "receiverId": receiver_id,
        "groupId": group_id
    });
    validate_file_transfer_command_payload(op, &payload)?;
    Ok(payload)
}

fn cancel_transfer_payload(transfer_id: String) -> Result<Value, QQNTError> {
    let payload = json!({ "transferId": transfer_id });
    validate_cancel_transfer_command_payload(&payload)?;
    Ok(payload)
}

fn send_group_message_payload(
    group_id: String,
    content: String,
    client_message_id: Option<String>,
) -> Result<Value, QQNTError> {
    let payload = compact_payload(json!({
        "groupId": group_id,
        "content": content,
        "clientMessageId": client_message_id
    }));
    validate_send_group_message_command_payload(&payload)?;
    Ok(payload)
}

fn create_group_payload(
    group_name: String,
    members: Option<Vec<Value>>,
    announcement: Option<String>,
) -> Result<Value, QQNTError> {
    let payload = compact_payload(json!({
        "groupName": group_name,
        "members": members,
        "announcement": announcement
    }));
    validate_create_group_command_payload(&payload)?;
    Ok(payload)
}

fn update_group_announcement_payload(
    group_id: String,
    announcement: String,
) -> Result<Value, QQNTError> {
    let payload = json!({
        "groupId": group_id,
        "announcement": announcement
    });
    validate_update_group_announcement_command_payload(&payload)?;
    Ok(payload)
}

fn query_resume_payload(
    transfer_id: String,
    file_path: Option<String>,
    receiver_id: Option<String>,
    group_id: Option<String>,
    content_type: Option<String>,
) -> Result<Value, QQNTError> {
    let payload = json!({
        "transferId": transfer_id,
        "filePath": file_path,
        "receiverId": receiver_id,
        "groupId": group_id,
        "contentType": content_type
    });
    validate_query_resume_command_payload(&payload)?;
    Ok(payload)
}

fn update_group_member_payload(
    group_id: String,
    member_id: String,
    action: String,
) -> Result<Value, QQNTError> {
    let payload = json!({
        "groupId": group_id,
        "memberId": member_id,
        "action": action
    });
    validate_update_group_member_command_payload(&payload)?;
    Ok(payload)
}

async fn call_engine_payload(
    state: &Arc<AppState>,
    op: &str,
    req_id: String,
    payload: Value,
) -> Result<Value, QQNTError> {
    let packet = bridge::call_engine(state, command_packet(op, req_id.clone(), payload)).await?;
    ack_payload(packet, op, &req_id)
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

fn ack_payload(packet: Value, op: &str, req_id: &str) -> Result<Value, QQNTError> {
    validate_generic_ack_packet(&packet, op, req_id)?;

    if packet.get("status").and_then(Value::as_str) == Some("error") {
        let error = packet.get("error").cloned().unwrap_or_else(|| json!({}));
        let mut mapped_error = QQNTError::new(
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
        );
        if let Some(details) = error.get("details").cloned() {
            mapped_error = mapped_error.with_details(details);
        }
        return Err(mapped_error);
    }

    let payload = packet.get("payload").cloned().unwrap_or_else(|| json!({}));

    Ok(payload)
}

fn validate_generic_command(command: &Value) -> Result<(), QQNTError> {
    let Some(command_object) = command.as_object() else {
        return Err(QQNTError::rust(
            "invalid_command",
            "Command must be a JSON object.",
        ));
    };
    require_command_envelope_fields(command_object)?;

    let has_non_empty_string = |field: &str| {
        command_object
            .get(field)
            .and_then(Value::as_str)
            .is_some_and(|value| !value.trim().is_empty())
    };

    if !has_non_empty_string("op") {
        return Err(QQNTError::rust("missing_op", "Command op is required."));
    }

    if !has_non_empty_string("reqId") {
        return Err(QQNTError::rust(
            "missing_req_id",
            "Command reqId is required.",
        ));
    }

    if command_object
        .get("payload")
        .is_some_and(|payload| !payload.is_object())
    {
        return Err(QQNTError::rust(
            "invalid_payload",
            "Command payload must be an object.",
        ));
    }

    let op = command_object
        .get("op")
        .and_then(Value::as_str)
        .unwrap_or_default();
    if let Some(payload) = command_object.get("payload") {
        validate_command_payload(op, payload)?;
    } else {
        validate_command_payload(op, &json!({}))?;
    }

    Ok(())
}

fn validate_command_payload(op: &str, payload: &Value) -> Result<(), QQNTError> {
    let op = op.trim();
    match op {
        "ready" | "disconnect" | "logout" | "get_user_list" | "get_friend_list"
        | "get_group_list" => require_empty_command_payload(op, payload),
        "connect" => validate_connect_command_payload(payload),
        "login" => validate_login_like_command_payload(payload, false),
        "register" => validate_login_like_command_payload(payload, true),
        "set_user_info" => validate_set_user_info_command_payload(payload),
        "search_friend" => validate_search_friend_command_payload(payload),
        "send_friend_request" => validate_send_friend_request_command_payload(payload),
        "respond_friend_request" => validate_respond_friend_request_command_payload(payload),
        "send_private_message" => validate_send_private_message_command_payload(payload),
        "e2e_status" => validate_e2e_status_command_payload(payload),
        "e2e_announce_identity" | "e2e_request_rotation" => {
            validate_e2e_peer_command_payload(op, payload)
        }
        "e2e_pin_identity" => validate_e2e_pin_identity_command_payload(payload),
        "profile_update" => validate_profile_update_command_payload(payload),
        "settings_sync" => validate_settings_sync_command_payload(payload),
        "send_group_message" => validate_send_group_message_command_payload(payload),
        "create_group" => validate_create_group_command_payload(payload),
        "update_group_announcement" => validate_update_group_announcement_command_payload(payload),
        "send_file" | "send_image" => validate_file_transfer_command_payload(op, payload),
        "cancel_transfer" => validate_cancel_transfer_command_payload(payload),
        "query_resume" => validate_query_resume_command_payload(payload),
        "update_group_member" => validate_update_group_member_command_payload(payload),
        _ => Err(QQNTError::rust(
            "unknown_op",
            format!("Unsupported command: {op}."),
        )),
    }
}

fn require_empty_command_payload(op: &str, payload: &Value) -> Result<(), QQNTError> {
    if payload.as_object().is_some_and(|object| object.is_empty()) {
        return Ok(());
    }

    Err(QQNTError::rust(
        "invalid_payload",
        format!("Command payload for {op} must be empty."),
    ))
}

fn validate_connect_command_payload(payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields("connect", payload, &["host", "port"])?;
    require_non_empty_command_string_field(payload, "host")?;
    require_command_tcp_port(payload, "port")?;
    Ok(())
}

fn validate_login_like_command_payload(
    payload: &Value,
    register_mode: bool,
) -> Result<(), QQNTError> {
    let op = if register_mode { "register" } else { "login" };
    let allowed_fields = if register_mode {
        &["account", "password", "userName"][..]
    } else {
        &["account", "password"][..]
    };
    require_command_payload_fields(op, payload, allowed_fields)?;
    require_non_empty_command_string_field(payload, "account")?;
    require_non_empty_command_string_field(payload, "password")?;
    if register_mode {
        require_non_empty_command_string_field(payload, "userName")?;
    }
    Ok(())
}

fn validate_set_user_info_command_payload(payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields("set_user_info", payload, &["userId", "userName"])?;
    require_non_empty_command_string_field(payload, "userId")?;
    require_non_empty_command_string_field(payload, "userName")?;
    Ok(())
}

fn validate_search_friend_command_payload(payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields("search_friend", payload, &["account"])?;
    require_non_empty_command_string_field(payload, "account")?;
    Ok(())
}

fn validate_send_friend_request_command_payload(payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields("send_friend_request", payload, &["receiverId"])?;
    require_non_empty_command_string_field(payload, "receiverId")?;
    Ok(())
}

fn validate_respond_friend_request_command_payload(payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields("respond_friend_request", payload, &["senderId", "accepted"])?;
    require_non_empty_command_string_field(payload, "senderId")?;
    require_command_bool_field(payload, "accepted")?;
    Ok(())
}

fn validate_send_private_message_command_payload(payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields(
        "send_private_message",
        payload,
        &["receiverId", "content", "clientMessageId"],
    )?;
    require_non_empty_command_string_field(payload, "receiverId")?;
    require_non_empty_command_string_field(payload, "content")?;
    optional_command_string_field(payload, "clientMessageId", "invalid_client_message_id")?;
    Ok(())
}

fn validate_cancel_transfer_command_payload(payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields("cancel_transfer", payload, &["transferId"])?;
    require_non_empty_command_string_field(payload, "transferId")?;
    Ok(())
}

fn validate_e2e_status_command_payload(payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields("e2e_status", payload, &["peerId"])?;
    optional_command_string_field(payload, "peerId", "invalid_peer_id")?;
    Ok(())
}

fn validate_e2e_peer_command_payload(op: &str, payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields(op, payload, &["peerId"])?;
    require_non_empty_command_string_field(payload, "peerId")?;
    Ok(())
}

fn validate_e2e_pin_identity_command_payload(payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields("e2e_pin_identity", payload, &["peerId", "fingerprint"])?;
    require_non_empty_command_string_field(payload, "peerId")?;
    optional_command_string_field(payload, "fingerprint", "invalid_fingerprint")?;
    Ok(())
}

fn validate_profile_update_command_payload(payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields("profile_update", payload, &["userName", "avatarBase64"])?;
    optional_command_string_field(payload, "userName", "invalid_profile_field")?;
    let avatar_base64 =
        optional_command_string_field(payload, "avatarBase64", "invalid_profile_field")?;
    if !avatar_base64.trim().is_empty() && !is_valid_standard_base64(avatar_base64.trim()) {
        return Err(QQNTError::rust(
            "invalid_profile_field",
            "payload.avatarBase64 must be valid Base64 when provided.",
        ));
    }
    Ok(())
}

fn validate_settings_sync_command_payload(payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields("settings_sync", payload, &["settings"])?;
    let Some(settings_value) = payload.get("settings") else {
        return Err(QQNTError::rust(
            "invalid_settings",
            "payload.settings must be an object.",
        ));
    };
    let Some(settings) = settings_value.as_object() else {
        return Err(QQNTError::rust(
            "invalid_settings",
            "payload.settings must be an object.",
        ));
    };

    optional_settings_string_field(settings_value, "fileDownloadDir", "fileDownloadDir")?;

    let Some(files_value) = settings.get("files") else {
        return Ok(());
    };
    if files_value.is_null() {
        return Ok(());
    }
    let Some(files) = files_value.as_object() else {
        return Err(QQNTError::rust(
            "invalid_settings",
            "settings.files must be an object when provided.",
        ));
    };
    let _ = files;

    optional_settings_string_field(files_value, "downloadDir", "files.downloadDir")?;
    optional_settings_string_field(files_value, "downloadDirectory", "files.downloadDirectory")?;
    Ok(())
}

fn validate_send_group_message_command_payload(payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields(
        "send_group_message",
        payload,
        &["groupId", "content", "clientMessageId"],
    )?;
    require_non_empty_command_string_field(payload, "groupId")?;
    require_non_empty_command_string_field(payload, "content")?;
    optional_command_string_field(payload, "clientMessageId", "invalid_client_message_id")?;
    Ok(())
}

fn validate_create_group_command_payload(payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields(
        "create_group",
        payload,
        &["groupName", "members", "announcement"],
    )?;
    require_non_empty_command_string_field(payload, "groupName")?;
    optional_command_string_field(payload, "announcement", "invalid_announcement")?;
    validate_create_group_members(payload)
}

fn validate_update_group_announcement_command_payload(payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields(
        "update_group_announcement",
        payload,
        &["groupId", "announcement"],
    )?;
    require_non_empty_command_string_field(payload, "groupId")?;
    require_command_string_field(payload, "announcement")?;
    Ok(())
}

fn validate_create_group_members(payload: &Value) -> Result<(), QQNTError> {
    let Some(members_value) = payload.get("members") else {
        return Ok(());
    };
    if members_value.is_null() {
        return Err(QQNTError::rust(
            "invalid_members",
            "create_group members must be an array when provided.",
        ));
    }
    let Some(members) = members_value.as_array() else {
        return Err(QQNTError::rust(
            "invalid_members",
            "create_group members must be an array when provided.",
        ));
    };

    for member in members {
        if let Some(member_id) = member.as_str() {
            validate_create_group_member_id(member_id)?;
            continue;
        }

        let Some(member_object) = member.as_object() else {
            return Err(QQNTError::rust(
                "invalid_members",
                "create_group members entries must be strings.",
            ));
        };
        let mut candidate = None;
        for field in ["userId", "account", "id", "memberId"] {
            let Some(value) = member_object.get(field) else {
                continue;
            };
            if value.is_null() {
                continue;
            }
            let Some(member_id) = value.as_str() else {
                return Err(QQNTError::rust(
                    "invalid_members",
                    format!("create_group member.{field} must be a string when provided."),
                ));
            };
            candidate = Some(member_id);
            break;
        }
        validate_create_group_member_id(candidate.unwrap_or_default())?;
    }

    Ok(())
}

fn validate_create_group_member_id(member_id: &str) -> Result<(), QQNTError> {
    if !member_id.trim().is_empty() {
        return Ok(());
    }

    Err(QQNTError::rust(
        "invalid_members",
        "create_group members entries must not be empty.",
    ))
}

fn validate_file_transfer_command_payload(op: &str, payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields(op, payload, &["filePath", "receiverId", "groupId"])?;
    require_non_empty_command_string_field(payload, "filePath")?;
    let receiver_id = optional_command_target_string_field(payload, "receiverId")?;
    let group_id = optional_command_target_string_field(payload, "groupId")?;
    validate_required_command_target(
        receiver_id,
        group_id,
        "File send requires exactly one of receiverId or groupId.",
        "File send target must not include both receiverId and groupId.",
    )
}

fn validate_query_resume_command_payload(payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields(
        "query_resume",
        payload,
        &[
            "transferId",
            "filePath",
            "receiverId",
            "groupId",
            "contentType",
        ],
    )?;
    require_non_empty_command_string_field(payload, "transferId")?;
    let file_path = optional_command_string_field(payload, "filePath", "invalid_file_path")?;
    let content_type =
        optional_command_string_field(payload, "contentType", "invalid_content_type")?;
    if !content_type.trim().is_empty() && !matches!(content_type, "file" | "image") {
        return Err(QQNTError::rust(
            "invalid_content_type",
            "query_resume contentType must be file or image when provided.",
        ));
    }

    let receiver_id = optional_command_target_string_field(payload, "receiverId")?;
    let group_id = optional_command_target_string_field(payload, "groupId")?;
    if file_path.trim().is_empty() {
        if !receiver_id.trim().is_empty() || !group_id.trim().is_empty() {
            return Err(QQNTError::rust(
                "invalid_target",
                "query_resume receiverId/groupId may only be provided with filePath.",
            ));
        }
    } else {
        validate_required_command_target(
            receiver_id,
            group_id,
            "Resume transfer requires exactly one of receiverId or groupId.",
            "Resume transfer target must not include both receiverId and groupId.",
        )?;
    }

    Ok(())
}

fn validate_required_command_target(
    receiver_id: &str,
    group_id: &str,
    missing_message: &str,
    ambiguous_message: &str,
) -> Result<(), QQNTError> {
    let has_receiver_id = !receiver_id.trim().is_empty();
    let has_group_id = !group_id.trim().is_empty();
    if !has_receiver_id && !has_group_id {
        return Err(QQNTError::rust("missing_target", missing_message));
    }
    if has_receiver_id && has_group_id {
        return Err(QQNTError::rust("ambiguous_target", ambiguous_message));
    }

    Ok(())
}

fn validate_update_group_member_command_payload(payload: &Value) -> Result<(), QQNTError> {
    require_command_payload_fields(
        "update_group_member",
        payload,
        &["groupId", "memberId", "action"],
    )?;
    require_non_empty_command_string_field(payload, "groupId")?;
    require_non_empty_command_string_field(payload, "memberId")?;
    let action = require_non_empty_command_string_field(payload, "action")?;
    if matches!(action, "add" | "remove" | "promote_admin" | "demote_admin") {
        return Ok(());
    }

    Err(QQNTError::rust(
        "invalid_action",
        "update_group_member action must be add, remove, promote_admin, or demote_admin.",
    ))
}

fn require_command_envelope_fields(
    command_object: &serde_json::Map<String, Value>,
) -> Result<(), QQNTError> {
    for field in command_object.keys() {
        if ["op", "reqId", "payload"].contains(&field.as_str()) {
            continue;
        }

        return Err(QQNTError::rust(
            "invalid_command",
            format!("Command contains unsupported field: {field}."),
        ));
    }

    Ok(())
}

fn require_command_payload_fields(
    op: &str,
    payload: &Value,
    allowed_fields: &[&str],
) -> Result<(), QQNTError> {
    let Some(payload_object) = payload.as_object() else {
        return Err(QQNTError::rust(
            "invalid_payload",
            "Command payload must be an object.",
        ));
    };

    for field in payload_object.keys() {
        if allowed_fields.contains(&field.as_str()) {
            continue;
        }

        return Err(QQNTError::rust(
            "invalid_payload",
            format!("Command payload for {op} contains unsupported field: {field}."),
        ));
    }

    Ok(())
}

fn optional_command_target_string_field<'a>(
    payload: &'a Value,
    field: &str,
) -> Result<&'a str, QQNTError> {
    optional_command_string_field(payload, field, "invalid_target")
}

fn optional_command_string_field<'a>(
    payload: &'a Value,
    field: &str,
    error_code: &str,
) -> Result<&'a str, QQNTError> {
    match payload.get(field) {
        None | Some(Value::Null) => Ok(""),
        Some(Value::String(value)) => Ok(value.as_str()),
        _ => Err(QQNTError::rust(
            error_code,
            format!("payload.{field} must be a string when provided."),
        )),
    }
}

fn optional_settings_string_field<'a>(
    object: &'a Value,
    field: &str,
    label: &str,
) -> Result<Option<&'a str>, QQNTError> {
    match object.get(field) {
        None | Some(Value::Null) => Ok(None),
        Some(Value::String(value)) => Ok(Some(value.as_str())),
        _ => Err(QQNTError::rust(
            "invalid_settings",
            format!("settings.{label} must be a string when provided."),
        )),
    }
}

fn is_valid_standard_base64(value: &str) -> bool {
    let mut padding_count = 0usize;
    let mut data_count = 0usize;
    let mut padding_started = false;

    for byte in value.bytes() {
        match byte {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'+' | b'/' => {
                if padding_started {
                    return false;
                }
                data_count += 1;
            }
            b'=' => {
                padding_started = true;
                padding_count += 1;
                if padding_count > 2 {
                    return false;
                }
            }
            _ => return false,
        }
    }

    let total_len = data_count + padding_count;
    if total_len == 0 || total_len % 4 == 1 {
        return false;
    }
    match padding_count {
        0 => true,
        1 => total_len % 4 == 0 && data_count % 4 == 3,
        2 => total_len % 4 == 0 && data_count % 4 == 2,
        _ => false,
    }
}

fn require_command_string_field<'a>(payload: &'a Value, field: &str) -> Result<&'a str, QQNTError> {
    match payload.get(field) {
        Some(Value::String(value)) => Ok(value.as_str()),
        _ => Err(QQNTError::rust(
            "missing_field",
            format!("payload.{field} is required."),
        )),
    }
}

fn require_command_bool_field(payload: &Value, field: &str) -> Result<bool, QQNTError> {
    match payload.get(field).and_then(Value::as_bool) {
        Some(value) => Ok(value),
        None => Err(QQNTError::rust(
            "missing_field",
            format!("payload.{field} is required."),
        )),
    }
}

fn require_non_empty_command_string_field<'a>(
    payload: &'a Value,
    field: &str,
) -> Result<&'a str, QQNTError> {
    match payload.get(field).and_then(Value::as_str) {
        Some(value) if !value.trim().is_empty() => Ok(value),
        _ => Err(QQNTError::rust(
            "missing_field",
            format!("payload.{field} is required."),
        )),
    }
}

fn require_command_tcp_port(payload: &Value, field: &str) -> Result<u16, QQNTError> {
    let Some(number) = payload.get(field).and_then(Value::as_number) else {
        return Err(QQNTError::rust(
            "missing_field",
            format!("payload.{field} is required."),
        ));
    };

    if let Some(port) = number.as_u64() {
        if (1..=65535).contains(&port) {
            return Ok(port as u16);
        }
    } else if let Some(port) = number.as_i64() {
        if (1..=65535).contains(&port) {
            return Ok(port as u16);
        }
    } else if let Some(port) = number.as_f64() {
        if port.fract() == 0.0 && (1.0..=65535.0).contains(&port) {
            return Ok(port as u16);
        }
    }

    Err(QQNTError::rust(
        "invalid_target",
        "connect requires a valid host and port.",
    ))
}

fn validate_generic_ack_packet(
    packet: &Value,
    expected_op: &str,
    expected_req_id: &str,
) -> Result<(), QQNTError> {
    let Some(packet_object) = packet.as_object() else {
        return Err(QQNTError::rust(
            "invalid_ack",
            "QQNTEngine ack must be a JSON object.",
        ));
    };

    let has_matching_string = |field: &str, expected: &str| {
        packet_object
            .get(field)
            .and_then(Value::as_str)
            .is_some_and(|value| value == expected)
    };

    if !has_matching_string("type", "ack")
        || !has_matching_string("op", expected_op)
        || !has_matching_string("reqId", expected_req_id)
    {
        return Err(QQNTError::rust(
            "unexpected_ack",
            format!("QQNTEngine returned an unexpected ack for {expected_op}."),
        ));
    }

    if packet_object
        .get("payload")
        .is_some_and(|payload| !payload.is_object())
    {
        return Err(QQNTError::rust(
            "invalid_ack_payload",
            "QQNTEngine ack payload must be an object.",
        ));
    }

    match packet_object.get("status").and_then(Value::as_str) {
        Some("ok") => {
            let payload = packet.get("payload").cloned().unwrap_or_else(|| json!({}));
            protocol::validate_command_ack_payload(expected_op, &payload)
        }
        Some("error") => validate_generic_error_ack(packet, expected_op),
        _ => Err(QQNTError::rust(
            "invalid_ack_status",
            "QQNTEngine ack status must be ok or error.",
        )),
    }
}

fn validate_generic_error_ack(packet: &Value, expected_op: &str) -> Result<(), QQNTError> {
    let Some(error) = packet.get("error").and_then(Value::as_object) else {
        return Err(QQNTError::rust(
            "invalid_ack_error",
            "QQNTEngine error ack must include an error object.",
        ));
    };

    for field in ["code", "message", "source"] {
        if !error
            .get(field)
            .and_then(Value::as_str)
            .is_some_and(|value| !value.trim().is_empty())
        {
            return Err(QQNTError::rust(
                "invalid_ack_error",
                format!("QQNTEngine error ack must include non-empty {field} string."),
            ));
        }
    }

    if error
        .get("details")
        .is_some_and(|details| !details.is_object())
    {
        return Err(QQNTError::rust(
            "invalid_ack_error",
            "QQNTEngine error ack details must be an object when provided.",
        ));
    }

    validate_target_error_details(expected_op, error)?;

    Ok(())
}

fn validate_target_error_details(
    expected_op: &str,
    error: &serde_json::Map<String, Value>,
) -> Result<(), QQNTError> {
    let code = error
        .get("code")
        .and_then(Value::as_str)
        .unwrap_or_default();
    let requires_target_fields = match expected_op {
        "send_file" | "send_image" => matches!(code, "missing_target" | "ambiguous_target"),
        "query_resume" => matches!(
            code,
            "missing_target" | "ambiguous_target" | "invalid_target"
        ),
        _ => false,
    };
    if !requires_target_fields {
        return Ok(());
    }

    let target_fields = error
        .get("details")
        .and_then(Value::as_object)
        .and_then(|details| details.get("targetFields"))
        .and_then(Value::as_array);
    let has_target_field_contract = target_fields.is_some_and(|fields| {
        fields.len() == 2
            && fields[0].as_str() == Some("receiverId")
            && fields[1].as_str() == Some("groupId")
    });
    if has_target_field_contract {
        return Ok(());
    }

    Err(QQNTError::rust(
        "invalid_ack_error",
        "QQNTEngine target error ack must include details.targetFields [\"receiverId\", \"groupId\"].",
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::time::{SystemTime, UNIX_EPOCH};

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
    static TEMP_COUNTER: AtomicUsize = AtomicUsize::new(0);

    fn temp_save_dir() -> PathBuf {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system clock should be after epoch")
            .as_millis();
        let index = TEMP_COUNTER.fetch_add(1, Ordering::Relaxed);
        std::env::temp_dir().join(format!("qqnt-save-test-{stamp}-{index}"))
    }

    #[test]
    fn save_file_to_directory_copies_source_and_avoids_collisions() {
        let root = temp_save_dir();
        let source_dir = root.join("source");
        let download_dir = root.join("downloads");
        fs::create_dir_all(&source_dir).expect("source dir should be created");
        let source = source_dir.join("photo.png");
        fs::write(&source, b"image-bytes").expect("source file should be written");

        let first = save_file_to_directory_impl(
            &source.to_string_lossy(),
            &download_dir.to_string_lossy(),
            Some("photo.png"),
        )
        .expect("first save should copy the source file");
        let second = save_file_to_directory_impl(
            &source.to_string_lossy(),
            &download_dir.to_string_lossy(),
            Some("photo.png"),
        )
        .expect("second save should choose a unique destination");

        assert!(Path::new(&first.file_path).is_file());
        assert!(Path::new(&second.file_path).is_file());
        assert_ne!(first.file_path, second.file_path);
        assert_eq!(
            fs::read(&first.file_path).expect("saved file should be readable"),
            b"image-bytes"
        );
        fs::remove_dir_all(root).expect("test save dir should be removable");
    }

    #[test]
    fn save_file_to_directory_reuses_files_already_in_download_dir() {
        let root = temp_save_dir();
        let download_dir = root.join("downloads");
        fs::create_dir_all(&download_dir).expect("download dir should be created");
        let source = download_dir.join("photo.png");
        fs::write(&source, b"image-bytes").expect("saved file should be written");

        let saved = save_file_to_directory_impl(
            &source.to_string_lossy(),
            &download_dir.to_string_lossy(),
            Some("photo.png"),
        )
        .expect("already saved file should be reused");

        assert!(paths_refer_to_same_file(
            Path::new(&saved.file_path),
            &source
        ));
        assert_eq!(saved.file_name, "photo.png");
        assert!(!download_dir.join("photo_2.png").exists());
        fs::remove_dir_all(root).expect("test save dir should be removable");
    }

    #[test]
    fn save_base64_file_to_directory_writes_payload_bytes() {
        let root = temp_save_dir();
        let download_dir = root.join("downloads");

        let saved = save_base64_file_to_directory_impl(
            "ZG9jLWJ5dGVz",
            &download_dir.to_string_lossy(),
            "report.txt",
        )
        .expect("base64 save should write bytes");

        assert_eq!(saved.file_name, "report.txt");
        assert_eq!(
            fs::read(&saved.file_path).expect("saved payload should be readable"),
            b"doc-bytes"
        );
        fs::remove_dir_all(root).expect("test save dir should be removable");
    }

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

    fn object_keys<'a>(value: &'a Value, key: &str) -> Vec<&'a str> {
        value[key]
            .as_object()
            .expect("protocol contract key should be an object")
            .keys()
            .map(|item| item.as_str())
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
    fn command_payload_contract_covers_protocol_commands() {
        let contract = protocol_contract();
        let mut commands = string_array(&contract, "commands");
        let mut command_payloads = object_keys(&contract, "commandPayloads");
        let mut typed_ops = TYPED_COMMAND_OPS.to_vec();

        commands.sort_unstable();
        command_payloads.sort_unstable();
        typed_ops.sort_unstable();

        assert_eq!(command_payloads, commands);
        assert_eq!(typed_ops, commands);
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
    fn generic_command_accepts_object_payload() {
        validate_generic_command(&json!({
            "op": "send_private_message",
            "reqId": "req-1",
            "payload": {
                "receiverId": "10001",
                "content": "hello"
            }
        }))
        .expect("generic command with object payload should pass");
    }

    #[test]
    fn generic_command_allows_omitted_payload() {
        validate_generic_command(&json!({
            "op": "ready",
            "reqId": "req-ready"
        }))
        .expect("generic command may omit payload");
    }

    #[test]
    fn generic_command_rejects_non_empty_empty_payload_command() {
        for op in [
            "ready",
            "disconnect",
            "logout",
            "get_user_list",
            "get_friend_list",
            "get_group_list",
        ] {
            let error = validate_generic_command(&json!({
                "op": op,
                "reqId": format!("req-{op}"),
                "payload": {
                    "unexpected": true
                }
            }))
            .expect_err("generic command should reject extra fields on empty payload commands");

            assert_eq!(error.code, "invalid_payload");
        }
    }

    #[test]
    fn generic_command_rejects_unknown_op() {
        let error = validate_generic_command(&json!({
            "op": "future_command",
            "reqId": "req-future",
            "payload": {}
        }))
        .expect_err("generic command should reject unknown op");

        assert_eq!(error.code, "unknown_op");
    }

    #[test]
    fn generic_command_rejects_non_object_envelope() {
        let error = validate_generic_command(&json!("bad"))
            .expect_err("generic command envelope must be an object");

        assert_eq!(error.code, "invalid_command");
    }

    #[test]
    fn generic_command_rejects_extra_envelope_field() {
        let error = validate_generic_command(&json!({
            "op": "ready",
            "reqId": "req-ready",
            "payload": {},
            "debug": true
        }))
        .expect_err("generic command envelope should reject unsupported fields");

        assert_eq!(error.code, "invalid_command");
        assert!(error.message.contains("debug"));
    }

    #[test]
    fn generic_command_rejects_missing_op() {
        let error = validate_generic_command(&json!({
            "reqId": "req-1",
            "payload": {}
        }))
        .expect_err("generic command without op should fail");

        assert_eq!(error.code, "missing_op");
    }

    #[test]
    fn generic_command_rejects_missing_req_id() {
        let error = validate_generic_command(&json!({
            "op": "ready",
            "payload": {}
        }))
        .expect_err("generic command without reqId should fail");

        assert_eq!(error.code, "missing_req_id");
    }

    #[test]
    fn generic_command_rejects_blank_req_id() {
        let error = validate_generic_command(&json!({
            "op": "ready",
            "reqId": "   ",
            "payload": {}
        }))
        .expect_err("generic command with blank reqId should fail");

        assert_eq!(error.code, "missing_req_id");
    }

    #[test]
    fn generic_command_rejects_non_object_payload() {
        let error = validate_generic_command(&json!({
            "op": "search_friend",
            "reqId": "req-2",
            "payload": "bad"
        }))
        .expect_err("generic command payload must be an object");

        assert_eq!(error.code, "invalid_payload");
    }

    #[test]
    fn generic_command_rejects_extra_connect_payload_field() {
        let error = validate_generic_command(&json!({
            "op": "connect",
            "reqId": "req-connect",
            "payload": {
                "host": "127.0.0.1",
                "port": 8888,
                "debug": true
            }
        }))
        .expect_err("generic connect should reject unsupported payload fields");

        assert_eq!(error.code, "invalid_payload");
        assert!(error.message.contains("debug"));
    }

    #[test]
    fn generic_command_rejects_connect_missing_port() {
        let error = validate_generic_command(&json!({
            "op": "connect",
            "reqId": "req-connect",
            "payload": {
                "host": "127.0.0.1"
            }
        }))
        .expect_err("generic connect should require a port");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn generic_command_rejects_connect_invalid_port() {
        for port in [json!(0), json!(65536), json!(8888.5)] {
            let error = validate_generic_command(&json!({
                "op": "connect",
                "reqId": "req-connect",
                "payload": {
                    "host": "127.0.0.1",
                    "port": port
                }
            }))
            .expect_err("generic connect should reject invalid TCP ports");

            assert_eq!(error.code, "invalid_target");
        }
    }

    #[test]
    fn generic_command_rejects_extra_login_payload_field() {
        let error = validate_generic_command(&json!({
            "op": "login",
            "reqId": "req-login",
            "payload": {
                "account": "10001",
                "password": "secret",
                "rememberMe": true
            }
        }))
        .expect_err("generic login should reject unsupported payload fields");

        assert_eq!(error.code, "invalid_payload");
        assert!(error.message.contains("rememberMe"));
    }

    #[test]
    fn generic_command_rejects_login_missing_credentials() {
        for payload in [
            json!({ "password": "secret" }),
            json!({ "account": "10001" }),
            json!({ "account": "10001", "password": " " }),
        ] {
            let error = validate_generic_command(&json!({
                "op": "login",
                "reqId": "req-login",
                "payload": payload
            }))
            .expect_err("generic login should require non-empty credentials");

            assert_eq!(error.code, "missing_field");
        }
    }

    #[test]
    fn generic_command_rejects_register_missing_credentials() {
        for payload in [
            json!({ "password": "secret", "userName": "Alice" }),
            json!({ "account": "10001", "userName": "Alice" }),
        ] {
            let error = validate_generic_command(&json!({
                "op": "register",
                "reqId": "req-register",
                "payload": payload
            }))
            .expect_err("generic register should require non-empty credentials");

            assert_eq!(error.code, "missing_field");
        }
    }

    #[test]
    fn generic_command_rejects_register_missing_user_name() {
        let error = validate_generic_command(&json!({
            "op": "register",
            "reqId": "req-register",
            "payload": {
                "account": "10001",
                "password": "secret"
            }
        }))
        .expect_err("generic register should require userName");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn generic_command_rejects_set_user_info_missing_fields() {
        for payload in [
            json!({ "userName": "Alice" }),
            json!({ "userId": "10001" }),
            json!({ "userId": " ", "userName": "Alice" }),
        ] {
            let error = validate_generic_command(&json!({
                "op": "set_user_info",
                "reqId": "req-user-info",
                "payload": payload
            }))
            .expect_err("generic set_user_info should require userId and userName");

            assert_eq!(error.code, "missing_field");
        }
    }

    #[test]
    fn generic_command_rejects_search_friend_blank_account() {
        let error = validate_generic_command(&json!({
            "op": "search_friend",
            "reqId": "req-search",
            "payload": {
                "account": " "
            }
        }))
        .expect_err("generic search_friend should require account");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn generic_command_rejects_friend_response_non_bool_accepted() {
        let error = validate_generic_command(&json!({
            "op": "respond_friend_request",
            "reqId": "req-friend-response",
            "payload": {
                "senderId": "10001",
                "accepted": "true"
            }
        }))
        .expect_err("generic friend response should require accepted boolean");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn generic_command_rejects_extra_private_message_payload_field() {
        let error = validate_generic_command(&json!({
            "op": "send_private_message",
            "reqId": "req-private-message",
            "payload": {
                "receiverId": "10001",
                "content": "hello",
                "debug": true
            }
        }))
        .expect_err("generic private message should reject unsupported payload fields");

        assert_eq!(error.code, "invalid_payload");
        assert!(error.message.contains("debug"));
    }

    #[test]
    fn generic_command_rejects_private_message_empty_content() {
        let error = validate_generic_command(&json!({
            "op": "send_private_message",
            "reqId": "req-private-message",
            "payload": {
                "receiverId": "10001",
                "content": ""
            }
        }))
        .expect_err("generic private message should require content");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn generic_command_rejects_e2e_status_invalid_peer_id() {
        let error = validate_generic_command(&json!({
            "op": "e2e_status",
            "reqId": "req-e2e-status",
            "payload": {
                "peerId": 10001
            }
        }))
        .expect_err("generic e2e_status peerId must be a string when provided");

        assert_eq!(error.code, "invalid_peer_id");
    }

    #[test]
    fn generic_command_rejects_e2e_announce_missing_peer_id() {
        for (op, payload) in [
            ("e2e_announce_identity", json!({})),
            ("e2e_announce_identity", json!({ "peerId": "" })),
            ("e2e_request_rotation", json!({})),
            ("e2e_request_rotation", json!({ "peerId": "   " })),
        ] {
            let error = validate_generic_command(&json!({
                "op": op,
                "reqId": "req-e2e-peer",
                "payload": payload
            }))
            .expect_err("generic e2e peer commands should require peerId");

            assert_eq!(error.code, "missing_field");
        }
    }

    #[test]
    fn generic_command_rejects_e2e_pin_invalid_fingerprint() {
        for payload in [json!({}), json!({ "peerId": " " })] {
            let error = validate_generic_command(&json!({
                "op": "e2e_pin_identity",
                "reqId": "req-e2e-pin",
                "payload": payload
            }))
            .expect_err("generic e2e_pin_identity should require peerId");

            assert_eq!(error.code, "missing_field");
        }

        let error = validate_generic_command(&json!({
            "op": "e2e_pin_identity",
            "reqId": "req-e2e-pin-fingerprint",
            "payload": {
                "peerId": "10001",
                "fingerprint": false
            }
        }))
        .expect_err("generic e2e_pin_identity fingerprint must be a string when provided");

        assert_eq!(error.code, "invalid_fingerprint");
    }

    #[test]
    fn generic_command_rejects_profile_update_invalid_avatar() {
        let error = validate_generic_command(&json!({
            "op": "profile_update",
            "reqId": "req-profile",
            "payload": {
                "avatarBase64": "not-base64%%%"
            }
        }))
        .expect_err("generic profile_update avatarBase64 must be valid Base64");

        assert_eq!(error.code, "invalid_profile_field");
    }

    #[test]
    fn generic_command_rejects_settings_sync_invalid_download_dir() {
        for settings in [
            json!({ "fileDownloadDir": false }),
            json!({ "files": "bad" }),
            json!({
                "files": {
                    "downloadDir": 42
                }
            }),
            json!({
                "files": {
                    "downloadDirectory": 42
                }
            }),
            json!({
                "fileDownloadDir": "C:/tmp/downloads",
                "files": "bad"
            }),
            json!({
                "fileDownloadDir": "C:/tmp/downloads",
                "files": {
                    "downloadDirectory": 42
                }
            }),
        ] {
            let error = validate_generic_command(&json!({
                "op": "settings_sync",
                "reqId": "req-settings",
                "payload": {
                    "settings": settings
                }
            }))
            .expect_err("generic settings_sync download directory fields must be valid");

            assert_eq!(error.code, "invalid_settings");
        }
    }

    #[test]
    fn generic_command_accepts_settings_sync_download_dir_aliases() {
        for settings in [
            json!({ "fileDownloadDir": "C:/tmp/downloads" }),
            json!({
                "files": {
                    "downloadDir": "C:/tmp/downloads"
                }
            }),
            json!({
                "files": {
                    "downloadDirectory": "C:/tmp/downloads"
                }
            }),
        ] {
            validate_generic_command(&json!({
                "op": "settings_sync",
                "reqId": "req-settings",
                "payload": {
                    "settings": settings
                }
            }))
            .expect("generic settings_sync should accept supported download directory aliases");
        }
    }

    #[test]
    fn generic_command_allows_settings_sync_blank_download_dir_aliases() {
        for settings in [
            json!({ "fileDownloadDir": "   " }),
            json!({
                "files": {
                    "downloadDir": ""
                }
            }),
            json!({
                "files": {
                    "downloadDirectory": " "
                }
            }),
        ] {
            validate_generic_command(&json!({
                "op": "settings_sync",
                "reqId": "req-settings",
                "payload": {
                    "settings": settings
                }
            }))
            .expect("generic settings_sync should allow blank download directory aliases");
        }
    }

    #[test]
    fn generic_command_rejects_settings_sync_invalid_settings_shape() {
        for payload in [
            json!({}),
            json!({ "settings": null }),
            json!({ "settings": "bad" }),
        ] {
            let error = validate_generic_command(&json!({
                "op": "settings_sync",
                "reqId": "req-settings",
                "payload": payload
            }))
            .expect_err("generic settings_sync should require object settings");

            assert_eq!(error.code, "invalid_settings");
        }
    }

    #[test]
    fn generic_command_rejects_extra_settings_sync_payload_field() {
        let error = validate_generic_command(&json!({
            "op": "settings_sync",
            "reqId": "req-settings",
            "payload": {
                "settings": {},
                "debug": true
            }
        }))
        .expect_err("generic settings_sync should reject unsupported payload fields");

        assert_eq!(error.code, "invalid_payload");
        assert!(error.message.contains("debug"));
    }

    #[test]
    fn generic_command_accepts_update_group_member_supported_action() {
        validate_generic_command(&json!({
            "op": "update_group_member",
            "reqId": "req-group-member",
            "payload": {
                "groupId": "group-1",
                "memberId": "10002",
                "action": "promote_admin"
            }
        }))
        .expect("generic update_group_member with supported action should pass");
    }

    #[test]
    fn generic_command_rejects_update_group_member_missing_action() {
        let error = validate_generic_command(&json!({
            "op": "update_group_member",
            "reqId": "req-group-member",
            "payload": {
                "groupId": "group-1",
                "memberId": "10002"
            }
        }))
        .expect_err("generic update_group_member without action should fail");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn generic_command_rejects_update_group_member_unknown_action() {
        for action in ["ban", "set_admin", "unset_admin"] {
            let error = validate_generic_command(&json!({
                "op": "update_group_member",
                "reqId": "req-group-member",
                "payload": {
                    "groupId": "group-1",
                    "memberId": "10002",
                    "action": action
                }
            }))
            .expect_err("generic update_group_member with unsupported action should fail");

            assert_eq!(error.code, "invalid_action");
        }
    }

    #[test]
    fn generic_command_rejects_update_group_member_non_canonical_action() {
        for action in ["ADD", " add "] {
            let error = validate_generic_command(&json!({
                "op": "update_group_member",
                "reqId": "req-group-member",
                "payload": {
                    "groupId": "group-1",
                    "memberId": "10002",
                    "action": action
                }
            }))
            .expect_err("generic update_group_member action must match protocol enum exactly");

            assert_eq!(error.code, "invalid_action");
        }
    }

    #[test]
    fn generic_command_accepts_create_group_member_objects() {
        validate_generic_command(&json!({
            "op": "create_group",
            "reqId": "req-create-group",
            "payload": {
                "groupName": "team",
                "members": [
                    "10001",
                    { "account": "10002" },
                    { "id": "10003" },
                    { "memberId": "10004" }
                ],
                "announcement": ""
            }
        }))
        .expect("generic create_group should accept supported member entry shapes");
    }

    #[test]
    fn generic_command_rejects_create_group_non_array_members() {
        let error = validate_generic_command(&json!({
            "op": "create_group",
            "reqId": "req-create-group",
            "payload": {
                "groupName": "team",
                "members": "10001"
            }
        }))
        .expect_err("generic create_group members must be an array");

        assert_eq!(error.code, "invalid_members");
    }

    #[test]
    fn generic_command_rejects_create_group_empty_member_entry() {
        let error = validate_generic_command(&json!({
            "op": "create_group",
            "reqId": "req-create-group",
            "payload": {
                "groupName": "team",
                "members": [""]
            }
        }))
        .expect_err("generic create_group member entries must be non-empty");

        assert_eq!(error.code, "invalid_members");
    }

    #[test]
    fn generic_command_rejects_create_group_invalid_member_object_field() {
        let error = validate_generic_command(&json!({
            "op": "create_group",
            "reqId": "req-create-group",
            "payload": {
                "groupName": "team",
                "members": [{ "userId": 10001 }]
            }
        }))
        .expect_err("generic create_group member object fields must be strings");

        assert_eq!(error.code, "invalid_members");
    }

    #[test]
    fn generic_command_rejects_create_group_invalid_announcement() {
        let error = validate_generic_command(&json!({
            "op": "create_group",
            "reqId": "req-create-group",
            "payload": {
                "groupName": "team",
                "announcement": false
            }
        }))
        .expect_err("generic create_group announcement must be a string when provided");

        assert_eq!(error.code, "invalid_announcement");
    }

    #[test]
    fn generic_command_accepts_empty_group_announcement_update() {
        validate_generic_command(&json!({
            "op": "update_group_announcement",
            "reqId": "req-announcement",
            "payload": {
                "groupId": "group-1",
                "announcement": ""
            }
        }))
        .expect("generic update_group_announcement may clear announcement");
    }

    #[test]
    fn generic_command_rejects_group_message_empty_content() {
        let error = validate_generic_command(&json!({
            "op": "send_group_message",
            "reqId": "req-group-message",
            "payload": {
                "groupId": "group-1",
                "content": ""
            }
        }))
        .expect_err("generic send_group_message content must be non-empty");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn generic_command_rejects_group_message_missing_group_id() {
        let error = validate_generic_command(&json!({
            "op": "send_group_message",
            "reqId": "req-group-message",
            "payload": {
                "content": "hello"
            }
        }))
        .expect_err("generic send_group_message groupId is required");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn generic_command_rejects_create_group_missing_name() {
        let error = validate_generic_command(&json!({
            "op": "create_group",
            "reqId": "req-create-group",
            "payload": {}
        }))
        .expect_err("generic create_group groupName is required");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn generic_command_rejects_group_announcement_missing_group_id() {
        let error = validate_generic_command(&json!({
            "op": "update_group_announcement",
            "reqId": "req-announcement",
            "payload": {
                "announcement": "hello"
            }
        }))
        .expect_err("generic update_group_announcement groupId is required");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn generic_command_accepts_file_transfer_target() {
        validate_generic_command(&json!({
            "op": "send_file",
            "reqId": "req-file",
            "payload": {
                "filePath": "C:/tmp/a.txt",
                "receiverId": "10001"
            }
        }))
        .expect("generic send_file with one target should pass");
    }

    #[test]
    fn generic_command_rejects_file_transfer_missing_target() {
        let error = validate_generic_command(&json!({
            "op": "send_file",
            "reqId": "req-file",
            "payload": {
                "filePath": "C:/tmp/a.txt"
            }
        }))
        .expect_err("generic send_file without target should fail");

        assert_eq!(error.code, "missing_target");
    }

    #[test]
    fn generic_command_rejects_file_transfer_blank_target() {
        let error = validate_generic_command(&json!({
            "op": "send_file",
            "reqId": "req-file",
            "payload": {
                "filePath": "C:/tmp/a.txt",
                "receiverId": "   "
            }
        }))
        .expect_err("generic send_file with blank target should fail");

        assert_eq!(error.code, "missing_target");
    }

    #[test]
    fn generic_command_rejects_file_transfer_ambiguous_target() {
        let error = validate_generic_command(&json!({
            "op": "send_image",
            "reqId": "req-image",
            "payload": {
                "filePath": "C:/tmp/a.png",
                "receiverId": "10001",
                "groupId": "group-1"
            }
        }))
        .expect_err("generic send_image with both targets should fail");

        assert_eq!(error.code, "ambiguous_target");
    }

    #[test]
    fn generic_command_rejects_file_transfer_invalid_target() {
        let error = validate_generic_command(&json!({
            "op": "send_file",
            "reqId": "req-file",
            "payload": {
                "filePath": "C:/tmp/a.txt",
                "receiverId": 10001
            }
        }))
        .expect_err("generic send_file target must be a string");

        assert_eq!(error.code, "invalid_target");
    }

    #[test]
    fn generic_command_rejects_file_transfer_missing_file_path() {
        let error = validate_generic_command(&json!({
            "op": "send_file",
            "reqId": "req-file",
            "payload": {
                "receiverId": "10001"
            }
        }))
        .expect_err("generic send_file filePath is required");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn generic_command_accepts_query_resume_query_only() {
        validate_generic_command(&json!({
            "op": "query_resume",
            "reqId": "req-resume",
            "payload": {
                "transferId": "transfer-1"
            }
        }))
        .expect("query_resume may query by transferId only");
    }

    #[test]
    fn generic_command_rejects_query_resume_missing_transfer_id() {
        let error = validate_generic_command(&json!({
            "op": "query_resume",
            "reqId": "req-resume",
            "payload": {}
        }))
        .expect_err("generic query_resume transferId is required");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn generic_command_rejects_extra_query_resume_payload_field() {
        let error = validate_generic_command(&json!({
            "op": "query_resume",
            "reqId": "req-resume",
            "payload": {
                "transferId": "transfer-1",
                "debug": true
            }
        }))
        .expect_err("generic query_resume should reject unsupported payload fields");

        assert_eq!(error.code, "invalid_payload");
        assert!(error.message.contains("debug"));
    }

    #[test]
    fn generic_command_rejects_query_resume_invalid_file_path() {
        let error = validate_generic_command(&json!({
            "op": "query_resume",
            "reqId": "req-resume",
            "payload": {
                "transferId": "transfer-1",
                "filePath": 1
            }
        }))
        .expect_err("query_resume filePath must be a string when provided");

        assert_eq!(error.code, "invalid_file_path");
    }

    #[test]
    fn generic_command_rejects_query_resume_invalid_content_type() {
        let error = validate_generic_command(&json!({
            "op": "query_resume",
            "reqId": "req-resume",
            "payload": {
                "transferId": "transfer-1",
                "contentType": "video"
            }
        }))
        .expect_err("query_resume contentType is limited to file/image");

        assert_eq!(error.code, "invalid_content_type");
    }

    #[test]
    fn generic_command_rejects_query_resume_non_canonical_content_type() {
        for content_type in ["IMAGE", " image "] {
            let error = validate_generic_command(&json!({
                "op": "query_resume",
                "reqId": "req-resume",
                "payload": {
                    "transferId": "transfer-1",
                    "contentType": content_type
                }
            }))
            .expect_err("query_resume contentType must match protocol enum exactly");

            assert_eq!(error.code, "invalid_content_type");
        }
    }

    #[test]
    fn generic_command_rejects_query_resume_target_without_file_path() {
        let error = validate_generic_command(&json!({
            "op": "query_resume",
            "reqId": "req-resume",
            "payload": {
                "transferId": "transfer-1",
                "receiverId": "10001"
            }
        }))
        .expect_err("query_resume query-only mode should not accept a target");

        assert_eq!(error.code, "invalid_target");
    }

    #[test]
    fn generic_command_rejects_query_resume_resume_missing_target() {
        let error = validate_generic_command(&json!({
            "op": "query_resume",
            "reqId": "req-resume",
            "payload": {
                "transferId": "transfer-1",
                "filePath": "C:/tmp/a.txt"
            }
        }))
        .expect_err("query_resume resume mode requires a target");

        assert_eq!(error.code, "missing_target");
    }

    #[test]
    fn generic_command_rejects_query_resume_resume_blank_target() {
        let error = validate_generic_command(&json!({
            "op": "query_resume",
            "reqId": "req-resume",
            "payload": {
                "transferId": "transfer-1",
                "filePath": "C:/tmp/a.txt",
                "groupId": "   "
            }
        }))
        .expect_err("query_resume resume mode should reject blank targets");

        assert_eq!(error.code, "missing_target");
    }

    #[test]
    fn generic_ack_packet_accepts_matching_ok_ack() {
        validate_generic_ack_packet(
            &json!({
                "type": "ack",
                "op": "send_private_message",
                "reqId": "req-message",
                "status": "ok",
                "payload": { "receiverId": "10001" }
            }),
            "send_private_message",
            "req-message",
        )
        .expect("matching generic ok ack should pass");
    }

    #[test]
    fn generic_ack_packet_accepts_matching_error_ack() {
        validate_generic_ack_packet(
            &json!({
                "type": "ack",
                "op": "send_file",
                "reqId": "req-file",
                "status": "error",
                "error": {
                    "code": "missing_target",
                    "message": "File target is required.",
                    "source": "engine",
                    "details": {
                        "targetFields": ["receiverId", "groupId"]
                    }
                }
            }),
            "send_file",
            "req-file",
        )
        .expect("matching generic error ack should pass through");
    }

    #[test]
    fn generic_ack_packet_requires_file_target_error_details() {
        let error = validate_generic_ack_packet(
            &json!({
                "type": "ack",
                "op": "send_image",
                "reqId": "req-file",
                "status": "error",
                "error": {
                    "code": "ambiguous_target",
                    "message": "File target is ambiguous.",
                    "source": "engine",
                    "details": {
                        "targetFields": ["groupId", "receiverId"]
                    }
                }
            }),
            "send_image",
            "req-file",
        )
        .expect_err("target validation error ack must include canonical target fields");

        assert_eq!(error.code, "invalid_ack_error");
        assert!(error.message.contains("details.targetFields"));
    }

    #[test]
    fn generic_ack_packet_requires_query_resume_target_error_details() {
        let error = validate_generic_ack_packet(
            &json!({
                "type": "ack",
                "op": "query_resume",
                "reqId": "req-resume",
                "status": "error",
                "error": {
                    "code": "invalid_target",
                    "message": "Resume target requires filePath.",
                    "source": "engine",
                    "details": {}
                }
            }),
            "query_resume",
            "req-resume",
        )
        .expect_err("query_resume target error ack must include target fields");

        assert_eq!(error.code, "invalid_ack_error");
        assert!(error.message.contains("details.targetFields"));
    }

    #[test]
    fn generic_ack_packet_rejects_mismatched_op() {
        let error = validate_generic_ack_packet(
            &json!({
                "type": "ack",
                "op": "login",
                "reqId": "req-1",
                "status": "ok",
                "payload": { "accepted": true, "requiresConnect": true, "mode": "login" }
            }),
            "connect",
            "req-1",
        )
        .expect_err("generic ack op must match command op");

        assert_eq!(error.code, "unexpected_ack");
    }

    #[test]
    fn generic_ack_packet_rejects_missing_status() {
        let error = validate_generic_ack_packet(
            &json!({
                "type": "ack",
                "op": "ready",
                "reqId": "req-ready",
                "payload": { "protocolVersion": protocol::EXPECTED_PROTOCOL_VERSION }
            }),
            "ready",
            "req-ready",
        )
        .expect_err("generic ack must include status");

        assert_eq!(error.code, "invalid_ack_status");
    }

    #[test]
    fn generic_ack_packet_rejects_invalid_payload_shape() {
        let error = validate_generic_ack_packet(
            &json!({
                "type": "ack",
                "op": "search_friend",
                "reqId": "req-search",
                "status": "ok",
                "payload": "bad"
            }),
            "search_friend",
            "req-search",
        )
        .expect_err("generic ack payload must be an object");

        assert_eq!(error.code, "invalid_ack_payload");
    }

    #[test]
    fn generic_ack_packet_rejects_malformed_error_ack() {
        let error = validate_generic_ack_packet(
            &json!({
                "type": "ack",
                "op": "send_file",
                "reqId": "req-file",
                "status": "error",
                "error": { "code": "missing_target", "source": "engine" }
            }),
            "send_file",
            "req-file",
        )
        .expect_err("generic error ack must include complete error object");

        assert_eq!(error.code, "invalid_ack_error");
    }

    #[test]
    fn generic_ack_packet_rejects_non_object_error_details() {
        let error = validate_generic_ack_packet(
            &json!({
                "type": "ack",
                "op": "send_file",
                "reqId": "req-file",
                "status": "error",
                "error": {
                    "code": "missing_target",
                    "message": "File target is required.",
                    "source": "engine",
                    "details": "receiverId"
                }
            }),
            "send_file",
            "req-file",
        )
        .expect_err("generic error ack details must be an object");

        assert_eq!(error.code, "invalid_ack_error");
    }

    #[test]
    fn cancel_transfer_payload_requires_transfer_id() {
        let payload = cancel_transfer_payload("transfer-1".to_string())
            .expect("valid cancel_transfer payload should pass");

        assert_eq!(payload["transferId"], "transfer-1");
    }

    #[test]
    fn connect_payload_rejects_invalid_port() {
        let error = connect_payload("127.0.0.1".to_string(), json!(70000))
            .expect_err("typed connect payload should reject invalid TCP port");

        assert_eq!(error.code, "invalid_target");
    }

    #[test]
    fn login_like_payload_rejects_empty_password() {
        for (account, password, user_name) in [
            ("", "secret", None),
            ("10001", "", None),
            ("10001", "secret", Some("")),
        ] {
            let error = login_like_payload(
                account.to_string(),
                password.to_string(),
                user_name.map(str::to_string),
            )
            .expect_err("typed login/register payload should require non-empty fields");

            assert_eq!(error.code, "missing_field");
        }
    }

    #[test]
    fn register_payload_preserves_user_name() {
        let packet = command_packet(
            "register",
            "req-register".to_string(),
            login_like_payload(
                "10001".to_string(),
                "secret".to_string(),
                Some("Alice".to_string()),
            )
            .expect("valid register payload should pass"),
        );

        assert_eq!(packet["payload"]["account"], "10001");
        assert_eq!(packet["payload"]["password"], "secret");
        assert_eq!(packet["payload"]["userName"], "Alice");
    }

    #[test]
    fn set_user_info_payload_rejects_empty_user_name() {
        for (user_id, user_name) in [("", "Alice"), ("10001", " ")] {
            let error = set_user_info_payload(user_id.to_string(), user_name.to_string())
                .expect_err("typed set_user_info payload should require userId and userName");

            assert_eq!(error.code, "missing_field");
        }
    }

    #[test]
    fn friend_command_payloads_require_targets() {
        let search_error = search_friend_payload(" ".to_string())
            .expect_err("typed search_friend should require account");
        let request_error = send_friend_request_payload("".to_string())
            .expect_err("typed send_friend_request should require receiverId");
        let response_error = respond_friend_request_payload("".to_string(), json!(true))
            .expect_err("typed respond_friend_request should require senderId");

        assert_eq!(search_error.code, "missing_field");
        assert_eq!(request_error.code, "missing_field");
        assert_eq!(response_error.code, "missing_field");
    }

    #[test]
    fn respond_friend_request_payload_rejects_non_bool_accepted() {
        let error = respond_friend_request_payload("10001".to_string(), json!("true"))
            .expect_err("typed respond_friend_request should require boolean accepted");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn send_private_message_payload_requires_content() {
        let error = send_private_message_payload("10001".to_string(), "".to_string(), None)
            .expect_err("typed send_private_message payload should require content");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn cancel_transfer_payload_rejects_empty_transfer_id() {
        let error = cancel_transfer_payload(" ".to_string())
            .expect_err("typed cancel_transfer payload should require transferId");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn e2e_payloads_validate_optional_and_required_peer_fields() {
        let status = e2e_status_payload(None).expect("e2e_status may omit peerId");
        let announce_error = e2e_peer_payload("e2e_announce_identity", " ".to_string())
            .expect_err("typed e2e peer command should require peerId");
        let pin_peer_error = e2e_pin_identity_payload("".to_string(), None)
            .expect_err("typed e2e_pin_identity payload should require peerId");
        let pin_payload = e2e_pin_identity_payload("10001".to_string(), Some("".to_string()))
            .expect("empty fingerprint is allowed for local clearing");

        assert!(status.get("peerId").is_none());
        assert_eq!(announce_error.code, "missing_field");
        assert_eq!(pin_peer_error.code, "missing_field");
        assert_eq!(pin_payload["fingerprint"], "");
    }

    #[test]
    fn profile_update_payload_validates_avatar_base64() {
        let packet = command_packet(
            "profile_update",
            "req-profile".to_string(),
            profile_update_payload(Some("Alice".to_string()), Some("aGVsbG8=".to_string()))
                .expect("valid profile_update payload should pass"),
        );
        let error = profile_update_payload(None, Some("not-base64%%%".to_string()))
            .expect_err("typed profile_update payload should reject invalid Base64");

        assert_eq!(packet["payload"]["userName"], "Alice");
        assert_eq!(packet["payload"]["avatarBase64"], "aGVsbG8=");
        assert_eq!(error.code, "invalid_profile_field");
    }

    #[test]
    fn image_mime_detection_accepts_extensionless_png_bytes() {
        let mime = image_mime_from_path_or_bytes(
            Path::new("5f79699f-4cd3-42b9-9340"),
            b"\x89PNG\r\n\x1A\npng-data",
        )
        .expect("extensionless PNG data should be accepted");

        assert_eq!(mime, "image/png");
    }

    #[test]
    fn settings_sync_payload_requires_object_settings() {
        let valid = settings_sync_payload(json!({
            "files": {
                "downloadDirectory": "C:/tmp/downloads"
            }
        }))
        .expect("valid settings_sync payload should pass");
        let error = settings_sync_payload(json!("bad"))
            .expect_err("typed settings_sync payload should require object settings");

        assert!(valid["settings"]["files"].is_object());
        assert_eq!(error.code, "invalid_settings");
    }

    #[test]
    fn settings_sync_payload_rejects_invalid_download_dir_fields() {
        let direct_error = settings_sync_payload(json!({ "fileDownloadDir": false }))
            .expect_err("settings.fileDownloadDir must be a string when provided");
        let files_error = settings_sync_payload(json!({ "files": "bad" }))
            .expect_err("settings.files must be an object when provided");
        let nested_download_dir_error = settings_sync_payload(json!({
            "files": {
                "downloadDir": 42
            }
        }))
        .expect_err("settings.files.downloadDir must be a string when provided");
        let nested_download_directory_error = settings_sync_payload(json!({
            "files": {
                "downloadDirectory": 42
            }
        }))
        .expect_err("settings.files.downloadDirectory must be a string when provided");
        let direct_plus_invalid_files_error = settings_sync_payload(json!({
            "fileDownloadDir": "C:/tmp/downloads",
            "files": "bad"
        }))
        .expect_err("settings.files must still be validated when fileDownloadDir is valid");
        let direct_plus_invalid_nested_error = settings_sync_payload(json!({
            "fileDownloadDir": "C:/tmp/downloads",
            "files": {
                "downloadDirectory": 42
            }
        }))
        .expect_err("settings.files aliases must still be validated when fileDownloadDir is valid");

        assert_eq!(direct_error.code, "invalid_settings");
        assert_eq!(files_error.code, "invalid_settings");
        assert_eq!(nested_download_dir_error.code, "invalid_settings");
        assert_eq!(nested_download_directory_error.code, "invalid_settings");
        assert_eq!(direct_plus_invalid_files_error.code, "invalid_settings");
        assert_eq!(direct_plus_invalid_nested_error.code, "invalid_settings");
    }

    #[test]
    fn send_group_message_payload_requires_content() {
        let error = send_group_message_payload("group-1".to_string(), "".to_string(), None)
            .expect_err("typed send_group_message payload should require content");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn send_group_message_payload_requires_group_id() {
        let error = send_group_message_payload(" ".to_string(), "hello".to_string(), None)
            .expect_err("typed send_group_message payload should require groupId");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn create_group_payload_requires_group_name() {
        let error = create_group_payload("".to_string(), None, None)
            .expect_err("typed create_group payload should require groupName");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn create_group_payload_preserves_members_and_announcement() {
        let packet = command_packet(
            "create_group",
            "req-create-group".to_string(),
            create_group_payload(
                "team".to_string(),
                Some(vec![json!("10001"), json!({ "account": "10002" })]),
                Some("hello".to_string()),
            )
            .expect("valid create_group payload should pass"),
        );

        assert_eq!(packet["payload"]["groupName"], "team");
        assert_eq!(packet["payload"]["members"][0], "10001");
        assert_eq!(packet["payload"]["members"][1]["account"], "10002");
        assert_eq!(packet["payload"]["announcement"], "hello");
    }

    #[test]
    fn create_group_payload_rejects_empty_member_entry() {
        let error = create_group_payload(
            "team".to_string(),
            Some(vec![json!("10001"), json!(" ")]),
            None,
        )
        .expect_err("typed create_group payload should reject empty member ids");

        assert_eq!(error.code, "invalid_members");
    }

    #[test]
    fn update_group_announcement_payload_allows_empty_announcement() {
        let packet = command_packet(
            "update_group_announcement",
            "req-announcement".to_string(),
            update_group_announcement_payload("group-1".to_string(), "".to_string())
                .expect("empty announcement should clear group announcement"),
        );

        assert_eq!(packet["payload"]["groupId"], "group-1");
        assert_eq!(packet["payload"]["announcement"], "");
    }

    #[test]
    fn update_group_announcement_payload_requires_group_id() {
        let error = update_group_announcement_payload(" ".to_string(), "hello".to_string())
            .expect_err("typed update_group_announcement payload should require groupId");

        assert_eq!(error.code, "missing_field");
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
                Some("group-1".to_string()),
                Some("image".to_string()),
            )
            .expect("valid query_resume payload should pass"),
        );

        assert_eq!(packet["payload"]["transferId"], "transfer-1");
        assert_eq!(packet["payload"]["filePath"], "C:/tmp/a.png");
        assert_eq!(packet["payload"]["groupId"], "group-1");
        assert_eq!(packet["payload"]["contentType"], "image");
        assert!(packet["payload"].get("receiverId").is_none());
    }

    #[test]
    fn send_file_like_payload_preserves_single_target() {
        let packet = command_packet(
            "send_file",
            "req-2".to_string(),
            send_file_like_payload(
                "send_file",
                "C:/tmp/a.txt".to_string(),
                Some("10001".to_string()),
                None,
            )
            .expect("valid send_file payload should pass"),
        );

        assert_eq!(packet["payload"]["filePath"], "C:/tmp/a.txt");
        assert_eq!(packet["payload"]["receiverId"], "10001");
        assert!(packet["payload"].get("groupId").is_none());
    }

    #[test]
    fn send_file_like_payload_rejects_ambiguous_target() {
        let error = send_file_like_payload(
            "send_file",
            "C:/tmp/a.txt".to_string(),
            Some("10001".to_string()),
            Some("group-1".to_string()),
        )
        .expect_err("typed send_file payload should reject both targets");

        assert_eq!(error.code, "ambiguous_target");
    }

    #[test]
    fn send_file_like_payload_rejects_blank_target() {
        let error = send_file_like_payload(
            "send_file",
            "C:/tmp/a.txt".to_string(),
            Some("   ".to_string()),
            None,
        )
        .expect_err("typed send_file payload should reject blank target");

        assert_eq!(error.code, "missing_target");
    }

    #[test]
    fn send_file_like_payload_requires_file_path() {
        let error = send_file_like_payload(
            "send_file",
            " ".to_string(),
            Some("10001".to_string()),
            None,
        )
        .expect_err("typed send_file payload should require filePath");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn query_resume_payload_accepts_query_only() {
        let packet = command_packet(
            "query_resume",
            "req-3".to_string(),
            query_resume_payload("transfer-1".to_string(), None, None, None, None)
                .expect("query-only resume payload should pass"),
        );

        assert_eq!(packet["payload"]["transferId"], "transfer-1");
        assert!(packet["payload"].get("filePath").is_none());
        assert!(packet["payload"].get("receiverId").is_none());
        assert!(packet["payload"].get("groupId").is_none());
    }

    #[test]
    fn query_resume_payload_rejects_invalid_content_type() {
        let error = query_resume_payload(
            "transfer-1".to_string(),
            None,
            None,
            None,
            Some("video".to_string()),
        )
        .expect_err("typed query_resume payload should reject unsupported contentType");

        assert_eq!(error.code, "invalid_content_type");
    }

    #[test]
    fn query_resume_payload_rejects_non_canonical_content_type() {
        for content_type in ["IMAGE", " image "] {
            let error = query_resume_payload(
                "transfer-1".to_string(),
                None,
                None,
                None,
                Some(content_type.to_string()),
            )
            .expect_err("typed query_resume payload should require canonical contentType enum");

            assert_eq!(error.code, "invalid_content_type");
        }
    }

    #[test]
    fn query_resume_payload_rejects_target_without_file_path() {
        let error = query_resume_payload(
            "transfer-1".to_string(),
            None,
            Some("10001".to_string()),
            None,
            None,
        )
        .expect_err("typed query_resume payload should reject target without filePath");

        assert_eq!(error.code, "invalid_target");
    }

    #[test]
    fn query_resume_payload_rejects_resume_missing_target() {
        let error = query_resume_payload(
            "transfer-1".to_string(),
            Some("C:/tmp/a.txt".to_string()),
            None,
            None,
            Some("file".to_string()),
        )
        .expect_err("typed query_resume resume payload should require a target");

        assert_eq!(error.code, "missing_target");
    }

    #[test]
    fn query_resume_payload_rejects_resume_blank_target() {
        let error = query_resume_payload(
            "transfer-1".to_string(),
            Some("C:/tmp/a.txt".to_string()),
            None,
            Some("   ".to_string()),
            Some("file".to_string()),
        )
        .expect_err("typed query_resume resume payload should reject blank targets");

        assert_eq!(error.code, "missing_target");
    }

    #[test]
    fn query_resume_payload_requires_transfer_id() {
        let error = query_resume_payload(" ".to_string(), None, None, None, None)
            .expect_err("typed query_resume payload should require transferId");

        assert_eq!(error.code, "missing_field");
    }

    #[test]
    fn update_group_member_payload_accepts_supported_actions() {
        for action in ["add", "remove", "promote_admin", "demote_admin"] {
            let payload = update_group_member_payload(
                "group-1".to_string(),
                "10002".to_string(),
                action.to_string(),
            )
            .expect("supported update_group_member action should pass");

            assert_eq!(payload["groupId"], "group-1");
            assert_eq!(payload["memberId"], "10002");
            assert_eq!(payload["action"], action);
        }
    }

    #[test]
    fn update_group_member_payload_rejects_unknown_action() {
        for action in ["ban", "set_admin", "unset_admin"] {
            let error = update_group_member_payload(
                "group-1".to_string(),
                "10002".to_string(),
                action.to_string(),
            )
            .expect_err(
                "typed update_group_member should reject unsupported actions before engine call",
            );

            assert_eq!(error.code, "invalid_action");
        }
    }

    #[test]
    fn update_group_member_payload_rejects_non_canonical_action() {
        for action in ["ADD", " add "] {
            let error = update_group_member_payload(
                "group-1".to_string(),
                "10002".to_string(),
                action.to_string(),
            )
            .expect_err("typed update_group_member should require canonical action enum");

            assert_eq!(error.code, "invalid_action");
        }
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
            "req-1",
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
            "req-2",
        )
        .expect_err("engine error ack should map to QQNTError");

        assert_eq!(error.code, "login_failed");
        assert_eq!(error.source, "engine");
    }

    #[test]
    fn ack_payload_maps_engine_error_ack_details() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "send_file",
                "reqId": "req-file",
                "status": "error",
                "error": {
                    "code": "missing_target",
                    "message": "File target is required.",
                    "source": "engine",
                    "details": {
                        "targetFields": ["receiverId", "groupId"]
                    }
                }
            }),
            "send_file",
            "req-file",
        )
        .expect_err("engine error ack should map optional details to QQNTError");

        assert_eq!(error.code, "missing_target");
        assert_eq!(error.source, "engine");
        assert_eq!(
            error.details,
            Some(json!({
                "targetFields": ["receiverId", "groupId"]
            }))
        );
    }

    #[test]
    fn ack_payload_rejects_mismatched_req_id() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "connect",
                "reqId": "req-actual",
                "status": "ok",
                "payload": { "connected": true, "host": "127.0.0.1", "port": 8888 }
            }),
            "connect",
            "req-expected",
        )
        .expect_err("typed ack reqId must match command reqId");

        assert_eq!(error.code, "unexpected_ack");
    }

    #[test]
    fn ack_payload_rejects_malformed_error_ack_before_mapping() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "send_file",
                "reqId": "req-file",
                "status": "error",
                "error": { "code": "missing_target", "source": "engine" }
            }),
            "send_file",
            "req-file",
        )
        .expect_err("typed error ack must keep the protocol error envelope");

        assert_eq!(error.code, "invalid_ack_error");
        assert_eq!(error.source, "rust");
    }

    #[test]
    fn ack_payload_rejects_error_ack_mismatched_op_before_mapping() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "login",
                "reqId": "req-login",
                "status": "error",
                "error": { "code": "login_failed", "message": "bad password", "source": "engine" }
            }),
            "connect",
            "req-login",
        )
        .expect_err("typed error ack op must match before engine error mapping");

        assert_eq!(error.code, "unexpected_ack");
        assert_eq!(error.source, "rust");
    }

    #[test]
    fn ack_payload_validates_ready_protocol_version() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "ready",
                "reqId": "req-ready",
                "status": "ok",
                "payload": { "protocolVersion": protocol::EXPECTED_PROTOCOL_VERSION + 1 }
            }),
            "ready",
            "req-ready",
        )
        .expect_err("ready ack protocol mismatch should fail");

        assert_eq!(error.code, "protocol_version_mismatch");
    }

    #[test]
    fn ack_payload_validates_group_list_contract_fields() {
        let payload = ack_payload(
            json!({
                "type": "ack",
                "op": "get_group_list",
                "reqId": "req-groups",
                "status": "ok",
                "payload": {
                    "groups": [],
                    "removedGroups": [],
                    "hasSnapshot": true
                }
            }),
            "get_group_list",
            "req-groups",
        )
        .expect("get_group_list ack with contract fields should pass");

        assert_eq!(payload["hasSnapshot"], true);
    }

    #[test]
    fn ack_payload_validates_connect_contract_fields() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "connect",
                "reqId": "req-connect",
                "status": "ok",
                "payload": { "connected": true, "host": "127.0.0.1", "port": 70000 }
            }),
            "connect",
            "req-connect",
        )
        .expect_err("connect ack with out-of-range port should fail");

        assert_eq!(error.code, "invalid_connect_payload");
    }

    #[test]
    fn ack_payload_validates_login_contract_fields() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "login",
                "reqId": "req-login",
                "status": "ok",
                "payload": { "accepted": true, "requiresConnect": true }
            }),
            "login",
            "req-login",
        )
        .expect_err("login ack without mode should fail");

        assert_eq!(error.code, "invalid_login_payload");
    }

    #[test]
    fn ack_payload_validates_file_send_contract_fields() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "send_file",
                "reqId": "req-file",
                "status": "ok",
                "payload": { "accepted": true }
            }),
            "send_file",
            "req-file",
        )
        .expect_err("send_file ack without transferId should fail");

        assert_eq!(error.code, "invalid_send_file_payload");
    }

    #[test]
    fn ack_payload_validates_send_private_message_contract_fields() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "send_private_message",
                "reqId": "req-message",
                "status": "ok",
                "payload": { "receiverId": "" }
            }),
            "send_private_message",
            "req-message",
        )
        .expect_err("send_private_message ack without non-empty receiverId should fail");

        assert_eq!(error.code, "invalid_send_private_message_payload");
    }

    #[test]
    fn ack_payload_validates_cancel_transfer_contract_fields() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "cancel_transfer",
                "reqId": "req-cancel",
                "status": "ok",
                "payload": { "cancelled": true }
            }),
            "cancel_transfer",
            "req-cancel",
        )
        .expect_err("cancel_transfer ack without transferId should fail");

        assert_eq!(error.code, "invalid_cancel_transfer_payload");
    }

    #[test]
    fn ack_payload_validates_query_resume_contract_fields() {
        let payload = ack_payload(
            json!({
                "type": "ack",
                "op": "query_resume",
                "reqId": "req-resume",
                "status": "ok",
                "payload": {
                    "canResume": true,
                    "transferId": "transfer-1",
                    "confirmedBytes": "128",
                    "nextChunkIndex": "2",
                    "fileSize": "1024",
                    "chunkSize": "64",
                    "chunkCount": "16",
                    "fileHash": "abc123",
                    "receivedChunks": ["0", "1"],
                    "resumed": true,
                    "mode": "resume"
                }
            }),
            "query_resume",
            "req-resume",
        )
        .expect("query_resume ack with contract fields should pass");

        assert_eq!(payload["mode"], "resume");
    }

    #[test]
    fn ack_payload_rejects_query_resume_without_received_chunks() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "query_resume",
                "reqId": "req-resume",
                "status": "ok",
                "payload": {
                    "canResume": true,
                    "transferId": "transfer-1",
                    "confirmedBytes": "128",
                    "nextChunkIndex": "2",
                    "fileSize": "1024",
                    "chunkSize": "64",
                    "chunkCount": "16",
                    "fileHash": "abc123",
                    "resumed": false,
                    "mode": "query"
                }
            }),
            "query_resume",
            "req-resume",
        )
        .expect_err("query_resume ack without receivedChunks should fail");

        assert_eq!(error.code, "invalid_query_resume_payload");
    }

    #[test]
    fn ack_payload_validates_e2e_status_contract_fields() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "e2e_status",
                "reqId": "req-e2e",
                "status": "ok",
                "payload": { "peerId": "10002", "session": {}, "identity": {} }
            }),
            "e2e_status",
            "req-e2e",
        )
        .expect_err("e2e_status ack without localIdentity should fail");

        assert_eq!(error.code, "invalid_e2e_status_payload");
    }

    #[test]
    fn ack_payload_validates_profile_update_contract_fields() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "profile_update",
                "reqId": "req-profile",
                "status": "ok",
                "payload": { "accepted": true, "avatarSent": true }
            }),
            "profile_update",
            "req-profile",
        )
        .expect_err("profile_update ack without userName should fail");

        assert_eq!(error.code, "invalid_profile_update_payload");
    }

    #[test]
    fn ack_payload_validates_user_list_contract_fields() {
        let payload = ack_payload(
            json!({
                "type": "ack",
                "op": "get_user_list",
                "reqId": "req-users",
                "status": "ok",
                "payload": {
                    "users": []
                }
            }),
            "get_user_list",
            "req-users",
        )
        .expect("get_user_list ack with contract fields should pass");

        assert!(payload["users"].is_array());
    }

    #[test]
    fn ack_payload_rejects_friend_list_without_friends() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "get_friend_list",
                "reqId": "req-friends",
                "status": "ok",
                "payload": {
                    "users": []
                }
            }),
            "get_friend_list",
            "req-friends",
        )
        .expect_err("get_friend_list ack without friends should fail");

        assert_eq!(error.code, "invalid_get_friend_list_payload");
    }

    #[test]
    fn ack_payload_rejects_group_list_without_removed_groups() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "get_group_list",
                "reqId": "req-groups",
                "status": "ok",
                "payload": {
                    "groups": [],
                    "hasSnapshot": true
                }
            }),
            "get_group_list",
            "req-groups",
        )
        .expect_err("get_group_list ack without removedGroups should fail");

        assert_eq!(error.code, "invalid_get_group_list_payload");
    }

    #[test]
    fn ack_payload_rejects_settings_sync_without_revision() {
        let error = ack_payload(
            json!({
                "type": "ack",
                "op": "settings_sync",
                "reqId": "req-settings",
                "status": "ok",
                "payload": { "accepted": true, "settings": {} }
            }),
            "settings_sync",
            "req-settings",
        )
        .expect_err("settings_sync ack without revision should fail");

        assert_eq!(error.code, "invalid_settings_sync_payload");
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
            "req-3",
        )
        .expect_err("mismatched ack op should fail");

        assert_eq!(error.code, "unexpected_ack");
    }
}
