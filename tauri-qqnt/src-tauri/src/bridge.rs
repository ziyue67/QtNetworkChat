use std::sync::Arc;
use std::time::Duration;

use serde_json::{json, Value};
use tauri::{AppHandle, Emitter};
use tauri_plugin_shell::process::CommandEvent;
use tokio::sync::oneshot;

use crate::error::{QQNTError, QQNTResult};
use crate::protocol;
use crate::state::{AppState, PendingEngineRequest};

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
    let op = command
        .get("op")
        .and_then(Value::as_str)
        .filter(|value| !value.trim().is_empty())
        .ok_or_else(|| QQNTError::rust("missing_op", "Command op is required."))?
        .to_string();

    let mut line = serde_json::to_vec(&command)
        .map_err(|error| QQNTError::rust("invalid_command", error.to_string()))?;
    line.push(b'\n');

    let (sender, receiver) = oneshot::channel();
    {
        let mut pending = state.engine.pending.lock().await;
        if pending.contains_key(&req_id) {
            return Err(QQNTError::rust(
                "duplicate_req_id",
                format!("Command reqId {req_id} is already pending."),
            ));
        }
        pending.insert(req_id.clone(), PendingEngineRequest { op, sender });
    }

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

    wait_for_engine_ack(
        state,
        &req_id,
        receiver,
        Duration::from_secs(ENGINE_TIMEOUT_SECONDS),
    )
    .await
}

