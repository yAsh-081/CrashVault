#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "processor.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct CrashVaultProcessor {
    char home_dir[PATH_MAX];
    char db_path[PATH_MAX];
    char processed_dir[PATH_MAX];
    sqlite3 *db;
};

static const char CRASHVAULT_DB_SCHEMA[] =
    "PRAGMA foreign_keys = ON;"
    "CREATE TABLE IF NOT EXISTS applications ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  name TEXT NOT NULL UNIQUE"
    ");"
    "CREATE TABLE IF NOT EXISTS crash_groups ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  application_id INTEGER NOT NULL,"
    "  fingerprint TEXT NOT NULL,"
    "  signal INTEGER NOT NULL,"
    "  top_function TEXT,"
    "  top_module TEXT,"
    "  occurrence_count INTEGER NOT NULL DEFAULT 0,"
    "  first_seen INTEGER NOT NULL,"
    "  last_seen INTEGER NOT NULL,"
    "  UNIQUE(application_id, fingerprint),"
    "  FOREIGN KEY(application_id) REFERENCES applications(id)"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_crash_groups_app ON crash_groups(application_id);"
    "CREATE TABLE IF NOT EXISTS crash_occurrences ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  group_id INTEGER NOT NULL,"
    "  source_report_id TEXT NOT NULL UNIQUE,"
    "  raw_report_path TEXT,"
    "  app_version TEXT,"
    "  imported_at INTEGER NOT NULL,"
    "  crash_time_sec INTEGER,"
    "  crash_time_nsec INTEGER,"
    "  pid INTEGER,"
    "  tid INTEGER,"
    "  fault_addr INTEGER,"
    "  rip INTEGER,"
    "  rsp INTEGER,"
    "  rbp INTEGER,"
    "  rax INTEGER,"
    "  rbx INTEGER,"
    "  rcx INTEGER,"
    "  rdx INTEGER,"
    "  rsi INTEGER,"
    "  rdi INTEGER,"
    "  r8 INTEGER,"
    "  r9 INTEGER,"
    "  r10 INTEGER,"
    "  r11 INTEGER,"
    "  r12 INTEGER,"
    "  r13 INTEGER,"
    "  r14 INTEGER,"
    "  r15 INTEGER,"
    "  executable_path TEXT,"
    "  executable_base INTEGER,"
    "  FOREIGN KEY(group_id) REFERENCES crash_groups(id)"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_occurrences_group ON crash_occurrences(group_id);"
    "CREATE TABLE IF NOT EXISTS frames ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  occurrence_id INTEGER NOT NULL,"
    "  frame_index INTEGER NOT NULL,"
    "  raw_address INTEGER NOT NULL,"
    "  normalized_address INTEGER,"
    "  module TEXT,"
    "  function_name TEXT,"
    "  source_file TEXT,"
    "  source_line INTEGER,"
    "  symbol_status TEXT NOT NULL,"
    "  FOREIGN KEY(occurrence_id) REFERENCES crash_occurrences(id)"
    ");"
    "CREATE INDEX IF NOT EXISTS idx_frames_occurrence ON frames(occurrence_id);";

static int crashvault_join_path(char *dest, size_t dest_size, const char *a, const char *b)
{
    size_t alen = strlen(a);
    size_t blen = strlen(b);
    int needs_sep = (alen > 0U && a[alen - 1U] != '/');

    if (alen + blen + (size_t)needs_sep + 1U > dest_size) {
        return -1;
    }

    memcpy(dest, a, alen);
    size_t offset = alen;
    if (needs_sep) {
        dest[offset++] = '/';
    }
    memcpy(dest + offset, b, blen + 1U);
    return 0;
}

