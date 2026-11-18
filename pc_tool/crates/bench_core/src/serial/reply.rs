//! Matches the lines that come back after a command until the next prompt.

use board_protocol::LineClass;
use tokio::sync::oneshot;

/// Reply to one console command.
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ConsoleReply {
    pub command: String,
    pub lines: Vec<String>,
    pub is_error: bool,
    pub error_text: Option<String>,
    pub timed_out: bool,
}

pub struct ReplyCollector {
    pub command: String,
    pub lines: Vec<String>,
    echo_seen: bool,
    done: Option<oneshot::Sender<Vec<String>>>,
}

impl ReplyCollector {
    pub fn new(command: String, done: oneshot::Sender<Vec<String>>) -> Self {
        Self {
            command,
            lines: Vec::new(),
            echo_seen: false,
            done: Some(done),
        }
    }

    /// Offer a line; log and telemetry lines belong to other tasks and are
    /// skipped. The first line equal to the command is the dumb-mode echo.
    pub fn push(&mut self, line: &str, class: LineClass) {
        match class {
            LineClass::Log | LineClass::Telemetry | LineClass::Panic => return,
            LineClass::JsonReply | LineClass::Console => {}
        }
        if !self.echo_seen && line.trim() == self.command.trim() {
            self.echo_seen = true;
            return;
        }
        self.lines.push(line.to_string());
    }

    /// The prompt arrived: hand the lines back.
    pub fn finish(mut self) {
        if let Some(done) = self.done.take() {
            let _ = done.send(std::mem::take(&mut self.lines));
        }
    }

    /// Lines collected so far (used when the reply times out).
    pub fn take_lines(&mut self) -> Vec<String> {
        std::mem::take(&mut self.lines)
    }
}

pub fn reply_from_lines(command: &str, lines: Vec<String>, timed_out: bool) -> ConsoleReply {
    let error_text = lines.iter().find_map(|line| {
        if line.starts_with("Unrecognized command")
            || line.starts_with("Command returned non-zero error code")
            || line.starts_with("Failed:")
            || line.starts_with("Usage:")
            || line.starts_with("Internal error:")
            || line.starts_with("Face store not available")
        {
            Some(line.clone())
        } else {
            None
        }
    });
    ConsoleReply {
        command: command.to_string(),
        is_error: error_text.is_some(),
        error_text,
        lines,
        timed_out,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn skips_echo_and_log_lines_and_detects_errors() {
        let (sender, mut receiver) = oneshot::channel();
        let mut collector = ReplyCollector::new("delete 9".into(), sender);
        collector.push("delete 9", LineClass::Console);
        collector.push("I (1) net_comm: publish x", LineClass::Log);
        collector.push("face not found", LineClass::Console);
        collector.push(
            "Command returned non-zero error code: 0x1 (ESP_FAIL)",
            LineClass::Console,
        );
        collector.finish();
        let lines = receiver.try_recv().unwrap();
        assert_eq!(lines.len(), 2);
        let reply = reply_from_lines("delete 9", lines, false);
        assert!(reply.is_error);
        assert!(reply.error_text.unwrap().starts_with("Command returned"));
    }
}