async fn wait_for_engine_ack(
    state: &Arc<AppState>,
    req_id: &str,
    receiver: oneshot::Receiver<Value>,
    timeout: Duration,
) -> QQNTResult<Value> {
    match tokio::time::timeout(timeout, receiver).await {
        Ok(Ok(packet)) => Ok(packet),
        Ok(Err(_)) => {
            state.engine.pending.lock().await.remove(req_id);
            Err(QQNTError::rust(
                "engine_channel_closed",
                "QQNTEngine ack channel closed.",
            ))
        }
        Err(_) => {
            state.engine.pending.lock().await.remove(req_id);
            Err(QQNTError::rust(
                "engine_timeout",
                "QQNTEngine did not ack before timeout.",
            ))
        }
    }
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
            fail_pending_engine_requests(state, &error.code, &error.message).await;
            let _ = app.emit("qqnt://engine/error", error);
        }
        CommandEvent::Terminated(payload) => {
            *state.engine.child.lock().await = None;
            let message = "QQNTEngine sidecar terminated.";
            fail_pending_engine_requests(state, "engine_terminated", message).await;
            let _ = app.emit(
                "qqnt://engine/error",
                json!({
                    "code": "engine_terminated",
                    "message": message,
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
            let Some(req_id) = packet
                .get("reqId")
                .and_then(Value::as_str)
                .filter(|value| !value.trim().is_empty())
            else {
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
    if let Some(request) = state.engine.pending.lock().await.remove(req_id) {
        let expected_op = request.op;
        let ack_op = packet.get("op").and_then(Value::as_str).unwrap_or_default();
        if ack_op != expected_op {
            let _ = request.sender.send(json!({
                "type": "ack",
                "op": expected_op,
                "reqId": req_id,
                "status": "error",
                "error": {
                    "code": "unexpected_ack",
                    "message": format!("QQNTEngine returned an unexpected ack for {expected_op}."),
                    "source": "rust"
                }
            }));
            return;
        }

        let _ = request.sender.send(packet);
    }
}

pub(crate) async fn fail_pending_engine_requests(state: &Arc<AppState>, code: &str, message: &str) {
    let pending = {
        let mut pending = state.engine.pending.lock().await;
        pending.drain().collect::<Vec<_>>()
    };

    for (req_id, request) in pending {
        let _ = request.sender.send(json!({
            "type": "ack",
            "op": request.op,
            "reqId": req_id,
            "status": "error",
            "error": {
                "code": code,
                "message": message,
                "source": "rust"
            }
        }));
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

    fn contract_event_payload(event_name: &str) -> Value {
        match event_name {
            "ready" => json!({
                "protocolVersion": protocol::EXPECTED_PROTOCOL_VERSION,
                "version": "test",
                "qtVersion": "6.8.0",
                "e2eStatus": "uninitialized",
                "contractProbe": true
            }),
            "connection_state" => json!({
                "connected": true,
                "host": "127.0.0.1",
                "port": 12345,
                "contractProbe": true
            }),
            "login_result" => json!({
                "success": true,
                "userId": "10001",
                "userName": "Alice",
                "registered": false,
                "contractProbe": true
            }),
            "friend_search_result" => json!({
                "found": true,
                "userId": "10002",
                "userName": "Bob",
                "online": true,
                "reason": "",
                "contractProbe": true
            }),
            "user_list" => json!({
                "users": [{
                    "id": "10002",
                    "name": "Bob",
                    "avatar": "",
                    "online": true,
                    "lastActive": ""
                }],
                "contractProbe": true
            }),
            "friend_list" => json!({
                "friends": [{
                    "id": "10002",
                    "name": "Bob",
                    "avatar": "",
                    "online": true,
                    "lastActive": ""
                }],
                "contractProbe": true
            }),
            "user_joined" => json!({
                "userId": "10002",
                "userName": "Bob",
                "contractProbe": true
            }),
            "user_left" => json!({
                "userId": "10002",
                "userName": "Bob",
                "contractProbe": true
            }),
            "friend_event" => json!({
                "type": "request_received",
                "senderId": "10002",
                "senderName": "Bob",
                "contractProbe": true
            }),
            "message" => json!({
                "sessionId": "10001",
                "message": {
                    "messageId": "message-1",
                    "sessionId": "10001",
                    "senderId": "10002",
                    "senderName": "Bob",
                    "timestamp": "1710000000000",
                    "contentType": "text",
                    "content": "hello",
                    "status": "received"
                },
                "contractProbe": true
            }),
            "group_snapshot" => json!({
                "groups": [group_contract_item()],
                "removedGroups": [removed_group_contract_item()],
                "hasSnapshot": true,
                "contractProbe": true
            }),
            "group_member_updated" => json!({
                "groupId": "group-1",
                "memberId": "10002",
                "action": "add",
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
                "fileName": "contract.bin",
                "reason": "cancelled",
                "bytes": "128",
                "total": "256",
                "direction": "outgoing",
                "contractProbe": true
            }),
            "e2e_session_state" => json!({
                "peerId": "10002",
                "rotationRequired": false,
                "contractProbe": true
            }),
            "e2e_identity_state" => json!({
                "peerId": "10002",
                "configured": true,
                "trusted": true,
                "publicKeyFingerprintSha256": "abcdef",
                "contractProbe": true
            }),
            "e2e_rotation_request" => json!({
                "peerId": "10002",
                "agreement": {},
                "contractProbe": true
            }),
            "e2e_rotation_response" => json!({
                "peerId": "10002",
                "agreement": {},
                "accepted": true,
                "reason": "",
                "contractProbe": true
            }),
            "settings_synced" => json!({
                "accepted": true,
                "revision": 1,
                "settings": { "notifications": { "desktop": true } },
                "contractProbe": true
            }),
            "notification" => json!({
                "title": "QQ NT",
                "body": "Contract notification",
                "contractProbe": true
            }),
            "error" => json!({
                "message": "contract error",
                "source": "client",
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
    fn dispatches_empty_ack_req_id_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"ack","op":"ready","reqId":"   ","status":"ok","payload":{"protocolVersion":1}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "missing_req_id");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_event_to_engine_topic() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"message","payload":{"sessionId":"10001","message":{"messageId":"message-1","sessionId":"10001","senderId":"10002","senderName":"Bob","timestamp":"1710000000000","contentType":"text","content":"hello","status":"received"}}}"#,
        );

        assert_eq!(
            dispatch,
            EngineDispatch::Event {
                topic: "qqnt://engine/message".to_string(),
                payload: json!({
                    "sessionId": "10001",
                    "message": {
                        "messageId": "message-1",
                        "sessionId": "10001",
                        "senderId": "10002",
                        "senderName": "Bob",
                        "timestamp": "1710000000000",
                        "contentType": "text",
                        "content": "hello",
                        "status": "received"
                    }
                }),
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
    fn dispatches_unknown_contract_event_as_error() {
        let dispatch =
            dispatch_stdout_line(br#"{"type":"event","event":"future_event","payload":{}}"#);

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "unknown_event");
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
    fn dispatches_invalid_group_snapshot_item_payload_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"group_snapshot","payload":{"groups":["private-1"],"removedGroups":[],"hasSnapshot":true}}"#,
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
    fn dispatches_invalid_connection_state_payload_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"connection_state","payload":{"host":"127.0.0.1","port":12345}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_connection_state_payload");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_invalid_login_result_payload_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"login_result","payload":{"success":false}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_login_result_payload");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_invalid_login_result_success_payload_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"login_result","payload":{"success":true,"userId":"10001","userName":"Alice"}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_login_result_payload");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_invalid_message_payload_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"message","payload":{"sessionId":"10001"}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_message_payload");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_invalid_user_list_payload_as_error() {
        let dispatch =
            dispatch_stdout_line(br#"{"type":"event","event":"user_list","payload":{}}"#);

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_user_list_payload");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_invalid_user_list_item_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"user_list","payload":{"users":[{"id":"10002","name":"Bob","avatar":"","online":"yes","lastActive":""}]}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_user_list_payload");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_invalid_friend_event_payload_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"friend_event","payload":{"type":"unknown"}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_friend_event_payload");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_invalid_friend_event_branch_payload_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"friend_event","payload":{"type":"request_sent","receiverId":"10002"}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_friend_event_payload");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_invalid_friend_search_result_payload_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"friend_search_result","payload":{"found":false,"userId":"","userName":"","online":false}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_friend_search_result_payload");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_invalid_group_member_updated_payload_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"group_member_updated","payload":{"groupId":"group-1","memberId":"10002"}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_group_member_updated_payload");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_invalid_e2e_rotation_request_payload_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"e2e_rotation_request","payload":{"peerId":"10002"}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_e2e_rotation_request_payload");
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
            br#"{"type":"event","event":"file_error","payload":{"transferId":"transfer-1","fileName":"contract.bin","reason":"cancelled","bytes":"128","total":"256"}}"#,
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
    fn dispatches_invalid_settings_synced_payload_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"settings_synced","payload":{"accepted":true,"settings":{}}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_settings_synced_payload");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_invalid_notification_payload_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"notification","payload":{"title":"QQ NT"}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_notification_payload");
                assert_eq!(error["source"], "rust");
            }
            other => panic!("expected error dispatch, got {other:?}"),
        }
    }

    #[test]
    fn dispatches_invalid_error_payload_as_error() {
        let dispatch = dispatch_stdout_line(
            br#"{"type":"event","event":"error","payload":{"message":"connection reset","source":"engine"}}"#,
        );

        match dispatch {
            EngineDispatch::Error(error) => {
                assert_eq!(error["code"], "invalid_error_payload");
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
        state.engine.pending.lock().await.insert(
            "req-42".to_string(),
            PendingEngineRequest {
                op: "login".to_string(),
                sender,
            },
        );

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

    #[tokio::test]
    async fn handle_ack_rejects_mismatched_op_for_pending_request() {
        let state = Arc::new(AppState::new());
        let (sender, receiver) = oneshot::channel();
        state.engine.pending.lock().await.insert(
            "req-42".to_string(),
            PendingEngineRequest {
                op: "connect".to_string(),
                sender,
            },
        );

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

        let packet = receiver
            .await
            .expect("pending receiver should resolve with protocol error");
        assert_eq!(packet["type"], "ack");
        assert_eq!(packet["op"], "connect");
        assert_eq!(packet["reqId"], "req-42");
        assert_eq!(packet["status"], "error");
        assert_eq!(packet["error"]["code"], "unexpected_ack");
        assert_eq!(packet["error"]["source"], "rust");
        assert!(state.engine.pending.lock().await.is_empty());
    }

    #[tokio::test]
    async fn handle_ack_rejects_missing_op_for_pending_request() {
        let state = Arc::new(AppState::new());
        let (sender, receiver) = oneshot::channel();
        state.engine.pending.lock().await.insert(
            "req-43".to_string(),
            PendingEngineRequest {
                op: "ready".to_string(),
                sender,
            },
        );

        handle_ack(
            &state,
            "req-43",
            json!({
                "type": "ack",
                "reqId": "req-43",
                "status": "ok",
                "payload": { "protocolVersion": 1 }
            }),
        )
        .await;

        let packet = receiver
            .await
            .expect("pending receiver should resolve with protocol error");
        assert_eq!(packet["op"], "ready");
        assert_eq!(packet["reqId"], "req-43");
        assert_eq!(packet["status"], "error");
        assert_eq!(packet["error"]["code"], "unexpected_ack");
        assert!(state.engine.pending.lock().await.is_empty());
    }

    #[tokio::test]
    async fn call_engine_rejects_duplicate_req_id_without_replacing_pending() {
        let state = Arc::new(AppState::new());
        let (existing_sender, existing_receiver) = oneshot::channel();
        state.engine.pending.lock().await.insert(
            "req-dupe".to_string(),
            PendingEngineRequest {
                op: "ready".to_string(),
                sender: existing_sender,
            },
        );

        let error = call_engine(
            &state,
            json!({
                "op": "login",
                "reqId": "req-dupe",
                "payload": {
                    "account": "10001",
                    "password": "secret"
                }
            }),
        )
        .await
        .expect_err("duplicate reqId should fail before writing to engine");

        assert_eq!(error.code, "duplicate_req_id");
        let existing_request = state
            .engine
            .pending
            .lock()
            .await
            .remove("req-dupe")
            .expect("existing pending request should remain registered");
        assert_eq!(existing_request.op, "ready");
        existing_request
            .sender
            .send(json!({
                "type": "ack",
                "op": "ready",
                "reqId": "req-dupe",
                "status": "ok",
                "payload": {}
            }))
            .expect("existing pending request channel should stay open");
        let packet = existing_receiver
            .await
            .expect("existing pending receiver should resolve");
        assert_eq!(packet["op"], "ready");
    }

    #[tokio::test]
    async fn wait_for_engine_ack_clears_pending_on_timeout() {
        let state = Arc::new(AppState::new());
        let (sender, receiver) = oneshot::channel();
        state.engine.pending.lock().await.insert(
            "req-timeout".to_string(),
            PendingEngineRequest {
                op: "ready".to_string(),
                sender,
            },
        );

        let error = wait_for_engine_ack(&state, "req-timeout", receiver, Duration::from_millis(1))
            .await
            .expect_err("missing ack should time out");

        assert_eq!(error.code, "engine_timeout");
        assert!(state.engine.pending.lock().await.is_empty());
    }

    #[tokio::test]
    async fn wait_for_engine_ack_clears_pending_on_closed_channel() {
        let state = Arc::new(AppState::new());
        let (sender, receiver) = oneshot::channel();
        state.engine.pending.lock().await.insert(
            "req-closed".to_string(),
            PendingEngineRequest {
                op: "ready".to_string(),
                sender,
            },
        );
        state.engine.pending.lock().await.remove("req-closed");

        let error = wait_for_engine_ack(&state, "req-closed", receiver, Duration::from_secs(1))
            .await
            .expect_err("dropped sender should close ack channel");

        assert_eq!(error.code, "engine_channel_closed");
        assert!(state.engine.pending.lock().await.is_empty());
    }

    #[tokio::test]
    async fn fail_pending_engine_requests_resolves_and_clears_all_pending() {
        let state = Arc::new(AppState::new());
        let (first_sender, first_receiver) = oneshot::channel();
        let (second_sender, second_receiver) = oneshot::channel();
        {
            let mut pending = state.engine.pending.lock().await;
            pending.insert(
                "req-first".to_string(),
                PendingEngineRequest {
                    op: "ready".to_string(),
                    sender: first_sender,
                },
            );
            pending.insert(
                "req-second".to_string(),
                PendingEngineRequest {
                    op: "send_file".to_string(),
                    sender: second_sender,
                },
            );
        }

        fail_pending_engine_requests(
            &state,
            "engine_terminated",
            "QQNTEngine sidecar terminated.",
        )
        .await;

        assert!(state.engine.pending.lock().await.is_empty());
        let first_packet = first_receiver
            .await
            .expect("first pending request should receive failure ack");
        let second_packet = second_receiver
            .await
            .expect("second pending request should receive failure ack");

        assert_eq!(first_packet["type"], "ack");
        assert_eq!(first_packet["op"], "ready");
        assert_eq!(first_packet["reqId"], "req-first");
        assert_eq!(first_packet["status"], "error");
        assert_eq!(first_packet["error"]["code"], "engine_terminated");
        assert_eq!(first_packet["error"]["source"], "rust");
        assert_eq!(second_packet["op"], "send_file");
        assert_eq!(second_packet["reqId"], "req-second");
        assert_eq!(
            second_packet["error"]["message"],
            "QQNTEngine sidecar terminated."
        );
    }
}
