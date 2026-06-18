use serde_json::Value;

use crate::error::{QQNTError, QQNTResult};

pub const EXPECTED_PROTOCOL_VERSION: u64 = 1;

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

    Ok(())
}

pub fn validate_event_payload(event_name: &str, payload: &Value) -> QQNTResult<()> {
    match event_name {
        "ready" => validate_ready_payload(payload),
        "connection_state" => validate_connection_state_payload(payload),
        "login_result" => validate_login_result_payload(payload),
        "friend_search_result" => validate_friend_search_result_payload(payload),
        "message" => validate_message_payload(payload),
        "group_snapshot" => validate_group_collection_payload(payload, "group_snapshot"),
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
        _ => Ok(()),
    }
}

pub fn validate_command_ack_payload(op: &str, payload: &Value) -> QQNTResult<()> {
    match op {
        "ready" => validate_ready_payload(payload),
        "get_group_list" => validate_group_collection_payload(payload, "get_group_list"),
        "settings_sync" => validate_settings_synced_payload(payload, "settings_sync"),
        _ => Ok(()),
    }
}

fn validate_group_collection_payload(payload: &Value, contract_name: &str) -> QQNTResult<()> {
    require_array_field(payload, "groups", contract_name)?;
    require_array_field(payload, "removedGroups", contract_name)?;
    if !matches!(payload.get("hasSnapshot"), Some(Value::Bool(_))) {
        return Err(QQNTError::rust(
            format!("invalid_{contract_name}_payload"),
            format!("QQNTEngine {contract_name} payload must include hasSnapshot boolean."),
        ));
    }

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

    require_string_field(payload, "userId", "login_result")?;
    require_string_field(payload, "userName", "login_result")?;
    require_optional_bool_field(payload, "registered", "login_result")?;
    require_optional_string_field(payload, "error", "login_result")?;

    Ok(())
}

fn validate_friend_search_result_payload(payload: &Value) -> QQNTResult<()> {
    require_bool_field(payload, "found", "friend_search_result")?;
    require_string_field(payload, "userId", "friend_search_result")?;
    require_string_field(payload, "userName", "friend_search_result")?;
    require_bool_field(payload, "online", "friend_search_result")?;
    require_optional_string_field(payload, "reason", "friend_search_result")?;

    Ok(())
}

fn validate_message_payload(payload: &Value) -> QQNTResult<()> {
    require_non_empty_string_field(payload, "sessionId", "message")?;
    require_object_field(payload, "message", "message")?;

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
    require_non_empty_string_field(payload, "direction", "file_progress")?;
    require_string_or_number_field(payload, "bytes", "file_progress")?;
    require_string_or_number_field(payload, "total", "file_progress")?;

    Ok(())
}

fn validate_file_done_payload(payload: &Value) -> QQNTResult<()> {
    require_non_empty_string_field(payload, "transferId", "file_done")?;
    require_non_empty_string_field(payload, "fileName", "file_done")?;
    require_non_empty_string_field(payload, "direction", "file_done")?;
    require_string_field(payload, "filePath", "file_done")?;

    Ok(())
}

fn validate_file_error_payload(payload: &Value) -> QQNTResult<()> {
    require_non_empty_string_field(payload, "transferId", "file_error")?;
    require_non_empty_string_field(payload, "reason", "file_error")?;

    Ok(())
}

fn validate_settings_synced_payload(payload: &Value, contract_name: &str) -> QQNTResult<()> {
    require_bool_field(payload, "accepted", contract_name)?;
    require_unsigned_number_field(payload, "revision", contract_name)?;
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

fn require_array_field(payload: &Value, field: &str, contract_name: &str) -> QQNTResult<()> {
    if matches!(payload.get(field), Some(Value::Array(_))) {
        return Ok(());
    }

    Err(QQNTError::rust(
        format!("invalid_{contract_name}_payload"),
        format!("QQNTEngine {contract_name} payload must include {field} array."),
    ))
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

fn require_string_field(payload: &Value, field: &str, contract_name: &str) -> QQNTResult<()> {
    if matches!(payload.get(field), Some(Value::String(_))) {
        return Ok(());
    }

    Err(QQNTError::rust(
        format!("invalid_{contract_name}_payload"),
        format!("QQNTEngine {contract_name} payload must include {field} string."),
    ))
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

    #[test]
    fn ready_payload_accepts_expected_protocol_version() {
        validate_ready_payload(&json!({ "protocolVersion": EXPECTED_PROTOCOL_VERSION }))
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
                "groups": [],
                "removedGroups": [],
                "hasSnapshot": true
            }),
        )
        .expect("group_snapshot with contract fields should pass");
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
    fn file_error_payload_accepts_contract_fields_and_extras() {
        validate_event_payload(
            "file_error",
            &json!({
                "transferId": "transfer-1",
                "reason": "cancelled",
                "fileName": "report.zip",
                "direction": "outgoing",
                "bytes": "128",
                "total": "256"
            }),
        )
        .expect("file_error with contract fields and extras should pass");
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
    fn get_group_list_ack_payload_accepts_contract_fields() {
        validate_command_ack_payload(
            "get_group_list",
            &json!({
                "groups": [],
                "removedGroups": [],
                "hasSnapshot": false
            }),
        )
        .expect("get_group_list ack with contract fields should pass");
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
}
