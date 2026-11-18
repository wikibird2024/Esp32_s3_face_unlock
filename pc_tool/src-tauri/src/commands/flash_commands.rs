//! Flashing disconnects the console first (same port), the UI reconnects.

use super::text_error;
use crate::app_state::AppState;
use bench_core::flash;
use std::sync::Arc;
use tauri::State;

#[tauri::command]
pub async fn flash_build(
    state: State<'_, AppState>,
    port_path: String,
    build_dir_path: String,
    baud_rate: Option<u32>,
    chip: Option<String>,
    python_path: Option<String>,
) -> Result<(), String> {
    state.serial.disconnect();
    flash::flash_build(
        python_path.as_deref(),
        chip.as_deref().unwrap_or("esp32s3"),
        &port_path,
        baud_rate.unwrap_or(460800),
        &build_dir_path,
        &Arc::clone(&state.sink),
    )
    .await
    .map_err(text_error)
}

#[tauri::command]
pub async fn erase_flash(
    state: State<'_, AppState>,
    port_path: String,
    chip: Option<String>,
    python_path: Option<String>,
) -> Result<(), String> {
    state.serial.disconnect();
    flash::erase_flash(
        python_path.as_deref(),
        chip.as_deref().unwrap_or("esp32s3"),
        &port_path,
        &Arc::clone(&state.sink),
    )
    .await
    .map_err(text_error)
}