static int crashvault_mkdir_p(const char *path, mode_t mode)
{
    char buf[PATH_MAX];
    size_t len;
    size_t i;

    if (path == NULL || path[0] == '\0' || strlen(path) >= sizeof(buf)) {
        return -1;
    }

    memcpy(buf, path, strlen(path) + 1U);
    len = strlen(buf);
    if (len > 1U && buf[len - 1U] == '/') {
        buf[len - 1U] = '\0';
    }

    for (i = 1U; buf[i] != '\0'; ++i) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            if (mkdir(buf, mode) != 0 && errno != EEXIST) {
                return -1;
            }
            buf[i] = '/';
        }
    }

    if (mkdir(buf, mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static void crashvault_sanitize_fixed_string(char *field, size_t field_size)
{
    size_t i;

    if (field_size == 0U) {
        return;
    }

    field[field_size - 1U] = '\0';
    for (i = 0U; i < field_size - 1U; ++i) {
        if (field[i] == '\0') {
            break;
        }
        if (field[i] < 32 || field[i] == 127) {
            field[i] = '?';
        }
    }
}

static uint64_t crashvault_fnv1a64(const uint8_t *data, size_t len)
{
    uint64_t hash = 14695981039346656037ULL;
    size_t i;

    for (i = 0U; i < len; ++i) {
        hash ^= (uint64_t)data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static int crashvault_hex_u64(uint64_t value, char *out, size_t out_size)
{
    if (out_size < 17U) {
        return -1;
    }
    (void)snprintf(out, out_size, "%016llx", (unsigned long long)value);
    return 0;
}

int crashvault_compute_source_id(const void *data, size_t len, char *out, size_t out_size)
{
    uint64_t hash = crashvault_fnv1a64((const uint8_t *)data, len);
    return crashvault_hex_u64(hash, out, out_size);
}

uint64_t crashvault_normalize_address(uint64_t runtime_address, uint64_t executable_base,
                                      int executable_valid)
{
    if (executable_valid && executable_base != 0U && runtime_address >= executable_base) {
        return runtime_address - executable_base;
    }
    return runtime_address;
}

const char *crashvault_parse_status_string(CrashVaultParseStatus status)
{
    switch (status) {
    case CRASHVAULT_PARSE_OK:
        return "ok";
    case CRASHVAULT_PARSE_IO_ERROR:
        return "io_error";
    case CRASHVAULT_PARSE_TRUNCATED:
        return "truncated";
    case CRASHVAULT_PARSE_OVERSIZED:
        return "oversized";
    case CRASHVAULT_PARSE_BAD_MAGIC:
        return "bad_magic";
    case CRASHVAULT_PARSE_UNSUPPORTED_VERSION:
        return "unsupported_version";
    case CRASHVAULT_PARSE_INVALID_FIELDS:
        return "invalid_fields";
    default:
        return "unknown";
    }
}

static CrashVaultParseStatus crashvault_validate_parsed_report(CrashVaultParsedReport *parsed)
{
    const CrashVaultRawReport *report = &parsed->report;

    if (!crashvault_raw_report_magic_valid(report->magic)) {
        return CRASHVAULT_PARSE_BAD_MAGIC;
    }
    if (!crashvault_raw_report_version_supported(report->format_version)) {
        return CRASHVAULT_PARSE_UNSUPPORTED_VERSION;
    }

    crashvault_sanitize_fixed_string(parsed->report.app_name, sizeof(parsed->report.app_name));
    crashvault_sanitize_fixed_string(parsed->report.app_version, sizeof(parsed->report.app_version));
    crashvault_sanitize_fixed_string(parsed->report.executable_path,
                                     sizeof(parsed->report.executable_path));

    if (parsed->report.app_name[0] == '\0') {
        return CRASHVAULT_PARSE_INVALID_FIELDS;
    }

    if (parsed->report.frame_count > CRASHVAULT_MAX_FRAMES) {
        return CRASHVAULT_PARSE_INVALID_FIELDS;
    }

    if ((parsed->report.flags & CRASHVAULT_FLAG_FRAMES_VALID) != 0U &&
        parsed->report.frame_count == 0U) {
        return CRASHVAULT_PARSE_INVALID_FIELDS;
    }

    return CRASHVAULT_PARSE_OK;
}

CrashVaultParseStatus crashvault_parse_raw_file(const char *path, CrashVaultParsedReport *out)
{
    int fd;
    struct stat st;
    uint8_t header[16];
    ssize_t n;
    size_t expected_size;
    CrashVaultParseStatus status;

    if (path == NULL || out == NULL) {
        return CRASHVAULT_PARSE_IO_ERROR;
    }

    memset(out, 0, sizeof(*out));
    strncpy(out->source_path, path, sizeof(out->source_path) - 1U);

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return CRASHVAULT_PARSE_IO_ERROR;
    }

    if (fstat(fd, &st) != 0) {
        close(fd);
        return CRASHVAULT_PARSE_IO_ERROR;
    }

    n = read(fd, header, sizeof(header));
    if (n < 8) {
        close(fd);
        return CRASHVAULT_PARSE_TRUNCATED;
    }

    {
        uint32_t magic;
        uint32_t version;
        memcpy(&magic, header, sizeof(magic));
        memcpy(&version, header + 4, sizeof(version));

        if (!crashvault_raw_report_magic_valid(magic)) {
            close(fd);
            return CRASHVAULT_PARSE_BAD_MAGIC;
        }

        expected_size = crashvault_raw_report_expected_size(version);
        if (expected_size == 0U) {
            close(fd);
            return CRASHVAULT_PARSE_UNSUPPORTED_VERSION;
        }

        if ((size_t)st.st_size < expected_size) {
            close(fd);
            return CRASHVAULT_PARSE_TRUNCATED;
        }

        if ((size_t)st.st_size > expected_size + 64U) {
            close(fd);
            return CRASHVAULT_PARSE_OVERSIZED;
        }
    }

    if (lseek(fd, 0, SEEK_SET) < 0) {
        close(fd);
        return CRASHVAULT_PARSE_IO_ERROR;
    }

    memset(&out->report, 0, sizeof(out->report));
    n = read(fd, &out->report, expected_size);
    close(fd);

    if (n != (ssize_t)expected_size) {
        return CRASHVAULT_PARSE_TRUNCATED;
    }

    out->bytes_read = expected_size;
    status = crashvault_validate_parsed_report(out);
    if (status != CRASHVAULT_PARSE_OK) {
        return status;
    }

    if (crashvault_compute_source_id(&out->report, expected_size, out->source_id,
                                     sizeof(out->source_id)) != 0) {
        return CRASHVAULT_PARSE_INVALID_FIELDS;
    }

    return CRASHVAULT_PARSE_OK;
}

int crashvault_compute_fingerprint(const CrashVaultParsedReport *parsed,
                                   const CrashVaultSymbolResult *top_symbol, char *out,
                                   size_t out_size)
{
    char buffer[1024];
    const CrashVaultRawReport *report = &parsed->report;
    uint64_t hash;
    if (top_symbol != NULL && top_symbol->function[0] != '\0' &&
        strcmp(top_symbol->function, "??") != 0) {
        (void)snprintf(buffer, sizeof(buffer), "%s|%d|%s", report->app_name, report->signal_num,
                       top_symbol->function);
    } else if ((report->flags & CRASHVAULT_FLAG_EXECUTABLE_VALID) != 0U &&
               report->rip >= report->executable_base) {
        uint64_t rel = report->rip - report->executable_base;
        if (rel < 0x01000000UL) {
            (void)snprintf(buffer, sizeof(buffer), "%s|%d|exe|0x%llx", report->app_name,
                           report->signal_num, (unsigned long long)rel);
        } else {
            (void)snprintf(buffer, sizeof(buffer), "%s|%d|sigcode:%d", report->app_name,
                           report->signal_num, report->si_code);
        }
    } else {
        (void)snprintf(buffer, sizeof(buffer), "%s|%d|sigcode:%d", report->app_name,
                       report->signal_num, report->si_code);
    }

    hash = crashvault_fnv1a64((const uint8_t *)buffer, strlen(buffer));
    return crashvault_hex_u64(hash, out, out_size);
}

static const char *crashvault_signal_name(int signum, char *buf, size_t buf_size)
{
    switch (signum) {
    case SIGSEGV:
        return "SIGSEGV";
    case SIGABRT:
        return "SIGABRT";
    case SIGFPE:
        return "SIGFPE";
    case SIGILL:
        return "SIGILL";
    default:
        (void)snprintf(buf, buf_size, "SIG%d", signum);
        return buf;
    }
}

static int64_t crashvault_report_timestamp(const CrashVaultRawReport *report, int64_t fallback)
{
    if ((report->flags & CRASHVAULT_FLAG_TIMESTAMP_VALID) != 0U) {
        return (int64_t)report->crash_time_sec;
    }
    return fallback;
}

static int crashvault_db_exec(sqlite3 *db, const char *sql)
{
    char *errmsg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        if (errmsg != NULL) {
            fprintf(stderr, "sqlite error: %s\n", errmsg);
            sqlite3_free(errmsg);
        }
        return -1;
    }
    return 0;
}

static int crashvault_db_init_schema(sqlite3 *db)
{
    return crashvault_db_exec(db, CRASHVAULT_DB_SCHEMA);
}

static int crashvault_resolve_home(const CrashVaultProcessorConfig *config, char *home,
                                   size_t home_size)
{
    const char *override;

    if (config != NULL && config->home_dir != NULL && config->home_dir[0] != '\0') {
        return snprintf(home, home_size, "%s", config->home_dir) >= (int)home_size ? -1 : 0;
    }

    override = getenv("CRASHVAULT_HOME");
    if (override != NULL && override[0] != '\0') {
        return snprintf(home, home_size, "%s", override) >= (int)home_size ? -1 : 0;
    }

    {
        const char *user_home = getenv("HOME");
        if (user_home == NULL || user_home[0] == '\0') {
            return -1;
        }
        return crashvault_join_path(home, home_size, user_home, ".crashvault");
    }
}

CrashVaultProcessor *crashvault_processor_open(const CrashVaultProcessorConfig *config)
{
    CrashVaultProcessor *processor;
    int rc;

    processor = calloc(1, sizeof(*processor));
    if (processor == NULL) {
        return NULL;
    }

    if (crashvault_resolve_home(config, processor->home_dir, sizeof(processor->home_dir)) != 0) {
        free(processor);
        return NULL;
    }

    if (crashvault_join_path(processor->db_path, sizeof(processor->db_path), processor->home_dir,
                             "crashvault.db") != 0 ||
        crashvault_join_path(processor->processed_dir, sizeof(processor->processed_dir),
                             processor->home_dir, "processed") != 0) {
        free(processor);
        return NULL;
    }

    if (crashvault_mkdir_p(processor->home_dir, (mode_t)0700) != 0 ||
        crashvault_mkdir_p(processor->processed_dir, (mode_t)0700) != 0) {
        free(processor);
        return NULL;
    }

    rc = sqlite3_open(processor->db_path, &processor->db);
    if (rc != SQLITE_OK) {
        free(processor);
        return NULL;
    }

    if (crashvault_db_init_schema(processor->db) != 0) {
        sqlite3_close(processor->db);
        free(processor);
        return NULL;
    }

    sqlite3_busy_timeout(processor->db, 5000);
    (void)sqlite3_exec(processor->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);

    if (config != NULL && config->addr2line_path != NULL) {
        crashvault_symbolize_init(config->addr2line_path);
    } else {
        crashvault_symbolize_init_default();
    }

    return processor;
}

void crashvault_processor_close(CrashVaultProcessor *processor)
{
    if (processor == NULL) {
        return;
    }
    if (processor->db != NULL) {
        sqlite3_close(processor->db);
    }
    free(processor);
}

static int crashvault_source_exists(CrashVaultProcessor *processor, const char *source_id)
{
    sqlite3_stmt *stmt = NULL;
    int exists = 0;
    int rc;

    rc = sqlite3_prepare_v2(processor->db, "SELECT 1 FROM crash_occurrences WHERE source_report_id = ? LIMIT 1;",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return 0;
    }

    sqlite3_bind_text(stmt, 1, source_id, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = 1;
    }
    sqlite3_finalize(stmt);
    return exists;
}

static int crashvault_move_to_processed(CrashVaultProcessor *processor, const char *source_path,
                                        char *dest_path, size_t dest_size)
{
    const char *basename = strrchr(source_path, '/');
    if (basename == NULL) {
        basename = source_path;
    } else {
        ++basename;
    }

    if (snprintf(dest_path, dest_size, "%s/%s", processor->processed_dir, basename) >=
        (int)dest_size) {
        return -1;
    }

    if (rename(source_path, dest_path) == 0) {
        return 0;
    }

    if (errno != EXDEV) {
        return -1;
    }

    {
        int in_fd = open(source_path, O_RDONLY);
        int out_fd;
        uint8_t buf[4096];
        ssize_t n;

        if (in_fd < 0) {
            return -1;
        }

        out_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, (mode_t)0600);
        if (out_fd < 0) {
            close(in_fd);
            return -1;
        }

        while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
            if (write(out_fd, buf, (size_t)n) != n) {
                close(in_fd);
                close(out_fd);
                unlink(dest_path);
                return -1;
            }
        }

        close(in_fd);
        close(out_fd);
        unlink(source_path);
    }

    return 0;
}

