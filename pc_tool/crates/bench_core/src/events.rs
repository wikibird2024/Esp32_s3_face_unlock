//! Names and payloads of everything the backend pushes to the UI.

use board_protocol::{BoardEvent, BoardStatus, FaceBox, LineClass, LogLevel, PanicBlock};
use serde::{Deserialize, Serialize};
use std::sync::Arc;

/// Where events go. The tauri crate implements it with `AppHandle::emit`.
pub trait EventSink: Send + Sync + 'static {
    fn emit(&self, event_name: &str, payload: serde_json::Value);
}

pub type SharedSink = Arc<dyn EventSink>;

/// Helper: serialize and emit, ignoring serialization errors (never happens
/// for our own structs).
pub fn emit<T: Serialize>(sink: &SharedSink, event_name: &str, payload: &T) {
    if let Ok(value) = serde_json::to_value(payload) {
        sink.emit(event_name, value);
    }
}

pub const SERIAL_LINES: &str = "serial:lines";
pub const SERIAL_STATE: &str = "serial:state";
pub const BOARD_BOOT: &str = "board:boot";
pub const BOARD_IP: &str = "board:ip";
pub const BOARD_READY: &str = "board:ready";
pub const BOARD_EVENT: &str = "board:event";
pub const BOARD_PANIC: &str = "board:panic";
pub const BOARD_STATUS: &str = "board:status";
pub const TELEMETRY_SAMPLE: &str = "telemetry:sample";
pub const TELEMETRY_BOX: &str = "telemetry:box";
pub const CAMERA_FRAME: &str = "camera:frame";
pub const CAMERA_STATE: &str = "camera:state";
pub const FLASH_PROGRESS: &str = "flash:progress";
pub const BUILD_OUTPUT: &str = "build:output";
pub const BUILD_DONE: &str = "build:done";

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct LogLine {
    pub sequence: u64,
    /// Host receive time, ms since epoch.
    pub ts_ms: u64,
    pub board_ms: Option<u64>,
    pub level: Option<LogLevel>,
    pub tag: Option<String>,
    pub message: String,
    pub raw: String,
    pub class: LineClass,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SerialLinesBatch {
    pub lines: Vec<LogLine>,
    pub dropped_line_count: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum SerialConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SerialStatePayload {
    pub state: SerialConnectionState,
    pub port_path: String,
    pub baud_rate: u32,
    pub error_text: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BoardBootPayload {
    pub version: String,
    pub idf_version: String,
    pub ts_ms: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BoardIpPayload {
    pub ip: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BoardEventPayload {
    #[serde(flatten)]
    pub event: BoardEvent,
    pub ts_ms: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TelemetrySamplePayload {
    pub ts_ms: u64,
    pub values: std::collections::BTreeMap<String, f64>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TelemetryBoxPayload {
    #[serde(flatten)]
    pub face_box: FaceBox,
    pub ts_ms: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BoardPanicPayload {
    #[serde(flatten)]
    pub block: PanicBlock,
    pub ts_ms: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BoardStatusPayload {
    #[serde(flatten)]
    pub status: BoardStatus,
    pub ts_ms: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CameraFramePayload {
    pub sequence: u64,
    pub byte_count: usize,
    pub received_fps: f64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum CameraConnectionState {
    Stopped,
    Connecting,
    Streaming,
    Offline,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CameraStatePayload {
    pub state: CameraConnectionState,
    pub board_ip: String,
    pub error_text: Option<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum FlashStage {
    Connecting,
    Erasing,
    Writing,
    Verifying,
    Done,
    Failed,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FlashProgressPayload {
    pub stage: FlashStage,
    pub file: Option<String>,
    pub percent: u8,
    pub message: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BuildOutputPayload {
    pub line: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BuildDonePayload {
    pub exit_code: i32,
}

/// Test helper: collects every emitted event.
#[derive(Default)]
pub struct RecordingSink {
    pub events: std::sync::Mutex<Vec<(String, serde_json::Value)>>,
}

impl EventSink for RecordingSink {
    fn emit(&self, event_name: &str, payload: serde_json::Value) {
        self.events
            .lock()
            .expect("sink lock")
            .push((event_name.to_string(), payload));
    }
}
