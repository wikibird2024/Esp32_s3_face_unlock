//! Shared services, created once in `setup`.

use bench_core::camera::CameraRelay;
use bench_core::events::EventSink;
use bench_core::idf_build::BuildRunner;
use bench_core::serial::SerialManager;
use std::sync::Arc;
use tauri::{AppHandle, Emitter};

/// Forwards backend events to the webview.
pub struct TauriSink(AppHandle);

impl EventSink for TauriSink {
    fn emit(&self, event_name: &str, payload: serde_json::Value) {
        let _ = self.0.emit(event_name, payload);
    }
}

pub struct AppState {
    pub sink: Arc<dyn EventSink>,
    pub serial: SerialManager,
    pub camera: CameraRelay,
    pub build: BuildRunner,
}

impl AppState {
    pub fn new(app: AppHandle) -> Self {
        let sink: Arc<dyn EventSink> = Arc::new(TauriSink(app));
        Self {
            serial: SerialManager::new(Arc::clone(&sink)),
            camera: CameraRelay::new(Arc::clone(&sink)),
            build: BuildRunner::default(),
            sink,
        }
    }
}
