use serde_json::Value;

use crate::error::{QQNTError, QQNTResult};

pub const EXPECTED_PROTOCOL_VERSION: u64 = 1;

#[derive(Clone, Copy)]
enum GroupMembershipState {
    Active,
    Removed,
}

impl GroupMembershipState {
    fn as_str(self) -> &'static str {
        match self {
            GroupMembershipState::Active => "active",
            GroupMembershipState::Removed => "removed",
        }
    }
}

pub fn validate_ready_payload(payload: &Value) -> QQNTResult<()> {
    let Some(protocol_version) = payload.get("protocolVersion").and_then(Value::as_u64) else {
        return Err(QQNTError::rust(
            "missing_protocol_version",
            "QQNTEngine ready payload did not include protocolVersion.",
        ));
    };

    if protocol_version != EXPECTED_PROTOCOL_VERSION {
        return Err(QQNTError::rust(
            "protocol_version_mismatch",
            format!(
                "QQNTEngine protocol version {protocol_version} does not match expected version {EXPECTED_PROTOCOL_VERSION}.",
            ),
        ));
    }

    require_non_empty_string_field(payload, "version", "ready")?;
    require_non_empty_string_field(payload, "qtVersion", "ready")?;
    require_non_empty_string_field(payload, "e2eStatus", "ready")?;

    Ok(())
}

pub fn validate_event_payload(event_name: &str, payload: &Value) -> QQNTResult<()> {
    match event_name {
        "ready" => validate_ready_payload(payload),
        "connection_state" => validate_connection_state_payload(payload),
        "login_result" => validate_login_result_payload(payload),
        "friend_search_result" => validate_friend_search_result_payload(payload),
        "user_list" => validate_user_list_payload(payload, "user_list"),
        "friend_list" => validate_friend_list_payload(payload, "friend_list"),
        "user_joined" => validate_user_presence_payload(payload, "user_joined"),
        "user_left" => validate_user_presence_payload(payload, "user_left"),
        "friend_event" => validate_friend_event_payload(payload),
        "message" => validate_message_payload(payload),
        "group_snapshot" => validate_group_collection_payload(payload, "group_snapshot"),
        "group_member_updated" => validate_group_member_updated_payload(payload),
        "file_progress" => validate_file_progress_payload(payload),
        "file_done" => validate_file_done_payload(payload),
        "file_error" => validate_file_error_payload(payload),
        "e2e_session_state" => validate_e2e_session_state_payload(payload),
        "e2e_identity_state" => validate_e2e_identity_state_payload(payload),
        "e2e_rotation_request" => validate_e2e_rotation_request_payload(payload),
        "e2e_rotation_response" => validate_e2e_rotation_response_payload(payload),
        "settings_synced" => validate_settings_synced_payload(payload, "settings_synced"),
        "notification" => validate_notification_payload(payload),
        "error" => validate_error_payload(payload),
        _ => Err(QQNTError::rust(
            "unknown_event",
            format!("QQNTEngine emitted unknown event {event_name}."),
        )),
    }
}

pub fn validate_command_ack_payload(op: &str, payload: &Value) -> QQNTResult<()> {
    match op {
        "ready" => validate_ready_payload(payload),
        "connect" => validate_connect_ack_payload(payload),
        "disconnect" | "logout" | "set_user_info" => validate_empty_object_ack_payload(payload, op),
        "login" => validate_login_ack_payload(payload, "login", "login"),
        "register" => validate_login_ack_payload(payload, "register", "register"),
        "get_user_list" => validate_user_list_payload(payload, "get_user_list"),
        "get_friend_list" => validate_friend_list_payload(payload, "get_friend_list"),
        "get_group_list" => validate_group_collection_payload(payload, "get_group_list"),
        "search_friend"
        | "send_friend_request"
        | "respond_friend_request"
        | "send_group_message"
        | "create_group"
        | "update_group_announcement"
        | "update_group_member"
        | "send_file"
        | "send_image"
        | "e2e_announce_identity"
        | "e2e_pin_identity"
        | "e2e_request_rotation" => validate_bool_ack_payload(payload, op),
        "send_private_message" => validate_send_private_message_ack_payload(payload),
        "cancel_transfer" => validate_cancel_transfer_ack_payload(payload),
        "query_resume" => validate_query_resume_ack_payload(payload),
        "e2e_status" => validate_e2e_status_ack_payload(payload),
        "profile_update" => validate_profile_update_ack_payload(payload),
        "settings_sync" => validate_settings_synced_payload(payload, "settings_sync"),
        _ => Err(QQNTError::rust(
            "unknown_ack_op",
            format!("QQNTEngine returned ack for unknown op {op}."),
        )),
    }
}

fn validate_connect_ack_payload(payload: &Value) -> QQNTResult<()> {
    require_true_bool_field(payload, "connected", "connect")?;
    require_non_empty_string_field(payload, "host", "connect")?;
    require_tcp_port_field(payload, "port", "connect")?;

    Ok(())
}

fn validate_login_ack_payload(
    payload: &Value,
    contract_name: &str,
    expected_mode: &str,
) -> QQNTResult<()> {
    require_true_bool_field(payload, "accepted", contract_name)?;
    require_bool_field(payload, "requiresConnect", contract_name)?;
    require_string_value_field(payload, "mode", expected_mode, contract_name)?;

    Ok(())
}

fn validate_bool_ack_payload(payload: &Value, contract_name: &str) -> QQNTResult<()> {
    require_true_bool_field(payload, "accepted", contract_name)?;

    Ok(())
}

fn validate_empty_object_ack_payload(payload: &Value, contract_name: &str) -> QQNTResult<()> {
    if matches!(payload, Value::Object(map) if map.is_empty()) {
        return Ok(());
    }

    Err(QQNTError::rust(
        format!("invalid_{contract_name}_payload"),
        format!("QQNTEngine {contract_name} payload must be an empty object."),
    ))
}

fn validate_send_private_message_ack_payload(payload: &Value) -> QQNTResult<()> {
    require_non_empty_string_field(payload, "receiverId", "send_private_message")?;

    Ok(())
}

fn validate_cancel_transfer_ack_payload(payload: &Value) -> QQNTResult<()> {
    require_true_bool_field(payload, "cancelled", "cancel_transfer")?;
    require_non_empty_string_field(payload, "transferId", "cancel_transfer")?;

    Ok(())
}

fn validate_query_resume_ack_payload(payload: &Value) -> QQNTResult<()> {
    require_true_bool_field(payload, "canResume", "query_resume")?;
    require_non_empty_string_field(payload, "transferId", "query_resume")?;
    require_unsigned_integer_string_field(payload, "confirmedBytes", "query_resume")?;
    require_unsigned_integer_string_field(payload, "nextChunkIndex", "query_resume")?;
    require_unsigned_integer_string_field(payload, "fileSize", "query_resume")?;
    require_unsigned_integer_string_field(payload, "chunkSize", "query_resume")?;
    require_unsigned_integer_string_field(payload, "chunkCount", "query_resume")?;
    require_string_field(payload, "fileHash", "query_resume")?;
    require_unsigned_integer_string_array_field(payload, "receivedChunks", "query_resume")?;
    require_bool_field(payload, "resumed", "query_resume")?;
    require_one_of_string_field(payload, "mode", &["query", "resume"], "query_resume")?;

    Ok(())
}

fn validate_e2e_status_ack_payload(payload: &Value) -> QQNTResult<()> {
    require_object_field(payload, "localIdentity", "e2e_status")?;

    let has_peer_id = payload.get("peerId").is_some();
    let has_session = payload.get("session").is_some();
    let has_identity = payload.get("identity").is_some();
    if !has_peer_id && (has_session || has_identity) {
        return Err(QQNTError::rust(
            "invalid_e2e_status_payload",
            "QQNTEngine e2e_status payload must include peerId when session or identity is present.",
        ));
    }

    if has_peer_id {
        require_non_empty_string_field(payload, "peerId", "e2e_status")?;
        require_object_field(payload, "session", "e2e_status")?;
        require_object_field(payload, "identity", "e2e_status")?;
    }

    Ok(())
}

fn validate_profile_update_ack_payload(payload: &Value) -> QQNTResult<()> {
    require_true_bool_field(payload, "accepted", "profile_update")?;
    require_bool_field(payload, "avatarSent", "profile_update")?;
    require_string_field(payload, "userName", "profile_update")?;

    Ok(())
}

fn validate_group_collection_payload(payload: &Value, contract_name: &str) -> QQNTResult<()> {
    require_group_array_field(
        payload,
        "groups",
        contract_name,
        GroupMembershipState::Active,
    )?;
    require_group_array_field(
        payload,
        "removedGroups",
        contract_name,
        GroupMembershipState::Removed,
    )?;
    require_bool_field(payload, "hasSnapshot", contract_name)?;

    Ok(())
}

fn require_group_array_field(
    payload: &Value,
    field: &str,
    contract_name: &str,
    expected_state: GroupMembershipState,
) -> QQNTResult<()> {
    let Some(items) = payload.get(field).and_then(Value::as_array) else {
        return Err(QQNTError::rust(
            format!("invalid_{contract_name}_payload"),
            format!("QQNTEngine {contract_name} payload must include {field} array."),
        ));
    };

    for item in items {
        validate_group_item_payload(item, contract_name, expected_state)?;
    }

    Ok(())
}

