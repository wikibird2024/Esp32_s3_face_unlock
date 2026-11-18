//! I/O services behind the bench tool: serial console, camera relay, build
//! identity, flashing, `idf.py build`, backtrace decoding. No tauri here:
//! results go through the [`EventSink`] trait so this crate compiles and
//! tests without the webkit system libraries. The tauri crate only forwards.

pub mod backtrace;
pub mod build_info;
pub mod camera;
pub mod editor;
pub mod events;
pub mod flash;
pub mod idf_build;
pub mod serial;

pub use events::EventSink;

/// Milliseconds since the Unix epoch (host clock).
pub fn now_ms() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|duration| duration.as_millis() as u64)
        .unwrap_or(0)
}
