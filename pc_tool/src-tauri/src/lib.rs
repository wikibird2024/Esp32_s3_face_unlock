//! Tauri glue: plugins, managed state and the command table. Everything
//! that does real work lives in `bench_core` / `board_protocol`.

mod app_state;
mod commands;

use tauri::Manager;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_store::Builder::new().build())
        .plugin(tauri_plugin_dialog::init())
        .setup(|app| {
            app.manage(app_state::AppState::new(app.handle().clone()));
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::serial_commands::list_serial_ports,
            commands::serial_commands::connect_serial,
            commands::serial_commands::disconnect_serial,
            commands::serial_commands::serial_state,
            commands::serial_commands::send_console_text,
            commands::serial_commands::run_console_command,
            commands::serial_commands::reset_board,
            commands::serial_commands::start_log_file,
            commands::serial_commands::stop_log_file,
            commands::board_commands::read_board_status,
            commands::board_commands::read_face_list,
            commands::board_commands::read_task_list,
            commands::board_commands::start_telemetry,
            commands::board_commands::stop_telemetry,
            commands::camera_commands::start_camera_stream,
            commands::camera_commands::stop_camera_stream,
            commands::camera_commands::read_latest_camera_frame,
            commands::camera_commands::save_camera_snapshot,
            commands::build_commands::read_repo_identity,
            commands::build_commands::read_build_identity,
            commands::build_commands::run_idf_build,
            commands::build_commands::stop_idf_build,
            commands::build_commands::decode_backtrace,
            commands::flash_commands::flash_build,
            commands::flash_commands::erase_flash,
            commands::misc_commands::write_text_file,
            commands::misc_commands::open_in_editor,
            commands::misc_commands::default_editor_template,
            commands::misc_commands::default_paths,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
