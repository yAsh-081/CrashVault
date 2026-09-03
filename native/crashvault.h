#ifndef CRASHVAULT_H
#define CRASHVAULT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CrashVault — Phase 1 public SDK.
 *
 * Initialize once before running application code. On fatal signals the
 * library captures a minimal raw crash report and terminates the process.
 */

#define CRASHVAULT_SDK_VERSION_MAJOR 1
#define CRASHVAULT_SDK_VERSION_MINOR 0
#define CRASHVAULT_SDK_VERSION_PATCH 0

typedef struct {
    const char *app_name;
    const char *version;
} CrashVaultConfig;

/**
 * Initialize CrashVault.
 *
 * Validates configuration, copies strings into internal storage (callers do
 * not need to keep config pointers alive), prepares ~/.crashvault/, installs
 * fatal signal handlers, and allocates an alternate signal stack.
 *
 * Returns 0 on success, a negative errno-style code on failure.
 * Safe to call repeatedly; only the first successful call installs handlers.
 */
int crashvault_init(const CrashVaultConfig *config);

/**
 * Restore previous signal handlers and release resources.
 * Does not remove crash reports already written.
 */
void crashvault_shutdown(void);

/**
 * Returns the absolute path to the crash-report storage directory, or NULL if
 * CrashVault has not been initialized successfully.
 */
const char *crashvault_storage_path(void);

#ifdef __cplusplus
}
#endif

#endif /* CRASHVAULT_H */
