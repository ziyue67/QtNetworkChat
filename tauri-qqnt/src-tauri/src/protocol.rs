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
}
