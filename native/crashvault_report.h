#ifndef CRASHVAULT_REPORT_H
#define CRASHVAULT_REPORT_H

/**
 * Fixed-layout raw crash report written from the fatal signal handler.
 *
 * Format version 1: 800 bytes (legacy).
 * Format version 2: adds crash timestamp, executable path, and load base for PIE.
 */

#include <stddef.h>
#include <stdint.h>

#define CRASHVAULT_RAW_MAGIC 0x43564155u /* 'CVAU' */
#define CRASHVAULT_RAW_FORMAT_VERSION 2u
#define CRASHVAULT_RAW_FORMAT_VERSION_V1 1u

#define CRASHVAULT_ARCH_UNKNOWN 0u
#define CRASHVAULT_ARCH_X86_64 1u

#define CRASHVAULT_FLAG_FAULT_ADDR_VALID (1u << 0)
#define CRASHVAULT_FLAG_RIP_VALID (1u << 1)
#define CRASHVAULT_FLAG_RSP_VALID (1u << 2)
#define CRASHVAULT_FLAG_RBP_VALID (1u << 3)
#define CRASHVAULT_FLAG_GPR_VALID (1u << 4)
#define CRASHVAULT_FLAG_FRAMES_VALID (1u << 5)
#define CRASHVAULT_FLAG_TIMESTAMP_VALID (1u << 6)
#define CRASHVAULT_FLAG_EXECUTABLE_VALID (1u << 7)

#define CRASHVAULT_APP_NAME_MAX 64u
#define CRASHVAULT_APP_VERSION_MAX 32u
#define CRASHVAULT_EXECUTABLE_PATH_MAX 512u
#define CRASHVAULT_MAX_FRAMES 64u

#define CRASHVAULT_RAW_V1_SIZE 800u
#define CRASHVAULT_RAW_V2_SIZE 1336u

typedef struct CrashVaultRawReport {
    uint32_t magic;
    uint32_t format_version;
    uint32_t sdk_version;
    uint32_t flags;

    char app_name[CRASHVAULT_APP_NAME_MAX];
    char app_version[CRASHVAULT_APP_VERSION_MAX];

    int32_t signal_num;
    int32_t si_code;
    uint64_t pid;
    uint64_t tid;
    uint64_t fault_addr;

    uint32_t arch_id;
    uint32_t frame_count;

    uint64_t rip;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    uint64_t frames[CRASHVAULT_MAX_FRAMES];

    /* v2 extension */
    uint64_t crash_time_sec;
    uint64_t crash_time_nsec;
    uint64_t executable_base;
    char executable_path[CRASHVAULT_EXECUTABLE_PATH_MAX];
} CrashVaultRawReport;

_Static_assert(sizeof(CrashVaultRawReport) == CRASHVAULT_RAW_V2_SIZE,
               "CrashVaultRawReport layout changed");

static inline int crashvault_raw_report_magic_valid(uint32_t magic)
{
    return magic == CRASHVAULT_RAW_MAGIC;
}

static inline int crashvault_raw_report_version_supported(uint32_t version)
{
    return version == CRASHVAULT_RAW_FORMAT_VERSION_V1 ||
           version == CRASHVAULT_RAW_FORMAT_VERSION;
}

static inline int crashvault_raw_report_valid(const CrashVaultRawReport *report)
{
    return report != NULL &&
           crashvault_raw_report_magic_valid(report->magic) &&
           crashvault_raw_report_version_supported(report->format_version);
}

static inline size_t crashvault_raw_report_expected_size(uint32_t format_version)
{
    if (format_version == CRASHVAULT_RAW_FORMAT_VERSION_V1) {
        return (size_t)CRASHVAULT_RAW_V1_SIZE;
    }
    if (format_version == CRASHVAULT_RAW_FORMAT_VERSION) {
        return (size_t)CRASHVAULT_RAW_V2_SIZE;
    }
    return 0U;
}

#endif /* CRASHVAULT_REPORT_H */
