use std::sync::Arc;
use std::time::Duration;

use serde_json::json;
use tauri::{AppHandle, Emitter};
use tauri_plugin_shell::{process::CommandEvent, ShellExt};
use tokio::io::{AsyncReadExt, AsyncWriteExt};

use crate::bridge;
use crate::error::QQNTError;
use crate::state::AppState;

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
        .sidecar("binaries/QQNTEngine")
        .map_err(|error| QQNTError::rust("engine_spawn_prepare_failed", error.to_string()))?
        .spawn()
        .map_err(|error| QQNTError::rust("engine_spawn_failed", error.to_string()))?;

    *state.engine.child.lock().await = Some(child);

    while let Some(event) = receiver.recv().await {
        bridge::handle_engine_event(&app, &state, event).await;
    }

    *state.engine.child.lock().await = None;
    Err(QQNTError::rust(
        "engine_event_stream_closed",
        "QQNTEngine event stream closed.",
    ))
}

async fn run_server(app: AppHandle, state: Arc<AppState>) -> Result<(), QQNTError> {
    let redis = redis_preflight(&app).await?;
    let mut command = app
        .shell()
        .sidecar("binaries/QQNTServer")
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
        handle_server_event(&app, &state, event).await;
    }

    *state.server.child.lock().await = None;
    Err(QQNTError::rust(
        "server_event_stream_closed",
        "QQNTServer event stream closed.",
    ))
}

async fn handle_server_event(app: &AppHandle, state: &Arc<AppState>, event: CommandEvent) {
    match event {
        CommandEvent::Stdout(line) | CommandEvent::Stderr(line) => {
            let message = String::from_utf8_lossy(&line).trim().to_string();
            if !message.is_empty() {
                let _ = app.emit("qqnt://server/log", json!({ "message": message }));
            }
        }
        CommandEvent::Error(message) => {
            let error = QQNTError::rust("server_process_error", message);
            state.server.set_last_error(error.clone()).await;
            let _ = app.emit("qqnt://server/fatal", error);
        }
        CommandEvent::Terminated(payload) => {
            *state.server.child.lock().await = None;
            let _ = app.emit(
                "qqnt://server/fatal",
                json!({
                    "code": "server_terminated",
                    "message": "QQNTServer sidecar terminated.",
                    "source": "rust",
                    "codeValue": payload.code,
                    "signal": payload.signal
                }),
            );
        }
        _ => {}
    }
}

struct RedisConfig {
    host: String,
    port: u16,
}

async fn redis_preflight(app: &AppHandle) -> Result<RedisConfig, QQNTError> {
    let host = std::env::var("QTNETWORKCHAT_REDIS_HOST").unwrap_or_else(|_| "127.0.0.1".into());
    let port = std::env::var("QTNETWORKCHAT_REDIS_PORT")
        .ok()
        .and_then(|value| value.parse::<u16>().ok())
        .unwrap_or(6379);
    let password = std::env::var("QTNETWORKCHAT_REDIS_PASSWORD")
        .ok()
        .filter(|value| !value.is_empty());

    if ping_redis(host.as_str(), port, password.as_deref())
        .await
        .is_ok()
    {
        return Ok(RedisConfig { host, port });
    }

    let error = QQNTError::rust("redis_unavailable", "Redis is unavailable for QQNTServer.");
    let _ = app.emit(
        "qqnt://server/fatal",
        json!({
            "reason": "redis_unavailable",
            "host": host,
            "port": port
        }),
    );
    Err(error)
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

    use tokio::net::TcpListener;

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
}
