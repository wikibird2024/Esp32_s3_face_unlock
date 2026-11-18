//! Open a source location in the user's editor.

/// Template placeholders: `{file}` and `{line}`. Examples:
/// `code -g {file}:{line}`, `kitty nvim +{line} {file}`.
pub fn open_in_editor(template: &str, file: &str, line: u32) -> std::io::Result<()> {
    let filled = template
        .replace("{file}", file)
        .replace("{line}", &line.to_string());
    let mut parts = filled.split_whitespace();
    let Some(program) = parts.next() else {
        return Err(std::io::Error::new(
            std::io::ErrorKind::InvalidInput,
            "empty editor command",
        ));
    };
    std::process::Command::new(program)
        .args(parts)
        .stdin(std::process::Stdio::null())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
        .map(|_| ())
}

/// A sensible default for this machine.
pub fn default_editor_template() -> String {
    let has = |name: &str| {
        std::env::var_os("PATH")
            .map(|paths| std::env::split_paths(&paths).any(|dir| dir.join(name).exists()))
            .unwrap_or(false)
    };
    if has("code") {
        "code -g {file}:{line}".to_string()
    } else if has("kitty") {
        "kitty nvim +{line} {file}".to_string()
    } else {
        "xdg-open {file}".to_string()
    }
}
