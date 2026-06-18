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
        "group_snapshot" => validate_group_collection_payload(payload, "group_snapshot"),
        _ => Ok(()),
    }
}

pub fn validate_command_ack_payload(op: &str, payload: &Value) -> QQNTResult<()> {
    match op {
        "ready" => validate_ready_payload(payload),
        "get_group_list" => validate_group_collection_payload(payload, "get_group_list"),
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

fn require_array_field(payload: &Value, field: &str, contract_name: &str) -> QQNTResult<()> {
    if matches!(payload.get(field), Some(Value::Array(_))) {
        return Ok(());
    }

    Err(QQNTError::rust(
        format!("invalid_{contract_name}_payload"),
        format!("QQNTEngine {contract_name} payload must include {field} array."),
    ))
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
}
