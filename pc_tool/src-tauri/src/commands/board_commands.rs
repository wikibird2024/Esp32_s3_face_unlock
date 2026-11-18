//! Typed wrappers over console commands (`status json`, `list json`, ...).

use super::text_error;
use crate::app_state::AppState;
use board_protocol::{
    parse_list_reply, parse_status_reply, parse_task_table, BoardStatus, FaceEntry, TaskRow,
};
use tauri::State;

const REPLY_TIMEOUT_MS: u64 = 3000;

async fn reply_lines(state: &State<'_, AppState>, command: &str) -> Result<Vec<String>, String> {
    let reply = state
        .serial
        .run_command(command, REPLY_TIMEOUT_MS)
        .await
        .map_err(text_error)?;
    if reply.timed_out && reply.lines.is_empty() {
        return Err(format!(
            "no reply to '{command}' (is the board connected on the UART port?)"
        ));
    }
    if let Some(error_text) = reply.error_text {
        return Err(error_text);
    }
    Ok(reply.lines)
}

#[tauri::command]
pub async fn read_board_status(state: State<'_, AppState>) -> Result<BoardStatus, String> {
    let lines = reply_lines(&state, "status json").await?;
    let line = lines
        .iter()
        .find(|line| line.starts_with("@status "))
        .ok_or_else(|| "no @status line in reply (firmware without JSON support?)".to_string())?;
    parse_status_reply(line).map_err(text_error)
}

#[tauri::command]
pub async fn read_face_list(state: State<'_, AppState>) -> Result<Vec<FaceEntry>, String> {
    let lines = reply_lines(&state, "list json").await?;
    let line = lines
        .iter()
        .find(|line| line.starts_with("@list "))
        .ok_or_else(|| "no @list line in reply".to_string())?;
    parse_list_reply(line).map_err(text_error)
}

#[tauri::command]
pub async fn read_task_list(state: State<'_, AppState>) -> Result<Vec<TaskRow>, String> {
    let lines = reply_lines(&state, "tasks").await?;
    Ok(parse_task_table(&lines))
}

#[tauri::command]
pub async fn start_telemetry(state: State<'_, AppState>, period_ms: u32) -> Result<(), String> {
    reply_lines(&state, &format!("stats {}", period_ms.max(100)))
        .await
        .map(|_| ())
}

#[tauri::command]
pub async fn stop_telemetry(state: State<'_, AppState>) -> Result<(), String> {
    reply_lines(&state, "stats 0").await.map(|_| ())
}
