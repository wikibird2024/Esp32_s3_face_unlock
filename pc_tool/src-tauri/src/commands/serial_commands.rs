use super::text_error;
use crate::app_state::AppState;
use bench_core::events::SerialStatePayload;
use bench_core::serial::{list_serial_ports as list_ports, ConsoleReply, SerialPortInfo};
use tauri::State;

#[tauri::command]
pub fn list_serial_ports() -> Vec<SerialPortInfo> {
    list_ports()
}

#[tauri::command]
pub fn connect_serial(
    state: State<'_, AppState>,
    port_path: String,
    baud_rate: u32,
) -> Result<(), String> {
    state
        .serial
        .connect(&port_path, baud_rate)
        .map_err(text_error)
}

#[tauri::command]
pub fn disconnect_serial(state: State<'_, AppState>) {
    state.serial.disconnect();
}

#[tauri::command]
pub fn serial_state(state: State<'_, AppState>) -> SerialStatePayload {
    state.serial.state()
}

#[tauri::command]
pub async fn send_console_text(state: State<'_, AppState>, text: String) -> Result<(), String> {
    state
        .serial
        .send_text_in_turn(&text)
        .await
        .map_err(text_error)
}

#[tauri::command]
pub async fn run_console_command(
    state: State<'_, AppState>,
    text: String,
    timeout_ms: Option<u64>,
) -> Result<ConsoleReply, String> {
    let timeout = timeout_ms.unwrap_or(bench_core::serial::manager::DEFAULT_COMMAND_TIMEOUT_MS);
    state
        .serial
        .run_command(&text, timeout)
        .await
        .map_err(text_error)
}

#[tauri::command]
pub fn reset_board(state: State<'_, AppState>) -> Result<(), String> {
    state.serial.reset_board().map_err(text_error)
}

#[tauri::command]
pub fn start_log_file(state: State<'_, AppState>, path: String) -> Result<(), String> {
    state.serial.start_log_file(&path).map_err(text_error)
}

#[tauri::command]
pub fn stop_log_file(state: State<'_, AppState>) {
    state.serial.stop_log_file();
}