fn validate_group_item_payload(
    payload: &Value,
    contract_name: &str,
    expected_state: GroupMembershipState,
) -> QQNTResult<()> {
    if !payload.is_object() {
        return Err(QQNTError::rust(
            format!("invalid_{contract_name}_payload"),
            format!("QQNTEngine {contract_name} payload group entries must be objects."),
        ));
    }

    require_non_empty_string_field(payload, "groupId", contract_name)?;
    require_string_field(payload, "groupName", contract_name)?;
    require_string_field(payload, "announcement", contract_name)?;
    require_string_field(payload, "ownerId", contract_name)?;
    require_one_of_string_field(payload, "groupType", &["public", "private"], contract_name)?;
    require_string_value_field(
        payload,
        "membershipState",
        expected_state.as_str(),
        contract_name,
    )?;
    require_string_field(payload, "historyPolicy", contract_name)?;
    require_string_field(payload, "filePolicy", contract_name)?;
    require_bool_field(payload, "canSend", contract_name)?;
    require_bool_field(payload, "canSendFiles", contract_name)?;
    require_bool_field(payload, "canReadHistory", contract_name)?;
    require_string_field(payload, "historyVisibility", contract_name)?;
    require_bool_field(payload, "historyReadOnly", contract_name)?;
    require_bool_field(payload, "historyRetainedAfterRemoval", contract_name)?;

    match expected_state {
        GroupMembershipState::Active => {
            require_group_member_array_field(payload, "members", contract_name)?;
            require_group_audit_event_array_field(payload, "auditEvents", contract_name)?;
        }
        GroupMembershipState::Removed => {
            require_string_field(payload, "removedBy", contract_name)?;
            require_string_field(payload, "removedByName", contract_name)?;
            require_string_field(payload, "removedAt", contract_name)?;
        }
    }

    Ok(())
}

fn require_group_member_array_field(
    payload: &Value,
    field: &str,
    contract_name: &str,
) -> QQNTResult<()> {
    let Some(items) = payload.get(field).and_then(Value::as_array) else {
        return Err(QQNTError::rust(
            format!("invalid_{contract_name}_payload"),
            format!("QQNTEngine {contract_name} payload must include {field} array."),
        ));
    };

    for item in items {
        validate_group_member_item_payload(item, contract_name)?;
    }

    Ok(())
}

fn validate_group_member_item_payload(payload: &Value, contract_name: &str) -> QQNTResult<()> {
    if !payload.is_object() {
        return Err(QQNTError::rust(
            format!("invalid_{contract_name}_payload"),
            format!("QQNTEngine {contract_name} payload group member entries must be objects."),
        ));
    }

    require_non_empty_string_field(payload, "userId", contract_name)?;
    require_string_field(payload, "userName", contract_name)?;
    require_one_of_string_field(
        payload,
        "role",
        &["owner", "admin", "member"],
        contract_name,
    )?;

    Ok(())
}

fn require_group_audit_event_array_field(
    payload: &Value,
    field: &str,
    contract_name: &str,
) -> QQNTResult<()> {
    let Some(items) = payload.get(field).and_then(Value::as_array) else {
        return Err(QQNTError::rust(
            format!("invalid_{contract_name}_payload"),
            format!("QQNTEngine {contract_name} payload must include {field} array."),
        ));
    };

    for item in items {
        validate_group_audit_event_item_payload(item, contract_name)?;
    }

    Ok(())
}

fn validate_group_audit_event_item_payload(payload: &Value, contract_name: &str) -> QQNTResult<()> {
    if !payload.is_object() {
        return Err(QQNTError::rust(
            format!("invalid_{contract_name}_payload"),
            format!("QQNTEngine {contract_name} payload group audit entries must be objects."),
        ));
    }

    require_non_empty_string_field(payload, "action", contract_name)?;
    require_string_field(payload, "actorId", contract_name)?;
    require_string_field(payload, "actorName", contract_name)?;
    require_string_field(payload, "targetUserId", contract_name)?;
    require_string_field(payload, "targetUserName", contract_name)?;
    require_object_field(payload, "details", contract_name)?;
    require_string_field(payload, "createdAt", contract_name)?;

    Ok(())
}

fn validate_connection_state_payload(payload: &Value) -> QQNTResult<()> {
    require_bool_field(payload, "connected", "connection_state")?;
    require_string_field(payload, "host", "connection_state")?;
    require_unsigned_number_field(payload, "port", "connection_state")?;

    Ok(())
}

fn validate_login_result_payload(payload: &Value) -> QQNTResult<()> {
    require_bool_field(payload, "success", "login_result")?;
    if payload.get("success").and_then(Value::as_bool) == Some(false) {
        require_non_empty_string_field(payload, "error", "login_result")?;
        return Ok(());
    }

    require_non_empty_string_field(payload, "userId", "login_result")?;
    require_string_field(payload, "userName", "login_result")?;
    require_bool_field(payload, "registered", "login_result")?;
    require_optional_string_field(payload, "error", "login_result")?;

    Ok(())
}

fn validate_friend_search_result_payload(payload: &Value) -> QQNTResult<()> {
    require_bool_field(payload, "found", "friend_search_result")?;
    if payload.get("found").and_then(Value::as_bool) == Some(false) {
        require_non_empty_string_field(payload, "reason", "friend_search_result")?;
        require_optional_string_field(payload, "userId", "friend_search_result")?;
        require_optional_string_field(payload, "userName", "friend_search_result")?;
        require_optional_bool_field(payload, "online", "friend_search_result")?;
        return Ok(());
    }

    require_non_empty_string_field(payload, "userId", "friend_search_result")?;
    require_string_field(payload, "userName", "friend_search_result")?;
    require_bool_field(payload, "online", "friend_search_result")?;
    require_optional_string_field(payload, "reason", "friend_search_result")?;

    Ok(())
}

fn validate_user_list_payload(payload: &Value, contract_name: &str) -> QQNTResult<()> {
    require_user_array_field(payload, "users", contract_name)?;

    Ok(())
}

fn validate_friend_list_payload(payload: &Value, contract_name: &str) -> QQNTResult<()> {
    require_user_array_field(payload, "friends", contract_name)?;

    Ok(())
}

fn require_user_array_field(payload: &Value, field: &str, contract_name: &str) -> QQNTResult<()> {
    let Some(items) = payload.get(field).and_then(Value::as_array) else {
        return Err(QQNTError::rust(
            format!("invalid_{contract_name}_payload"),
            format!("QQNTEngine {contract_name} payload must include {field} array."),
        ));
    };

    for item in items {
        validate_user_item_payload(item, contract_name)?;
    }

    Ok(())
}

fn validate_user_item_payload(payload: &Value, contract_name: &str) -> QQNTResult<()> {
    if !payload.is_object() {
        return Err(QQNTError::rust(
            format!("invalid_{contract_name}_payload"),
            format!("QQNTEngine {contract_name} payload user entries must be objects."),
        ));
    }

    require_non_empty_string_field(payload, "id", contract_name)?;
    require_string_field(payload, "name", contract_name)?;
    require_string_field(payload, "avatar", contract_name)?;
    require_bool_field(payload, "online", contract_name)?;
    require_string_field(payload, "lastActive", contract_name)?;

    Ok(())
}

fn validate_user_presence_payload(payload: &Value, contract_name: &str) -> QQNTResult<()> {
    require_non_empty_string_field(payload, "userId", contract_name)?;
    require_string_field(payload, "userName", contract_name)?;

    Ok(())
}

fn validate_friend_event_payload(payload: &Value) -> QQNTResult<()> {
    require_non_empty_string_field(payload, "type", "friend_event")?;

    match payload.get("type").and_then(Value::as_str) {
        Some("request_received") => {
            require_non_empty_string_field(payload, "senderId", "friend_event")?;
            require_string_field(payload, "senderName", "friend_event")?;
        }
        Some("request_sent") => {
            require_non_empty_string_field(payload, "receiverId", "friend_event")?;
            require_bool_field(payload, "delivered", "friend_event")?;
        }
        Some("response_received") => {
            require_non_empty_string_field(payload, "senderId", "friend_event")?;
            require_string_field(payload, "senderName", "friend_event")?;
            require_bool_field(payload, "accepted", "friend_event")?;
        }
        _ => {
            return Err(QQNTError::rust(
                "invalid_friend_event_payload",
                "QQNTEngine friend_event payload must include a supported type.",
            ));
        }
    }

    Ok(())
}

fn validate_message_payload(payload: &Value) -> QQNTResult<()> {
    require_non_empty_string_field(payload, "sessionId", "message")?;
    require_object_field(payload, "message", "message")?;
    let session_id = payload
        .get("sessionId")
        .and_then(Value::as_str)
        .expect("sessionId was validated as a non-empty string");
    let message = payload
        .get("message")
        .expect("message was validated as an object");

    require_non_empty_string_field(message, "messageId", "message")?;
    require_non_empty_string_field(message, "sessionId", "message")?;
    if message.get("sessionId").and_then(Value::as_str) != Some(session_id) {
        return Err(QQNTError::rust(
            "invalid_message_payload",
            "QQNTEngine message payload sessionId must match message.sessionId.",
        ));
    }
    require_string_field(message, "senderId", "message")?;
    require_string_field(message, "senderName", "message")?;
    require_string_or_number_field(message, "timestamp", "message")?;
    require_one_of_string_field(
        message,
        "contentType",
        &["text", "image", "file", "system"],
        "message",
    )?;
    require_string_field(message, "content", "message")?;
    require_one_of_string_field(
        message,
        "status",
        &["sending", "sent", "failed", "received"],
        "message",
    )?;
    require_optional_string_field(message, "clientMessageId", "message")?;

    Ok(())
}

