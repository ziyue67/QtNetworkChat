use std::collections::HashMap;

use serde_json::Value;
use tauri_plugin_shell::process::CommandChild;
use tokio::sync::{oneshot, Mutex};

use crate::error::QQNTError;

pub struct AppState {
    pub engine: EngineState,
    pub server: ServerState,
}

pub struct EngineState {
    pub child: Mutex<Option<CommandChild>>,
    pub pending: Mutex<HashMap<String, PendingEngineRequest>>,
    pub last_error: Mutex<Option<QQNTError>>,
}

pub struct PendingEngineRequest {
    pub op: String,
    pub sender: oneshot::Sender<Value>,
}

pub struct ServerState {
    pub child: Mutex<Option<CommandChild>>,
    pub last_error: Mutex<Option<QQNTError>>,
}

impl AppState {
    pub fn new() -> Self {
        Self {
            engine: EngineState::new(),
            server: ServerState::new(),
        }
    }
}

impl EngineState {
    fn new() -> Self {
        Self {
            child: Mutex::new(None),
            pending: Mutex::new(HashMap::new()),
            last_error: Mutex::new(None),
        }
    }

    pub async fn set_last_error(&self, error: QQNTError) {
        *self.last_error.lock().await = Some(error);
    }
}

impl ServerState {
    fn new() -> Self {
        Self {
            child: Mutex::new(None),
            last_error: Mutex::new(None),
        }
    }

    pub async fn set_last_error(&self, error: QQNTError) {
        *self.last_error.lock().await = Some(error);
    }
}
