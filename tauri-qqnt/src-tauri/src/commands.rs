use std::sync::Arc;

use serde_json::Value;
use tauri::State;

use crate::bridge;
use crate::error::QQNTError;
use crate::state::AppState;

#[tauri::command]
pub async fn qqnt_command(
    state: State<'_, Arc<AppState>>,
    payload: Value,
) -> Result<Value, QQNTError> {
    bridge::call_engine(state.inner(), payload).await
}
