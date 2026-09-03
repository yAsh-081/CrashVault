#ifndef CRASHVAULT_PROCESSOR_H
#define CRASHVAULT_PROCESSOR_H

#include "crashvault_report.h"
#include "symbolize.h"

#include <stddef.h>
#include <stdint.h>

#define CRASHVAULT_FINGERPRINT_HEX_MAX 33u
#define CRASHVAULT_SOURCE_ID_HEX_MAX 33u
#define CRASHVAULT_SIGNAL_NAME_MAX 16u

typedef enum CrashVaultParseStatus {
    CRASHVAULT_PARSE_OK = 0,
    CRASHVAULT_PARSE_IO_ERROR,
    CRASHVAULT_PARSE_TRUNCATED,
    CRASHVAULT_PARSE_OVERSIZED,
    CRASHVAULT_PARSE_BAD_MAGIC,
    CRASHVAULT_PARSE_UNSUPPORTED_VERSION,
    CRASHVAULT_PARSE_INVALID_FIELDS
} CrashVaultParseStatus;

typedef struct CrashVaultParsedReport {
    CrashVaultRawReport report;
    size_t bytes_read;
    char source_path[512];
    char source_id[CRASHVAULT_SOURCE_ID_HEX_MAX];
} CrashVaultParsedReport;

typedef struct CrashVaultImportStats {
    size_t scanned;
    size_t imported;
    size_t rejected;
    size_t duplicate;
} CrashVaultImportStats;

typedef struct CrashVaultProcessorConfig {
    const char *home_dir;
    const char *addr2line_path;
} CrashVaultProcessorConfig;

typedef struct CrashVaultProcessor CrashVaultProcessor;

CrashVaultProcessor *crashvault_processor_open(const CrashVaultProcessorConfig *config);
void crashvault_processor_close(CrashVaultProcessor *processor);

const char *crashvault_parse_status_string(CrashVaultParseStatus status);

CrashVaultParseStatus crashvault_parse_raw_file(const char *path, CrashVaultParsedReport *out);

int crashvault_compute_source_id(const void *data, size_t len, char *out, size_t out_size);

int crashvault_compute_fingerprint(const CrashVaultParsedReport *parsed,
                                   const CrashVaultSymbolResult *top_symbol,
                                   char *out, size_t out_size);

uint64_t crashvault_normalize_address(uint64_t runtime_address, uint64_t executable_base,
                                      int executable_valid);

int crashvault_processor_import_file(CrashVaultProcessor *processor, const char *path,
                                     CrashVaultImportStats *stats);

int crashvault_processor_import_pending(CrashVaultProcessor *processor,
                                        CrashVaultImportStats *stats);

int crashvault_processor_list_groups(CrashVaultProcessor *processor);

int crashvault_processor_show_group(CrashVaultProcessor *processor, int64_t group_id);

int crashvault_processor_get_group_occurrence_count(CrashVaultProcessor *processor,
                                                    int64_t group_id, int64_t *count_out);

int crashvault_processor_get_group_signal(CrashVaultProcessor *processor, int64_t group_id,
                                          int *signal_out);

int crashvault_processor_sum_occurrences_for_signal(CrashVaultProcessor *processor, int signal,
                                                    int64_t *count_out);

int crashvault_processor_count_groups(CrashVaultProcessor *processor, int64_t *count_out);

#endif /* CRASHVAULT_PROCESSOR_H */
