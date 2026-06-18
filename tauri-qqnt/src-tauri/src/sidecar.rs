use std::sync::Arc;
use std::time::Duration;

use serde_json::{json, Value};
use tauri::{AppHandle, Emitter};
use tauri_plugin_shell::{process::CommandEvent, ShellExt};
use tokio::io::{AsyncReadExt, AsyncWriteExt};

use crate::bridge;
use crate::error::QQNTError;
use crate::state::AppState;

#[cfg(test)]
const ENGINE_SIDECAR_NAME: &str = "QQNTEngine";
#[cfg(test)]
const SERVER_SIDECAR_NAME: &str = "QQNTServer";
const ENGINE_SIDECAR_PATH: &str = "binaries/QQNTEngine";
const SERVER_SIDECAR_PATH: &str = "binaries/QQNTServer";

pub fn start_engine(app: AppHandle, state: Arc<AppState>) {
    tauri::async_runtime::spawn(async move {
        if let Err(error) = run_engine(app.clone(), state.clone()).await {
            state.engine.set_last_error(error.clone()).await;
            let _ = app.emit("qqnt://engine/error", error);
        }
    });
}

pub fn start_server(app: AppHandle, state: Arc<AppState>) {
    tauri::async_runtime::spawn(async move {
        if let Err(error) = run_server(app.clone(), state.clone()).await {
            state.server.set_last_error(error.clone()).await;
            let _ = app.emit("qqnt://server/fatal", error);
        }
    });
}

async fn run_engine(app: AppHandle, state: Arc<AppState>) -> Result<(), QQNTError> {
    let (mut receiver, child) = app
        .shell()
        .sidecar(ENGINE_SIDECAR_PATH)
        .map_err(|error| QQNTError::rust("engine_spawn_prepare_failed", error.to_string()))?
        .spawn()
        .map_err(|error| QQNTError::rust("engine_spawn_failed", error.to_string()))?;

    *state.engine.child.lock().await = Some(child);

    while let Some(event) = receiver.recv().await {
        if bridge::handle_engine_event(&app, &state, event).await {
            return Ok(());
        }
    }

    *state.engine.child.lock().await = None;
    let error = QQNTError::rust(
        "engine_event_stream_closed",
        "QQNTEngine event stream closed.",
    );
    bridge::fail_pending_engine_requests(&state, &error.code, &error.message).await;
    Err(error)
}

async fn run_server(app: AppHandle, state: Arc<AppState>) -> Result<(), QQNTError> {
    let redis = redis_preflight().await?;
    let mut command = app
        .shell()
        .sidecar(SERVER_SIDECAR_PATH)
        .map_err(|error| QQNTError::rust("server_spawn_prepare_failed", error.to_string()))?
        .env("QTNETWORKCHAT_REDIS", "1")
        .env("QTNETWORKCHAT_REDIS_HOST", redis.host.as_str())
        .env("QTNETWORKCHAT_REDIS_PORT", redis.port.to_string());

    for key in [
        "QTNETWORKCHAT_REDIS_PASSWORD",
        "QTNETWORKCHAT_REDIS_PREFIX",
        "QTNETWORKCHAT_TLS",
        "QTNETWORKCHAT_TLS_CERT",
        "QTNETWORKCHAT_TLS_KEY",
        "QTNETWORKCHAT_TLS_VERIFY",
        "QTNETWORKCHAT_APPDATA_DIR",
        "QQNT_SERVER_PORT",
    ] {
        if let Ok(value) = std::env::var(key) {
            command = command.env(key, value);
        }
    }

    let (mut receiver, child) = command
        .spawn()
        .map_err(|error| QQNTError::rust("server_spawn_failed", error.to_string()))?;

    *state.server.child.lock().await = Some(child);
    let _ = app.emit(
        "qqnt://server/ready",
        json!({
            "host": redis.host,
            "redisPort": redis.port,
            "serverPort": std::env::var("QQNT_SERVER_PORT").unwrap_or_else(|_| "8888".into())
        }),
    );

    while let Some(event) = receiver.recv().await {
        if handle_server_event(&app, &state, event).await {
            return Ok(());
        }
    }

    *state.server.child.lock().await = None;
    Err(QQNTError::rust(
        "server_event_stream_closed",
        "QQNTServer event stream closed.",
    ))
}