static int crashvault_import_parsed(CrashVaultProcessor *processor, CrashVaultParsedReport *parsed,
                                    const char *source_path, CrashVaultImportStats *stats)
{
    const CrashVaultRawReport *report = &parsed->report;
    CrashVaultSymbolResult top_symbol;
    char fingerprint[CRASHVAULT_FINGERPRINT_HEX_MAX];
    int executable_valid = (report->flags & CRASHVAULT_FLAG_EXECUTABLE_VALID) != 0U;
    const char *executable_path = executable_valid ? report->executable_path : NULL;
    const char *default_exec = getenv("CRASHVAULT_DEFAULT_EXECUTABLE");
    int64_t now = (int64_t)time(NULL);
    int64_t crash_ts;
    sqlite3_stmt *stmt = NULL;
    int64_t application_id = 0;
    int64_t group_id = 0;
    int64_t occurrence_id = 0;
    int rc;

    if (crashvault_source_exists(processor, parsed->source_id)) {
        if (stats != NULL) {
            stats->duplicate++;
        }
        return 1;
    }

    if (executable_path == NULL && default_exec != NULL && default_exec[0] != '\0') {
        executable_path = default_exec;
        executable_valid = 1;
    }

    (void)crashvault_symbolize_address(executable_path,
                                     executable_valid ? report->executable_base : 0U, report->rip,
                                     &top_symbol);

    if (crashvault_compute_fingerprint(parsed, &top_symbol, fingerprint, sizeof(fingerprint)) != 0) {
        return -1;
    }

    crash_ts = crashvault_report_timestamp(report, now);

    rc = sqlite3_exec(processor->db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    rc = sqlite3_prepare_v2(processor->db, "INSERT OR IGNORE INTO applications(name) VALUES(?);", -1,
                            &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_exec(processor->db, "ROLLBACK;", NULL, NULL, NULL);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, report->app_name, -1, SQLITE_TRANSIENT);
    (void)sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    stmt = NULL;

    rc = sqlite3_prepare_v2(processor->db, "SELECT id FROM applications WHERE name = ?;", -1, &stmt,
                            NULL);
    if (rc != SQLITE_OK) {
        sqlite3_exec(processor->db, "ROLLBACK;", NULL, NULL, NULL);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, report->app_name, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        application_id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    if (application_id == 0) {
        sqlite3_exec(processor->db, "ROLLBACK;", NULL, NULL, NULL);
        return -1;
    }

    rc = sqlite3_prepare_v2(
        processor->db,
        "SELECT id, occurrence_count FROM crash_groups WHERE application_id = ? AND fingerprint = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_exec(processor->db, "ROLLBACK;", NULL, NULL, NULL);
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, application_id);
    sqlite3_bind_text(stmt, 2, fingerprint, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        group_id = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    stmt = NULL;

    if (group_id == 0) {
        rc = sqlite3_prepare_v2(processor->db,
                                "INSERT INTO crash_groups(application_id, fingerprint, signal, "
                                "top_function, top_module, occurrence_count, first_seen, last_seen) "
                                "VALUES(?, ?, ?, ?, ?, 0, ?, ?);",
                                -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            sqlite3_exec(processor->db, "ROLLBACK;", NULL, NULL, NULL);
            return -1;
        }

        sqlite3_bind_int64(stmt, 1, application_id);
        sqlite3_bind_text(stmt, 2, fingerprint, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, report->signal_num);
        sqlite3_bind_text(stmt, 4, top_symbol.function, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, top_symbol.module, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 6, crash_ts);
        sqlite3_bind_int64(stmt, 7, crash_ts);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            sqlite3_exec(processor->db, "ROLLBACK;", NULL, NULL, NULL);
            return -1;
        }
        sqlite3_finalize(stmt);
        group_id = sqlite3_last_insert_rowid(processor->db);
    } else {
        rc = sqlite3_prepare_v2(processor->db,
                                "UPDATE crash_groups SET top_function = ?, top_module = ?, last_seen = ? "
                                "WHERE id = ?;",
                                -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, top_symbol.function, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, top_symbol.module, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 3, crash_ts);
            sqlite3_bind_int64(stmt, 4, group_id);
            (void)sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    rc = sqlite3_prepare_v2(
        processor->db,
        "INSERT INTO crash_occurrences(group_id, source_report_id, raw_report_path, app_version, "
        "imported_at, crash_time_sec, crash_time_nsec, pid, tid, fault_addr, rip, rsp, rbp, rax, rbx, "
        "rcx, rdx, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15, executable_path, executable_base) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_exec(processor->db, "ROLLBACK;", NULL, NULL, NULL);
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, group_id);
    sqlite3_bind_text(stmt, 2, parsed->source_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, source_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, report->app_version, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, now);
    sqlite3_bind_int64(stmt, 6, (int64_t)report->crash_time_sec);
    sqlite3_bind_int64(stmt, 7, (int64_t)report->crash_time_nsec);
    sqlite3_bind_int64(stmt, 8, (int64_t)report->pid);
    sqlite3_bind_int64(stmt, 9, (int64_t)report->tid);
    sqlite3_bind_int64(stmt, 10, (int64_t)report->fault_addr);
    sqlite3_bind_int64(stmt, 11, (int64_t)report->rip);
    sqlite3_bind_int64(stmt, 12, (int64_t)report->rsp);
    sqlite3_bind_int64(stmt, 13, (int64_t)report->rbp);
    sqlite3_bind_int64(stmt, 14, (int64_t)report->rax);
    sqlite3_bind_int64(stmt, 15, (int64_t)report->rbx);
    sqlite3_bind_int64(stmt, 16, (int64_t)report->rcx);
    sqlite3_bind_int64(stmt, 17, (int64_t)report->rdx);
    sqlite3_bind_int64(stmt, 18, (int64_t)report->rsi);
    sqlite3_bind_int64(stmt, 19, (int64_t)report->rdi);
    sqlite3_bind_int64(stmt, 20, (int64_t)report->r8);
    sqlite3_bind_int64(stmt, 21, (int64_t)report->r9);
    sqlite3_bind_int64(stmt, 22, (int64_t)report->r10);
    sqlite3_bind_int64(stmt, 23, (int64_t)report->r11);
    sqlite3_bind_int64(stmt, 24, (int64_t)report->r12);
    sqlite3_bind_int64(stmt, 25, (int64_t)report->r13);
    sqlite3_bind_int64(stmt, 26, (int64_t)report->r14);
    sqlite3_bind_int64(stmt, 27, (int64_t)report->r15);
    sqlite3_bind_text(stmt, 28, executable_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 29, (int64_t)report->executable_base);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(processor->db, "ROLLBACK;", NULL, NULL, NULL);
        return -1;
    }
    occurrence_id = sqlite3_last_insert_rowid(processor->db);
    sqlite3_finalize(stmt);
    stmt = NULL;

    {
        uint32_t i;
        for (i = 0U; i < report->frame_count && i < CRASHVAULT_MAX_FRAMES; ++i) {
            CrashVaultSymbolResult frame_symbol;
            uint64_t frame_addr = report->frames[i];
            if (frame_addr == 0U) {
                continue;
            }

            (void)crashvault_symbolize_address(
                executable_path, executable_valid ? report->executable_base : 0U, frame_addr,
                &frame_symbol);

            rc = sqlite3_prepare_v2(processor->db,
                                    "INSERT INTO frames(occurrence_id, frame_index, raw_address, "
                                    "normalized_address, module, function_name, source_file, source_line, "
                                    "symbol_status) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?);",
                                    -1, &stmt, NULL);
            if (rc != SQLITE_OK) {
                sqlite3_exec(processor->db, "ROLLBACK;", NULL, NULL, NULL);
                return -1;
            }

            sqlite3_bind_int64(stmt, 1, occurrence_id);
            sqlite3_bind_int(stmt, 2, (int)i);
            sqlite3_bind_int64(stmt, 3, (int64_t)frame_symbol.raw_address);
            sqlite3_bind_int64(stmt, 4, (int64_t)frame_symbol.normalized_address);
            sqlite3_bind_text(stmt, 5, frame_symbol.module, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 6, frame_symbol.function, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 7, frame_symbol.source_file, -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stmt, 8, frame_symbol.source_line);
            sqlite3_bind_text(stmt, 9, crashvault_symbol_status_string(frame_symbol.status), -1,
                              SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                sqlite3_finalize(stmt);
                sqlite3_exec(processor->db, "ROLLBACK;", NULL, NULL, NULL);
                return -1;
            }
            sqlite3_finalize(stmt);
            stmt = NULL;
        }
    }

    rc = sqlite3_prepare_v2(processor->db,
                            "UPDATE crash_groups SET occurrence_count = occurrence_count + 1, "
                            "last_seen = ?, first_seen = MIN(first_seen, ?) WHERE id = ?;",
                            -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        sqlite3_exec(processor->db, "ROLLBACK;", NULL, NULL, NULL);
        return -1;
    }
    sqlite3_bind_int64(stmt, 1, crash_ts);
    sqlite3_bind_int64(stmt, 2, crash_ts);
    sqlite3_bind_int64(stmt, 3, group_id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(processor->db, "ROLLBACK;", NULL, NULL, NULL);
        return -1;
    }
    sqlite3_finalize(stmt);

    if (sqlite3_exec(processor->db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_exec(processor->db, "ROLLBACK;", NULL, NULL, NULL);
        return -1;
    }

    {
        char processed_path[PATH_MAX];
        if (crashvault_move_to_processed(processor, source_path, processed_path,
                                         sizeof(processed_path)) != 0) {
            /* Imported in DB; leaving source file in place is acceptable. */
        }
    }

    if (stats != NULL) {
        stats->imported++;
    }
    return 0;
}

int crashvault_processor_import_file(CrashVaultProcessor *processor, const char *path,
                                     CrashVaultImportStats *stats)
{
    CrashVaultParsedReport parsed;
    CrashVaultParseStatus status;

    if (processor == NULL || path == NULL) {
        return -1;
    }

    if (stats != NULL) {
        stats->scanned++;
    }

    status = crashvault_parse_raw_file(path, &parsed);
    if (status != CRASHVAULT_PARSE_OK) {
        if (stats != NULL) {
            stats->rejected++;
        }
        return -1;
    }

    return crashvault_import_parsed(processor, &parsed, path, stats);
}

static int crashvault_is_raw_report_name(const char *name)
{
    return strncmp(name, "crash_", 6) == 0 && strstr(name, ".raw") != NULL;
}

int crashvault_processor_import_pending(CrashVaultProcessor *processor,
                                        CrashVaultImportStats *stats)
{
    DIR *dir;
    struct dirent *entry;
    char path[PATH_MAX];

    if (processor == NULL) {
        return -1;
    }

    dir = opendir(processor->home_dir);
    if (dir == NULL) {
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (!crashvault_is_raw_report_name(entry->d_name)) {
            continue;
        }

        if (snprintf(path, sizeof(path), "%s/%s", processor->home_dir, entry->d_name) >=
            (int)sizeof(path)) {
            if (stats != NULL) {
                stats->rejected++;
            }
            continue;
        }

        (void)crashvault_processor_import_file(processor, path, stats);
    }

    closedir(dir);
    return 0;
}

int crashvault_processor_list_groups(CrashVaultProcessor *processor)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (processor == NULL) {
        return -1;
    }

    printf("ID  Signal   Top Function                 Count   Last Seen\n");
    rc = sqlite3_prepare_v2(
        processor->db,
        "SELECT g.id, g.signal, g.top_function, g.occurrence_count, g.last_seen "
        "FROM crash_groups g ORDER BY g.last_seen DESC, g.id ASC;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        char sigbuf[16];
        const char *sig =
            crashvault_signal_name((int)sqlite3_column_int(stmt, 1), sigbuf, sizeof(sigbuf));
        const char *func = (const char *)sqlite3_column_text(stmt, 2);
        time_t last_seen = (time_t)sqlite3_column_int64(stmt, 4);
        char timebuf[32];

        if (func == NULL || func[0] == '\0') {
            func = "??";
        }

        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&last_seen));
        printf("%-3lld %-8s %-28s %-7lld %s\n", (long long)sqlite3_column_int64(stmt, 0), sig,
               func, (long long)sqlite3_column_int64(stmt, 3), timebuf);
    }

    sqlite3_finalize(stmt);
    return 0;
}

