pub mod bridge;
pub mod commands;
pub mod error;
pub mod protocol;
pub mod shared_buffer;
pub mod sidecar;
pub mod state;

use std::sync::Arc;

use state::AppState;
use tauri::{Manager, RunEvent};

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_global_shortcut::Builder::new().build())
        .setup(|app| {
            let state = Arc::new(AppState::new());
            app.manage(state.clone());
            sidecar::start_server(app.handle().clone(), state.clone());
            sidecar::start_engine(app.handle().clone(), state);
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::qqnt_command,
            commands::read_image_base64,
            commands::get_screenshot_monitor_info,
            commands::get_screenshot_virtual_screen_info,
            commands::capture_screenshot,
            commands::capture_screenshot_shared_buffer,
            commands::crop_screenshot,
            commands::release_screenshot_capture,
            commands::set_screenshot_window_exclude_from_capture,
            commands::prepare_screenshot_window,
            commands::hide_main_window,
            commands::restore_main_window,
            commands::save_file_to_directory,
            commands::save_base64_file_to_directory,
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
            commands::settings_sync,
            commands::clear_session_history,
            commands::delete_local_message,
            commands::favorite_local_message,
            commands::add_local_emoji,
            commands::multi_select_local_message,
            commands::quote_local_message,
            commands::set_essence_local_message,
            commands::recall_local_message,
            commands::forward_local_message,
            commands::view_local_profile,
            commands::add_local_friend,
            commands::report_local_user,
            commands::block_local_user,
            commands::edit_local_group_nickname,
            commands::get_local_chat_actions
        ])
        .build(tauri::generate_context!())
        .expect("error while building tauri application")
        .run(|app_handle, event| {
            if let RunEvent::ExitRequested { api, .. } = &event {
                if let Some(main_window) = app_handle.get_webview_window("main") {
                    if !main_window.is_visible().unwrap_or(true) {
                        api.prevent_exit();
                        let _ = main_window.show();
                        let _ = main_window.unminimize();
                        let _ = main_window.set_focus();
                        return;
                    }
                }
            }
            if matches!(event, RunEvent::ExitRequested { .. } | RunEvent::Exit) {
                let state = app_handle.state::<Arc<AppState>>().inner().clone();
                tauri::async_runtime::block_on(async move {
                    sidecar::stop_sidecars(&state).await;
                });
            }
        });
}

#[cfg(test)]
mod tests {
    use std::collections::BTreeSet;

    use serde_json::Value;

    const GENERIC_COMMAND: &str = "qqnt_command";

    fn protocol_contract() -> Value {
        serde_json::from_str(include_str!(
            "../../../tests/fixtures/protocol_contract.json"
        ))
        .expect("protocol contract fixture should parse")
    }

    fn contract_commands() -> BTreeSet<String> {
        protocol_contract()["commands"]
            .as_array()
            .expect("protocol contract commands should be an array")
            .iter()
            .map(|item| {
                item.as_str()
                    .expect("protocol contract command should be a string")
                    .to_string()
            })
            .collect()
    }

    fn registered_commands() -> BTreeSet<&'static str> {
        let source = include_str!("lib.rs");
        let handler_start = source
            .find("tauri::generate_handler![")
            .expect("Tauri generate_handler block should exist");
        let handler_source = &source[handler_start..];
        let handler_end = handler_source
            .find("])")
            .expect("Tauri generate_handler block should close");

        handler_source[..handler_end]
            .lines()
            .filter_map(|line| {
                line.trim()
                    .strip_prefix("commands::")
                    .map(|name| name.trim_end_matches(',').trim())
            })
            .collect()
    }

    fn protocol_op_for_command(command: &str) -> Option<&str> {
        match command {
            GENERIC_COMMAND => None,
            "read_image_base64" => None,
            "get_screenshot_monitor_info" => None,
            "get_screenshot_virtual_screen_info" => None,
            "capture_screenshot" => None,
            "capture_screenshot_shared_buffer" => None,
            "crop_screenshot" => None,
            "release_screenshot_capture" => None,
            "set_screenshot_window_exclude_from_capture" => None,
            "prepare_screenshot_window" => None,
            "hide_main_window" => None,
            "restore_main_window" => None,
            "save_file_to_directory" => None,
            "save_base64_file_to_directory" => None,
            "clear_session_history" => None,
            "delete_local_message" => None,
            "favorite_local_message" => None,
            "add_local_emoji" => None,
            "multi_select_local_message" => None,
            "quote_local_message" => None,
            "set_essence_local_message" => None,
            "recall_local_message" => None,
            "forward_local_message" => None,
            "view_local_profile" => None,
            "add_local_friend" => None,
            "report_local_user" => None,
            "block_local_user" => None,
            "edit_local_group_nickname" => None,
            "get_local_chat_actions" => None,
            "engine_ready" => Some("ready"),
            "connect_server" => Some("connect"),
            "register_account" => Some("register"),
            "disconnect_server" => Some("disconnect"),
            command => Some(command),
        }
    }

    #[test]
    fn tauri_handler_registration_covers_protocol_contract() {
        let registered = registered_commands();
        assert!(
            registered.contains(GENERIC_COMMAND),
            "generic qqnt_command should remain registered"
        );

        let actual: BTreeSet<String> = registered
            .iter()
            .filter_map(|command| protocol_op_for_command(command).map(str::to_string))
            .collect();

        assert_eq!(actual, contract_commands());
    }
}
