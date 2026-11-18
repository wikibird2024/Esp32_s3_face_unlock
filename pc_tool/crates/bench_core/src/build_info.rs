//! Which firmware is where: the git tree, the build directory and (via
//! `@status`) the board. The ELF SHA-256 prefix is the only exact key.

use board_protocol::build_files::{
    parse_flasher_args, parse_partitions_csv, parse_project_description,
};
use board_protocol::{FlasherArgs, Partition, ProjectDescription};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RepoIdentity {
    pub repo_path: String,
    /// `git describe --always --tags --dirty`, what ESP-IDF puts in PROJECT_VER.
    pub git_describe: String,
    pub is_dirty: bool,
    pub head_short_hash: String,
    pub branch: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FlashFileInfo {
    pub offset: u32,
    pub path: String,
    pub byte_count: u64,
    pub exists: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BuildIdentity {
    pub build_dir_path: String,
    pub project: ProjectDescription,
    pub flasher: FlasherArgs,
    pub flash_files: Vec<FlashFileInfo>,
    pub elf_path: String,
    pub elf_sha256: Option<String>,
    pub elf_modified_at_ms: Option<u64>,
    pub partitions: Vec<Partition>,
}

#[derive(Debug, thiserror::Error)]
pub enum BuildInfoError {
    #[error("{0}")]
    Io(#[from] std::io::Error),
    #[error("git failed: {0}")]
    Git(String),
    #[error("{file}: {error}")]
    Parse { file: String, error: String },
}

fn run_git(repo_path: &Path, args: &[&str]) -> Result<String, BuildInfoError> {
    let output = Command::new("git")
        .arg("-C")
        .arg(repo_path)
        .args(args)
        .output()?;
    if !output.status.success() {
        return Err(BuildInfoError::Git(
            String::from_utf8_lossy(&output.stderr).trim().to_string(),
        ));
    }
    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

pub fn read_repo_identity(repo_path: &str) -> Result<RepoIdentity, BuildInfoError> {
    let path = Path::new(repo_path);
    let git_describe = run_git(path, &["describe", "--always", "--tags", "--dirty"])?;
    let head_short_hash = run_git(path, &["rev-parse", "--short", "HEAD"])?;
    let branch = run_git(path, &["rev-parse", "--abbrev-ref", "HEAD"])?;
    let is_dirty = git_describe.ends_with("-dirty");
    Ok(RepoIdentity {
        repo_path: repo_path.to_string(),
        git_describe,
        is_dirty,
        head_short_hash,
        branch,
    })
}

pub fn sha256_of_file(path: &Path) -> std::io::Result<String> {
    let mut file = std::fs::File::open(path)?;
    let mut hasher = Sha256::new();
    std::io::copy(&mut file, &mut hasher)?;
    Ok(format!("{:x}", hasher.finalize()))
}

pub fn read_build_identity(
    build_dir_path: &str,
    repo_path: Option<&str>,
) -> Result<BuildIdentity, BuildInfoError> {
    let build_dir = Path::new(build_dir_path);
    let read = |name: &str| -> Result<String, BuildInfoError> {
        Ok(std::fs::read_to_string(build_dir.join(name))?)
    };
    let project =
        parse_project_description(&read("project_description.json")?).map_err(|error| {
            BuildInfoError::Parse {
                file: "project_description.json".into(),
                error: error.to_string(),
            }
        })?;
    let flasher =
        parse_flasher_args(&read("flasher_args.json")?).map_err(|error| BuildInfoError::Parse {
            file: "flasher_args.json".into(),
            error: error.to_string(),
        })?;
    let flash_files = flasher
        .flash_files
        .iter()
        .map(|file| {
            let full = build_dir.join(&file.path);
            let metadata = std::fs::metadata(&full).ok();
            FlashFileInfo {
                offset: file.offset,
                path: file.path.clone(),
                byte_count: metadata.as_ref().map(|meta| meta.len()).unwrap_or(0),
                exists: metadata.is_some(),
            }
        })
        .collect();
    let elf_path = build_dir.join(&project.app_elf);
    let elf_metadata = std::fs::metadata(&elf_path).ok();
    let elf_sha256 = elf_metadata
        .as_ref()
        .and_then(|_| sha256_of_file(&elf_path).ok());
    let elf_modified_at_ms = elf_metadata
        .and_then(|meta| meta.modified().ok())
        .and_then(|time| time.duration_since(std::time::UNIX_EPOCH).ok())
        .map(|duration| duration.as_millis() as u64);
    let partitions_path = partitions_csv_path(repo_path, &project);
    let partitions = std::fs::read_to_string(partitions_path)
        .map(|text| parse_partitions_csv(&text))
        .unwrap_or_default();
    Ok(BuildIdentity {
        build_dir_path: build_dir_path.to_string(),
        project,
        flasher,
        flash_files,
        elf_path: elf_path.to_string_lossy().to_string(),
        elf_sha256,
        elf_modified_at_ms,
        partitions,
    })
}

fn partitions_csv_path(repo_path: Option<&str>, project: &ProjectDescription) -> PathBuf {
    let root = repo_path
        .map(PathBuf::from)
        .filter(|path| path.join("partitions.csv").exists())
        .unwrap_or_else(|| PathBuf::from(&project.project_path));
    root.join("partitions.csv")
}

/// The toolchain binary next to the compiler named in project_description.json.
pub fn toolchain_binary(project: &ProjectDescription, suffix: &str) -> Option<PathBuf> {
    let compiler = Path::new(&project.c_compiler);
    let file_name = compiler.file_name()?.to_string_lossy().to_string();
    let prefix = file_name.strip_suffix("gcc")?;
    let candidate = compiler.with_file_name(format!("{prefix}{suffix}"));
    candidate.exists().then_some(candidate)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn repo_root() -> PathBuf {
        Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../..")
            .canonicalize()
            .unwrap()
    }

    #[test]
    fn reads_repo_identity_of_this_repo() {
        let identity = read_repo_identity(repo_root().to_str().unwrap()).unwrap();
        assert!(!identity.git_describe.is_empty());
        assert!(identity.head_short_hash.len() >= 7);
    }

    #[test]
    fn reads_build_identity_when_build_dir_exists() {
        let root = repo_root();
        let build_dir = root.join("build");
        if !build_dir.join("project_description.json").exists() {
            eprintln!("no build/ directory, skipping");
            return;
        }
        let identity = read_build_identity(build_dir.to_str().unwrap(), root.to_str()).unwrap();
        assert_eq!(identity.flash_files.len(), 3);
        assert_eq!(identity.partitions.len(), 4);
        assert!(identity.elf_path.ends_with(".elf"));
        if let Some(sha) = &identity.elf_sha256 {
            assert_eq!(sha.len(), 64);
        }
        let addr2line = toolchain_binary(&identity.project, "addr2line");
        assert!(
            addr2line.is_none() || addr2line.unwrap().ends_with("xtensa-esp32s3-elf-addr2line")
        );
    }
}
