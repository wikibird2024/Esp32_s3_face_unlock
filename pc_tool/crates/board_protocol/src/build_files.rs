//! ESP-IDF build output files: `build/flasher_args.json`,
//! `build/project_description.json` and the project's `partitions.csv`.

use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FlashFile {
    pub offset: u32,
    /// Path relative to the build directory.
    pub path: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FlasherArgs {
    pub chip: String,
    pub flash_mode: String,
    pub flash_size: String,
    pub flash_freq: String,
    /// Sorted by offset.
    pub flash_files: Vec<FlashFile>,
}

#[derive(Deserialize)]
struct RawFlasherArgs {
    flash_settings: BTreeMap<String, String>,
    flash_files: BTreeMap<String, String>,
    extra_esptool_args: BTreeMap<String, serde_json::Value>,
}

pub fn parse_flasher_args(json: &str) -> Result<FlasherArgs, serde_json::Error> {
    let raw: RawFlasherArgs = serde_json::from_str(json)?;
    let mut flash_files: Vec<FlashFile> = raw
        .flash_files
        .iter()
        .filter_map(|(offset_text, path)| {
            Some(FlashFile {
                offset: parse_offset(offset_text)?,
                path: path.clone(),
            })
        })
        .collect();
    flash_files.sort_by_key(|file| file.offset);
    let setting = |key: &str| raw.flash_settings.get(key).cloned().unwrap_or_default();
    Ok(FlasherArgs {
        chip: raw
            .extra_esptool_args
            .get("chip")
            .and_then(|value| value.as_str())
            .unwrap_or("esp32s3")
            .to_string(),
        flash_mode: setting("flash_mode"),
        flash_size: setting("flash_size"),
        flash_freq: setting("flash_freq"),
        flash_files,
    })
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all(deserialize = "snake_case", serialize = "camelCase"))]
pub struct ProjectDescription {
    pub project_name: String,
    pub project_version: String,
    #[serde(default)]
    pub project_path: String,
    #[serde(default)]
    pub idf_path: String,
    #[serde(default)]
    pub build_dir: String,
    pub app_elf: String,
    #[serde(default)]
    pub app_bin: String,
    #[serde(default)]
    pub target: String,
    #[serde(default)]
    pub git_revision: String,
    #[serde(default)]
    pub monitor_baud: String,
    #[serde(default)]
    pub c_compiler: String,
}

pub fn parse_project_description(json: &str) -> Result<ProjectDescription, serde_json::Error> {
    serde_json::from_str(json)
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Partition {
    pub name: String,
    pub kind: String,
    pub subtype: String,
    pub offset: u32,
    pub size_bytes: u32,
}

/// Parse `partitions.csv`; blank offsets are filled the way ESP-IDF does
/// (app partitions aligned to 0x10000, data to 0x1000).
pub fn parse_partitions_csv(text: &str) -> Vec<Partition> {
    let mut partitions = Vec::new();
    let mut next_offset: u32 = 0x8000 + 0x1000; // partition table itself
    for line in text.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let fields: Vec<&str> = line.split(',').map(str::trim).collect();
        if fields.len() < 5 {
            continue;
        }
        let kind = fields[1].to_string();
        let align: u32 = if kind == "app" { 0x10000 } else { 0x1000 };
        let offset = match parse_offset(fields[3]) {
            Some(offset) => offset,
            None => next_offset.div_ceil(align) * align,
        };
        let Some(size_bytes) = parse_size(fields[4]) else {
            continue;
        };
        next_offset = offset + size_bytes;
        partitions.push(Partition {
            name: fields[0].to_string(),
            kind,
            subtype: fields[2].to_string(),
            offset,
            size_bytes,
        });
    }
    partitions
}

fn parse_offset(text: &str) -> Option<u32> {
    let text = text.trim();
    if text.is_empty() {
        return None;
    }
    if let Some(hex) = text.strip_prefix("0x").or_else(|| text.strip_prefix("0X")) {
        return u32::from_str_radix(hex, 16).ok();
    }
    parse_size(text)
}

