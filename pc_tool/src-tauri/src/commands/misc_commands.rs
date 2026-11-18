use bench_core::editor;

#[tauri::command]
pub fn write_text_file(path: String, text: String) -> Result<(), String> {
    std::fs::write(&path, text).map_err(|error| error.to_string())
}

#[tauri::command]
pub fn open_in_editor(template: Option<String>, file: String, line: u32) -> Result<(), String> {
    let template = template
        .filter(|text| !text.trim().is_empty())
        .unwrap_or_else(editor::default_editor_template);
    editor::open_in_editor(&template, &file, line).map_err(|error| error.to_string())
}

#[tauri::command]
pub fn default_editor_template() -> String {
    editor::default_editor_template()
}

#[derive(serde::Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DefaultPaths {
    pub repo_path: String,
    pub build_dir_path: String,
    pub idf_path: String,
    pub esptool_python_path: String,
}

/// Guesses for first start: the repo is two levels above `pc_tool/`.
#[tauri::command]
pub fn default_paths() -> DefaultPaths {
    let current = std::env::current_dir().unwrap_or_default();
    let repo = [current.clone(), current.join(".."), current.join("../..")]
        .into_iter()
        .find(|candidate| {
            candidate.join("partitions.csv").exists() && candidate.join("CMakeLists.txt").exists()
        })
        .and_then(|path| path.canonicalize().ok())
        .unwrap_or(current);
    let home = std::env::var("HOME").unwrap_or_default();
    let idf = std::env::var("IDF_PATH")
        .ok()
        .filter(|path| !path.is_empty())
        .unwrap_or_else(|| format!("{home}/esp/esp-idf-v5.5.1"));
    DefaultPaths {
        build_dir_path: repo.join("build").to_string_lossy().to_string(),
        repo_path: repo.to_string_lossy().to_string(),
        idf_path: idf,
        esptool_python_path: bench_core::flash::find_esptool_python(None)
            .map(|path| path.to_string_lossy().to_string())
            .unwrap_or_default(),
    }
}