async fn handle_server_event(app: &AppHandle, state: &Arc<AppState>, event: CommandEvent) -> bool {
    match event {
        CommandEvent::Stdout(line) | CommandEvent::Stderr(line) => {
            let message = String::from_utf8_lossy(&line).trim().to_string();
            if !message.is_empty() {
                let _ = app.emit("qqnt://server/log", json!({ "message": message }));
            }
            false
        }
        CommandEvent::Error(message) => {
            let error = QQNTError::rust("server_process_error", message);
            state.server.set_last_error(error.clone()).await;
            let _ = app.emit("qqnt://server/fatal", error);
            false
        }
        CommandEvent::Terminated(payload) => {
            *state.server.child.lock().await = None;
            let error = server_terminated_error(payload.code, payload.signal);
            state.server.set_last_error(error.clone()).await;
            let _ = app.emit("qqnt://server/fatal", sidecar_termination_payload(&error));
            true
        }
        _ => false,
    }
}

struct RedisConfig {
    host: String,
    port: u16,
}

async fn redis_preflight() -> Result<RedisConfig, QQNTError> {
    let host = std::env::var("QTNETWORKCHAT_REDIS_HOST").unwrap_or_else(|_| "127.0.0.1".into());
    let port = std::env::var("QTNETWORKCHAT_REDIS_PORT")
        .ok()
        .and_then(|value| value.parse::<u16>().ok())
        .unwrap_or(6379);
    let password = std::env::var("QTNETWORKCHAT_REDIS_PASSWORD")
        .ok()
        .filter(|value| !value.is_empty());

    match ping_redis(host.as_str(), port, password.as_deref()).await {
        Ok(()) => Ok(RedisConfig { host, port }),
        Err(cause) => Err(redis_unavailable_error(host.as_str(), port, &cause)),
    }
}

fn redis_unavailable_error(host: &str, port: u16, cause: &QQNTError) -> QQNTError {
    QQNTError::rust("redis_unavailable", "Redis is unavailable for QQNTServer.").with_details(
        json!({
            "host": host,
            "port": port,
            "cause": cause
        }),
    )
}

fn server_terminated_error(code: Option<i32>, signal: Option<i32>) -> QQNTError {
    QQNTError::rust("server_terminated", "QQNTServer sidecar terminated.").with_details(json!({
        "codeValue": code,
        "signal": signal
    }))
}

fn sidecar_termination_payload(error: &QQNTError) -> Value {
    let mut payload = serde_json::to_value(error).unwrap_or_else(|_| {
        json!({
            "code": error.code,
            "message": error.message,
            "source": error.source
        })
    });

    if let (Some(details), Some(object)) = (&error.details, payload.as_object_mut()) {
        object.insert(
            "codeValue".to_string(),
            details.get("codeValue").cloned().unwrap_or(Value::Null),
        );
        object.insert(
            "signal".to_string(),
            details.get("signal").cloned().unwrap_or(Value::Null),
        );
    }

    payload
}

async fn ping_redis(host: &str, port: u16, password: Option<&str>) -> Result<(), QQNTError> {
    let mut stream = tokio::time::timeout(
        Duration::from_secs(2),
        tokio::net::TcpStream::connect((host, port)),
    )
    .await
    .map_err(|_| QQNTError::rust("redis_connect_timeout", "Redis connection timed out."))?
    .map_err(|error| QQNTError::rust("redis_connect_failed", error.to_string()))?;

    if let Some(password) = password {
        write_resp_command(&mut stream, &["AUTH", password]).await?;
        let auth_reply = read_resp_line(&mut stream).await?;
        if !auth_reply.starts_with("+OK") {
            return Err(QQNTError::rust("redis_auth_failed", auth_reply));
        }
    }

    write_resp_command(&mut stream, &["PING"]).await?;
    let pong = read_resp_line(&mut stream).await?;
    if pong.starts_with("+PONG") {
        Ok(())
    } else {
        Err(QQNTError::rust("redis_ping_failed", pong))
    }
}

async fn write_resp_command(
    stream: &mut tokio::net::TcpStream,
    args: &[&str],
) -> Result<(), QQNTError> {
    let mut command = format!("*{}\r\n", args.len()).into_bytes();
    for arg in args {
        command.extend_from_slice(format!("${}\r\n", arg.as_bytes().len()).as_bytes());
        command.extend_from_slice(arg.as_bytes());
        command.extend_from_slice(b"\r\n");
    }

    tokio::time::timeout(Duration::from_secs(2), stream.write_all(&command))
        .await
        .map_err(|_| QQNTError::rust("redis_write_timeout", "Redis write timed out."))?
        .map_err(|error| QQNTError::rust("redis_write_failed", error.to_string()))
}

