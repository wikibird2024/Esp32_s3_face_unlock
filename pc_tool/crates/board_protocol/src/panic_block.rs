//! Collects an ESP-IDF panic block and pulls out the backtrace.
//!
//! ```text
//! Guru Meditation Error: Core  1 panic'ed (LoadProhibited). Exception was unhandled.
//! Core  1 register dump:
//! PC      : 0x4200abcd  PS      : 0x00060030  ...
//! Backtrace: 0x4200abcd:0x3fcebbd0 0x42001234:0x3fcebbf0
//!
//! ELF file SHA256: 1a2b3c4d5e6f...
//! Rebooting...
//! ```

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BacktraceFrame {
    pub pc: u32,
    pub sp: u32,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PanicBlock {
    /// First line, e.g. `Guru Meditation Error: Core  1 panic'ed (LoadProhibited). ...`
    pub reason: String,
    pub core: Option<u32>,
    pub frames: Vec<BacktraceFrame>,
    pub is_corrupted: bool,
    /// Hex as printed, may be a prefix only.
    pub elf_sha256: Option<String>,
    pub raw_lines: Vec<String>,
}

/// Maximum lines kept per block before it is force-closed.
const MAX_BLOCK_LINES: usize = 400;

#[derive(Debug, Default)]
pub struct PanicCollector {
    lines: Vec<String>,
    active: bool,
}

impl PanicCollector {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn is_active(&self) -> bool {
        self.active
    }

    /// Feed one cleaned line. Returns the block when it is complete.
    /// Returns `true` in `.1` when the line belongs to a panic block.
    pub fn push(&mut self, line: &str) -> (Option<PanicBlock>, bool) {
        if !self.active {
            if is_panic_start(line) {
                self.active = true;
                self.lines.clear();
                self.lines.push(line.to_string());
                return (None, true);
            }
            return (None, false);
        }
        self.lines.push(line.to_string());
        let is_end = line.starts_with("Rebooting...")
            || line.starts_with("CPU halted")
            || line.starts_with("ESP-ROM:")
            || self.lines.len() >= MAX_BLOCK_LINES;
        if is_end {
            return (Some(self.finish()), true);
        }
        (None, true)
    }

    /// Close an unfinished block (e.g. the board went silent).
    pub fn finish(&mut self) -> PanicBlock {
        self.active = false;
        let lines = std::mem::take(&mut self.lines);
        build_block(lines)
    }
}

fn is_panic_start(line: &str) -> bool {
    line.starts_with("Guru Meditation Error:")
        || line.starts_with("abort() was called")
        || line.starts_with("assert failed:")
        || line.contains("***ERROR*** A stack overflow in task")
}

fn build_block(lines: Vec<String>) -> PanicBlock {
    let reason = lines.first().cloned().unwrap_or_default();
    let core = parse_core(&reason);
    let mut frames = Vec::new();
    let mut is_corrupted = false;
    let mut elf_sha256 = None;
    for line in &lines {
        if let Some(rest) = line.strip_prefix("Backtrace:") {
            let (parsed, corrupted) = parse_backtrace(rest);
            frames.extend(parsed);
            is_corrupted |= corrupted;
        } else if let Some(rest) = line.strip_prefix("ELF file SHA256:") {
            elf_sha256 = Some(rest.trim().to_string());
        }
    }
    PanicBlock {
        reason,
        core,
        frames,
        is_corrupted,
        elf_sha256,
        raw_lines: lines,
    }
}

fn parse_core(reason: &str) -> Option<u32> {
    let rest = reason.split("Core").nth(1)?;
    rest.split_whitespace().next()?.parse().ok()
}

/// Parse `0x4200abcd:0x3fcebbd0 0x42001234:0x3fcebbf0 |<-CORRUPTED`.
pub fn parse_backtrace(text: &str) -> (Vec<BacktraceFrame>, bool) {
    let mut frames = Vec::new();
    let mut is_corrupted = false;
    for token in text.split_whitespace() {
        if token.contains("CORRUPTED") {
            is_corrupted = true;
            continue;
        }
        let Some((pc_text, sp_text)) = token.split_once(':') else {
            continue;
        };
        let (Some(pc), Some(sp)) = (parse_hex(pc_text), parse_hex(sp_text)) else {
            continue;
        };
        frames.push(BacktraceFrame { pc, sp });
    }
    (frames, is_corrupted)
}

pub fn parse_hex(text: &str) -> Option<u32> {
    let digits = text
        .trim()
        .trim_start_matches("0x")
        .trim_start_matches("0X");
    u32::from_str_radix(digits, 16).ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    const BLOCK: &[&str] = &[
        "Guru Meditation Error: Core  1 panic'ed (LoadProhibited). Exception was unhandled.",
        "Core  1 register dump:",
        "PC      : 0x4200abcd  PS      : 0x00060030  A0      : 0x82001234  A1      : 0x3fcebbd0",
        "Backtrace: 0x4200abcd:0x3fcebbd0 0x42001234:0x3fcebbf0 0x42002345:0x3fcebc20",
        "",
        "ELF file SHA256: 1a2b3c4d5e6f7788",
        "Rebooting...",
    ];

    #[test]
    fn collects_block_and_backtrace() {
        let mut collector = PanicCollector::new();
        assert_eq!(collector.push("I (1) tag: normal"), (None, false));
        let mut result = None;
        for line in BLOCK {
            let (block, in_block) = collector.push(line);
            assert!(in_block);
            if block.is_some() {
                result = block;
            }
        }
        let block = result.expect("block closed on Rebooting");
        assert_eq!(block.core, Some(1));
        assert_eq!(block.frames.len(), 3);
        assert_eq!(
            block.frames[0],
            BacktraceFrame {
                pc: 0x4200abcd,
                sp: 0x3fcebbd0
            }
        );
        assert_eq!(block.elf_sha256.as_deref(), Some("1a2b3c4d5e6f7788"));
        assert!(!block.is_corrupted);
        assert!(!collector.is_active());
    }

    #[test]
    fn marks_corrupted_backtrace() {
        let (frames, corrupted) = parse_backtrace(" 0x40000001:0x3fc00000 |<-CORRUPTED");
        assert_eq!(frames.len(), 1);
        assert!(corrupted);
    }

    #[test]
    fn abort_starts_a_block() {
        let mut collector = PanicCollector::new();
        assert!(
            collector
                .push("abort() was called at PC 0x4037abcd on core 0")
                .1
        );
        assert!(collector.is_active());
        let block = collector.finish();
        assert_eq!(block.core, None);
        assert!(block.frames.is_empty());
    }
}
