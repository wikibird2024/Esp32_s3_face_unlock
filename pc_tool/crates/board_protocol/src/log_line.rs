//! ESP-IDF log line: `I (12345) tag: message`.

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum LogLevel {
    #[serde(rename = "E")]
    Error,
    #[serde(rename = "W")]
    Warn,
    #[serde(rename = "I")]
    Info,
    #[serde(rename = "D")]
    Debug,
    #[serde(rename = "V")]
    Verbose,
}

impl LogLevel {
    fn from_letter(letter: char) -> Option<Self> {
        match letter {
            'E' => Some(Self::Error),
            'W' => Some(Self::Warn),
            'I' => Some(Self::Info),
            'D' => Some(Self::Debug),
            'V' => Some(Self::Verbose),
            _ => None,
        }
    }

    pub fn letter(self) -> char {
        match self {
            Self::Error => 'E',
            Self::Warn => 'W',
            Self::Info => 'I',
            Self::Debug => 'D',
            Self::Verbose => 'V',
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ParsedLogLine {
    pub level: LogLevel,
    /// Milliseconds since boot as printed by the board.
    pub board_ms: u64,
    pub tag: String,
    pub message: String,
}

/// Parse `I (12345) tag: message`; `None` when the line is not a log line.
pub fn parse_log_line(line: &str) -> Option<ParsedLogLine> {
    let mut chars = line.chars();
    let level = LogLevel::from_letter(chars.next()?)?;
    let rest = line.get(1..)?;
    let rest = rest.strip_prefix(" (")?;
    let close = rest.find(')')?;
    let board_ms: u64 = rest[..close].trim().parse().ok()?;
    let rest = rest[close + 1..].strip_prefix(' ')?;
    let colon = rest.find(": ")?;
    let tag = rest[..colon].to_string();
    if tag.is_empty() || tag.contains(' ') {
        return None;
    }
    let message = rest[colon + 2..].to_string();
    Some(ParsedLogLine {
        level,
        board_ms,
        tag,
        message,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_info_line() {
        let parsed = parse_log_line("I (5123) net_comm: Wi-Fi connected, IP 192.168.1.50").unwrap();
        assert_eq!(parsed.level, LogLevel::Info);
        assert_eq!(parsed.board_ms, 5123);
        assert_eq!(parsed.tag, "net_comm");
        assert_eq!(parsed.message, "Wi-Fi connected, IP 192.168.1.50");
    }

    #[test]
    fn parses_error_with_colon_in_message() {
        let parsed = parse_log_line(
            "E (10) app_main: Recognition NOT running (ESP_FAIL). Door stays locked: x",
        )
        .unwrap();
        assert_eq!(parsed.level, LogLevel::Error);
        assert_eq!(parsed.tag, "app_main");
        assert!(parsed.message.starts_with("Recognition NOT running"));
    }

    #[test]
    fn rejects_console_text() {
        assert!(parse_log_line("state            idle").is_none());
        assert!(parse_log_line("Unrecognized command").is_none());
        assert!(parse_log_line(">fps:1").is_none());
        assert!(parse_log_line("I (x) tag: msg").is_none());
    }
}
