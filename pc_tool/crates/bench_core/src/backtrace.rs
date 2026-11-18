//! Backtrace addresses → file:line, from the ELF's DWARF (addr2line crate)
//! with the toolchain's `addr2line` binary as a fallback.

use serde::{Deserialize, Serialize};
use std::path::Path;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FrameLocation {
    pub address: u32,
    pub function: Option<String>,
    pub file: Option<String>,
    pub line: Option<u32>,
    pub is_inlined: bool,
}

#[derive(Debug, thiserror::Error)]
pub enum BacktraceError {
    #[error("cannot load ELF {path}: {error}")]
    Load { path: String, error: String },
}

/// Resolve every address. Inlined callers come first, the outermost frame
/// last, matching `addr2line -i`.
pub fn decode_addresses(
    elf_path: &str,
    addresses: &[u32],
) -> Result<Vec<FrameLocation>, BacktraceError> {
    let loader = addr2line::Loader::new(elf_path).map_err(|error| BacktraceError::Load {
        path: elf_path.to_string(),
        error: error.to_string(),
    })?;
    let mut locations = Vec::new();
    for &address in addresses {
        let mut found_any = false;
        if let Ok(mut frames) = loader.find_frames(u64::from(address)) {
            let mut frames_for_address = Vec::new();
            while let Ok(Some(frame)) = frames.next() {
                let function = frame
                    .function
                    .as_ref()
                    .and_then(|name| name.demangle().ok().map(|text| text.to_string()));
                let (file, line) = match frame.location {
                    Some(location) => (location.file.map(str::to_string), location.line),
                    None => (None, None),
                };
                frames_for_address.push(FrameLocation {
                    address,
                    function,
                    file,
                    line,
                    is_inlined: true,
                });
            }
            if let Some(last) = frames_for_address.last_mut() {
                last.is_inlined = false;
            }
            found_any = !frames_for_address.is_empty();
            locations.extend(frames_for_address);
        }
        if !found_any {
            locations.push(FrameLocation {
                address,
                function: loader.find_symbol(u64::from(address)).map(str::to_string),
                file: None,
                line: None,
                is_inlined: false,
            });
        }
    }
    Ok(locations)
}

/// Fallback: run the toolchain's addr2line and keep its text lines.
pub fn decode_with_binary(
    addr2line_path: &Path,
    elf_path: &str,
    addresses: &[u32],
) -> std::io::Result<Vec<String>> {
    let output = std::process::Command::new(addr2line_path)
        .args(["-pfiaC", "-e", elf_path])
        .args(addresses.iter().map(|address| format!("0x{address:08x}")))
        .output()?;
    Ok(String::from_utf8_lossy(&output.stdout)
        .lines()
        .map(str::to_string)
        .collect())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decodes_app_main_in_the_real_elf_when_present() {
        let elf =
            Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../build/face_unlock_esp32_s3.elf");
        if !elf.exists() {
            eprintln!("no ELF, skipping");
            return;
        }
        // app_main is at 0x4200c6cc in the build checked on 2026-11-04; look
        // the symbol up so the test survives rebuilds.
        let loader = addr2line::Loader::new(&elf).unwrap();
        let text_start = loader
            .get_section_range(b".flash.text")
            .map(|range| range.begin)
            .unwrap_or(0x4200_0000);
        let mut probe = None;
        for offset in (0..0x200_000u64).step_by(4) {
            if loader.find_symbol(text_start + offset) == Some("app_main") {
                probe = Some(text_start + offset);
                break;
            }
        }
        let Some(address) = probe else {
            eprintln!("app_main not found by scanning, skipping");
            return;
        };
        let frames = decode_addresses(elf.to_str().unwrap(), &[address as u32]).unwrap();
        assert!(!frames.is_empty());
        let outer = frames.last().unwrap();
        assert_eq!(outer.function.as_deref(), Some("app_main"));
        assert!(outer.file.as_deref().unwrap_or("").ends_with("main/main.c"));
        assert!(outer.line.unwrap_or(0) > 0);
    }
}
