#define _GNU_SOURCE

#include "crashvault.h"
#include "crashvault_report.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__) && defined(__x86_64__)
#define CRASHVAULT_LINUX_X86_64 1
#include <sys/syscall.h>
#include <sys/ucontext.h>
#endif

#define CRASHVAULT_STORAGE_SUBDIR ".crashvault"
#define CRASHVAULT_REPORT_PREFIX "crash_"
#define CRASHVAULT_REPORT_SUFFIX ".raw"

#define CRASHVAULT_ERR_INVALID_CONFIG (-EINVAL)
#define CRASHVAULT_ERR_ALREADY_INIT (-EBUSY)
#define CRASHVAULT_ERR_NO_MEMORY (-ENOMEM)
#define CRASHVAULT_ERR_IO (-EIO)

#ifndef SIGSTKSZ
#define SIGSTKSZ 16384
#endif

typedef struct {
    int installed;
    struct sigaction previous;
} CrashVaultSignalSlot;

static struct {
    int initialized;
    char app_name[CRASHVAULT_APP_NAME_MAX];
    char app_version[CRASHVAULT_APP_VERSION_MAX];
    char storage_path[PATH_MAX];
    char executable_path[CRASHVAULT_EXECUTABLE_PATH_MAX];
    uint64_t executable_base;
    int executable_valid;
    void *alt_stack;
    size_t alt_stack_size;
    volatile sig_atomic_t handling_crash;
    volatile sig_atomic_t report_sequence;
    CrashVaultSignalSlot slots[4];
} g_cv;

static const int g_fatal_signals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL};

static uint32_t crashvault_sdk_version_packed(void)
{
    return (uint32_t)((CRASHVAULT_SDK_VERSION_MAJOR << 16) |
                      (CRASHVAULT_SDK_VERSION_MINOR << 8) |
                      CRASHVAULT_SDK_VERSION_PATCH);
}

static int crashvault_string_nonempty(const char *value)
{
    return value != NULL && value[0] != '\0';
}

static int crashvault_copy_string(char *dest, size_t dest_size, const char *src)
{
    size_t i;

    if (dest_size == 0U) {
        return -1;
    }

    for (i = 0U; i < dest_size - 1U; ++i) {
        dest[i] = src[i];
        if (src[i] == '\0') {
            return 0;
        }
    }

    dest[dest_size - 1U] = '\0';
    return (src[i] == '\0') ? 0 : -1;
}

static int crashvault_join_path(char *dest, size_t dest_size, const char *a, const char *b)
{
    size_t alen;
    size_t blen;
    int needs_sep;

    if (a == NULL || b == NULL) {
        return -1;
    }

    alen = strlen(a);
    blen = strlen(b);
    needs_sep = (alen > 0U && a[alen - 1U] != '/');
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

static int crashvault_prepare_storage_path(char *dest, size_t dest_size)
{
    const char *home;
    const char *override;

    override = getenv("CRASHVAULT_HOME");
    if (override != NULL && override[0] != '\0') {
        return crashvault_copy_string(dest, dest_size, override);
    }

    home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return -1;
    }

    if (crashvault_join_path(dest, dest_size, home, CRASHVAULT_STORAGE_SUBDIR) != 0) {
        return -1;
    }
    return 0;
}

