pub mod bridge;
pub mod commands;
pub mod error;
pub mod sidecar;
pub mod state;

use std::sync::Arc;

use state::AppState;
use tauri::Manager;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_opener::init())
        .setup(|app| {
            let state = Arc::new(AppState::new());
            app.manage(state.clone());
            sidecar::start_server(app.handle().clone(), state.clone());
            sidecar::start_engine(app.handle().clone(), state);
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![commands::qqnt_command])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