fn validate_e2e_session_state_payload(payload: &Value) -> QQNTResult<()> {
    require_non_empty_string_field(payload, "peerId", "e2e_session_state")?;
    require_bool_field(payload, "rotationRequired", "e2e_session_state")?;

    Ok(())
}

fn validate_e2e_identity_state_payload(payload: &Value) -> QQNTResult<()> {
    require_non_empty_string_field(payload, "peerId", "e2e_identity_state")?;
    require_optional_bool_field(payload, "configured", "e2e_identity_state")?;
    require_bool_field(payload, "trusted", "e2e_identity_state")?;
    if payload.get("configured").and_then(Value::as_bool) == Some(false) {
        require_optional_non_empty_string_field(
            payload,
            "publicKeyFingerprintSha256",
            "e2e_identity_state",
        )?;
    } else {
        require_non_empty_string_field(
            payload,
            "publicKeyFingerprintSha256",
            "e2e_identity_state",
        )?;
    }

    Ok(())
}

fn validate_e2e_rotation_request_payload(payload: &Value) -> QQNTResult<()> {
    require_non_empty_string_field(payload, "peerId", "e2e_rotation_request")?;
    require_object_field(payload, "agreement", "e2e_rotation_request")?;

    Ok(())
}

fn validate_e2e_rotation_response_payload(payload: &Value) -> QQNTResult<()> {
    require_non_empty_string_field(payload, "peerId", "e2e_rotation_response")?;
    require_object_field(payload, "agreement", "e2e_rotation_response")?;
    require_bool_field(payload, "accepted", "e2e_rotation_response")?;
    require_optional_string_field(payload, "reason", "e2e_rotation_response")?;

    Ok(())
}

fn validate_file_progress_payload(payload: &Value) -> QQNTResult<()> {
    require_non_empty_string_field(payload, "transferId", "file_progress")?;
    require_non_empty_string_field(payload, "fileName", "file_progress")?;
    require_one_of_string_field(
        payload,
        "direction",
        &["incoming", "outgoing"],
        "file_progress",
    )?;
    require_string_or_number_field(payload, "bytes", "file_progress")?;
    require_string_or_number_field(payload, "total", "file_progress")?;

    Ok(())
}

fn validate_file_done_payload(payload: &Value) -> QQNTResult<()> {
    require_non_empty_string_field(payload, "transferId", "file_done")?;
    require_non_empty_string_field(payload, "fileName", "file_done")?;
    require_one_of_string_field(payload, "direction", &["incoming", "outgoing"], "file_done")?;
    require_string_field(payload, "filePath", "file_done")?;

    Ok(())
}

fn validate_file_error_payload(payload: &Value) -> QQNTResult<()> {
    require_non_empty_string_field(payload, "transferId", "file_error")?;
    require_non_empty_string_field(payload, "fileName", "file_error")?;
    require_non_empty_string_field(payload, "reason", "file_error")?;
    require_one_of_string_field(
        payload,
        "direction",
        &["incoming", "outgoing"],
        "file_error",
    )?;
    require_string_or_number_field(payload, "bytes", "file_error")?;
    require_string_or_number_field(payload, "total", "file_error")?;

    Ok(())
}

fn validate_group_member_updated_payload(payload: &Value) -> QQNTResult<()> {
    require_non_empty_string_field(payload, "groupId", "group_member_updated")?;
    require_non_empty_string_field(payload, "memberId", "group_member_updated")?;
    require_one_of_string_field(
        payload,
        "action",
        &["add", "remove", "promote_admin", "demote_admin"],
        "group_member_updated",
    )?;

    Ok(())
}

fn validate_settings_synced_payload(payload: &Value, contract_name: &str) -> QQNTResult<()> {
    require_bool_field(payload, "accepted", contract_name)?;
    require_positive_unsigned_number_field(payload, "revision", contract_name)?;
    require_object_field(payload, "settings", contract_name)?;
    require_optional_non_empty_string_field(payload, "appliedDownloadDir", contract_name)?;

    Ok(())
}

fn validate_notification_payload(payload: &Value) -> QQNTResult<()> {
    require_non_empty_string_field(payload, "title", "notification")?;
    require_non_empty_string_field(payload, "body", "notification")?;

    Ok(())
}

fn validate_error_payload(payload: &Value) -> QQNTResult<()> {
    require_non_empty_string_field(payload, "message", "error")?;
    require_non_empty_string_field(payload, "source", "error")?;

    Ok(())
}

fn require_bool_field(payload: &Value, field: &str, contract_name: &str) -> QQNTResult<()> {
    if matches!(payload.get(field), Some(Value::Bool(_))) {
        return Ok(());
    }

    Err(QQNTError::rust(
        format!("invalid_{contract_name}_payload"),
        format!("QQNTEngine {contract_name} payload must include {field} boolean."),
    ))
}

fn require_unsigned_number_field(
    payload: &Value,
    field: &str,
    contract_name: &str,
) -> QQNTResult<()> {
    if payload.get(field).and_then(Value::as_u64).is_some() {
        return Ok(());
    }

    Err(QQNTError::rust(
        format!("invalid_{contract_name}_payload"),
        format!("QQNTEngine {contract_name} payload must include {field} unsigned number."),
    ))
}

fn require_positive_unsigned_number_field(
    payload: &Value,
    field: &str,
    contract_name: &str,
) -> QQNTResult<()> {
    if matches!(payload.get(field).and_then(Value::as_u64), Some(value) if value > 0) {
        return Ok(());
    }

    Err(QQNTError::rust(
        format!("invalid_{contract_name}_payload"),
        format!(
            "QQNTEngine {contract_name} payload must include {field} positive unsigned number."
        ),
    ))
}

fn require_tcp_port_field(payload: &Value, field: &str, contract_name: &str) -> QQNTResult<()> {
    if matches!(
        payload.get(field).and_then(Value::as_u64),
        Some(value) if (1..=u16::MAX as u64).contains(&value)
    ) {
        return Ok(());
    }

    Err(QQNTError::rust(
        format!("invalid_{contract_name}_payload"),
        format!("QQNTEngine {contract_name} payload must include {field} TCP port number."),
    ))
}

fn require_object_field(payload: &Value, field: &str, contract_name: &str) -> QQNTResult<()> {
    if matches!(payload.get(field), Some(Value::Object(_))) {
        return Ok(());
    }

    Err(QQNTError::rust(
        format!("invalid_{contract_name}_payload"),
        format!("QQNTEngine {contract_name} payload must include {field} object."),
    ))
}

fn require_non_empty_string_field(
    payload: &Value,
    field: &str,
    contract_name: &str,
) -> QQNTResult<()> {
    if matches!(
        payload.get(field).and_then(Value::as_str),
        Some(value) if !value.trim().is_empty()
    ) {
        return Ok(());
    }

    Err(QQNTError::rust(
        format!("invalid_{contract_name}_payload"),
        format!("QQNTEngine {contract_name} payload must include non-empty {field} string."),
    ))
}

fn require_true_bool_field(payload: &Value, field: &str, contract_name: &str) -> QQNTResult<()> {
    if matches!(payload.get(field), Some(Value::Bool(true))) {
        return Ok(());
    }

    Err(QQNTError::rust(
        format!("invalid_{contract_name}_payload"),
        format!("QQNTEngine {contract_name} payload must include {field} true."),
    ))
}

fn require_string_field(payload: &Value, field: &str, contract_name: &str) -> QQNTResult<()> {
    if matches!(payload.get(field), Some(Value::String(_))) {
        return Ok(());
    }

    Err(QQNTError::rust(
        format!("invalid_{contract_name}_payload"),
        format!("QQNTEngine {contract_name} payload must include {field} string."),
    ))
}

fn require_string_value_field(
    payload: &Value,
    field: &str,
    expected: &str,
    contract_name: &str,
) -> QQNTResult<()> {
    if payload.get(field).and_then(Value::as_str) == Some(expected) {
        return Ok(());
    }

    Err(QQNTError::rust(
        format!("invalid_{contract_name}_payload"),
        format!(
            "QQNTEngine {contract_name} payload must include {field} string equal to {expected}."
        ),
    ))
}

fn require_one_of_string_field(
    payload: &Value,
    field: &str,
    expected: &[&str],
    contract_name: &str,
) -> QQNTResult<()> {
    match payload.get(field).and_then(Value::as_str) {
        Some(value) if expected.contains(&value) => Ok(()),
        _ => Err(QQNTError::rust(
            format!("invalid_{contract_name}_payload"),
            format!(
                "QQNTEngine {contract_name} payload must include {field} as a supported string."
            ),
        )),
    }
}

fn require_unsigned_integer_string_field(
    payload: &Value,
    field: &str,
    contract_name: &str,
) -> QQNTResult<()> {
    if matches!(
        payload.get(field).and_then(Value::as_str),
        Some(value) if value.parse::<u64>().is_ok()
    ) {
        return Ok(());
    }

    Err(QQNTError::rust(
        format!("invalid_{contract_name}_payload"),
        format!("QQNTEngine {contract_name} payload must include {field} unsigned integer string."),
    ))
}