static int crashvault_mkdir_p(const char *path, mode_t mode)
{
    char buf[PATH_MAX];
    size_t len;
    size_t i;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }

    if (strlen(path) >= sizeof(buf)) {
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

/* --- Async-signal-safe helpers (handler path only) --- */

static ssize_t cv_write_all(int fd, const void *buf, size_t len)
{
    const uint8_t *cursor = (const uint8_t *)buf;
    size_t remaining = len;

    while (remaining > 0U) {
        ssize_t written = write(fd, cursor, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (written == 0) {
            return -1;
        }
        cursor += (size_t)written;
        remaining -= (size_t)written;
    }
    return (ssize_t)len;
}

static size_t cv_u64_to_dec(char *buf, size_t buf_size, uint64_t value)
{
    char tmp[24];
    size_t count = 0U;
    size_t i;

    if (buf_size == 0U) {
        return 0U;
    }

    if (value == 0U) {
        if (buf_size < 2U) {
            return 0U;
        }
        buf[0] = '0';
        buf[1] = '\0';
        return 1U;
    }

    while (value > 0U && count < sizeof(tmp)) {
        tmp[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    if (count >= buf_size) {
        return 0U;
    }

    for (i = 0U; i < count; ++i) {
        buf[i] = tmp[count - 1U - i];
    }
    buf[count] = '\0';
    return count;
}

static size_t cv_str_len(const char *s)
{
    size_t n = 0U;
    while (s[n] != '\0') {
        ++n;
    }
    return n;
}

static size_t cv_append_str(char *dest, size_t dest_size, size_t offset, const char *src)
{
    size_t i = 0U;
    while (src[i] != '\0') {
        if (offset + i + 1U >= dest_size) {
            return 0U;
        }
        dest[offset + i] = src[i];
        ++i;
    }
    dest[offset + i] = '\0';
    return offset + i;
}

static int cv_build_report_path(char *path, size_t path_size, uint64_t pid, uint64_t tid,
                                uint32_t seq)
{
    char pid_buf[24];
    char tid_buf[24];
    char seq_buf[24];
    size_t offset;

    if (cv_u64_to_dec(pid_buf, sizeof(pid_buf), pid) == 0U ||
        cv_u64_to_dec(tid_buf, sizeof(tid_buf), tid) == 0U ||
        cv_u64_to_dec(seq_buf, sizeof(seq_buf), (uint64_t)seq) == 0U) {
        return -1;
    }

    if (cv_str_len(g_cv.storage_path) + 64U >= path_size) {
        return -1;
    }

    offset = cv_append_str(path, path_size, 0U, g_cv.storage_path);
    if (offset == 0U) {
        return -1;
    }
    if (offset + 1U >= path_size) {
        return -1;
    }
    path[offset++] = '/';

    offset = cv_append_str(path, path_size, offset, CRASHVAULT_REPORT_PREFIX);
    if (offset == 0U) {
        return -1;
    }
    offset = cv_append_str(path, path_size, offset, pid_buf);
    if (offset == 0U) {
        return -1;
    }
    offset = cv_append_str(path, path_size, offset, "_");
    if (offset == 0U) {
        return -1;
    }
    offset = cv_append_str(path, path_size, offset, tid_buf);
    if (offset == 0U) {
        return -1;
    }
    offset = cv_append_str(path, path_size, offset, "_");
    if (offset == 0U) {
        return -1;
    }
    offset = cv_append_str(path, path_size, offset, seq_buf);
    if (offset == 0U) {
        return -1;
    }
    offset = cv_append_str(path, path_size, offset, CRASHVAULT_REPORT_SUFFIX);
    return (offset == 0U) ? -1 : 0;
}

static int crashvault_read_executable_base_from_maps(const char *exe_path, uint64_t *base_out)
{
    FILE *maps;
    char line[1024];
    uint64_t best = 0U;
    int found = 0;

    maps = fopen("/proc/self/maps", "r");
    if (maps == NULL) {
        return -1;
    }

    while (fgets(line, (int)sizeof(line), maps) != NULL) {
        unsigned long start = 0U;
        unsigned long end = 0U;
        char perms[8];
        char pathbuf[PATH_MAX];

        pathbuf[0] = '\0';
        if (sscanf(line, "%lx-%lx %7s %*s %*s %*s %[^\n]", &start, &end, perms, pathbuf) < 3) {
            continue;
        }

        if (strchr(perms, 'x') == NULL || pathbuf[0] == '\0') {
            continue;
        }

        if (strcmp(pathbuf, exe_path) != 0) {
            continue;
        }

        if (!found || (uint64_t)start < best) {
            best = (uint64_t)start;
            found = 1;
        }
    }

    fclose(maps);
    if (!found) {
        return -1;
    }

    *base_out = best;
    return 0;
}

static int crashvault_read_executable_metadata(void)
{
    Dl_info info;
    ssize_t link_len;
    char proc_path[PATH_MAX];
    uint64_t base = 0U;

    g_cv.executable_valid = 0;
    g_cv.executable_base = 0U;
    g_cv.executable_path[0] = '\0';

    link_len = readlink("/proc/self/exe", proc_path, sizeof(proc_path) - 1U);
    if (link_len <= 0) {
        return -1;
    }
    proc_path[(size_t)link_len] = '\0';

    if (crashvault_copy_string(g_cv.executable_path, sizeof(g_cv.executable_path), proc_path) != 0) {
        return -1;
    }

    if (dladdr((void *)(uintptr_t)&crashvault_init, &info) != 0 && info.dli_fbase != NULL) {
        base = (uint64_t)(uintptr_t)info.dli_fbase;
    } else if (crashvault_read_executable_base_from_maps(g_cv.executable_path, &base) != 0) {
        return -1;
    }

    g_cv.executable_base = base;
    g_cv.executable_valid = 1;
    return 0;
}

static void cv_capture_timestamp(CrashVaultRawReport *report)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return;
    }

    report->crash_time_sec = (uint64_t)ts.tv_sec;
    report->crash_time_nsec = (uint64_t)ts.tv_nsec;
    report->flags |= CRASHVAULT_FLAG_TIMESTAMP_VALID;
}

static void cv_capture_executable(CrashVaultRawReport *report)
{
    if (!g_cv.executable_valid) {
        return;
    }

    memset(report->executable_path, 0, sizeof(report->executable_path));
    memcpy(report->executable_path, g_cv.executable_path, sizeof(report->executable_path) - 1U);
    report->executable_base = g_cv.executable_base;
    report->flags |= CRASHVAULT_FLAG_EXECUTABLE_VALID;
}

#if defined(CRASHVAULT_LINUX_X86_64)

static uint64_t cv_get_tid(void)
{
    return (uint64_t)syscall(SYS_gettid);
}

static void cv_capture_registers(CrashVaultRawReport *report, const ucontext_t *uctx)
{
    const greg_t *gregs = uctx->uc_mcontext.gregs;

    report->arch_id = CRASHVAULT_ARCH_X86_64;
    report->rip = (uint64_t)gregs[REG_RIP];
    report->rsp = (uint64_t)gregs[REG_RSP];
    report->rbp = (uint64_t)gregs[REG_RBP];
    report->rax = (uint64_t)gregs[REG_RAX];
    report->rbx = (uint64_t)gregs[REG_RBX];
    report->rcx = (uint64_t)gregs[REG_RCX];
    report->rdx = (uint64_t)gregs[REG_RDX];
    report->rsi = (uint64_t)gregs[REG_RSI];
    report->rdi = (uint64_t)gregs[REG_RDI];
    report->r8 = (uint64_t)gregs[REG_R8];
    report->r9 = (uint64_t)gregs[REG_R9];
    report->r10 = (uint64_t)gregs[REG_R10];
    report->r11 = (uint64_t)gregs[REG_R11];
    report->r12 = (uint64_t)gregs[REG_R12];
    report->r13 = (uint64_t)gregs[REG_R13];
    report->r14 = (uint64_t)gregs[REG_R14];
    report->r15 = (uint64_t)gregs[REG_R15];

    report->flags |= CRASHVAULT_FLAG_RIP_VALID;
    report->flags |= CRASHVAULT_FLAG_RSP_VALID;
    report->flags |= CRASHVAULT_FLAG_RBP_VALID;
    report->flags |= CRASHVAULT_FLAG_GPR_VALID;
}

/*
 * Phase 1 records RIP from ucontext only. Dereferencing frame pointers inside
 * the handler can synchronously fault and re-enter the handler; that is
 * intentionally avoided here. RBP/RSP are still captured for later unwind.
 */
static void cv_capture_stack_frames(CrashVaultRawReport *report, const ucontext_t *uctx)
{
    (void)uctx;

    report->frames[0] = report->rip;
    report->frame_count = 1U;
    report->flags |= CRASHVAULT_FLAG_FRAMES_VALID;
}

#else

static uint64_t cv_get_tid(void)
{
    return (uint64_t)getpid();
}

static void cv_capture_registers(CrashVaultRawReport *report, const ucontext_t *uctx)
{
    (void)uctx;
    report->arch_id = CRASHVAULT_ARCH_UNKNOWN;
}

static void cv_capture_stack_frames(CrashVaultRawReport *report, const ucontext_t *uctx)
{
    (void)uctx;
    report->frame_count = 0U;
}

#endif

static void cv_fill_report(CrashVaultRawReport *report, int signum, siginfo_t *info,
                           void *context)
{
    memset(report, 0, sizeof(*report));

    report->magic = CRASHVAULT_RAW_MAGIC;
    report->format_version = CRASHVAULT_RAW_FORMAT_VERSION;
    report->sdk_version = crashvault_sdk_version_packed();
    report->signal_num = signum;
    report->si_code = (info != NULL) ? info->si_code : 0;
    report->pid = (uint64_t)getpid();
    report->tid = cv_get_tid();

    memcpy(report->app_name, g_cv.app_name, sizeof(report->app_name));
    memcpy(report->app_version, g_cv.app_version, sizeof(report->app_version));

    if (info != NULL && (info->si_code == SEGV_MAPERR || info->si_code == SEGV_ACCERR)) {
        report->fault_addr = (uint64_t)(uintptr_t)info->si_addr;
        report->flags |= CRASHVAULT_FLAG_FAULT_ADDR_VALID;
    }

#if defined(CRASHVAULT_LINUX_X86_64)
    if (context != NULL) {
        const ucontext_t *uctx = (const ucontext_t *)context;
        cv_capture_registers(report, uctx);
        cv_capture_stack_frames(report, uctx);
    }
#else
    (void)context;
#endif

    cv_capture_timestamp(report);
    cv_capture_executable(report);
}

static void cv_write_report_or_fail(const CrashVaultRawReport *report)
{
    char path[PATH_MAX];
    uint32_t seq;
    int fd;

    seq = (uint32_t)g_cv.report_sequence;
    g_cv.report_sequence = (sig_atomic_t)(seq + 1U);

    if (cv_build_report_path(path, sizeof(path), report->pid, report->tid, seq) != 0) {
        return;
    }

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, (mode_t)0600);
    if (fd < 0) {
        return;
    }

    (void)cv_write_all(fd, report, sizeof(*report));
    close(fd);
}

static void crashvault_fatal_handler(int signum, siginfo_t *info, void *context)
{
    CrashVaultRawReport report;

    if (g_cv.handling_crash) {
        _exit(128 + signum);
    }
    g_cv.handling_crash = 1;

    cv_fill_report(&report, signum, info, context);
    cv_write_report_or_fail(&report);

    _exit(128 + signum);
}

static int crashvault_install_handlers(void)
{
    struct sigaction sa;
    size_t i;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crashvault_fatal_handler;
    sigfillset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;

    for (i = 0U; i < sizeof(g_fatal_signals) / sizeof(g_fatal_signals[0]); ++i) {
        int signum = g_fatal_signals[i];
        if (sigaction(signum, &sa, &g_cv.slots[i].previous) != 0) {
            while (i > 0U) {
                --i;
                sigaction(g_fatal_signals[i], &g_cv.slots[i].previous, NULL);
                g_cv.slots[i].installed = 0;
            }
            return -1;
        }
        g_cv.slots[i].installed = 1;
    }

    return 0;
}

static void crashvault_restore_handlers(void)
{
    size_t i;

    for (i = 0U; i < sizeof(g_fatal_signals) / sizeof(g_fatal_signals[0]); ++i) {
        if (g_cv.slots[i].installed) {
            sigaction(g_fatal_signals[i], &g_cv.slots[i].previous, NULL);
            g_cv.slots[i].installed = 0;
        }
    }
}

static int crashvault_setup_alt_stack(void)
{
    stack_t ss;
    stack_t old;

    g_cv.alt_stack_size = (size_t)SIGSTKSZ;
    g_cv.alt_stack = malloc(g_cv.alt_stack_size);
    if (g_cv.alt_stack == NULL) {
        return -1;
    }

    ss.ss_sp = g_cv.alt_stack;
    ss.ss_size = g_cv.alt_stack_size;
    ss.ss_flags = 0;
    if (sigaltstack(&ss, &old) != 0) {
        free(g_cv.alt_stack);
        g_cv.alt_stack = NULL;
        g_cv.alt_stack_size = 0U;
        return -1;
    }

    return 0;
}

static void crashvault_teardown_alt_stack(void)
{
    stack_t disable;

    if (g_cv.alt_stack == NULL) {
        return;
    }

    disable.ss_sp = NULL;
    disable.ss_size = 0U;
    disable.ss_flags = SS_DISABLE;
    sigaltstack(&disable, NULL);
    free(g_cv.alt_stack);
    g_cv.alt_stack = NULL;
    g_cv.alt_stack_size = 0U;
}

int crashvault_init(const CrashVaultConfig *config)
{
    if (config == NULL || !crashvault_string_nonempty(config->app_name) ||
        !crashvault_string_nonempty(config->version)) {
        return CRASHVAULT_ERR_INVALID_CONFIG;
    }

    if (g_cv.initialized) {
        return 0;
    }

    if (crashvault_copy_string(g_cv.app_name, sizeof(g_cv.app_name), config->app_name) != 0 ||
        crashvault_copy_string(g_cv.app_version, sizeof(g_cv.app_version), config->version) != 0) {
        return CRASHVAULT_ERR_INVALID_CONFIG;
    }

    if (crashvault_prepare_storage_path(g_cv.storage_path, sizeof(g_cv.storage_path)) != 0) {
        return CRASHVAULT_ERR_IO;
    }

    if (crashvault_mkdir_p(g_cv.storage_path, (mode_t)0700) != 0) {
        return CRASHVAULT_ERR_IO;
    }

    (void)crashvault_read_executable_metadata();

    if (crashvault_setup_alt_stack() != 0) {
        return CRASHVAULT_ERR_NO_MEMORY;
    }

    if (crashvault_install_handlers() != 0) {
        crashvault_teardown_alt_stack();
        return CRASHVAULT_ERR_IO;
    }

    g_cv.handling_crash = 0;
    g_cv.report_sequence = 0;
    g_cv.initialized = 1;
    return 0;
}

void crashvault_shutdown(void)
{
    if (!g_cv.initialized) {
        return;
    }

    crashvault_restore_handlers();
    crashvault_teardown_alt_stack();
    g_cv.initialized = 0;
}

const char *crashvault_storage_path(void)
{
    if (!g_cv.initialized) {
        return NULL;
    }
    return g_cv.storage_path;
}
