//! Parsers for everything the face-unlock board sends to the PC: console
//! lines, `@status` / `@list` JSON replies, `>` telemetry, event log lines,
//! panic blocks, the MJPEG stream and the ESP-IDF build output files.
//!
//! No tauri or I/O dependency: every function takes bytes or text and returns
//! plain data, so `cargo test -p board_protocol` runs on any machine.
//! The contract is `docs/console_protocol.md` in the repo root.

pub mod board_event;
pub mod build_files;
pub mod line_class;
pub mod line_splitter;
pub mod log_line;
pub mod mjpeg_parser;
pub mod panic_block;
pub mod status_json;
pub mod task_table;
pub mod telemetry_line;

pub use board_event::{BoardEvent, BoardEventKind, BoardNotice};
pub use build_files::{FlashFile, FlasherArgs, Partition, ProjectDescription};
pub use line_class::{classify_line, LineClass};
pub use line_splitter::{LineSplitter, SplitItem, PROMPT};
pub use log_line::{LogLevel, ParsedLogLine};
pub use mjpeg_parser::{boundary_from_content_type, MjpegParser, DEFAULT_BOUNDARY};
pub use panic_block::{PanicBlock, PanicCollector};
pub use status_json::{parse_list_reply, parse_status_reply, BoardStatus, FaceEntry};
pub use task_table::{parse_task_table, TaskRow};
pub use telemetry_line::{parse_telemetry_line, FaceBox, TelemetryLine, TelemetrySample};