int crashvault_processor_show_group(CrashVaultProcessor *processor, int64_t group_id)
{
    sqlite3_stmt *stmt = NULL;
    char sigbuf[16];
    const char *sig = "?";
    int rc;

    if (processor == NULL) {
        return -1;
    }

    rc = sqlite3_prepare_v2(
        processor->db,
        "SELECT signal, top_function, occurrence_count, first_seen, last_seen, fingerprint "
        "FROM crash_groups WHERE id = ?;",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, group_id);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }

    sig = crashvault_signal_name((int)sqlite3_column_int(stmt, 0), sigbuf, sizeof(sigbuf));
    {
        const char *func = (const char *)sqlite3_column_text(stmt, 1);
        time_t first_seen = (time_t)sqlite3_column_int64(stmt, 3);
        time_t last_seen = (time_t)sqlite3_column_int64(stmt, 4);
        char t1[32];
        char t2[32];

        if (func == NULL || func[0] == '\0') {
            func = "??";
        }

        strftime(t1, sizeof(t1), "%Y-%m-%d %H:%M:%S", localtime(&first_seen));
        strftime(t2, sizeof(t2), "%Y-%m-%d %H:%M:%S", localtime(&last_seen));

        printf("%s · %s\n\n", sig, func);
        printf("Fingerprint: %s\n", sqlite3_column_text(stmt, 5));
        printf("Occurrences: %lld\n", (long long)sqlite3_column_int64(stmt, 2));
        printf("First Seen:  %s\n", t1);
        printf("Last Seen:   %s\n\n", t2);
    }
    sqlite3_finalize(stmt);

    rc = sqlite3_prepare_v2(
        processor->db,
        "SELECT DISTINCT app_version FROM crash_occurrences WHERE group_id = ? AND app_version IS NOT NULL;",
        -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, group_id);
        int first = 1;
        printf("Versions:");
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            printf("%s %s", first ? "" : ",", sqlite3_column_text(stmt, 0));
            first = 0;
        }
        printf("\n\n");
        sqlite3_finalize(stmt);
    }

    rc = sqlite3_prepare_v2(
        processor->db,
        "SELECT o.id, o.pid, o.tid, o.rip, o.rsp, o.rbp, o.imported_at, o.executable_path, "
        "o.executable_base "
        "FROM crash_occurrences o WHERE o.group_id = ? ORDER BY o.id ASC LIMIT 1;",
        -1, &stmt, NULL);
    if (rc == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, group_id);
    }
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        int64_t occurrence_id = sqlite3_column_int64(stmt, 0);
        uint64_t rip = (uint64_t)sqlite3_column_int64(stmt, 3);
        uint64_t base = (uint64_t)sqlite3_column_int64(stmt, 8);
        char exe_path[PATH_MAX];
        const char *exe_col = (const char *)sqlite3_column_text(stmt, 7);

        exe_path[0] = '\0';
        if (exe_col != NULL) {
            strncpy(exe_path, exe_col, sizeof(exe_path) - 1U);
            exe_path[sizeof(exe_path) - 1U] = '\0';
        }
        sqlite3_finalize(stmt);

        if (exe_path[0] != '\0') {
            printf("Executable: %s (base 0x%llx)\n", exe_path, (unsigned long long)base);
            if (base != 0U && rip >= base) {
                printf("Top frame offset: 0x%llx\n", (unsigned long long)(rip - base));
            }
            printf("\n");
        }

        printf("Registers (sample occurrence #%lld)\n", (long long)occurrence_id);
        rc = sqlite3_prepare_v2(
            processor->db,
            "SELECT rip, rsp, rbp, rax, rbx, rcx, rdx, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15 "
            "FROM crash_occurrences WHERE id = ?;",
            -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, occurrence_id);
        }
        if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
            const char *names[] = {"RIP", "RSP", "RBP", "RAX", "RBX", "RCX", "RDX", "RSI", "RDI",
                                   "R8",  "R9",  "R10", "R11", "R12", "R13", "R14", "R15"};
            int i;
            for (i = 0; i < 17; ++i) {
                printf("%-4s 0x%llx\n", names[i],
                       (unsigned long long)sqlite3_column_int64(stmt, i));
            }
            sqlite3_finalize(stmt);
        }
        printf("\nStack\n");

        rc = sqlite3_prepare_v2(
            processor->db,
            "SELECT frame_index, function_name, source_file, source_line, raw_address "
            "FROM frames WHERE occurrence_id = ? ORDER BY frame_index ASC;",
            -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, occurrence_id);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const char *func = (const char *)sqlite3_column_text(stmt, 1);
                const char *file = (const char *)sqlite3_column_text(stmt, 2);
                int line = sqlite3_column_int(stmt, 3);
                if (func == NULL) {
                    func = "??";
                }
                if (file == NULL || file[0] == '\0') {
                    printf("#%d %s (0x%llx)\n", sqlite3_column_int(stmt, 0), func,
                           (unsigned long long)sqlite3_column_int64(stmt, 4));
                } else {
                    printf("#%d %s %s:%d (0x%llx)\n", sqlite3_column_int(stmt, 0), func, file,
                           line, (unsigned long long)sqlite3_column_int64(stmt, 4));
                }
            }
            sqlite3_finalize(stmt);
        }
    }

    return 0;
}

