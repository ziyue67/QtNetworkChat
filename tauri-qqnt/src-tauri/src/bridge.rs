use std::sync::Arc;
use std::time::Duration;

use serde_json::{json, Value};
use tauri::{AppHandle, Emitter};
use tauri_plugin_shell::process::CommandEvent;
use tokio::sync::oneshot;

use crate::error::{QQNTError, QQNTResult};
use crate::protocol;
use crate::state::AppState;

const ENGINE_TIMEOUT_SECONDS: u64 = 30;

#[derive(Debug, PartialEq)]
enum EngineDispatch {
    Ack { req_id: String, packet: Value },
    Event { topic: String, payload: Value },
    Error(Value),
    Ignore,
}

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
    match dispatch_stdout_line(&line) {
        EngineDispatch::Ack { req_id, packet } => handle_ack(state, &req_id, packet).await,
        EngineDispatch::Event { topic, payload } => {
            let _ = app.emit(topic.as_str(), payload);
        }
        EngineDispatch::Error(error) => {
            let _ = app.emit("qqnt://engine/error", error);
        }
        EngineDispatch::Ignore => {}
    }
}

fn dispatch_stdout_line(line: &[u8]) -> EngineDispatch {
    let trimmed = String::from_utf8_lossy(line).trim().to_string();
    if trimmed.is_empty() {
        return EngineDispatch::Ignore;
    }

    let packet: Value = match serde_json::from_str(&trimmed) {
        Ok(value) => value,
        Err(error) => {
            return EngineDispatch::Error(json!({
                "code": "invalid_engine_json",
                "message": error.to_string(),
                "source": "rust"
            }));
        }
    };

    match packet.get("type").and_then(Value::as_str) {
        Some("ack") => {
            let Some(req_id) = packet.get("reqId").and_then(Value::as_str) else {
                return EngineDispatch::Error(json!({
                    "code": "missing_req_id",
                    "message": "QQNTEngine ack packet did not include reqId.",
                    "source": "rust"
                }));
            };
            EngineDispatch::Ack {
                req_id: req_id.to_string(),
                packet,
            }
        }
        Some("event") => {
            let Some(event_name) = packet.get("event").and_then(Value::as_str) else {
                return EngineDispatch::Error(json!({
                "code": "missing_event_name",
                "message": "QQNTEngine event packet did not include event.",
                "source": "rust"
                }));
            };

            let payload = packet.get("payload").cloned().unwrap_or_else(|| json!({}));
            if let Err(error) = protocol::validate_event_payload(event_name, &payload) {
                return EngineDispatch::Error(json!(error));
            }

            EngineDispatch::Event {
                topic: format!("qqnt://engine/{event_name}"),
                payload,
            }
        }
        _ => EngineDispatch::Error(json!({
            "code": "unknown_engine_packet",
            "message": "QQNTEngine emitted an unsupported packet envelope.",
            "source": "rust"
        })),
    }
}

async fn handle_ack(state: &Arc<AppState>, req_id: &str, packet: Value) {
    if let Some(sender) = state.engine.pending.lock().await.remove(req_id) {
        let _ = sender.send(packet);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::state::AppState;

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

    fn contract_event_payload(event_name: &str) -> Value {
        match event_name {
            "ready" => json!({
                "protocolVersion": protocol::EXPECTED_PROTOCOL_VERSION,
                "contractProbe": true
            }),
            "group_snapshot" => json!({
                "groups": [],
                "removedGroups": [],
                "hasSnapshot": true,
                "contractProbe": true
            }),
            "file_progress" => json!({
                "transferId": "contract-transfer",
                "fileName": "contract.bin",
                "bytes": "128",
                "total": "256",
                "direction": "outgoing",
                "contractProbe": true
            }),
            "file_done" => json!({
                "transferId": "contract-transfer",
                "fileName": "contract.bin",
                "filePath": "C:/tmp/contract.bin",
                "direction": "incoming",
                "contractProbe": true
            }),
            "file_error" => json!({
                "transferId": "contract-transfer",
                "reason": "cancelled",
                "contractProbe": true
            }),
            _ => json!({ "contractProbe": true }),
        }
    }

    #[test]
    fn dispatches_ack_by_req_id() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"ack","op":"ready","reqId":"req-1","status":"ok","payload":{"protocolVersion":1}}"#,
        );

        match dispatch {
            EngineDispatch::Ack { req_id, packet } => {
                assert_eq!(req_id, "req-1");
                assert_eq!(packet["payload"]["protocolVersion"], 1);
            }
            other => panic!("expected ack dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_event_to_engine_topic() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"message","payload":{"sessionId":"10001"}}"#,
        );

        assert_eq!(
            dispatch,
            EngineDispatch::Event {
                topic: "qqnt://engine/message".to_string(),
                payload: json!({ "sessionId": "10001" }),
            }
        );
    }

    #[test]
    fn dispatches_protocol_contract_events_to_engine_topics() {
        let contract = protocol_contract();
        for event_name in string_array(&contract, "events") {
            let payload = contract_event_payload(event_name);
            let line = json!({
                "type": "event",
                "event": event_name,
                "payload": payload
            })
            .to_string();

            assert_eq!(
                dispatch_stdout_line(line.as_bytes()),
                EngineDispatch::Event {
                    topic: format!("qqnt://engine/{event_name}"),
                    payload,
                }
            );
        }
    }

    #[test]
    fn dispatches_ready_protocol_mismatch_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"ready","payload":{"protocolVersion":2}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "protocol_version_mismatch");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_invalid_group_snapshot_payload_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"group_snapshot","payload":{"groups":[],"hasSnapshot":true}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_group_snapshot_payload");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_invalid_file_progress_payload_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"file_progress","payload":{"transferId":"transfer-1","fileName":"contract.bin","bytes":"128","direction":"outgoing"}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_file_progress_payload");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_invalid_file_done_payload_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"file_done","payload":{"transferId":"transfer-1","fileName":"contract.bin","direction":"incoming"}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_file_done_payload");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_invalid_file_error_payload_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"file_error","payload":{"transferId":"transfer-1"}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_file_error_payload");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_invalid_json_as_error_event() {
        let dispatch = dispatch_stdout_line(br#"{"type":"event""#);

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_engine_json");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[tokio::test]
    async fn handle_ack_resolves_matching_pending_request() {
        let state = Arc::new(AppState::new());
        let (sender, receiver) = oneshot::channel();
        state
            .engine
            .pending
            .lock()
            .await
            .insert("req-42".to_string(), sender);

        handle_ack(
            &state,
            "req-42",
            json!({
                "type": "ack",
                "op": "login",
                "reqId": "req-42",
                "status": "ok",
                "payload": { "accepted": true }
            }),
        )
        .await;

        let packet = receiver.await.expect("pending receiver should resolve");
        assert_eq!(packet["reqId"], "req-42");
        assert!(state.engine.pending.lock().await.is_empty());
    }
}