async fn read_resp_line(stream: &mut tokio::net::TcpStream) -> Result<String, QQNTError> {
    let mut buffer = Vec::new();
    loop {
        let mut byte = [0_u8; 1];
        let read = tokio::time::timeout(Duration::from_secs(2), stream.read(&mut byte))
            .await
            .map_err(|_| QQNTError::rust("redis_read_timeout", "Redis read timed out."))?
            .map_err(|error| QQNTError::rust("redis_read_failed", error.to_string()))?;
        if read == 0 {
            return Err(QQNTError::rust(
                "redis_read_closed",
                "Redis closed the connection during preflight.",
            ));
        }

        buffer.push(byte[0]);
        if buffer.ends_with(b"\r\n") {
            return String::from_utf8(buffer)
                .map(|line| line.trim_end_matches("\r\n").to_string())
                .map_err(|error| QQNTError::rust("redis_invalid_reply", error.to_string()));
        }

        if buffer.len() > 1024 {
            return Err(QQNTError::rust(
                "redis_invalid_reply",
                "Redis preflight reply exceeded 1024 bytes.",
            ));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use serde_json::Value;
    use tokio::net::TcpListener;

    fn string_array<'a>(value: &'a Value, key: &str) -> Vec<&'a str> {
        value[key]
            .as_array()
            .expect("json key should be an array")
            .iter()
            .map(|item| item.as_str().expect("json array item should be a string"))
            .collect()
    }

    #[test]
    fn tauri_external_bins_match_rust_sidecars() {
        let config: Value = serde_json::from_str(include_str!("../tauri.conf.json"))
            .expect("tauri.conf.json should parse");
        let mut actual = string_array(&config["bundle"], "externalBin");
        let mut expected = vec![ENGINE_SIDECAR_PATH, SERVER_SIDECAR_PATH];

        actual.sort_unstable();
        expected.sort_unstable();

        assert_eq!(actual, expected);
    }

    #[test]
    fn tauri_packaging_contract_keeps_backend_bundle_entrypoints() {
        let config: Value = serde_json::from_str(include_str!("../tauri.conf.json"))
            .expect("tauri.conf.json should parse");
        let package: Value = serde_json::from_str(include_str!("../../package.json"))
            .expect("package.json should parse");
        let capability: Value = serde_json::from_str(include_str!("../capabilities/default.json"))
            .expect("default capability should parse");

        assert_eq!(config["build"]["beforeBuildCommand"], "npm run build");
        assert_eq!(config["build"]["frontendDist"], "../dist");
        assert_eq!(config["bundle"]["active"], true);
        assert_eq!(string_array(&config["bundle"], "targets"), vec!["nsis"]);

        assert_eq!(package["scripts"]["build"], "tsc && vite build");
        assert_eq!(package["scripts"]["tauri"], "tauri");

        assert_eq!(config["app"]["windows"][0]["label"], "main");
        assert_eq!(config["app"]["windows"][0]["decorations"], false);
        assert_eq!(string_array(&capability, "windows"), vec!["main"]);
        assert!(
            string_array(&capability, "permissions").contains(&"core:default"),
            "default capability should keep core desktop permissions"
        );
    }

    #[test]
    fn copy_sidecars_defaults_match_rust_sidecars() {
        let script = include_str!("../../../scripts/copy-sidecars.ps1");
        let sidecar_param = script
            .lines()
            .find(|line| line.contains("[string[]]$Sidecars"))
            .expect("copy-sidecars.ps1 should declare Sidecars defaults");

        for sidecar in [ENGINE_SIDECAR_NAME, SERVER_SIDECAR_NAME] {
            assert!(
                sidecar_param.contains(&format!("'{sidecar}'")),
                "copy-sidecars.ps1 default Sidecars should include {sidecar}"
            );
        }

        assert!(
            script.contains("$sidecar-$Triplet.exe"),
            "copy-sidecars.ps1 should emit Tauri triplet-suffixed sidecar executables"
        );
    }

    #[test]
    fn cmake_post_build_copy_sidecars_is_wired_for_backend_targets() {
        let cmake = include_str!("../../../CMakeLists.txt");

        assert!(
            cmake.contains("function(qtnetworkchat_copy_sidecar target_name)"),
            "CMake should define the sidecar copy helper"
        );
        assert!(
            cmake.contains("scripts/copy-sidecars.ps1"),
            "CMake sidecar copy helper should invoke copy-sidecars.ps1"
        );
        assert!(
            cmake.contains("-BuildDir $<TARGET_FILE_DIR:${target_name}>"),
            "CMake sidecar copy helper should copy from the built target directory"
        );
        assert!(
            cmake.contains("-Sidecars ${target_name}"),
            "CMake sidecar copy helper should copy only the target that just built"
        );

        for sidecar in [ENGINE_SIDECAR_NAME, SERVER_SIDECAR_NAME] {
            assert!(
                cmake.contains(&format!("add_executable({sidecar}")),
                "CMake should build {sidecar}"
            );
            assert!(
                cmake.contains(&format!("qtnetworkchat_copy_sidecar({sidecar})")),
                "CMake should attach POST_BUILD sidecar copy for {sidecar}"
            );
        }
    }

    #[tokio::test]
    async fn redis_ping_preflight_sends_auth_then_ping() {
        let listener = TcpListener::bind(("127.0.0.1", 0))
            .await
            .expect("mock Redis should bind");
        let port = listener
            .local_addr()
            .expect("mock Redis should have a local address")
            .port();

        let server = tokio::spawn(async move {
            let (mut socket, _) = listener.accept().await.expect("client should connect");
            let mut buffer = [0_u8; 256];
            let read = socket.read(&mut buffer).await.expect("AUTH should read");
            let auth = String::from_utf8_lossy(&buffer[..read]);
            assert!(auth.contains("AUTH"));
            assert!(auth.contains("secret"));
            socket
                .write_all(b"+OK\r\n")
                .await
                .expect("AUTH should reply");

            let read = socket.read(&mut buffer).await.expect("PING should read");
            let ping = String::from_utf8_lossy(&buffer[..read]);
            assert!(ping.contains("PING"));
            socket
                .write_all(b"+PONG\r\n")
                .await
                .expect("PING should reply");
        });

        ping_redis("127.0.0.1", port, Some("secret"))
            .await
            .expect("mock Redis PING should pass");
        server.await.expect("mock Redis task should finish");
    }

    #[tokio::test]
    async fn redis_ping_preflight_rejects_non_pong_reply() {
        let listener = TcpListener::bind(("127.0.0.1", 0))
            .await
            .expect("mock Redis should bind");
        let port = listener
            .local_addr()
            .expect("mock Redis should have a local address")
            .port();

        let server = tokio::spawn(async move {
            let (mut socket, _) = listener.accept().await.expect("client should connect");
            let mut buffer = [0_u8; 128];
            let read = socket.read(&mut buffer).await.expect("PING should read");
            let ping = String::from_utf8_lossy(&buffer[..read]);
            assert!(ping.contains("PING"));
            socket
                .write_all(b"-ERR unavailable\r\n")
                .await
                .expect("PING should reply with error");
        });

        let error = ping_redis("127.0.0.1", port, None)
            .await
            .expect_err("non-PONG reply should fail preflight");
        assert_eq!(error.code, "redis_ping_failed");
        server.await.expect("mock Redis task should finish");
    }

    #[test]
    fn redis_preflight_failure_keeps_endpoint_and_cause_details() {
        let cause = QQNTError::rust("redis_connect_failed", "connection refused");
        let error = redis_unavailable_error("10.0.0.5", 6380, &cause);
        let value = serde_json::to_value(error).expect("redis fatal error should serialize");

        assert_eq!(value["code"], "redis_unavailable");
        assert_eq!(value["message"], "Redis is unavailable for QQNTServer.");
        assert_eq!(value["source"], "rust");
        assert_eq!(value["details"]["host"], "10.0.0.5");
        assert_eq!(value["details"]["port"], 6380);
        assert_eq!(value["details"]["cause"]["code"], "redis_connect_failed");
        assert_eq!(value["details"]["cause"]["message"], "connection refused");
        assert_eq!(value["details"]["cause"]["source"], "rust");
    }

    #[test]
    fn server_termination_error_keeps_state_and_event_fields_aligned() {
        let error = server_terminated_error(Some(2), Some(15));
        let value =
            serde_json::to_value(&error).expect("server termination error should serialize");
        let event = sidecar_termination_payload(&error);

        assert_eq!(value["code"], "server_terminated");
        assert_eq!(value["message"], "QQNTServer sidecar terminated.");
        assert_eq!(value["source"], "rust");
        assert_eq!(value["details"]["codeValue"], 2);
        assert_eq!(value["details"]["signal"], 15);

        assert_eq!(event["code"], value["code"]);
        assert_eq!(event["message"], value["message"]);
        assert_eq!(event["source"], value["source"]);
        assert_eq!(event["details"], value["details"]);
        assert_eq!(event["codeValue"], 2);
        assert_eq!(event["signal"], 15);
    }
}
