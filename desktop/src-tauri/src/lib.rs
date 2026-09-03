mod crash_service;
mod database;
mod settings;

use std::path::PathBuf;
use std::sync::Arc;

use tauri::Manager;

use crate::crash_service::ImportService;
use crate::database::{
    count_processed_reports, count_raw_reports, database_path, empty_dashboard, open_db,
    AppSettings, ApplicationSummary, CrashGroupDetail, CrashGroupFilters, CrashGroupSummary,
    DashboardData, ImportStats, OccurrenceDetail, StorageStatus, WatcherStatus,
};

struct AppState {
    import_service: Arc<ImportService>,
}

#[tauri::command]
fn get_dashboard(state: tauri::State<AppState>) -> Result<DashboardData, String> {
    let settings = state.import_service.current_settings()?;
    let db_path = database_path(PathBuf::from(settings.crash_directory).as_path());
    let conn = match open_db(&db_path) {
        Ok(conn) => conn,
        Err(crate::database::DbError::NotFound(_)) => return Ok(empty_dashboard()),
        Err(err) => return Err(err.to_string()),
    };
    crate::database::get_dashboard(&conn).map_err(|e| e.to_string())
}

fn with_db<T, F>(state: &AppState, f: F) -> Result<T, String>
where
    F: FnOnce(&rusqlite::Connection) -> Result<T, crate::database::DbError>,
{
    let settings = state.import_service.current_settings()?;
    let db_path = database_path(PathBuf::from(settings.crash_directory).as_path());
    let conn = open_db(&db_path).map_err(|e| e.to_string())?;
    f(&conn).map_err(|e| e.to_string())
}

fn with_db_or<T, F>(state: &AppState, empty: T, f: F) -> Result<T, String>
where
    F: FnOnce(&rusqlite::Connection) -> Result<T, crate::database::DbError>,
{
    let settings = state.import_service.current_settings()?;
    let db_path = database_path(PathBuf::from(settings.crash_directory).as_path());
    match open_db(&db_path) {
        Ok(conn) => f(&conn).map_err(|e| e.to_string()),
        Err(crate::database::DbError::NotFound(_)) => Ok(empty),
        Err(err) => Err(err.to_string()),
    }
}

#[tauri::command]
fn get_crash_groups(
    filters: CrashGroupFilters,
    state: tauri::State<AppState>,
) -> Result<Vec<CrashGroupSummary>, String> {
    with_db_or(&state, Vec::new(), |conn| {
        crate::database::get_crash_groups(conn, &filters)
    })
}

#[tauri::command]
fn get_crash_group_detail(
    id: i64,
    state: tauri::State<AppState>,
) -> Result<CrashGroupDetail, String> {
    with_db(&state, |conn| {
        crate::database::get_crash_group_detail(conn, id)
    })
}

#[tauri::command]
fn get_occurrence_detail(
    id: i64,
    state: tauri::State<AppState>,
) -> Result<OccurrenceDetail, String> {
    with_db(&state, |conn| {
        crate::database::get_occurrence_detail(conn, id)
    })
}

#[tauri::command]
fn get_applications(state: tauri::State<AppState>) -> Result<Vec<ApplicationSummary>, String> {
    with_db_or(&state, Vec::new(), crate::database::get_applications)
}

#[tauri::command]
fn get_settings(state: tauri::State<AppState>) -> Result<AppSettings, String> {
    state.import_service.current_settings()
}

#[tauri::command]
fn set_settings(settings: AppSettings, state: tauri::State<AppState>) -> Result<(), String> {
    state.import_service.save_settings(&settings)
}

#[tauri::command]
fn get_storage_status(state: tauri::State<AppState>) -> Result<StorageStatus, String> {
    let settings = state.import_service.current_settings()?;
    let crash_dir = PathBuf::from(&settings.crash_directory);
    let db_path = database_path(&crash_dir);
    Ok(StorageStatus {
        crash_directory: settings.crash_directory.clone(),
        database_path: db_path.display().to_string(),
        pending_reports: count_raw_reports(&crash_dir),
        processed_reports: count_processed_reports(&crash_dir),
        database_exists: db_path.exists(),
    })
}

#[tauri::command]
fn process_pending_reports(
    app: tauri::AppHandle,
    state: tauri::State<AppState>,
) -> Result<ImportStats, String> {
    state.import_service.process_pending(&app)
}

#[tauri::command]
fn get_watcher_status(state: tauri::State<AppState>) -> Result<WatcherStatus, String> {
    Ok(state.import_service.watcher_status())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .setup(|app| {
            if let Some(window) = app.get_webview_window("main") {
                use tauri::window::Color;
                let _ = window.set_background_color(Some(Color(18, 20, 24, 255)));
            }

            let app_data_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
            std::fs::create_dir_all(&app_data_dir).map_err(|e| e.to_string())?;

            let import_service = Arc::new(ImportService::new(app_data_dir));
            app.manage(AppState {
                import_service: import_service.clone(),
            });

            let handle = app.handle().clone();
            import_service.start(handle);
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            get_dashboard,
            get_crash_groups,
            get_crash_group_detail,
            get_occurrence_detail,
            get_applications,
            get_settings,
            set_settings,
            get_storage_status,
            process_pending_reports,
            get_watcher_status
        ])
        .run(tauri::generate_context!())
        .expect("error while running CrashVault");
}
