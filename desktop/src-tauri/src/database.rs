use std::path::Path;
use std::time::Duration;

use rusqlite::{params, Connection, OpenFlags, OptionalExtension};
use serde::{Deserialize, Serialize};
use thiserror::Error;

// --- API types (serialized to the Vue frontend) ---

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DashboardSummary {
    pub crashes_today: i64,
    pub unique_groups: i64,
    pub total_occurrences: i64,
    pub affected_versions: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FrequentCrash {
    pub id: i64,
    pub signal: i32,
    pub signal_name: String,
    pub top_function: String,
    pub application: String,
    pub occurrence_count: i64,
    pub last_seen: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RecentActivity {
    pub occurrence_id: i64,
    pub group_id: i64,
    pub signal: i32,
    pub signal_name: String,
    pub top_function: String,
    pub application: String,
    pub imported_at: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TrendPoint {
    pub day: String,
    pub count: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DashboardData {
    pub summary: DashboardSummary,
    pub frequent_crashes: Vec<FrequentCrash>,
    pub recent_activity: Vec<RecentActivity>,
    pub trend: Vec<TrendPoint>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct CrashGroupFilters {
    pub application: Option<String>,
    pub signal: Option<i32>,
    pub search: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CrashGroupSummary {
    pub id: i64,
    pub signal: i32,
    pub signal_name: String,
    pub top_function: String,
    pub top_module: Option<String>,
    pub application: String,
    pub versions: Vec<String>,
    pub occurrence_count: i64,
    pub first_seen: i64,
    pub last_seen: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VersionCount {
    pub version: String,
    pub count: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CrashOccurrenceSummary {
    pub id: i64,
    pub imported_at: i64,
    pub crash_time_sec: Option<i64>,
    pub app_version: Option<String>,
    pub pid: Option<i64>,
    pub tid: Option<i64>,
    pub fault_addr: Option<i64>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CrashGroupDetail {
    pub id: i64,
    pub signal: i32,
    pub signal_name: String,
    pub top_function: String,
    pub top_module: Option<String>,
    pub application: String,
    pub fingerprint: String,
    pub occurrence_count: i64,
    pub first_seen: i64,
    pub last_seen: i64,
    pub versions: Vec<VersionCount>,
    pub occurrences: Vec<CrashOccurrenceSummary>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StackFrame {
    pub frame_index: i32,
    pub raw_address: i64,
    pub normalized_address: Option<i64>,
    pub module: Option<String>,
    pub function_name: String,
    pub source_file: Option<String>,
    pub source_line: Option<i32>,
    pub symbol_status: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct RegisterSet {
    pub rip: Option<i64>,
    pub rsp: Option<i64>,
    pub rbp: Option<i64>,
    pub rax: Option<i64>,
    pub rbx: Option<i64>,
    pub rcx: Option<i64>,
    pub rdx: Option<i64>,
    pub rsi: Option<i64>,
    pub rdi: Option<i64>,
    pub r8: Option<i64>,
    pub r9: Option<i64>,
    pub r10: Option<i64>,
    pub r11: Option<i64>,
    pub r12: Option<i64>,
    pub r13: Option<i64>,
    pub r14: Option<i64>,
    pub r15: Option<i64>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct OccurrenceDetail {
    pub id: i64,
    pub group_id: i64,
    pub imported_at: i64,
    pub crash_time_sec: Option<i64>,
    pub app_version: Option<String>,
    pub pid: Option<i64>,
    pub tid: Option<i64>,
    pub fault_addr: Option<i64>,
    pub executable_path: Option<String>,
    pub executable_base: Option<i64>,
    pub registers: RegisterSet,
    pub frames: Vec<StackFrame>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ApplicationSummary {
    pub name: String,
    pub group_count: i64,
    pub occurrence_count: i64,
    pub last_crash: Option<i64>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AppSettings {
    pub crash_directory: String,
    pub auto_import: bool,
    pub processor_path: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StorageStatus {
    pub crash_directory: String,
    pub database_path: String,
    pub pending_reports: usize,
    pub processed_reports: usize,
    pub database_exists: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct ImportStats {
    pub scanned: usize,
    pub imported: usize,
    pub rejected: usize,
    pub duplicate: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct WatcherStatus {
    pub watching: bool,
    pub auto_import: bool,
    pub last_import_at: Option<i64>,
    pub last_import_stats: Option<ImportStats>,
    pub last_error: Option<String>,
}

fn signal_name(signal: i32) -> &'static str {
    match signal {
        11 => "SIGSEGV",
        6 => "SIGABRT",
        8 => "SIGFPE",
        4 => "SIGILL",
        _ => "UNKNOWN",
    }
}

// --- SQLite access ---

#[derive(Debug, Error)]
pub enum DbError {
    #[error("sqlite error: {0}")]
    Sqlite(#[from] rusqlite::Error),
    #[error("database not found at {0}")]
    NotFound(String),
    #[error("group not found")]
    GroupNotFound,
    #[error("occurrence not found")]
    OccurrenceNotFound,
}

pub fn empty_dashboard() -> DashboardData {
    DashboardData {
        summary: DashboardSummary {
            crashes_today: 0,
            unique_groups: 0,
            total_occurrences: 0,
            affected_versions: 0,
        },
        frequent_crashes: Vec::new(),
        recent_activity: Vec::new(),
        trend: Vec::new(),
    }
}

pub fn open_db(path: &Path) -> Result<Connection, DbError> {
    if !path.exists() {
        return Err(DbError::NotFound(path.display().to_string()));
    }
    let conn = Connection::open_with_flags(
        path,
        OpenFlags::SQLITE_OPEN_READ_ONLY | OpenFlags::SQLITE_OPEN_NO_MUTEX,
    )?;
    conn.busy_timeout(Duration::from_secs(5))?;
    Ok(conn)
}

pub fn database_path(crash_directory: &Path) -> std::path::PathBuf {
    crash_directory.join("crashvault.db")
}

pub fn get_dashboard(conn: &Connection) -> Result<DashboardData, DbError> {
    let summary = DashboardSummary {
        crashes_today: conn.query_row(
            "SELECT COUNT(*) FROM crash_occurrences
             WHERE date(imported_at, 'unixepoch', 'localtime') = date('now', 'localtime')",
            [],
            |row| row.get(0),
        )?,
        unique_groups: conn.query_row("SELECT COUNT(*) FROM crash_groups", [], |row| row.get(0))?,
        total_occurrences: conn.query_row("SELECT COUNT(*) FROM crash_occurrences", [], |row| {
            row.get(0)
        })?,
        affected_versions: conn.query_row(
            "SELECT COUNT(DISTINCT app_version) FROM crash_occurrences
             WHERE app_version IS NOT NULL AND app_version != ''",
            [],
            |row| row.get(0),
        )?,
    };

    let mut frequent_stmt = conn.prepare(
        "SELECT g.id, g.signal, g.top_function, a.name, g.occurrence_count, g.last_seen
         FROM crash_groups g
         JOIN applications a ON a.id = g.application_id
         ORDER BY g.occurrence_count DESC, g.last_seen DESC
         LIMIT 10",
    )?;
    let frequent_crashes = frequent_stmt
        .query_map([], |row| {
            let signal: i32 = row.get(1)?;
            Ok(FrequentCrash {
                id: row.get(0)?,
                signal,
                signal_name: signal_name(signal).to_string(),
                top_function: row
                    .get::<_, Option<String>>(2)?
                    .unwrap_or_else(|| "??".into()),
                application: row.get(3)?,
                occurrence_count: row.get(4)?,
                last_seen: row.get(5)?,
            })
        })?
        .collect::<Result<Vec<_>, _>>()?;

    let mut recent_stmt = conn.prepare(
        "SELECT o.id, o.group_id, g.signal, g.top_function, a.name, o.imported_at
         FROM crash_occurrences o
         JOIN crash_groups g ON g.id = o.group_id
         JOIN applications a ON a.id = g.application_id
         ORDER BY o.imported_at DESC
         LIMIT 20",
    )?;
    let recent_activity = recent_stmt
        .query_map([], |row| {
            let signal: i32 = row.get(2)?;
            Ok(RecentActivity {
                occurrence_id: row.get(0)?,
                group_id: row.get(1)?,
                signal,
                signal_name: signal_name(signal).to_string(),
                top_function: row
                    .get::<_, Option<String>>(3)?
                    .unwrap_or_else(|| "??".into()),
                application: row.get(4)?,
                imported_at: row.get(5)?,
            })
        })?
        .collect::<Result<Vec<_>, _>>()?;

    let mut trend_stmt = conn.prepare(
        "SELECT date(imported_at, 'unixepoch') AS day, COUNT(*) AS count
         FROM crash_occurrences
         WHERE imported_at >= strftime('%s', 'now', '-14 days')
         GROUP BY day
         ORDER BY day ASC",
    )?;
    let trend = trend_stmt
        .query_map([], |row| {
            Ok(TrendPoint {
                day: row.get(0)?,
                count: row.get(1)?,
            })
        })?
        .collect::<Result<Vec<_>, _>>()?;

    Ok(DashboardData {
        summary,
        frequent_crashes,
        recent_activity,
        trend,
    })
}

pub fn get_crash_groups(
    conn: &Connection,
    filters: &CrashGroupFilters,
) -> Result<Vec<CrashGroupSummary>, DbError> {
    let search = filters.search.as_ref().map(|s| format!("%{s}%"));

    let mut stmt = conn.prepare(
        "SELECT g.id, g.signal, g.top_function, g.top_module, a.name, g.occurrence_count,
                g.first_seen, g.last_seen,
                (SELECT GROUP_CONCAT(DISTINCT o.app_version)
                 FROM crash_occurrences o
                 WHERE o.group_id = g.id AND o.app_version IS NOT NULL AND o.app_version != '')
         FROM crash_groups g
         JOIN applications a ON a.id = g.application_id
         WHERE (?1 IS NULL OR a.name = ?1)
           AND (?2 IS NULL OR g.signal = ?2)
           AND (?3 IS NULL OR g.top_function LIKE ?3 OR a.name LIKE ?3)
         ORDER BY g.last_seen DESC, g.id DESC",
    )?;

    let rows = stmt.query_map(
        params![filters.application, filters.signal, search],
        |row| {
            let signal: i32 = row.get(1)?;
            let versions_csv: Option<String> = row.get(8)?;
            let versions = versions_csv
                .map(|csv| csv.split(',').map(|s| s.to_string()).collect())
                .unwrap_or_default();
            Ok(CrashGroupSummary {
                id: row.get(0)?,
                signal,
                signal_name: signal_name(signal).to_string(),
                top_function: row
                    .get::<_, Option<String>>(2)?
                    .unwrap_or_else(|| "??".into()),
                top_module: row.get(3)?,
                application: row.get(4)?,
                versions,
                occurrence_count: row.get(5)?,
                first_seen: row.get(6)?,
                last_seen: row.get(7)?,
            })
        },
    )?;

    rows.collect::<Result<Vec<_>, _>>().map_err(DbError::from)
}

pub fn get_crash_group_detail(conn: &Connection, id: i64) -> Result<CrashGroupDetail, DbError> {
    let detail = conn
        .query_row(
            "SELECT g.id, g.signal, g.top_function, g.top_module, a.name, g.fingerprint,
                    g.occurrence_count, g.first_seen, g.last_seen
             FROM crash_groups g
             JOIN applications a ON a.id = g.application_id
             WHERE g.id = ?1",
            [id],
            |row| {
                let signal: i32 = row.get(1)?;
                Ok(CrashGroupDetail {
                    id: row.get(0)?,
                    signal,
                    signal_name: signal_name(signal).to_string(),
                    top_function: row
                        .get::<_, Option<String>>(2)?
                        .unwrap_or_else(|| "??".into()),
                    top_module: row.get(3)?,
                    application: row.get(4)?,
                    fingerprint: row.get(5)?,
                    occurrence_count: row.get(6)?,
                    first_seen: row.get(7)?,
                    last_seen: row.get(8)?,
                    versions: Vec::new(),
                    occurrences: Vec::new(),
                })
            },
        )
        .optional()?
        .ok_or(DbError::GroupNotFound)?;

    let mut versions_stmt = conn.prepare(
        "SELECT app_version, COUNT(*) FROM crash_occurrences
         WHERE group_id = ?1 AND app_version IS NOT NULL AND app_version != ''
         GROUP BY app_version ORDER BY COUNT(*) DESC",
    )?;
    let versions = versions_stmt
        .query_map([id], |row| {
            Ok(VersionCount {
                version: row.get(0)?,
                count: row.get(1)?,
            })
        })?
        .collect::<Result<Vec<_>, _>>()?;

    let mut occ_stmt = conn.prepare(
        "SELECT id, imported_at, crash_time_sec, app_version, pid, tid, fault_addr
         FROM crash_occurrences WHERE group_id = ?1 ORDER BY imported_at DESC",
    )?;
    let occurrences = occ_stmt
        .query_map([id], |row| {
            Ok(CrashOccurrenceSummary {
                id: row.get(0)?,
                imported_at: row.get(1)?,
                crash_time_sec: row.get(2)?,
                app_version: row.get(3)?,
                pid: row.get(4)?,
                tid: row.get(5)?,
                fault_addr: row.get(6)?,
            })
        })?
        .collect::<Result<Vec<_>, _>>()?;

    Ok(CrashGroupDetail {
        versions,
        occurrences,
        ..detail
    })
}

pub fn get_occurrence_detail(conn: &Connection, id: i64) -> Result<OccurrenceDetail, DbError> {
    let occurrence = conn
        .query_row(
            "SELECT id, group_id, imported_at, crash_time_sec, app_version, pid, tid, fault_addr,
                    executable_path, executable_base,
                    rip, rsp, rbp, rax, rbx, rcx, rdx, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15
             FROM crash_occurrences WHERE id = ?1",
            [id],
            |row| {
                Ok(OccurrenceDetail {
                    id: row.get(0)?,
                    group_id: row.get(1)?,
                    imported_at: row.get(2)?,
                    crash_time_sec: row.get(3)?,
                    app_version: row.get(4)?,
                    pid: row.get(5)?,
                    tid: row.get(6)?,
                    fault_addr: row.get(7)?,
                    executable_path: row.get(8)?,
                    executable_base: row.get(9)?,
                    registers: RegisterSet {
                        rip: row.get(10)?,
                        rsp: row.get(11)?,
                        rbp: row.get(12)?,
                        rax: row.get(13)?,
                        rbx: row.get(14)?,
                        rcx: row.get(15)?,
                        rdx: row.get(16)?,
                        rsi: row.get(17)?,
                        rdi: row.get(18)?,
                        r8: row.get(19)?,
                        r9: row.get(20)?,
                        r10: row.get(21)?,
                        r11: row.get(22)?,
                        r12: row.get(23)?,
                        r13: row.get(24)?,
                        r14: row.get(25)?,
                        r15: row.get(26)?,
                    },
                    frames: Vec::new(),
                })
            },
        )
        .optional()?
        .ok_or(DbError::OccurrenceNotFound)?;

    let mut frame_stmt = conn.prepare(
        "SELECT frame_index, raw_address, normalized_address, module, function_name,
                source_file, source_line, symbol_status
         FROM frames WHERE occurrence_id = ?1 ORDER BY frame_index ASC",
    )?;
    let frames = frame_stmt
        .query_map([id], |row| {
            Ok(StackFrame {
                frame_index: row.get(0)?,
                raw_address: row.get(1)?,
                normalized_address: row.get(2)?,
                module: row.get(3)?,
                function_name: row
                    .get::<_, Option<String>>(4)?
                    .unwrap_or_else(|| "??".into()),
                source_file: row.get(5)?,
                source_line: row.get(6)?,
                symbol_status: row.get(7)?,
            })
        })?
        .collect::<Result<Vec<_>, _>>()?;

    Ok(OccurrenceDetail {
        frames,
        ..occurrence
    })
}

pub fn get_applications(conn: &Connection) -> Result<Vec<ApplicationSummary>, DbError> {
    let mut stmt = conn.prepare(
        "SELECT a.name,
                COUNT(DISTINCT g.id) AS group_count,
                COALESCE(SUM(g.occurrence_count), 0) AS occurrence_count,
                MAX(g.last_seen) AS last_crash
         FROM applications a
         LEFT JOIN crash_groups g ON g.application_id = a.id
         GROUP BY a.id
         ORDER BY last_crash DESC, a.name ASC",
    )?;
    let rows = stmt.query_map([], |row| {
        Ok(ApplicationSummary {
            name: row.get(0)?,
            group_count: row.get(1)?,
            occurrence_count: row.get(2)?,
            last_crash: row.get(3)?,
        })
    })?;
    rows.collect::<Result<Vec<_>, _>>().map_err(DbError::from)
}

pub fn count_raw_reports(dir: &Path) -> usize {
    count_matching_files(dir, "crash_", ".raw")
}

pub fn count_processed_reports(crash_directory: &Path) -> usize {
    count_matching_files(&crash_directory.join("processed"), "crash_", ".raw")
}

fn count_matching_files(dir: &Path, prefix: &str, suffix: &str) -> usize {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return 0;
    };
    entries
        .filter_map(Result::ok)
        .filter(|entry| {
            let name = entry.file_name();
            let name = name.to_string_lossy();
            name.starts_with(prefix) && name.ends_with(suffix)
        })
        .count()
}