int crashvault_processor_get_group_occurrence_count(CrashVaultProcessor *processor,
                                                    int64_t group_id, int64_t *count_out)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (processor == NULL || count_out == NULL) {
        return -1;
    }

    rc = sqlite3_prepare_v2(processor->db,
                            "SELECT occurrence_count FROM crash_groups WHERE id = ?;", -1, &stmt,
                            NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, group_id);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }

    *count_out = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return 0;
}

int crashvault_processor_get_group_signal(CrashVaultProcessor *processor, int64_t group_id,
                                          int *signal_out)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (processor == NULL || signal_out == NULL) {
        return -1;
    }

    rc = sqlite3_prepare_v2(processor->db, "SELECT signal FROM crash_groups WHERE id = ?;", -1,
                            &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_int64(stmt, 1, group_id);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }

    *signal_out = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return 0;
}

int crashvault_processor_sum_occurrences_for_signal(CrashVaultProcessor *processor, int signal,
                                                    int64_t *count_out)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (processor == NULL || count_out == NULL) {
        return -1;
    }

    rc = sqlite3_prepare_v2(
        processor->db,
        "SELECT COALESCE(SUM(occurrence_count), 0) FROM crash_groups WHERE signal = ?;", -1, &stmt,
        NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    sqlite3_bind_int(stmt, 1, signal);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }

    *count_out = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return 0;
}

int crashvault_processor_count_groups(CrashVaultProcessor *processor, int64_t *count_out)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (processor == NULL || count_out == NULL) {
        return -1;
    }

    rc = sqlite3_prepare_v2(processor->db, "SELECT COUNT(*) FROM crash_groups;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }

    *count_out = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return 0;
}
