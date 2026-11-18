pub mod board_commands;
pub mod build_commands;
pub mod camera_commands;
pub mod flash_commands;
pub mod misc_commands;
pub mod serial_commands;

/// Every command returns `Result<T, String>` so the UI gets a readable text.
pub fn text_error<E: std::fmt::Display>(error: E) -> String {
    error.to_string()
}