fn require_unsigned_integer_string_array_field(
    payload: &Value,
    field: &str,
    contract_name: &str,
) -> QQNTResult<()> {
    match payload.get(field).and_then(Value::as_array) {
        Some(items)
            if items.iter().all(|item| {
                matches!(
                    item.as_str(),
                    Some(value) if value.parse::<u64>().is_ok()
                )
            }) =>
        {
            Ok(())
        }
        _ => Err(QQNTError::rust(
            format!("invalid_{contract_name}_payload"),
            format!(
                "QQNTEngine {contract_name} payload must include {field} as an unsigned integer string array."
            ),
        )),
    }
}

fn require_optional_non_empty_string_field(
    payload: &Value,
    field: &str,
    contract_name: &str,
) -> QQNTResult<()> {
    match payload.get(field) {
        None => Ok(()),
        Some(Value::String(value)) if !value.trim().is_empty() => Ok(()),
        _ => Err(QQNTError::rust(
            format!("invalid_{contract_name}_payload"),
            format!(
                "QQNTEngine {contract_name} payload must include {field} as a non-empty string when present."
            ),
        )),
    }
}

fn require_optional_string_field(
    payload: &Value,
    field: &str,
    contract_name: &str,
) -> QQNTResult<()> {
    match payload.get(field) {
        None | Some(Value::String(_)) => Ok(()),
        _ => Err(QQNTError::rust(
            format!("invalid_{contract_name}_payload"),
            format!(
                "QQNTEngine {contract_name} payload must include {field} as a string when present."
            ),
        )),
    }
}

fn require_optional_bool_field(
    payload: &Value,
    field: &str,
    contract_name: &str,
) -> QQNTResult<()> {
    match payload.get(field) {
        None | Some(Value::Bool(_)) => Ok(()),
        _ => Err(QQNTError::rust(
            format!("invalid_{contract_name}_payload"),
            format!(
                "QQNTEngine {contract_name} payload must include {field} as a boolean when present."
            ),
        )),
    }
}