/// `4M`, `256K`, `0x6000` or plain bytes.
pub fn parse_size(text: &str) -> Option<u32> {
    let text = text.trim();
    if let Some(hex) = text.strip_prefix("0x").or_else(|| text.strip_prefix("0X")) {
        return u32::from_str_radix(hex, 16).ok();
    }
    if let Some(number) = text.strip_suffix(['M', 'm']) {
        return number.parse::<u32>().ok()?.checked_mul(1024 * 1024);
    }
    if let Some(number) = text.strip_suffix(['K', 'k']) {
        return number.parse::<u32>().ok()?.checked_mul(1024);
    }
    text.parse().ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    const FLASHER_ARGS: &str = r#"{
        "write_flash_args" : [ "--flash_mode", "dio", "--flash_size", "8MB", "--flash_freq", "80m" ],
        "flash_settings" : { "flash_mode": "dio", "flash_size": "8MB", "flash_freq": "80m" },
        "flash_files" : {
            "0x0" : "bootloader/bootloader.bin",
            "0x10000" : "face_unlock_esp32_s3.bin",
            "0x8000" : "partition_table/partition-table.bin"
        },
        "extra_esptool_args" : { "after"  : "hard_reset", "before" : "default_reset", "stub"   : true, "chip"   : "esp32s3" }
    }"#;

    #[test]
    fn parses_flasher_args_sorted() {
        let args = parse_flasher_args(FLASHER_ARGS).unwrap();
        assert_eq!(args.chip, "esp32s3");
        assert_eq!(args.flash_size, "8MB");
        let offsets: Vec<u32> = args.flash_files.iter().map(|file| file.offset).collect();
        assert_eq!(offsets, vec![0x0, 0x8000, 0x10000]);
        assert_eq!(args.flash_files[2].path, "face_unlock_esp32_s3.bin");
    }

    #[test]
    fn parses_project_description() {
        let json = r#"{"version":"1.2","project_name":"face_unlock_esp32_s3","project_version":"8df079c-dirty","app_elf":"face_unlock_esp32_s3.elf","target":"esp32s3","c_compiler":"/x/bin/xtensa-esp32s3-elf-gcc","config_environment":{"a":"b"}}"#;
        let description = parse_project_description(json).unwrap();
        assert_eq!(description.project_version, "8df079c-dirty");
        assert_eq!(description.app_elf, "face_unlock_esp32_s3.elf");
        assert!(description.c_compiler.ends_with("gcc"));
    }

    #[test]
    fn parses_partitions_with_blank_offset() {
        let csv = "# Name, Type, SubType, Offset, Size\nnvs, data, nvs, 0x9000, 0x6000\nphy_init, data, phy, 0xf000, 0x1000\nfactory, app, factory, 0x10000, 4M\nface_nvs, data, nvs, , 256K\n";
        let partitions = parse_partitions_csv(csv);
        assert_eq!(partitions.len(), 4);
        assert_eq!(partitions[2].size_bytes, 4 * 1024 * 1024);
        assert_eq!(partitions[3].offset, 0x10000 + 4 * 1024 * 1024);
        assert_eq!(partitions[3].size_bytes, 256 * 1024);
    }

    #[test]
    fn parses_real_repo_files_when_present() {
        let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../..");
        if let Ok(text) = std::fs::read_to_string(root.join("build/flasher_args.json")) {
            let args = parse_flasher_args(&text).unwrap();
            assert_eq!(args.flash_files.len(), 3);
        }
        if let Ok(text) = std::fs::read_to_string(root.join("build/project_description.json")) {
            let description = parse_project_description(&text).unwrap();
            assert_eq!(description.project_name, "face_unlock_esp32_s3");
        }
        let csv =
            std::fs::read_to_string(root.join("partitions.csv")).expect("partitions.csv in repo");
        assert_eq!(parse_partitions_csv(&csv).len(), 4);
    }
}
