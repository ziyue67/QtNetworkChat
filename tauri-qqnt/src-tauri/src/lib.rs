pub mod bridge;
pub mod commands;
pub mod error;
pub mod protocol;
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
