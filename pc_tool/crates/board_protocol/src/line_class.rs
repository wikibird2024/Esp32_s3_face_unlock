//! Which kind of line did the board send? Used by the log view and by the
//! command/reply matcher to skip lines that belong to other tasks.

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum LineClass {
    /// `I (123) tag: text`
    Log,
    /// `>fps:..` or `>box:..`
    Telemetry,
    /// `@status {...}` or `@list [...]`
    JsonReply,
    /// Inside a Guru Meditation / abort block.
    Panic,
    /// Anything else: command echo and human replies.
    Console,
}

/// Classify one cleaned line (no prompt prefix, no line ending).
/// Panic lines are decided by the `PanicCollector`, not here.
pub fn classify_line(line: &str) -> LineClass {
    if line.starts_with('>') {
        return LineClass::Telemetry;
    }
    if line.starts_with("@status ") || line.starts_with("@list ") {
        return LineClass::JsonReply;
    }
    if crate::log_line::parse_log_line(line).is_some() {
        return LineClass::Log;
    }
    LineClass::Console
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn classifies_each_kind() {
        assert_eq!(classify_line("I (1) app_main: Ready."), LineClass::Log);
        assert_eq!(classify_line(">fps:1"), LineClass::Telemetry);
        assert_eq!(classify_line("@status {}"), LineClass::JsonReply);
        assert_eq!(classify_line("@list []"), LineClass::JsonReply);
        assert_eq!(classify_line("Deleted"), LineClass::Console);
        assert_eq!(classify_line("status"), LineClass::Console);
    }
}
