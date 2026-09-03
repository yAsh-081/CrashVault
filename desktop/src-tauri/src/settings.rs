use std::fs;
use std::path::{Path, PathBuf};

use crate::database::AppSettings;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum SettingsError {
    #[error("failed to read settings: {0}")]
    Read(String),
    #[error("failed to write settings: {0}")]
    Write(String),
}

pub fn default_crash_directory() -> PathBuf {
    if let Ok(home) = std::env::var("CRASHVAULT_HOME") {
        if !home.is_empty() {
            return PathBuf::from(home);
        }
    }

    if let Ok(home) = std::env::var("HOME") {
        return PathBuf::from(home).join(".crashvault");
    }

    PathBuf::from(".crashvault")
}

pub fn default_settings() -> AppSettings {
    AppSettings {
        crash_directory: default_crash_directory().to_string_lossy().into_owned(),
        auto_import: true,
        processor_path: None,
    }
}

pub fn settings_path(app_data_dir: &Path) -> PathBuf {
    app_data_dir.join("settings.json")
}

pub fn load_settings(app_data_dir: &Path) -> Result<AppSettings, SettingsError> {
    let path = settings_path(app_data_dir);
    if !path.exists() {
        return Ok(default_settings());
    }

    let data = fs::read_to_string(&path).map_err(|e| SettingsError::Read(e.to_string()))?;
    serde_json::from_str(&data).map_err(|e| SettingsError::Read(e.to_string()))
}

pub fn save_settings(app_data_dir: &Path, settings: &AppSettings) -> Result<(), SettingsError> {
    fs::create_dir_all(app_data_dir).map_err(|e| SettingsError::Write(e.to_string()))?;
    let path = settings_path(app_data_dir);
    let data =
        serde_json::to_string_pretty(settings).map_err(|e| SettingsError::Write(e.to_string()))?;
    fs::write(path, data).map_err(|e| SettingsError::Write(e.to_string()))
}
