//! Owns the serial port: a reader thread turns bytes into lines, classifies
//! them, feeds the reply collector and pushes batches to the UI.

use crate::events::{
    self, BoardBootPayload, BoardEventPayload, BoardIpPayload, BoardPanicPayload,
    BoardStatusPayload, LogLine, SerialConnectionState, SerialLinesBatch, SerialStatePayload,
    SharedSink, TelemetryBoxPayload, TelemetrySamplePayload,
};
use crate::now_ms;
pub use crate::serial::reply::ConsoleReply;
use crate::serial::reply::{reply_from_lines, ReplyCollector};
use board_protocol::{
    board_event, classify_line, log_line, parse_status_reply, parse_telemetry_line, LineClass,
    LineSplitter, PanicCollector, SplitItem, TelemetryLine,
};
use serialport::SerialPort;
use std::io::Write;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tokio::sync::oneshot;

const READ_TIMEOUT_MS: u64 = 100;
const BATCH_MAX_LINES: usize = 200;
const BATCH_MAX_AGE_MS: u64 = 50;
const RECONNECT_DELAY_MS: u64 = 500;
pub const DEFAULT_COMMAND_TIMEOUT_MS: u64 = 3000;

#[derive(Debug, thiserror::Error)]
pub enum SerialError {
    #[error("not connected")]
    NotConnected,
    #[error("serial port: {0}")]
    Port(#[from] serialport::Error),
    #[error("write: {0}")]
    Io(#[from] std::io::Error),
    #[error("another command is running")]
    Busy,
    #[error("reply timed out")]
    Timeout,
}

pub struct Shared {
    writer: Mutex<Option<Box<dyn SerialPort>>>,
    collector: Mutex<Option<ReplyCollector>>,
    state: Mutex<SerialStatePayload>,
    stop_requested: AtomicBool,
    generation: AtomicU64,
    log_file: Mutex<Option<std::io::BufWriter<std::fs::File>>>,
}

pub struct SerialManager {
    sink: SharedSink,
    shared: Arc<Shared>,
    command_lock: tokio::sync::Mutex<()>,
}

impl SerialManager {
    pub fn new(sink: SharedSink) -> Self {
        Self {
            sink,
            shared: Arc::new(Shared {
                writer: Mutex::new(None),
                collector: Mutex::new(None),
                state: Mutex::new(SerialStatePayload {
                    state: SerialConnectionState::Disconnected,
                    port_path: String::new(),
                    baud_rate: 115200,
                    error_text: None,
                }),
                stop_requested: AtomicBool::new(false),
                generation: AtomicU64::new(0),
                log_file: Mutex::new(None),
            }),
            command_lock: tokio::sync::Mutex::new(()),
        }
    }

    pub fn state(&self) -> SerialStatePayload {
        self.shared.state.lock().expect("state lock").clone()
    }

    /// Open the port now (errors come back directly), then keep reading on a
    /// thread until `disconnect`. A later read error starts a reconnect loop.
    pub fn connect(&self, port_path: &str, baud_rate: u32) -> Result<(), SerialError> {
        self.disconnect();
        let port = open_port(port_path, baud_rate)?;
        let generation = self.shared.generation.fetch_add(1, Ordering::SeqCst) + 1;
        self.shared.stop_requested.store(false, Ordering::SeqCst);
        self.set_state(SerialConnectionState::Connected, port_path, baud_rate, None);
        let shared = Arc::clone(&self.shared);
        let sink = Arc::clone(&self.sink);
        let path = port_path.to_string();
        std::thread::Builder::new()
            .name("serial-reader".into())
            .spawn(move || reader_thread(shared, sink, path, baud_rate, generation, Some(port)))
            .map_err(SerialError::Io)?;
        Ok(())
    }

    pub fn disconnect(&self) {
        self.shared.stop_requested.store(true, Ordering::SeqCst);
        self.shared.generation.fetch_add(1, Ordering::SeqCst);
        *self.shared.writer.lock().expect("writer lock") = None;
        if let Some(collector) = self.shared.collector.lock().expect("collector lock").take() {
            collector.finish();
        }
        let previous = self.state();
        if previous.state != SerialConnectionState::Disconnected {
            self.set_state(
                SerialConnectionState::Disconnected,
                &previous.port_path,
                previous.baud_rate,
                None,
            );
        }
    }

    /// Write raw text plus newline, no reply tracking.
    pub fn send_text(&self, text: &str) -> Result<(), SerialError> {
        let mut guard = self.shared.writer.lock().expect("writer lock");
        let writer = guard.as_mut().ok_or(SerialError::NotConnected)?;
        writer.write_all(text.as_bytes())?;
        writer.write_all(b"\n")?;
        writer.flush()?;
        Ok(())
    }

    /// Send a command and collect its reply until the next prompt.
    pub async fn run_command(
        &self,
        text: &str,
        timeout_ms: u64,
    ) -> Result<ConsoleReply, SerialError> {
        let _guard = self.command_lock.lock().await;
        let (sender, receiver) = oneshot::channel();
        {
            let mut collector = self.shared.collector.lock().expect("collector lock");
            *collector = Some(ReplyCollector::new(text.to_string(), sender));
        }
        if let Err(error) = self.send_text(text) {
            self.shared.collector.lock().expect("collector lock").take();
            return Err(error);
        }
        match tokio::time::timeout(Duration::from_millis(timeout_ms), receiver).await {
            Ok(Ok(lines)) => Ok(reply_from_lines(text, lines, false)),
            Ok(Err(_)) => Err(SerialError::NotConnected),
            Err(_) => {
                let lines = self
                    .shared
                    .collector
                    .lock()
                    .expect("collector lock")
                    .take()
                    .map(|mut collector| collector.take_lines())
                    .unwrap_or_default();
                Ok(reply_from_lines(text, lines, true))
            }
        }
    }

    /// esptool-style reset through DTR/RTS. Errors (e.g. on a pty) are ignored.
    pub fn reset_board(&self) -> Result<(), SerialError> {
        let mut guard = self.shared.writer.lock().expect("writer lock");
        let writer = guard.as_mut().ok_or(SerialError::NotConnected)?;
        let _ = writer.write_data_terminal_ready(false);
        let _ = writer.write_request_to_send(true);
        std::thread::sleep(Duration::from_millis(100));
        let _ = writer.write_request_to_send(false);
        Ok(())
    }

    pub fn start_log_file(&self, path: &str) -> Result<(), SerialError> {
        let file = std::fs::OpenOptions::new()
            .create(true)
            .append(true)
            .open(path)?;
        *self.shared.log_file.lock().expect("log file lock") = Some(std::io::BufWriter::new(file));
        Ok(())
    }

    pub fn stop_log_file(&self) {
        if let Some(mut file) = self.shared.log_file.lock().expect("log file lock").take() {
            let _ = file.flush();
        }
    }

    fn set_state(
        &self,
        state: SerialConnectionState,
        port_path: &str,
        baud_rate: u32,
        error_text: Option<String>,
    ) {
        set_state(
            &self.shared,
            &self.sink,
            state,
            port_path,
            baud_rate,
            error_text,
        );
    }
}

fn set_state(
    shared: &Shared,
    sink: &SharedSink,
    state: SerialConnectionState,
    port_path: &str,
    baud_rate: u32,
    error_text: Option<String>,
) {
    let payload = SerialStatePayload {
        state,
        port_path: port_path.to_string(),
        baud_rate,
        error_text,
    };
    *shared.state.lock().expect("state lock") = payload.clone();
    events::emit(sink, events::SERIAL_STATE, &payload);
}

fn open_port(port_path: &str, baud_rate: u32) -> Result<Box<dyn SerialPort>, SerialError> {
    let port = serialport::new(port_path, baud_rate)
        .timeout(Duration::from_millis(READ_TIMEOUT_MS))
        .open()?;
    Ok(port)
}

fn reader_thread(
    shared: Arc<Shared>,
    sink: SharedSink,
    port_path: String,
    baud_rate: u32,
    generation: u64,
    mut first_port: Option<Box<dyn SerialPort>>,
) {
    let mut processor = LineProcessor::new(Arc::clone(&sink));
    let is_current = |shared: &Shared| {
        !shared.stop_requested.load(Ordering::SeqCst)
            && shared.generation.load(Ordering::SeqCst) == generation
    };
    while is_current(&shared) {
        let mut port = match first_port.take() {
            Some(port) => port,
            None => match open_port(&port_path, baud_rate) {
                Ok(port) => port,
                Err(_) => {
                    std::thread::sleep(Duration::from_millis(RECONNECT_DELAY_MS));
                    continue;
                }
            },
        };
        match port.try_clone() {
            Ok(writer) => *shared.writer.lock().expect("writer lock") = Some(writer),
            Err(error) => {
                set_state(
                    &shared,
                    &sink,
                    SerialConnectionState::Reconnecting,
                    &port_path,
                    baud_rate,
                    Some(error.to_string()),
                );
                std::thread::sleep(Duration::from_millis(RECONNECT_DELAY_MS));
                continue;
            }
        }
        set_state(
            &shared,
            &sink,
            SerialConnectionState::Connected,
            &port_path,
            baud_rate,
            None,
        );
        let mut buffer = [0u8; 4096];
        loop {
            if !is_current(&shared) {
                processor.flush();
                return;
            }
            match port.read(&mut buffer) {
                Ok(0) => {}
                Ok(count) => processor.push_bytes(&buffer[..count], &shared),
                Err(error) if error.kind() == std::io::ErrorKind::TimedOut => {}
                Err(error) if error.kind() == std::io::ErrorKind::Interrupted => {}
                Err(error) => {
                    processor.flush();
                    *shared.writer.lock().expect("writer lock") = None;
                    set_state(
                        &shared,
                        &sink,
                        SerialConnectionState::Reconnecting,
                        &port_path,
                        baud_rate,
                        Some(error.to_string()),
                    );
                    break;
                }
            }
            processor.flush_if_due();
        }
        std::thread::sleep(Duration::from_millis(RECONNECT_DELAY_MS));
    }
}

/// Bytes → lines → classified events. Kept separate from the port so it can
/// be tested with a recording sink.
pub struct LineProcessor {
    sink: SharedSink,
    splitter: LineSplitter,
    panic: PanicCollector,
    batch: Vec<LogLine>,
    last_flush: Instant,
    sequence: u64,
    dropped_line_count: u64,
}

impl LineProcessor {
    pub fn new(sink: SharedSink) -> Self {
        Self {
            sink,
            splitter: LineSplitter::new(),
            panic: PanicCollector::new(),
            batch: Vec::new(),
            last_flush: Instant::now(),
            sequence: 0,
            dropped_line_count: 0,
        }
    }

    pub fn push_bytes(&mut self, bytes: &[u8], shared: &Shared) {
        for item in self.splitter.push(bytes) {
            match item {
                SplitItem::Prompt => {
                    if let Some(collector) = shared.collector.lock().expect("collector lock").take()
                    {
                        collector.finish();
                    }
                }
                SplitItem::Line(text) => self.handle_line(text, shared),
            }
        }
        if self.batch.len() >= BATCH_MAX_LINES {
            self.flush();
        }
    }

    fn handle_line(&mut self, text: String, shared: &Shared) {
        let ts_ms = now_ms();
        let (panic_block, in_panic) = self.panic.push(&text);
        let class = if in_panic {
            LineClass::Panic
        } else {
            classify_line(&text)
        };
        let mut line = LogLine {
            sequence: self.sequence,
            ts_ms,
            board_ms: None,
            level: None,
            tag: None,
            message: text.clone(),
            raw: text.clone(),
            class,
        };
        self.sequence += 1;

        match class {
            LineClass::Log => {
                if let Some(parsed) = log_line::parse_log_line(&text) {
                    if let Some(notice) =
                        board_event::parse_board_notice(&parsed.tag, &parsed.message)
                    {
                        self.emit_notice(notice, ts_ms);
                    }
                    if parsed.tag == "net_comm" {
                        if let Some(event) = board_event::parse_publish_message(&parsed.message) {
                            events::emit(
                                &self.sink,
                                events::BOARD_EVENT,
                                &BoardEventPayload { event, ts_ms },
                            );
                        }
                    }
                    line.board_ms = Some(parsed.board_ms);
                    line.level = Some(parsed.level);
                    line.tag = Some(parsed.tag);
                    line.message = parsed.message;
                }
            }
            LineClass::Telemetry => match parse_telemetry_line(&text) {
                Some(TelemetryLine::Sample(sample)) => {
                    let values = sample.values.into_iter().collect();
                    events::emit(
                        &self.sink,
                        events::TELEMETRY_SAMPLE,
                        &TelemetrySamplePayload { ts_ms, values },
                    );
                }
                Some(TelemetryLine::Box(face_box)) => {
                    events::emit(
                        &self.sink,
                        events::TELEMETRY_BOX,
                        &TelemetryBoxPayload { face_box, ts_ms },
                    );
                }
                None => {}
            },
            LineClass::JsonReply | LineClass::Console => {
                if class == LineClass::JsonReply && text.starts_with("@status ") {
                    if let Ok(status) = parse_status_reply(&text) {
                        events::emit(
                            &self.sink,
                            events::BOARD_STATUS,
                            &BoardStatusPayload { status, ts_ms },
                        );
                    }
                }
                if let Some(collector) = shared.collector.lock().expect("collector lock").as_mut() {
                    collector.push(&text, class);
                }
            }
            LineClass::Panic => {}
        }
        if let Some(block) = panic_block {
            events::emit(
                &self.sink,
                events::BOARD_PANIC,
                &BoardPanicPayload { block, ts_ms },
            );
        }
        if let Some(file) = shared.log_file.lock().expect("log file lock").as_mut() {
            let _ = writeln!(file, "{ts_ms} {text}");
        }
        self.batch.push(line);
    }

    fn emit_notice(&self, notice: board_event::BoardNotice, ts_ms: u64) {
        match notice {
            board_event::BoardNotice::Boot {
                version,
                idf_version,
            } => {
                events::emit(
                    &self.sink,
                    events::BOARD_BOOT,
                    &BoardBootPayload {
                        version,
                        idf_version,
                        ts_ms,
                    },
                );
            }
            board_event::BoardNotice::Ip { ip } => {
                events::emit(&self.sink, events::BOARD_IP, &BoardIpPayload { ip });
            }
            board_event::BoardNotice::Ready => {
                events::emit(
                    &self.sink,
                    events::BOARD_READY,
                    &serde_json::json!({ "tsMs": ts_ms }),
                );
            }
        }
    }

    pub fn flush_if_due(&mut self) {
        if !self.batch.is_empty()
            && self.last_flush.elapsed() >= Duration::from_millis(BATCH_MAX_AGE_MS)
        {
            self.flush();
        }
    }

    pub fn flush(&mut self) {
        self.last_flush = Instant::now();
        if self.batch.is_empty() {
            return;
        }
        let batch = SerialLinesBatch {
            lines: std::mem::take(&mut self.batch),
            dropped_line_count: self.dropped_line_count,
        };
        self.dropped_line_count = 0;
        events::emit(&self.sink, events::SERIAL_LINES, &batch);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::events::RecordingSink;

    fn shared_for_test() -> Shared {
        Shared {
            writer: Mutex::new(None),
            collector: Mutex::new(None),
            state: Mutex::new(SerialStatePayload {
                state: SerialConnectionState::Disconnected,
                port_path: String::new(),
                baud_rate: 0,
                error_text: None,
            }),
            stop_requested: AtomicBool::new(false),
            generation: AtomicU64::new(0),
            log_file: Mutex::new(None),
        }
    }

    fn event_names(sink: &RecordingSink) -> Vec<String> {
        sink.events
            .lock()
            .unwrap()
            .iter()
            .map(|(name, _)| name.clone())
            .collect()
    }

    #[test]
    fn boot_event_ip_telemetry_and_reply_flow() {
        let sink = Arc::new(RecordingSink::default());
        let shared = shared_for_test();
        let mut processor = LineProcessor::new(sink.clone());
        processor.push_bytes(
            b"I (300) app_main: Face unlock fcb63ff (IDF v5.5.1)\r\n\
              I (5123) net_comm: Wi-Fi connected, IP 192.168.1.50\r\n\
              face> ",
            &shared,
        );
        // The UI sends "status json" after the prompt: install the collector now.
        let (sender, mut receiver) = oneshot::channel();
        *shared.collector.lock().unwrap() = Some(ReplyCollector::new("status json".into(), sender));
        processor.push_bytes(
            b"status json\r\n\
              I (6000) net_comm: publish face_unlock/door_opened {\"id\":4,\"name\":\"alice\",\"similarity\":0.712,\"request\":0}\r\n\
              >fps:9.8,infer_ms:85\r\n\
              @status {\"state\":\"idle\",\"state_id\":0,\"frames\":1,\"frames_with_face\":0,\"inference_ms\":1,\"last_match\":{\"id\":0,\"similarity\":0,\"threshold\":0.55},\"approval\":null,\"door\":\"locked\",\"wifi\":{\"enabled\":true,\"connected\":true,\"ip\":\"1.2.3.4\"},\"faces\":null,\"heap\":{\"internal\":1,\"psram\":2},\"version\":{\"app\":\"x\",\"idf\":\"y\"}}\r\n\
              face> ",
            &shared,
        );
        processor.flush();
        let names = event_names(&sink);
        assert!(names.contains(&"board:boot".to_string()));
        assert!(names.contains(&"board:ip".to_string()));
        assert!(names.contains(&"board:event".to_string()));
        assert!(names.contains(&"telemetry:sample".to_string()));
        assert!(names.contains(&"board:status".to_string()));
        assert!(names.contains(&"serial:lines".to_string()));
        let lines = receiver.try_recv().expect("reply finished at prompt");
        assert_eq!(lines.len(), 1);
        assert!(lines[0].starts_with("@status "));
        let batch = sink
            .events
            .lock()
            .unwrap()
            .iter()
            .find(|(name, _)| name == "serial:lines")
            .unwrap()
            .1
            .clone();
        assert_eq!(batch["lines"].as_array().unwrap().len(), 6);
        assert_eq!(batch["lines"][0]["tag"], "app_main");
        assert_eq!(batch["lines"][0]["boardMs"], 300);
    }

    #[test]
    fn panic_block_is_collected_and_classified() {
        let sink = Arc::new(RecordingSink::default());
        let shared = shared_for_test();
        let mut processor = LineProcessor::new(sink.clone());
        processor.push_bytes(
            b"Guru Meditation Error: Core  1 panic'ed (LoadProhibited). Exception was unhandled.\r\n\
              Backtrace: 0x4200abcd:0x3fcebbd0 0x42001234:0x3fcebbf0\r\n\
              ELF file SHA256: fe621321f63b7b9b\r\n\
              Rebooting...\r\n",
            &shared,
        );
        processor.flush();
        let events = sink.events.lock().unwrap();
        let panic = events
            .iter()
            .find(|(name, _)| name == "board:panic")
            .expect("panic event");
        assert_eq!(panic.1["frames"].as_array().unwrap().len(), 2);
        assert_eq!(panic.1["elfSha256"], "fe621321f63b7b9b");
        let batch = &events
            .iter()
            .find(|(name, _)| name == "serial:lines")
            .unwrap()
            .1;
        assert_eq!(batch["lines"][0]["class"], "panic");
    }
}
