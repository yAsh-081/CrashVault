#ifndef CRASHVAULT_SYMBOLIZE_H
#define CRASHVAULT_SYMBOLIZE_H

#include <stddef.h>
#include <stdint.h>

#define CRASHVAULT_SYMBOL_FUNCTION_MAX 256u
#define CRASHVAULT_SYMBOL_SOURCE_MAX 512u
#define CRASHVAULT_SYMBOL_MODULE_MAX 512u

typedef enum CrashVaultSymbolStatus {
    CRASHVAULT_SYMBOL_OK = 0,
    CRASHVAULT_SYMBOL_NOT_FOUND,
    CRASHVAULT_SYMBOL_TOOL_ERROR,
    CRASHVAULT_SYMBOL_NO_EXECUTABLE,
    CRASHVAULT_SYMBOL_INVALID_ADDRESS
} CrashVaultSymbolStatus;

typedef struct CrashVaultSymbolResult {
    CrashVaultSymbolStatus status;
    uint64_t raw_address;
    uint64_t normalized_address;
    char module[CRASHVAULT_SYMBOL_MODULE_MAX];
    char function[CRASHVAULT_SYMBOL_FUNCTION_MAX];
    char source_file[CRASHVAULT_SYMBOL_SOURCE_MAX];
    int source_line;
} CrashVaultSymbolResult;

void crashvault_symbolize_init(const char *addr2line_path);

/* Uses compile-time detected addr2line when no path is provided. */
void crashvault_symbolize_init_default(void);

int crashvault_symbolize_address(const char *executable_path, uint64_t executable_base,
                                 uint64_t runtime_address, CrashVaultSymbolResult *out);

const char *crashvault_symbol_status_string(CrashVaultSymbolStatus status);

#endif /* CRASHVAULT_SYMBOLIZE_H */
