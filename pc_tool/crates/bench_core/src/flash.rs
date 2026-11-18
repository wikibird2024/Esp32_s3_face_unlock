//! Flash and erase through esptool from the ESP-IDF python environment.
//! Chosen over the espflash crate so the exact bins/offsets from
//! `build/flash_args` are written the same way `idf.py flash` does.

use crate::events::{self, FlashProgressPayload, FlashStage, SharedSink};
use std::path::{Path, PathBuf};
use std::process::Stdio;
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::Command;

#[derive(Debug, thiserror::Error)]
pub enum FlashError {
    #[error(
        "esptool not found; set the python path in Settings (looked in ~/.espressif/python_env)"
    )]
    EsptoolMissing,
    #[error("{0}")]
    Io(#[from] std::io::Error),
    #[error("esptool exited with code {0}")]
    Failed(i32),
}

/// Python interpreter that has esptool: an explicit path, else the newest
/// `~/.espressif/python_env/*/bin/python`, else `python3` on PATH.
pub fn find_esptool_python(explicit: Option<&str>) -> Option<PathBuf> {
    if let Some(path) = explicit.filter(|text| !text.trim().is_empty()) {
        let candidate = PathBuf::from(path);
        return candidate.exists().then_some(candidate);
    }
    let home = std::env::var_os("HOME").map(PathBuf::from)?;
    let env_dir = home.join(".espressif/python_env");
    let mut candidates: Vec<PathBuf> = std::fs::read_dir(&env_dir)
        .ok()?
        .flatten()
        .map(|entry| entry.path().join("bin/python"))
        .filter(|path| path.exists())
        .collect();
    candidates.sort();
    candidates.pop().or_else(|| Some(PathBuf::from("python3")))
}

/// Parse `Writing at 0x00010000... (12 %)` style progress.
pub fn parse_progress_percent(line: &str) -> Option<u8> {
    let open = line.rfind('(')?;
    let close = line[open..].find('%')?;
    line[open + 1..open + close].trim().parse().ok()
}

fn stage_for_line(line: &str) -> Option<FlashStage> {
    let lower = line.to_ascii_lowercase();
    if lower.starts_with("connecting") || lower.contains("serial port") {
        Some(FlashStage::Connecting)
    } else if lower.starts_with("erasing") || lower.contains("flash erase") {
        Some(FlashStage::Erasing)
    } else if lower.starts_with("writing at") || lower.starts_with("compressed") {
        Some(FlashStage::Writing)
    } else if lower.starts_with("hash of data verified") || lower.starts_with("verifying") {
        Some(FlashStage::Verifying)
    } else {
        None
    }
}

async fn run_esptool(
    python: &Path,
    args: &[String],
    working_dir: &Path,
    sink: &SharedSink,
) -> Result<(), FlashError> {
    let mut child = Command::new(python)
        .arg("-m")
        .arg("esptool")
        .args(args)
        .current_dir(working_dir)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .kill_on_drop(true)
        .spawn()?;
    let stdout = child.stdout.take().expect("piped stdout");
    let stderr = child.stderr.take().expect("piped stderr");
    let sink_for_errors = sink.clone();
    let error_task = tokio::spawn(async move {
        let mut lines = BufReader::new(stderr).lines();
        let mut collected = Vec::new();
        while let Ok(Some(line)) = lines.next_line().await {
            events::emit(
                &sink_for_errors,
                events::FLASH_PROGRESS,
                &FlashProgressPayload {
                    stage: FlashStage::Writing,
                    file: None,
                    percent: 0,
                    message: Some(line.clone()),
                },
            );
            collected.push(line);
        }
        collected
    });
    let mut lines = BufReader::new(stdout).lines();
    let mut current_file: Option<String> = None;
    let mut stage = FlashStage::Connecting;
    while let Ok(Some(line)) = lines.next_line().await {
        if let Some(new_stage) = stage_for_line(&line) {
            stage = new_stage;
        }
        if line.starts_with("Compressed ") || line.contains("bytes to ") {
            current_file = None;
        }
        let percent = parse_progress_percent(&line).unwrap_or(0);
        events::emit(
            sink,
            events::FLASH_PROGRESS,
            &FlashProgressPayload {
                stage,
                file: current_file.clone(),
                percent,
                message: Some(line),
            },
        );
    }
    let status = child.wait().await?;
    let _ = error_task.await;
    let code = status.code().unwrap_or(-1);
    if status.success() {
        events::emit(
            sink,
            events::FLASH_PROGRESS,
            &FlashProgressPayload {
                stage: FlashStage::Done,
                file: None,
                percent: 100,
                message: None,
            },
        );
        Ok(())
    } else {
        events::emit(
            sink,
            events::FLASH_PROGRESS,
            &FlashProgressPayload {
                stage: FlashStage::Failed,
                file: None,
                percent: 0,
                message: Some(format!("esptool exited with code {code}")),
            },
        );
        Err(FlashError::Failed(code))
    }
}

/// `python -m esptool --chip <chip> -p <port> -b <baud> write_flash @flash_args`
/// run inside the build directory (paths in flash_args are relative to it).
pub async fn flash_build(
    python: Option<&str>,
    chip: &str,
    port_path: &str,
    baud_rate: u32,
    build_dir_path: &str,
    sink: &SharedSink,
) -> Result<(), FlashError> {
    let python = find_esptool_python(python).ok_or(FlashError::EsptoolMissing)?;
    let args = vec![
        "--chip".to_string(),
        chip.to_string(),
        "--port".to_string(),
        port_path.to_string(),
        "--baud".to_string(),
        baud_rate.to_string(),
        "--before".to_string(),
        "default_reset".to_string(),
        "--after".to_string(),
        "hard_reset".to_string(),
        "write_flash".to_string(),
        "@flash_args".to_string(),
    ];
    run_esptool(&python, &args, Path::new(build_dir_path), sink).await
}

pub async fn erase_flash(
    python: Option<&str>,
    chip: &str,
    port_path: &str,
    sink: &SharedSink,
) -> Result<(), FlashError> {
    let python = find_esptool_python(python).ok_or(FlashError::EsptoolMissing)?;
    let args = vec![
        "--chip".to_string(),
        chip.to_string(),
        "--port".to_string(),
        port_path.to_string(),
        "erase_flash".to_string(),
    ];
    run_esptool(&python, &args, Path::new("."), sink).await
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_progress_lines() {
        assert_eq!(
            parse_progress_percent("Writing at 0x00010000... (12 %)"),
            Some(12)
        );
        assert_eq!(
            parse_progress_percent("Writing at 0x0001a000... (100 %)"),
            Some(100)
        );
        assert_eq!(parse_progress_percent("Hash of data verified."), None);
        assert_eq!(
            stage_for_line("Writing at 0x0 (3 %)"),
            Some(FlashStage::Writing)
        );
        assert_eq!(
            stage_for_line("Hash of data verified."),
            Some(FlashStage::Verifying)
        );
    }

    #[test]
    fn finds_a_python() {
        let python = find_esptool_python(None);
        assert!(python.is_some());
    }
}
