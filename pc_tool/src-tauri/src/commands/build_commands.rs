use super::text_error;
use crate::app_state::AppState;
use bench_core::backtrace::{decode_addresses, decode_with_binary, FrameLocation};
use bench_core::build_info::{self, BuildIdentity, RepoIdentity};
use std::sync::Arc;
use tauri::State;

#[tauri::command]
pub fn read_repo_identity(repo_path: String) -> Result<RepoIdentity, String> {
    build_info::read_repo_identity(&repo_path).map_err(text_error)
}

#[tauri::command]
pub fn read_build_identity(
    build_dir_path: String,
    repo_path: Option<String>,
) -> Result<BuildIdentity, String> {
    build_info::read_build_identity(&build_dir_path, repo_path.as_deref()).map_err(text_error)
}

#[tauri::command]
pub async fn run_idf_build(
    state: State<'_, AppState>,
    repo_path: String,
    idf_path: String,
    extra_args: Option<Vec<String>>,
) -> Result<(), String> {
    state
        .build
        .start(
            &repo_path,
            &idf_path,
            &extra_args.unwrap_or_default(),
            Arc::clone(&state.sink),
        )
        .await
        .map_err(text_error)
}

#[tauri::command]
pub async fn stop_idf_build(state: State<'_, AppState>) -> Result<(), String> {
    state.build.stop().await;
    Ok(())
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DecodedBacktrace {
    pub frames: Vec<FrameLocation>,
    /// Text from the toolchain addr2line when the DWARF path failed.
    pub fallback_lines: Vec<String>,
}

/// Decode on a blocking thread: loading a 30 MB ELF takes a moment.
#[tauri::command]
pub async fn decode_backtrace(
    elf_path: String,
    addresses: Vec<u32>,
) -> Result<DecodedBacktrace, String> {
    tokio::task::spawn_blocking(move || match decode_addresses(&elf_path, &addresses) {
        Ok(frames) => Ok(DecodedBacktrace {
            frames,
            fallback_lines: Vec::new(),
        }),
        Err(error) => {
            let build_dir = std::path::Path::new(&elf_path)
                .parent()
                .unwrap_or(std::path::Path::new("."));
            let description = std::fs::read_to_string(build_dir.join("project_description.json"))
                .ok()
                .and_then(|text| {
                    board_protocol::build_files::parse_project_description(&text).ok()
                });
            let binary =
                description.and_then(|project| build_info::toolchain_binary(&project, "addr2line"));
            match binary {
                Some(binary) => decode_with_binary(&binary, &elf_path, &addresses)
                    .map(|fallback_lines| DecodedBacktrace {
                        frames: Vec::new(),
                        fallback_lines,
                    })
                    .map_err(|io| format!("{error}; addr2line fallback failed: {io}")),
                None => Err(error.to_string()),
            }
        }
    })
    .await
    .map_err(text_error)?
}