fn require_string_or_number_field(
    payload: &Value,
    field: &str,
    contract_name: &str,
) -> QQNTResult<()> {
    match payload.get(field) {
        Some(Value::Number(_)) => Ok(()),
        Some(Value::String(value)) if !value.trim().is_empty() => Ok(()),
        _ => Err(QQNTError::rust(
            format!("invalid_{contract_name}_payload"),
            format!(
                "QQNTEngine {contract_name} payload must include {field} as a string or number."
            ),
        )),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    const VALIDATED_COMMAND_ACK_OPS: &[&str] = &[
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

    const VALIDATED_EVENT_NAMES: &[&str] = &[
        "ready",
        "connection_state",
        "login_result",
        "user_list",
        "user_joined",
        "user_left",
        "friend_list",
        "friend_event",
        "friend_search_result",
        "message",
        "group_snapshot",
        "group_member_updated",
        "file_progress",
        "file_done",
        "file_error",
        "e2e_session_state",
        "e2e_identity_state",
        "e2e_rotation_request",
        "e2e_rotation_response",
        "settings_synced",
        "notification",
        "error",
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

    fn object_keys<'a>(value: &'a Value, key: &str) -> Vec<&'a str> {
        value[key]
            .as_object()
            .expect("protocol contract key should be an object")
            .keys()
            .map(|item| item.as_str())
            .collect()
    }

    fn user_contract_item() -> Value {
        json!({
            "id": "10002",
            "name": "Bob",
            "avatar": "",
            "online": true,
            "lastActive": ""
        })
    }

    fn group_contract_item() -> Value {
        json!({
            "groupId": "private-1",
            "groupName": "Backend Group",
            "announcement": "ship it",
            "ownerId": "10001",
            "groupType": "private",
            "membershipState": "active",
            "historyPolicy": "member-and-removed-readonly",
            "filePolicy": "members-only",
            "canSend": true,
            "canSendFiles": true,
            "canReadHistory": true,
            "historyVisibility": "active-members-and-removed-readonly",
            "historyReadOnly": false,
            "historyRetainedAfterRemoval": true,
            "members": [{
                "userId": "10001",
                "userName": "Alice",
                "role": "owner"
            }],
            "auditEvents": [{
                "action": "create_group",
                "actorId": "10001",
                "actorName": "Alice",
                "targetUserId": "",
                "targetUserName": "",
                "details": { "groupName": "Backend Group" },
                "createdAt": "2026-06-18T10:00:00Z"
            }]
        })
    }

    fn removed_group_contract_item() -> Value {
        json!({
            "groupId": "private-2",
            "groupName": "Archive Group",
            "announcement": "",
            "ownerId": "10001",
            "groupType": "private",
            "membershipState": "removed",
            "historyPolicy": "member-and-removed-readonly",
            "filePolicy": "members-only",
            "canSend": false,
            "canSendFiles": false,
            "canReadHistory": true,
            "historyVisibility": "removed-member-readonly",
            "historyReadOnly": true,
            "historyRetainedAfterRemoval": true,
            "removedBy": "10001",
            "removedByName": "Alice",
            "removedAt": "2026-06-18T10:30:00Z"
        })
    }

    fn contract_command_ack_payload(op: &str) -> Value {
        match op {
            "ready" => json!({
                "protocolVersion": EXPECTED_PROTOCOL_VERSION,
                "version": "test",
                "qtVersion": "6.8.0",
                "e2eStatus": "uninitialized"
            }),
            "connect" => json!({
                "connected": true,
                "host": "127.0.0.1",
                "port": 8888
            }),
            "disconnect" | "logout" | "set_user_info" => json!({}),
            "login" => json!({
                "accepted": true,
                "requiresConnect": false,
                "mode": "login"
            }),
            "register" => json!({
                "accepted": true,
                "requiresConnect": true,
                "mode": "register"
            }),
            "get_user_list" => json!({
                "users": [{
                    "id": "10002",
                    "name": "Bob",
                    "avatar": "",
                    "online": true,
                    "lastActive": ""
                }]
            }),
            "get_friend_list" => json!({
                "friends": [{
                    "id": "10002",
                    "name": "Bob",
                    "avatar": "",
                    "online": true,
                    "lastActive": ""
                }]
            }),
            "get_group_list" => json!({
                "groups": [group_contract_item()],
                "removedGroups": [removed_group_contract_item()],
                "hasSnapshot": false
            }),
            "search_friend"
            | "send_friend_request"
            | "respond_friend_request"
            | "send_group_message"
            | "create_group"
            | "update_group_announcement"
            | "update_group_member"
            | "send_file"
            | "send_image"
            | "e2e_announce_identity"
            | "e2e_pin_identity"
            | "e2e_request_rotation" => json!({ "accepted": true }),
            "send_private_message" => json!({ "receiverId": "10001" }),
            "cancel_transfer" => json!({
                "cancelled": true,
                "transferId": "transfer-1"
            }),
            "query_resume" => json!({
                "canResume": true,
                "transferId": "transfer-1",
                "confirmedBytes": "128",
                "nextChunkIndex": "2",
                "fileSize": "1024",
                "chunkSize": "64",
                "chunkCount": "16",
                "fileHash": "abc123",
                "receivedChunks": ["0", "1"],
                "resumed": false,
                "mode": "query"
            }),
            "e2e_status" => json!({ "localIdentity": {} }),
            "profile_update" => json!({
                "accepted": true,
                "avatarSent": false,
                "userName": "Alice"
            }),
            "settings_sync" => json!({
                "accepted": true,
                "revision": 1,
                "settings": {}
            }),
            _ => panic!("missing command ack contract sample for {op}"),
        }
    }

    fn contract_event_payload(event_name: &str) -> Value {
        match event_name {
            "ready" => json!({
                "protocolVersion": EXPECTED_PROTOCOL_VERSION,
                "version": "test",
                "qtVersion": "6.8.0",
                "e2eStatus": "uninitialized"
            }),
            "connection_state" => json!({
                "connected": true,
                "host": "127.0.0.1",
                "port": 8888
            }),
            "login_result" => json!({
                "success": true,
                "userId": "10001",
                "userName": "Alice",
                "registered": false
            }),
            "user_list" => json!({
                "users": [{
                    "id": "10002",
                    "name": "Bob",
                    "avatar": "",
                    "online": true,
                    "lastActive": ""
                }]
            }),
            "user_joined" | "user_left" => json!({
                "userId": "10002",
                "userName": "Bob"
            }),
            "friend_list" => json!({
                "friends": [{
                    "id": "10002",
                    "name": "Bob",
                    "avatar": "",
                    "online": true,
                    "lastActive": ""
                }]
            }),
            "friend_event" => json!({
                "type": "request_received",
                "senderId": "10002",
                "senderName": "Bob"
            }),
            "friend_search_result" => json!({
                "found": true,
                "userId": "10002",
                "userName": "Bob",
                "online": true
            }),
            "message" => json!({
                "sessionId": "10002",
                "message": {
                    "messageId": "message-1",
                    "sessionId": "10002",
                    "senderId": "10001",
                    "senderName": "Alice",
                    "timestamp": "1710000000000",
                    "contentType": "text",
                    "content": "hello",
                    "status": "received"
                }
            }),
            "group_snapshot" => json!({
                "groups": [group_contract_item()],
                "removedGroups": [removed_group_contract_item()],
                "hasSnapshot": true
            }),
            "group_member_updated" => json!({
                "groupId": "group-1",
                "memberId": "10002",
                "action": "add"
            }),
            "file_progress" => json!({
                "transferId": "transfer-1",
                "fileName": "contract.bin",
                "bytes": "128",
                "total": "256",
                "direction": "outgoing"
            }),
            "file_done" => json!({
                "transferId": "transfer-1",
                "fileName": "contract.bin",
                "filePath": "C:/tmp/contract.bin",
                "direction": "incoming"
            }),
            "file_error" => json!({
                "transferId": "transfer-1",
                "fileName": "contract.bin",
                "reason": "cancelled",
                "bytes": "128",
                "total": "256",
                "direction": "outgoing"
            }),
            "e2e_session_state" => json!({
                "peerId": "10002",
                "rotationRequired": false
            }),
            "e2e_identity_state" => json!({
                "peerId": "10002",
                "configured": true,
                "trusted": true,
                "publicKeyFingerprintSha256": "abcdef"
            }),
            "e2e_rotation_request" => json!({
                "peerId": "10002",
                "agreement": {}
            }),
            "e2e_rotation_response" => json!({
                "peerId": "10002",
                "agreement": {},
                "accepted": true
            }),
            "settings_synced" => json!({
                "accepted": true,
                "revision": 1,
                "settings": {}
            }),
            "notification" => json!({
                "title": "QQ NT",
                "body": "Contract notification"
            }),
            "error" => json!({
                "message": "contract error",
                "source": "engine"
            }),
            _ => panic!("missing event contract sample for {event_name}"),
        }
    }

    #[test]
    fn command_ack_contract_covers_protocol_commands() {
        let contract = protocol_contract();
        let mut commands = string_array(&contract, "commands");
        let mut ack_payloads = object_keys(&contract, "ackPayloads");
        let mut validated = VALIDATED_COMMAND_ACK_OPS.to_vec();

        commands.sort_unstable();
        ack_payloads.sort_unstable();
        validated.sort_unstable();

        assert_eq!(ack_payloads, commands);
        assert_eq!(validated, commands);
    }

    #[test]
    fn command_ack_contract_payload_samples_pass_validator() {
        let contract = protocol_contract();
        for op in string_array(&contract, "commands") {
            let payload = contract_command_ack_payload(op);
            validate_command_ack_payload(op, &payload)
                .unwrap_or_else(|error| panic!("{op} ack sample should pass: {error:?}"));
        }
    }

    #[test]
    fn event_payload_contract_covers_protocol_events() {
        let contract = protocol_contract();
        let mut events = string_array(&contract, "events");
        let mut event_payloads = object_keys(&contract, "eventPayloads");
        let mut validated = VALIDATED_EVENT_NAMES.to_vec();

        events.sort_unstable();
        event_payloads.sort_unstable();
        validated.sort_unstable();

        assert_eq!(event_payloads, events);
        assert_eq!(validated, events);
    }

    #[test]
    fn event_contract_payload_samples_pass_validator() {
        let contract = protocol_contract();
        for event_name in string_array(&contract, "events") {
            let payload = contract_event_payload(event_name);
            validate_event_payload(event_name, &payload)
                .unwrap_or_else(|error| panic!("{event_name} event sample should pass: {error:?}"));
        }
    }

    #[test]
    fn event_payload_rejects_unknown_event() {
        let error = validate_event_payload("future_event", &json!({}))
            .expect_err("unknown event should fail");

        assert_eq!(error.code, "unknown_event");
    }

    #[test]
    fn command_ack_payload_rejects_unknown_op() {
        let error = validate_command_ack_payload("future_command", &json!({}))
            .expect_err("unknown command ack op should fail");

        assert_eq!(error.code, "unknown_ack_op");
    }

    #[test]
    fn ready_payload_accepts_expected_protocol_version() {
        validate_ready_payload(&json!({
            "protocolVersion": EXPECTED_PROTOCOL_VERSION,
            "version": "test",
            "qtVersion": "6.8.0",
            "e2eStatus": "uninitialized"
        }))
        .expect("matching protocol version should pass");
    }

    #[test]
    fn ready_payload_rejects_mismatched_protocol_version() {
        let error =
            validate_ready_payload(&json!({ "protocolVersion": EXPECTED_PROTOCOL_VERSION + 1 }))
                .expect_err("mismatched protocol version should fail");

        assert_eq!(error.code, "protocol_version_mismatch");
    }

    #[test]
    fn ready_payload_requires_protocol_version() {
        let error =
            validate_ready_payload(&json!({})).expect_err("missing protocol version should fail");

        assert_eq!(error.code, "missing_protocol_version");
    }

    #[test]
    fn ready_payload_requires_version_metadata() {
        let missing_version = validate_ready_payload(&json!({
            "protocolVersion": EXPECTED_PROTOCOL_VERSION,
            "qtVersion": "6.8.0",
            "e2eStatus": "uninitialized"
        }))
        .expect_err("missing version metadata should fail");
        assert_eq!(missing_version.code, "invalid_ready_payload");

        let empty_e2e_status = validate_ready_payload(&json!({
            "protocolVersion": EXPECTED_PROTOCOL_VERSION,
            "version": "test",
            "qtVersion": "6.8.0",
            "e2eStatus": ""
        }))
        .expect_err("empty e2e status should fail");
        assert_eq!(empty_e2e_status.code, "invalid_ready_payload");

        let object_e2e_status = validate_ready_payload(&json!({
            "protocolVersion": EXPECTED_PROTOCOL_VERSION,
            "version": "test",
            "qtVersion": "6.8.0",
            "e2eStatus": {}
        }))
        .expect_err("object e2e status should fail");
        assert_eq!(object_e2e_status.code, "invalid_ready_payload");
    }

    #[test]
    fn connection_state_payload_accepts_contract_fields() {
        validate_event_payload(
            "connection_state",
            &json!({
                "connected": true,
                "host": "127.0.0.1",
                "port": 12345
            }),
        )
        .expect("connection_state with contract fields should pass");
    }

    #[test]
    fn connection_state_payload_requires_connected() {
        let error = validate_event_payload(
            "connection_state",
            &json!({
                "host": "127.0.0.1",
                "port": 12345
            }),
        )
        .expect_err("connection_state without connected should fail");

        assert_eq!(error.code, "invalid_connection_state_payload");
    }

    #[test]
    fn login_result_payload_accepts_success_contract_fields() {
        validate_event_payload(
            "login_result",
            &json!({
                "success": true,
                "userId": "10001",
                "userName": "Alice",
                "registered": false
            }),
        )
        .expect("successful login_result with contract fields should pass");
    }

    #[test]
    fn login_result_payload_accepts_failure_without_user_fields() {
        validate_event_payload(
            "login_result",
            &json!({
                "success": false,
                "error": "invalid-password"
            }),
        )
        .expect("failed login_result with error should pass");
    }

    #[test]
    fn login_result_payload_requires_error_on_failure() {
        let error = validate_event_payload(
            "login_result",
            &json!({
                "success": false
            }),
        )
        .expect_err("failed login_result without error should fail");

        assert_eq!(error.code, "invalid_login_result_payload");
    }

    #[test]
    fn login_result_payload_requires_registered_on_success() {
        let error = validate_event_payload(
            "login_result",
            &json!({
                "success": true,
                "userId": "10001",
                "userName": "Alice"
            }),
        )
        .expect_err("successful login_result without registered should fail");

        assert_eq!(error.code, "invalid_login_result_payload");
    }

    #[test]
    fn login_result_payload_requires_non_empty_user_id_on_success() {
        let error = validate_event_payload(
            "login_result",
            &json!({
                "success": true,
                "userId": " ",
                "userName": "Alice",
                "registered": false
            }),
        )
        .expect_err("successful login_result without userId should fail");

        assert_eq!(error.code, "invalid_login_result_payload");
    }

    #[test]
    fn friend_search_result_payload_accepts_empty_not_found_fields() {
        validate_event_payload(
            "friend_search_result",
            &json!({
                "found": false,
                "userId": "",
                "userName": "",
                "online": false,
                "reason": "not-found"
            }),
        )
        .expect("friend_search_result should accept empty identity fields when not found");
    }

    #[test]
    fn friend_search_result_payload_requires_reason_when_not_found() {
        let error = validate_event_payload(
            "friend_search_result",
            &json!({
                "found": false,
                "userId": "",
                "userName": "",
                "online": false
            }),
        )
        .expect_err("not-found friend_search_result without reason should fail");

        assert_eq!(error.code, "invalid_friend_search_result_payload");
    }

    #[test]
    fn friend_search_result_payload_requires_non_empty_user_id_when_found() {
        let error = validate_event_payload(
            "friend_search_result",
            &json!({
                "found": true,
                "userId": " ",
                "userName": "Bob",
                "online": true
            }),
        )
        .expect_err("found friend_search_result without userId should fail");

        assert_eq!(error.code, "invalid_friend_search_result_payload");
    }

    #[test]
    fn user_list_payload_accepts_users_array() {
        validate_event_payload(
            "user_list",
            &json!({
                "users": [user_contract_item()]
            }),
        )
        .expect("user_list with user contract item should pass");
    }

    #[test]
    fn friend_list_payload_accepts_friends_array() {
        validate_event_payload(
            "friend_list",
            &json!({
                "friends": [user_contract_item()]
            }),
        )
        .expect("friend_list with user contract item should pass");
    }

    #[test]
    fn user_list_payload_accepts_empty_users_array() {
        validate_event_payload(
            "user_list",
            &json!({
                "users": []
            }),
        )
        .expect("user_list with empty users array should pass");
    }

    #[test]
    fn user_list_payload_rejects_non_object_user() {
        let error = validate_event_payload(
            "user_list",
            &json!({
                "users": ["10002"]
            }),
        )
        .expect_err("user_list with non-object entry should fail");

        assert_eq!(error.code, "invalid_user_list_payload");
    }

    #[test]
    fn user_list_payload_rejects_empty_user_id() {
        let error = validate_event_payload(
            "user_list",
            &json!({
                "users": [{
                    "id": " ",
                    "name": "Bob",
                    "avatar": "",
                    "online": true,
                    "lastActive": ""
                }]
            }),
        )
        .expect_err("user_list with empty user id should fail");

        assert_eq!(error.code, "invalid_user_list_payload");
    }

    #[test]
    fn friend_list_payload_rejects_missing_online() {
        let error = validate_event_payload(
            "friend_list",
            &json!({
                "friends": [{
                    "id": "10002",
                    "name": "Bob",
                    "avatar": "",
                    "lastActive": ""
                }]
            }),
        )
        .expect_err("friend_list without online should fail");

        assert_eq!(error.code, "invalid_friend_list_payload");
    }

    #[test]
    fn user_joined_payload_requires_user_id() {
        let error = validate_event_payload(
            "user_joined",
            &json!({
                "userName": "Bob"
            }),
        )
        .expect_err("user_joined without userId should fail");

        assert_eq!(error.code, "invalid_user_joined_payload");
    }

    #[test]
    fn friend_event_payload_accepts_request_received() {
        validate_event_payload(
            "friend_event",
            &json!({
                "type": "request_received",
                "senderId": "10002",
                "senderName": "Bob"
            }),
        )
        .expect("friend_event request_received should pass");
    }

    #[test]
    fn friend_event_payload_accepts_request_sent() {
        validate_event_payload(
            "friend_event",
            &json!({
                "type": "request_sent",
                "receiverId": "10002",
                "delivered": true
            }),
        )
        .expect("friend_event request_sent should pass");
    }

    #[test]
    fn friend_event_payload_accepts_response_received() {
        validate_event_payload(
            "friend_event",
            &json!({
                "type": "response_received",
                "senderId": "10002",
                "senderName": "Bob",
                "accepted": false
            }),
        )
        .expect("friend_event response_received should pass");
    }

    #[test]
    fn friend_event_payload_requires_shape_for_type() {
        let error = validate_event_payload(
            "friend_event",
            &json!({
                "type": "request_sent",
                "receiverId": "10002"
            }),
        )
        .expect_err("request_sent without delivered should fail");

        assert_eq!(error.code, "invalid_friend_event_payload");
    }

    #[test]
    fn friend_event_payload_rejects_unknown_type() {
        let error = validate_event_payload(
            "friend_event",
            &json!({
                "type": "request_pending",
                "senderId": "10002",
                "senderName": "Bob"
            }),
        )
        .expect_err("unknown friend_event type should fail");

        assert_eq!(error.code, "invalid_friend_event_payload");
    }

    #[test]
    fn group_member_updated_payload_requires_action() {
        let error = validate_event_payload(
            "group_member_updated",
            &json!({
                "groupId": "group-1",
                "memberId": "10002"
            }),
        )
        .expect_err("group_member_updated without action should fail");

        assert_eq!(error.code, "invalid_group_member_updated_payload");
    }

    #[test]
    fn group_member_updated_payload_accepts_supported_actions() {
        for action in ["add", "remove", "promote_admin", "demote_admin"] {
            validate_event_payload(
                "group_member_updated",
                &json!({
                    "groupId": "group-1",
                    "memberId": "10002",
                    "action": action
                }),
            )
            .expect("supported group_member_updated action should pass");
        }
    }

    #[test]
    fn group_member_updated_payload_rejects_unknown_action() {
        let error = validate_event_payload(
            "group_member_updated",
            &json!({
                "groupId": "group-1",
                "memberId": "10002",
                "action": "ban"
            }),
        )
        .expect_err("unknown group_member_updated action should fail");

        assert_eq!(error.code, "invalid_group_member_updated_payload");
    }

    #[test]
    fn message_payload_requires_message_object() {
        let error = validate_event_payload(
            "message",
            &json!({
                "sessionId": "10001"
            }),
        )
        .expect_err("message without message object should fail");

        assert_eq!(error.code, "invalid_message_payload");
    }

    #[test]
    fn message_payload_accepts_minimal_message_model() {
        validate_event_payload(
            "message",
            &json!({
                "sessionId": "10001",
                "message": {
                    "messageId": "message-1",
                    "clientMessageId": "client-1",
                    "sessionId": "10001",
                    "senderId": "10002",
                    "senderName": "Bob",
                    "timestamp": 1710000000000_u64,
                    "contentType": "file",
                    "content": "",
                    "status": "sent"
                }
            }),
        )
        .expect("message event with minimal message model should pass");
    }

    #[test]
    fn message_payload_requires_message_model_fields() {
        let error = validate_event_payload(
            "message",
            &json!({
                "sessionId": "10001",
                "message": {}
            }),
        )
        .expect_err("message event with empty message object should fail");

        assert_eq!(error.code, "invalid_message_payload");
    }

    #[test]
    fn message_payload_rejects_session_mismatch() {
        let error = validate_event_payload(
            "message",
            &json!({
                "sessionId": "10001",
                "message": {
                    "messageId": "message-1",
                    "sessionId": "10002",
                    "senderId": "10002",
                    "senderName": "Bob",
                    "timestamp": "1710000000000",
                    "contentType": "text",
                    "content": "hello",
                    "status": "received"
                }
            }),
        )
        .expect_err("message event sessionId mismatch should fail");

        assert_eq!(error.code, "invalid_message_payload");
    }

    #[test]
    fn message_payload_rejects_unknown_content_type() {
        let error = validate_event_payload(
            "message",
            &json!({
                "sessionId": "10001",
                "message": {
                    "messageId": "message-1",
                    "sessionId": "10001",
                    "senderId": "10002",
                    "senderName": "Bob",
                    "timestamp": "1710000000000",
                    "contentType": "video",
                    "content": "hello",
                    "status": "received"
                }
            }),
        )
        .expect_err("message event with unknown contentType should fail");

        assert_eq!(error.code, "invalid_message_payload");
    }

    #[test]
    fn message_payload_rejects_unknown_status() {
        let error = validate_event_payload(
            "message",
            &json!({
                "sessionId": "10001",
                "message": {
                    "messageId": "message-1",
                    "sessionId": "10001",
                    "senderId": "10002",
                    "senderName": "Bob",
                    "timestamp": "1710000000000",
                    "contentType": "text",
                    "content": "hello",
                    "status": "queued"
                }
            }),
        )
        .expect_err("message event with unknown status should fail");

        assert_eq!(error.code, "invalid_message_payload");
    }

    #[test]
    fn e2e_identity_state_payload_accepts_public_key_fingerprint() {
        validate_event_payload(
            "e2e_identity_state",
            &json!({
                "peerId": "10002",
                "configured": true,
                "trusted": true,
                "publicKeyFingerprintSha256": "abcdef"
            }),
        )
        .expect("e2e_identity_state should accept public key fingerprint field");
    }

    #[test]
    fn e2e_identity_state_payload_allows_missing_fingerprint_when_unconfigured() {
        validate_event_payload(
            "e2e_identity_state",
            &json!({
                "peerId": "10002",
                "configured": false,
                "trusted": false
            }),
        )
        .expect("unconfigured e2e_identity_state should not require a fingerprint");
    }

    #[test]
    fn e2e_rotation_response_payload_requires_accepted() {
        let error = validate_event_payload(
            "e2e_rotation_response",
            &json!({
                "peerId": "10002",
                "agreement": {}
            }),
        )
        .expect_err("e2e_rotation_response without accepted should fail");

        assert_eq!(error.code, "invalid_e2e_rotation_response_payload");
    }

    #[test]
    fn group_snapshot_payload_accepts_contract_fields() {
        validate_event_payload(
            "group_snapshot",
            &json!({
                "groups": [group_contract_item()],
                "removedGroups": [removed_group_contract_item()],
                "hasSnapshot": true
            }),
        )
        .expect("group_snapshot with contract fields should pass");
    }

    #[test]
    fn group_snapshot_payload_accepts_empty_collections() {
        validate_event_payload(
            "group_snapshot",
            &json!({
                "groups": [],
                "removedGroups": [],
                "hasSnapshot": true
            }),
        )
        .expect("group_snapshot with empty collections should pass");
    }

    #[test]
    fn file_progress_payload_accepts_contract_fields() {
        validate_event_payload(
            "file_progress",
            &json!({
                "transferId": "transfer-1",
                "fileName": "report.zip",
                "bytes": "128",
                "total": 256,
                "direction": "outgoing"
            }),
        )
        .expect("file_progress with contract fields should pass");
    }

    #[test]
    fn file_progress_payload_requires_transfer_id() {
        let error = validate_event_payload(
            "file_progress",
            &json!({
                "fileName": "report.zip",
                "bytes": "128",
                "total": "256",
                "direction": "outgoing"
            }),
        )
        .expect_err("file_progress without transferId should fail");

        assert_eq!(error.code, "invalid_file_progress_payload");
    }

    #[test]
    fn file_progress_payload_requires_total() {
        let error = validate_event_payload(
            "file_progress",
            &json!({
                "transferId": "transfer-1",
                "fileName": "report.zip",
                "bytes": "128",
                "direction": "outgoing"
            }),
        )
        .expect_err("file_progress without total should fail");

        assert_eq!(error.code, "invalid_file_progress_payload");
    }

    #[test]
    fn file_progress_payload_rejects_unknown_direction() {
        let error = validate_event_payload(
            "file_progress",
            &json!({
                "transferId": "transfer-1",
                "fileName": "report.zip",
                "bytes": "128",
                "total": "256",
                "direction": "sideways"
            }),
        )
        .expect_err("file_progress with unknown direction should fail");

        assert_eq!(error.code, "invalid_file_progress_payload");
    }

    #[test]
    fn file_done_payload_accepts_contract_fields() {
        validate_event_payload(
            "file_done",
            &json!({
                "transferId": "transfer-1",
                "fileName": "report.zip",
                "filePath": "C:/tmp/report.zip",
                "direction": "incoming"
            }),
        )
        .expect("file_done with contract fields should pass");
    }

    #[test]
    fn file_done_payload_requires_file_path_field() {
        let error = validate_event_payload(
            "file_done",
            &json!({
                "transferId": "transfer-1",
                "fileName": "report.zip",
                "direction": "incoming"
            }),
        )
        .expect_err("file_done without filePath should fail");

        assert_eq!(error.code, "invalid_file_done_payload");
    }

    #[test]
    fn file_done_payload_rejects_unknown_direction() {
        let error = validate_event_payload(
            "file_done",
            &json!({
                "transferId": "transfer-1",
                "fileName": "report.zip",
                "filePath": "C:/tmp/report.zip",
                "direction": "sideways"
            }),
        )
        .expect_err("file_done with unknown direction should fail");

        assert_eq!(error.code, "invalid_file_done_payload");
    }

    #[test]
    fn file_error_payload_accepts_contract_fields() {
        validate_event_payload(
            "file_error",
            &json!({
                "transferId": "transfer-1",
                "fileName": "report.zip",
                "reason": "cancelled",
                "direction": "outgoing",
                "bytes": "128",
                "total": "256"
            }),
        )
        .expect("file_error with contract fields should pass");
    }

    #[test]
    fn file_error_payload_requires_reason() {
        let error = validate_event_payload(
            "file_error",
            &json!({
                "transferId": "transfer-1"
            }),
        )
        .expect_err("file_error without reason should fail");

        assert_eq!(error.code, "invalid_file_error_payload");
    }

    #[test]
    fn file_error_payload_requires_direction() {
        let error = validate_event_payload(
            "file_error",
            &json!({
                "transferId": "transfer-1",
                "fileName": "report.zip",
                "reason": "cancelled",
                "bytes": "128",
                "total": "256"
            }),
        )
        .expect_err("file_error without direction should fail");

        assert_eq!(error.code, "invalid_file_error_payload");
    }

    #[test]
    fn settings_synced_payload_accepts_contract_fields() {
        validate_event_payload(
            "settings_synced",
            &json!({
                "accepted": true,
                "revision": 1,
                "settings": { "notifications": { "desktop": true } },
                "appliedDownloadDir": "C:/tmp/downloads"
            }),
        )
        .expect("settings_synced with contract fields should pass");
    }

    #[test]
    fn settings_synced_payload_requires_settings_object() {
        let error = validate_event_payload(
            "settings_synced",
            &json!({
                "accepted": true,
                "revision": 1,
                "settings": "bad"
            }),
        )
        .expect_err("settings_synced without settings object should fail");

        assert_eq!(error.code, "invalid_settings_synced_payload");
    }

    #[test]
    fn settings_synced_payload_rejects_zero_revision() {
        let error = validate_event_payload(
            "settings_synced",
            &json!({
                "accepted": true,
                "revision": 0,
                "settings": {}
            }),
        )
        .expect_err("settings_synced with zero revision should fail");

        assert_eq!(error.code, "invalid_settings_synced_payload");
    }

    #[test]
    fn settings_synced_payload_rejects_empty_applied_download_dir() {
        let error = validate_event_payload(
            "settings_synced",
            &json!({
                "accepted": true,
                "revision": 1,
                "settings": {},
                "appliedDownloadDir": " "
            }),
        )
        .expect_err("settings_synced with empty appliedDownloadDir should fail");

        assert_eq!(error.code, "invalid_settings_synced_payload");
    }

    #[test]
    fn notification_payload_accepts_contract_fields() {
        validate_event_payload(
            "notification",
            &json!({
                "title": "Alice",
                "body": "hello"
            }),
        )
        .expect("notification with contract fields should pass");
    }

    #[test]
    fn notification_payload_requires_body() {
        let error = validate_event_payload(
            "notification",
            &json!({
                "title": "Alice"
            }),
        )
        .expect_err("notification without body should fail");

        assert_eq!(error.code, "invalid_notification_payload");
    }

    #[test]
    fn group_snapshot_payload_requires_removed_groups() {
        let error = validate_event_payload(
            "group_snapshot",
            &json!({
                "groups": [],
                "hasSnapshot": true
            }),
        )
        .expect_err("group_snapshot without removedGroups should fail");

        assert_eq!(error.code, "invalid_group_snapshot_payload");
    }

    #[test]
    fn group_snapshot_payload_requires_has_snapshot_boolean() {
        let error = validate_event_payload(
            "group_snapshot",
            &json!({
                "groups": [],
                "removedGroups": [],
                "hasSnapshot": "yes"
            }),
        )
        .expect_err("group_snapshot without boolean hasSnapshot should fail");

        assert_eq!(error.code, "invalid_group_snapshot_payload");
    }

    #[test]
    fn group_snapshot_payload_rejects_non_object_group_item() {
        let error = validate_event_payload(
            "group_snapshot",
            &json!({
                "groups": ["private-1"],
                "removedGroups": [],
                "hasSnapshot": true
            }),
        )
        .expect_err("group_snapshot with non-object group should fail");

        assert_eq!(error.code, "invalid_group_snapshot_payload");
    }

    #[test]
    fn group_snapshot_payload_rejects_empty_group_id() {
        let mut group = group_contract_item();
        group["groupId"] = json!(" ");

        let error = validate_event_payload(
            "group_snapshot",
            &json!({
                "groups": [group],
                "removedGroups": [],
                "hasSnapshot": true
            }),
        )
        .expect_err("group_snapshot with empty groupId should fail");

        assert_eq!(error.code, "invalid_group_snapshot_payload");
    }

    #[test]
    fn group_snapshot_payload_rejects_unknown_group_type() {
        let mut group = group_contract_item();
        group["groupType"] = json!("secret");

        let error = validate_event_payload(
            "group_snapshot",
            &json!({
                "groups": [group],
                "removedGroups": [],
                "hasSnapshot": true
            }),
        )
        .expect_err("group_snapshot with unknown groupType should fail");

        assert_eq!(error.code, "invalid_group_snapshot_payload");
    }

    #[test]
    fn group_snapshot_payload_rejects_wrong_membership_bucket() {
        let error = validate_event_payload(
            "group_snapshot",
            &json!({
                "groups": [removed_group_contract_item()],
                "removedGroups": [],
                "hasSnapshot": true
            }),
        )
        .expect_err("group_snapshot with removed group in active bucket should fail");

        assert_eq!(error.code, "invalid_group_snapshot_payload");
    }

    #[test]
    fn group_snapshot_payload_rejects_active_group_without_members() {
        let mut group = group_contract_item();
        group.as_object_mut().unwrap().remove("members");

        let error = validate_event_payload(
            "group_snapshot",
            &json!({
                "groups": [group],
                "removedGroups": [],
                "hasSnapshot": true
            }),
        )
        .expect_err("group_snapshot active group without members should fail");

        assert_eq!(error.code, "invalid_group_snapshot_payload");
    }

    #[test]
    fn group_snapshot_payload_rejects_bad_member_role() {
        let mut group = group_contract_item();
        group["members"][0]["role"] = json!("moderator");

        let error = validate_event_payload(
            "group_snapshot",
            &json!({
                "groups": [group],
                "removedGroups": [],
                "hasSnapshot": true
            }),
        )
        .expect_err("group_snapshot with bad member role should fail");

        assert_eq!(error.code, "invalid_group_snapshot_payload");
    }

    #[test]
    fn group_snapshot_payload_rejects_bad_audit_details() {
        let mut group = group_contract_item();
        group["auditEvents"][0]["details"] = json!("created");

        let error = validate_event_payload(
            "group_snapshot",
            &json!({
                "groups": [group],
                "removedGroups": [],
                "hasSnapshot": true
            }),
        )
        .expect_err("group_snapshot with bad audit details should fail");

        assert_eq!(error.code, "invalid_group_snapshot_payload");
    }

    #[test]
    fn group_snapshot_payload_rejects_removed_group_without_removed_at() {
        let mut group = removed_group_contract_item();
        group.as_object_mut().unwrap().remove("removedAt");

        let error = validate_event_payload(
            "group_snapshot",
            &json!({
                "groups": [],
                "removedGroups": [group],
                "hasSnapshot": true
            }),
        )
        .expect_err("group_snapshot removed group without removedAt should fail");

        assert_eq!(error.code, "invalid_group_snapshot_payload");
    }

    #[test]
    fn group_snapshot_payload_rejects_bad_permission_boolean() {
        let mut group = removed_group_contract_item();
        group["canSend"] = json!("no");

        let error = validate_event_payload(
            "group_snapshot",
            &json!({
                "groups": [],
                "removedGroups": [group],
                "hasSnapshot": true
            }),
        )
        .expect_err("group_snapshot with bad permission flag should fail");

        assert_eq!(error.code, "invalid_group_snapshot_payload");
    }

    #[test]
    fn connect_ack_payload_accepts_contract_fields() {
        validate_command_ack_payload(
            "connect",
            &json!({
                "connected": true,
                "host": "127.0.0.1",
                "port": 8888
            }),
        )
        .expect("connect ack with contract fields should pass");
    }

    #[test]
    fn connect_ack_payload_rejects_invalid_port() {
        let error = validate_command_ack_payload(
            "connect",
            &json!({
                "connected": true,
                "host": "127.0.0.1",
                "port": 0
            }),
        )
        .expect_err("connect ack with invalid port should fail");

        assert_eq!(error.code, "invalid_connect_payload");
    }

    #[test]
    fn login_ack_payload_requires_login_mode() {
        let error = validate_command_ack_payload(
            "login",
            &json!({
                "accepted": true,
                "requiresConnect": false,
                "mode": "register"
            }),
        )
        .expect_err("login ack with register mode should fail");

        assert_eq!(error.code, "invalid_login_payload");
    }

    #[test]
    fn register_ack_payload_accepts_register_mode() {
        validate_command_ack_payload(
            "register",
            &json!({
                "accepted": true,
                "requiresConnect": true,
                "mode": "register"
            }),
        )
        .expect("register ack with register mode should pass");
    }

    #[test]
    fn bool_ack_payload_requires_accepted_true() {
        let error = validate_command_ack_payload(
            "send_file",
            &json!({
                "accepted": false
            }),
        )
        .expect_err("successful bool ack with accepted false should fail");

        assert_eq!(error.code, "invalid_send_file_payload");
    }

    #[test]
    fn send_private_message_ack_payload_requires_receiver_id() {
        let error = validate_command_ack_payload("send_private_message", &json!({}))
            .expect_err("send_private_message ack without receiverId should fail");

        assert_eq!(error.code, "invalid_send_private_message_payload");
    }

    #[test]
    fn cancel_transfer_ack_payload_accepts_contract_fields() {
        validate_command_ack_payload(
            "cancel_transfer",
            &json!({
                "cancelled": true,
                "transferId": "transfer-1"
            }),
        )
        .expect("cancel_transfer ack with contract fields should pass");
    }

    #[test]
    fn query_resume_ack_payload_accepts_contract_fields() {
        validate_command_ack_payload(
            "query_resume",
            &json!({
                "canResume": true,
                "transferId": "transfer-1",
                "confirmedBytes": "128",
                "nextChunkIndex": "2",
                "fileSize": "1024",
                "chunkSize": "64",
                "chunkCount": "16",
                "fileHash": "abc123",
                "receivedChunks": ["0", "1"],
                "resumed": false,
                "mode": "query"
            }),
        )
        .expect("query_resume ack with contract fields should pass");
    }

    #[test]
    fn query_resume_ack_payload_requires_received_chunks() {
        let error = validate_command_ack_payload(
            "query_resume",
            &json!({
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
            }),
        )
        .expect_err("query_resume ack without receivedChunks should fail");

        assert_eq!(error.code, "invalid_query_resume_payload");
    }

    #[test]
    fn query_resume_ack_payload_rejects_numeric_counters() {
        let error = validate_command_ack_payload(
            "query_resume",
            &json!({
                "canResume": true,
                "transferId": "transfer-1",
                "confirmedBytes": 128,
                "nextChunkIndex": "2",
                "fileSize": "1024",
                "chunkSize": "64",
                "chunkCount": "16",
                "fileHash": "abc123",
                "receivedChunks": ["0", "1"],
                "resumed": false,
                "mode": "query"
            }),
        )
        .expect_err("query_resume ack uses C++ string counters");

        assert_eq!(error.code, "invalid_query_resume_payload");
    }

    #[test]
    fn e2e_status_ack_payload_accepts_local_identity_only() {
        validate_command_ack_payload(
            "e2e_status",
            &json!({
                "localIdentity": {}
            }),
        )
        .expect("e2e_status ack with localIdentity should pass");
    }

    #[test]
    fn e2e_status_ack_payload_requires_peer_shape_when_present() {
        let error = validate_command_ack_payload(
            "e2e_status",
            &json!({
                "localIdentity": {},
                "peerId": "10002",
                "session": {}
            }),
        )
        .expect_err("e2e_status ack with peerId must include identity");

        assert_eq!(error.code, "invalid_e2e_status_payload");
    }

    #[test]
    fn profile_update_ack_payload_requires_avatar_sent() {
        let error = validate_command_ack_payload(
            "profile_update",
            &json!({
                "accepted": true,
                "userName": "Alice"
            }),
        )
        .expect_err("profile_update ack without avatarSent should fail");

        assert_eq!(error.code, "invalid_profile_update_payload");
    }

    #[test]
    fn get_group_list_ack_payload_accepts_contract_fields() {
        validate_command_ack_payload(
            "get_group_list",
            &json!({
                "groups": [group_contract_item()],
                "removedGroups": [removed_group_contract_item()],
                "hasSnapshot": false
            }),
        )
        .expect("get_group_list ack with contract fields should pass");
    }

    #[test]
    fn get_group_list_ack_payload_accepts_empty_collections() {
        validate_command_ack_payload(
            "get_group_list",
            &json!({
                "groups": [],
                "removedGroups": [],
                "hasSnapshot": false
            }),
        )
        .expect("get_group_list ack with empty collections should pass");
    }

    #[test]
    fn get_user_list_ack_payload_accepts_users_array() {
        validate_command_ack_payload(
            "get_user_list",
            &json!({
                "users": [user_contract_item()]
            }),
        )
        .expect("get_user_list ack with user contract item should pass");
    }

    #[test]
    fn get_friend_list_ack_payload_requires_friends_array() {
        let error = validate_command_ack_payload(
            "get_friend_list",
            &json!({
                "users": []
            }),
        )
        .expect_err("get_friend_list ack without friends array should fail");

        assert_eq!(error.code, "invalid_get_friend_list_payload");
    }

    #[test]
    fn get_friend_list_ack_payload_rejects_bad_friend_item() {
        let error = validate_command_ack_payload(
            "get_friend_list",
            &json!({
                "friends": [{
                    "id": "10002",
                    "name": "Bob",
                    "avatar": "",
                    "online": "yes",
                    "lastActive": ""
                }]
            }),
        )
        .expect_err("get_friend_list ack with bad friend item should fail");

        assert_eq!(error.code, "invalid_get_friend_list_payload");
    }

    #[test]
    fn get_group_list_ack_payload_requires_removed_groups() {
        let error = validate_command_ack_payload(
            "get_group_list",
            &json!({
                "groups": [],
                "hasSnapshot": false
            }),
        )
        .expect_err("get_group_list ack without removedGroups should fail");

        assert_eq!(error.code, "invalid_get_group_list_payload");
    }

    #[test]
    fn get_group_list_ack_payload_rejects_bad_removed_group_item() {
        let mut group = removed_group_contract_item();
        group["membershipState"] = json!("active");

        let error = validate_command_ack_payload(
            "get_group_list",
            &json!({
                "groups": [],
                "removedGroups": [group],
                "hasSnapshot": false
            }),
        )
        .expect_err("get_group_list ack with bad removed group item should fail");

        assert_eq!(error.code, "invalid_get_group_list_payload");
    }

    #[test]
    fn settings_sync_ack_payload_accepts_contract_fields() {
        validate_command_ack_payload(
            "settings_sync",
            &json!({
                "accepted": true,
                "revision": 1,
                "settings": { "files": { "autoDownload": false } }
            }),
        )
        .expect("settings_sync ack with contract fields should pass");
    }

    #[test]
    fn settings_sync_ack_payload_requires_revision() {
        let error = validate_command_ack_payload(
            "settings_sync",
            &json!({
                "accepted": true,
                "settings": {}
            }),
        )
        .expect_err("settings_sync ack without revision should fail");

        assert_eq!(error.code, "invalid_settings_sync_payload");
    }

    #[test]
    fn settings_sync_ack_payload_rejects_zero_revision() {
        let error = validate_command_ack_payload(
            "settings_sync",
            &json!({
                "accepted": true,
                "revision": 0,
                "settings": {}
            }),
        )
        .expect_err("settings_sync ack with zero revision should fail");

        assert_eq!(error.code, "invalid_settings_sync_payload");
    }
}
