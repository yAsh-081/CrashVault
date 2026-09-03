use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::Arc;
use std::thread;
use std::time::Duration;

use parking_lot::Mutex;
use tauri::{AppHandle, Emitter, Manager};
use thiserror::Error;

use crate::database::{count_raw_reports, AppSettings, ImportStats, WatcherStatus};
use crate::settings;

#[derive(Debug, Error)]
pub enum ImportError {
    #[error("processor executable not found")]
    ProcessorNotFound,
    #[error("failed to run processor: {0}")]
    Run(String),
    #[error("processor exited with status {0}")]
    Exit(i32),
}

struct WatcherState {
    last_import_at: Option<i64>,
    last_import_stats: Option<ImportStats>,
    last_error: Option<String>,
    watching: bool,
}

pub struct ImportService {
    app_data_dir: PathBuf,
    state: Arc<Mutex<WatcherState>>,
    import_lock: Arc<Mutex<()>>,
}

impl ImportService {
    pub fn new(app_data_dir: PathBuf) -> Self {
        Self {
            app_data_dir,
            state: Arc::new(Mutex::new(WatcherState {
                last_import_at: None,
                last_import_stats: None,
                last_error: None,
                watching: false,
            })),
            import_lock: Arc::new(Mutex::new(())),
        }
    }

    pub fn current_settings(&self) -> Result<AppSettings, String> {
        settings::load_settings(&self.app_data_dir).map_err(|e| e.to_string())
    }

    pub fn save_settings(&self, settings: &AppSettings) -> Result<(), String> {
        settings::save_settings(&self.app_data_dir, settings).map_err(|e| e.to_string())
    }

    pub fn watcher_status(&self) -> WatcherStatus {
        let settings = self
            .current_settings()
            .unwrap_or_else(|_| settings::default_settings());
        let state = self.state.lock();
        WatcherStatus {
            watching: state.watching && settings.auto_import,
            auto_import: settings.auto_import,
            last_import_at: state.last_import_at,
            last_import_stats: state.last_import_stats.clone(),
            last_error: state.last_error.clone(),
        }
    }

    pub fn process_pending(&self, app: &AppHandle) -> Result<ImportStats, String> {
        let Some(_guard) = self.import_lock.try_lock() else {
            let state = self.state.lock();
            return Ok(state.last_import_stats.clone().unwrap_or_default());
        };

        let settings = self.current_settings()?;
        let crash_dir = PathBuf::from(&settings.crash_directory);
        std::fs::create_dir_all(&crash_dir).map_err(|e| e.to_string())?;

        let stats = run_processor(Some(app), &crash_dir, settings.processor_path.as_deref())
            .map_err(|e| e.to_string())?;

        {
            let mut state = self.state.lock();
            state.last_import_at = Some(chrono::Utc::now().timestamp());
            state.last_import_stats = Some(stats.clone());
            state.last_error = None;
        }

        let _ = app.emit("import-complete", &stats);
        Ok(stats)
    }

    pub fn start(&self, app: AppHandle) {
        self.state.lock().watching = true;

        if let Err(err) = self.process_pending(&app) {
            self.state.lock().last_error = Some(err);
        }

        let service = Arc::new(ImportService {
            app_data_dir: self.app_data_dir.clone(),
            state: self.state.clone(),
            import_lock: self.import_lock.clone(),
        });

        thread::spawn(move || loop {
            thread::sleep(Duration::from_secs(5));
            let settings = match service.current_settings() {
                Ok(s) => s,
                Err(_) => continue,
            };
            if !settings.auto_import {
                continue;
            }
            let crash_dir = PathBuf::from(settings.crash_directory);
            if crash_dir.exists() && count_raw_reports(&crash_dir) > 0 {
                if let Err(err) = service.process_pending(&app) {
                    service.state.lock().last_error = Some(err);
                }
            }
        });
    }
}

fn resolve_processor_path(
    app: Option<&AppHandle>,
    settings_path: Option<&str>,
) -> Result<PathBuf, ImportError> {
    if let Some(path) = settings_path {
        if !path.is_empty() {
            let candidate = PathBuf::from(path);
            if candidate.is_file() {
                return Ok(candidate);
            }
        }
    }

    if let Ok(path) = std::env::var("CRASHVAULT_PROCESSOR_BIN") {
        if !path.is_empty() {
            let candidate = PathBuf::from(&path);
            if candidate.is_file() {
                return Ok(candidate);
            }
        }
    }

    if let Some(app) = app {
        if let Ok(path) = app.path().resolve(
            "binaries/crashvault-process-x86_64-unknown-linux-gnu",
            tauri::path::BaseDirectory::Resource,
        ) {
            if path.is_file() {
                return Ok(path);
            }
        }
    }

    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            for name in [
                "crashvault-process",
                "crashvault-process-x86_64-unknown-linux-gnu",
            ] {
                let candidate = dir.join(name);
                if candidate.is_file() {
                    return Ok(candidate);
                }
            }
        }
    }

    for candidate in [
        "/usr/local/bin/crashvault-process",
        "/usr/bin/crashvault-process",
    ] {
        let path = PathBuf::from(candidate);
        if path.is_file() {
            return Ok(path);
        }
    }

    which_processor_in_path()
}

fn which_processor_in_path() -> Result<PathBuf, ImportError> {
    let output = Command::new("which")
        .arg("crashvault-process")
        .output()
        .map_err(|e| ImportError::Run(e.to_string()))?;
    if !output.status.success() {
        return Err(ImportError::ProcessorNotFound);
    }
    let path = String::from_utf8_lossy(&output.stdout).trim().to_string();
    if path.is_empty() {
        return Err(ImportError::ProcessorNotFound);
    }
    Ok(PathBuf::from(path))
}

fn run_processor(
    app: Option<&AppHandle>,
    crash_directory: &Path,
    settings_processor: Option<&str>,
) -> Result<ImportStats, ImportError> {
    let processor = resolve_processor_path(app, settings_processor)?;
    let output = Command::new(&processor)
        .env("CRASHVAULT_HOME", crash_directory)
        .output()
        .map_err(|e| ImportError::Run(e.to_string()))?;

    if !output.status.success() {
        let code = output.status.code().unwrap_or(-1);
        let stderr = String::from_utf8_lossy(&output.stderr);
        if !stderr.is_empty() {
            return Err(ImportError::Run(stderr.to_string()));
        }
        return Err(ImportError::Exit(code));
    }

    Ok(parse_import_stats(&String::from_utf8_lossy(&output.stdout)))
}

fn parse_import_stats(stdout: &str) -> ImportStats {
    let mut stats = ImportStats::default();
    for line in stdout.lines() {
        let line = line.trim();
        if let Some(rest) = line.strip_prefix("Scanned:") {
            stats.scanned = rest.trim().parse().unwrap_or(0);
        } else if let Some(rest) = line.strip_prefix("Imported:") {
            stats.imported = rest.trim().parse().unwrap_or(0);
        } else if let Some(rest) = line.strip_prefix("Rejected:") {
            stats.rejected = rest.trim().parse().unwrap_or(0);
        } else if let Some(rest) = line.strip_prefix("Duplicate:") {
            stats.duplicate = rest.trim().parse().unwrap_or(0);
        }
    }
    stats
}
