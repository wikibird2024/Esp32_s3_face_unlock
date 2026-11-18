use crate::app_state::AppState;
use tauri::State;

fn base_url(board_ip: &str) -> String {
    let trimmed = board_ip.trim().trim_end_matches('/');
    if trimmed.starts_with("http://") || trimmed.starts_with("https://") {
        trimmed.to_string()
    } else {
        format!("http://{trimmed}")
    }
}

#[tauri::command]
pub async fn start_camera_stream(
    state: State<'_, AppState>,
    board_ip: String,
) -> Result<(), String> {
    if board_ip.trim().is_empty() {
        return Err("no board address".into());
    }
    state.camera.start(&base_url(&board_ip));
    Ok(())
}

#[tauri::command]
pub fn stop_camera_stream(state: State<'_, AppState>) {
    state.camera.stop();
}

/// Raw JPEG bytes of the newest frame (empty when none yet).
#[tauri::command]
pub fn read_latest_camera_frame(state: State<'_, AppState>) -> tauri::ipc::Response {
    tauri::ipc::Response::new(state.camera.latest_frame().bytes)
}

#[tauri::command]
pub fn save_camera_snapshot(state: State<'_, AppState>, path: String) -> Result<(), String> {
    let frame = state.camera.latest_frame();
    if frame.bytes.is_empty() {
        return Err("no frame received yet".into());
    }
    std::fs::write(&path, &frame.bytes).map_err(|error| error.to_string())
}
