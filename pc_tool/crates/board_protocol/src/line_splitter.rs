//! Turns raw serial bytes into whole lines plus prompt markers.
//!
//! The board console runs in "dumb" mode (we never answer the terminal
//! probe), so: lines end in `\r\n`, every typed byte is echoed, the prompt
//! `face> ` is printed without a newline, and a log line written while the
//! prompt is showing arrives as `face> I (123) tag: text`.

/// Prompt the firmware prints (`esp_console` repl, see console_commands.c).
pub const PROMPT: &str = "face> ";

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SplitItem {
    /// A full line without its line ending, ESC sequences and prompt prefix.
    Line(String),
    /// The console printed its prompt: the previous reply is complete.
    Prompt,
}

#[derive(Debug, Default)]
pub struct LineSplitter {
    pending: Vec<u8>,
}

impl LineSplitter {
    pub fn new() -> Self {
        Self::default()
    }

    /// Feed bytes; returns every item that became complete.
    pub fn push(&mut self, bytes: &[u8]) -> Vec<SplitItem> {
        self.pending.extend_from_slice(bytes);
        let mut items = Vec::new();
        while let Some(newline_index) = self.pending.iter().position(|byte| *byte == b'\n') {
            let raw: Vec<u8> = self.pending.drain(..=newline_index).collect();
            emit_line(clean_line(&raw), &mut items);
        }
        // A prompt without a newline marks the end of a reply. The prompt
        // text is removed, so a line that follows later is a plain line.
        if clean_line(&self.pending) == PROMPT {
            self.pending.clear();
            items.push(SplitItem::Prompt);
        }
        items
    }

    /// Bytes received so far that do not yet form a line (for debugging).
    pub fn pending_text(&self) -> String {
        clean_line(&self.pending)
    }
}

fn emit_line(text: String, items: &mut Vec<SplitItem>) {
    if let Some(rest) = text.strip_prefix(PROMPT) {
        items.push(SplitItem::Prompt);
        if !rest.is_empty() {
            items.push(SplitItem::Line(rest.to_string()));
        }
    } else if text == PROMPT.trim_end() {
        items.push(SplitItem::Prompt);
    } else if !text.is_empty() {
        items.push(SplitItem::Line(text));
    }
}

/// Strip `\r`, ESC sequences and the linenoise backspace echo (`\x08 `).
fn clean_line(raw: &[u8]) -> String {
    let mut out = String::with_capacity(raw.len());
    let mut index = 0;
    while index < raw.len() {
        let byte = raw[index];
        if byte == 0x1b {
            index += 1;
            if index < raw.len() && raw[index] == b'[' {
                index += 1;
                while index < raw.len() && !(0x40..=0x7e).contains(&raw[index]) {
                    index += 1;
                }
            }
            index += 1;
            continue;
        }
        if byte == b'\r' || byte == b'\n' || byte == 0x08 {
            index += 1;
            continue;
        }
        if byte < 0x20 && byte != b'\t' {
            index += 1;
            continue;
        }
        if byte < 0x80 {
            out.push(byte as char);
            index += 1;
            continue;
        }
        // Non-ASCII: decode as much UTF-8 as possible, else '?'.
        match std::str::from_utf8(&raw[index..]) {
            Ok(rest) => {
                let first = rest.chars().next().unwrap_or('?');
                out.push(first);
                index += first.len_utf8();
            }
            Err(error) => {
                let valid = error.valid_up_to();
                if valid > 0 {
                    let first = std::str::from_utf8(&raw[index..index + valid])
                        .unwrap_or("?")
                        .chars()
                        .next()
                        .unwrap_or('?');
                    out.push(first);
                    index += first.len_utf8().max(1);
                } else {
                    out.push('?');
                    index += 1;
                }
            }
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn lines(items: &[SplitItem]) -> Vec<String> {
        items
            .iter()
            .map(|item| match item {
                SplitItem::Line(text) => text.clone(),
                SplitItem::Prompt => "<prompt>".to_string(),
            })
            .collect()
    }

    #[test]
    fn splits_crlf_lines_and_keeps_partial() {
        let mut splitter = LineSplitter::new();
        let items = splitter.push(b"I (1) app_main: hello\r\nI (2) net_comm: wo");
        assert_eq!(lines(&items), vec!["I (1) app_main: hello"]);
        let items = splitter.push(b"rld\r\n");
        assert_eq!(lines(&items), vec!["I (2) net_comm: world"]);
    }

    #[test]
    fn prompt_without_newline_ends_reply() {
        let mut splitter = LineSplitter::new();
        let items = splitter.push(b"status\r\nstate            idle\r\nface> ");
        assert_eq!(
            lines(&items),
            vec!["status", "state            idle", "<prompt>"]
        );
        // Echo of the next command after the prompt.
        let items = splitter.push(b"list\r\n");
        assert_eq!(lines(&items), vec!["list"]);
    }

    #[test]
    fn prompt_arrives_in_two_chunks() {
        let mut splitter = LineSplitter::new();
        assert!(splitter.push(b"fac").is_empty());
        let items = splitter.push(b"e> ");
        assert_eq!(items, vec![SplitItem::Prompt]);
    }

    #[test]
    fn log_line_after_prompt_in_one_chunk() {
        let mut splitter = LineSplitter::new();
        let items = splitter.push(b"face> I (5) net_comm: Wi-Fi connected, IP 1.2.3.4\r\n");
        assert_eq!(
            lines(&items),
            vec!["<prompt>", "I (5) net_comm: Wi-Fi connected, IP 1.2.3.4"]
        );
    }

    #[test]
    fn prompt_then_log_line_in_later_chunk_emits_one_prompt() {
        let mut splitter = LineSplitter::new();
        let first = splitter.push(b"face> ");
        assert_eq!(first, vec![SplitItem::Prompt]);
        let second = splitter.push(b"I (5) tag: msg\r\n");
        assert_eq!(lines(&second), vec!["I (5) tag: msg"]);
    }

    #[test]
    fn strips_escape_sequences_and_colour() {
        let mut splitter = LineSplitter::new();
        let items =
            splitter.push(b"\x1b[0;32mI (1) tag: green\x1b[0m\r\n\x1b[5n\x1b[0;32mface> \x1b[0m");
        assert_eq!(lines(&items), vec!["I (1) tag: green", "<prompt>"]);
    }

    #[test]
    fn empty_lines_are_dropped() {
        let mut splitter = LineSplitter::new();
        let items = splitter.push(b"\r\n\r\nx\r\n");
        assert_eq!(lines(&items), vec!["x"]);
    }
}
