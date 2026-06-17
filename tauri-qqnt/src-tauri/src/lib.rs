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
        .invoke_handler(tauri::generate_handler![
            commands::qqnt_command,
            commands::engine_ready,
            commands::connect_server,
            commands::login,
            commands::register_account,
            commands::disconnect_server,
            commands::logout,
            commands::set_user_info,
            commands::get_user_list,
            commands::get_friend_list,
            commands::get_group_list,
            commands::search_friend,
            commands::send_friend_request,
            commands::respond_friend_request,
            commands::send_private_message,
            commands::send_group_message,
            commands::create_group,
            commands::update_group_announcement,
            commands::update_group_member,
            commands::send_file,
            commands::send_image,
            commands::cancel_transfer,
            commands::query_resume,
            commands::e2e_status,
            commands::e2e_announce_identity,
            commands::e2e_pin_identity,
            commands::e2e_request_rotation,
            commands::profile_update,
            commands::settings_sync
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
