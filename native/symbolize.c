#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "symbolize.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_addr2line_path[PATH_MAX] = "/usr/bin/addr2line";

void crashvault_symbolize_init(const char *addr2line_path)
{
    if (addr2line_path != NULL && addr2line_path[0] != '\0') {
        strncpy(g_addr2line_path, addr2line_path, sizeof(g_addr2line_path) - 1U);
        g_addr2line_path[sizeof(g_addr2line_path) - 1U] = '\0';
    }
}

void crashvault_symbolize_init_default(void)
{
#ifdef CRASHVAULT_ADDR2LINE_PATH
    crashvault_symbolize_init(CRASHVAULT_ADDR2LINE_PATH);
#else
    crashvault_symbolize_init("/usr/bin/addr2line");
#endif
}

static void crashvault_symbolize_reset(CrashVaultSymbolResult *out, uint64_t runtime_address,
                                       uint64_t normalized_address)
{
    memset(out, 0, sizeof(*out));
    out->status = CRASHVAULT_SYMBOL_NOT_FOUND;
    out->raw_address = runtime_address;
    out->normalized_address = normalized_address;
    strncpy(out->function, "??", sizeof(out->function) - 1U);
}

const char *crashvault_symbol_status_string(CrashVaultSymbolStatus status)
{
    switch (status) {
    case CRASHVAULT_SYMBOL_OK:
        return "ok";
    case CRASHVAULT_SYMBOL_NOT_FOUND:
        return "not_found";
    case CRASHVAULT_SYMBOL_TOOL_ERROR:
        return "tool_error";
    case CRASHVAULT_SYMBOL_NO_EXECUTABLE:
        return "no_executable";
    case CRASHVAULT_SYMBOL_INVALID_ADDRESS:
        return "invalid_address";
    default:
        return "unknown";
    }
}

int crashvault_symbolize_address(const char *executable_path, uint64_t executable_base,
                                 uint64_t runtime_address, CrashVaultSymbolResult *out)
{
    char command[PATH_MAX + 128];
    char line1[CRASHVAULT_SYMBOL_FUNCTION_MAX];
    char line2[CRASHVAULT_SYMBOL_SOURCE_MAX];
    uint64_t normalized;
    FILE *fp;

    if (out == NULL) {
        return -1;
    }

    normalized = runtime_address;
    if (executable_base != 0U && runtime_address >= executable_base) {
        normalized = runtime_address - executable_base;
    }

    crashvault_symbolize_reset(out, runtime_address, normalized);

    if (executable_path == NULL || executable_path[0] == '\0') {
        out->status = CRASHVAULT_SYMBOL_NO_EXECUTABLE;
        return -1;
    }

    if (runtime_address == 0U) {
        out->status = CRASHVAULT_SYMBOL_INVALID_ADDRESS;
        return -1;
    }

    strncpy(out->module, executable_path, sizeof(out->module) - 1U);

    if (snprintf(command, sizeof(command), "%s -e %s -f -C 0x%llx", g_addr2line_path,
                 executable_path, (unsigned long long)normalized) >= (int)sizeof(command)) {
        out->status = CRASHVAULT_SYMBOL_TOOL_ERROR;
        return -1;
    }

    fp = popen(command, "r");
    if (fp == NULL) {
        out->status = CRASHVAULT_SYMBOL_TOOL_ERROR;
        return -1;
    }

    if (fgets(line1, (int)sizeof(line1), fp) == NULL) {
        pclose(fp);
        out->status = CRASHVAULT_SYMBOL_TOOL_ERROR;
        return -1;
    }

    if (fgets(line2, (int)sizeof(line2), fp) == NULL) {
        pclose(fp);
        out->status = CRASHVAULT_SYMBOL_TOOL_ERROR;
        return -1;
    }

    pclose(fp);

    {
        size_t len = strlen(line1);
        if (len > 0U && line1[len - 1U] == '\n') {
            line1[len - 1U] = '\0';
        }
    }
    {
        size_t len = strlen(line2);
        if (len > 0U && line2[len - 1U] == '\n') {
            line2[len - 1U] = '\0';
        }
    }

    if ((strcmp(line1, "??") == 0 || strcmp(line2, "??:0") == 0 || strcmp(line2, "??:?") == 0) &&
        executable_base != 0U && normalized != runtime_address) {
        if (snprintf(command, sizeof(command), "%s -e %s -f -C 0x%llx", g_addr2line_path,
                     executable_path, (unsigned long long)runtime_address) >= (int)sizeof(command)) {
            out->status = CRASHVAULT_SYMBOL_TOOL_ERROR;
            return -1;
        }
        fp = popen(command, "r");
        if (fp != NULL) {
            if (fgets(line1, (int)sizeof(line1), fp) != NULL &&
                fgets(line2, (int)sizeof(line2), fp) != NULL) {
                size_t len1 = strlen(line1);
                size_t len2 = strlen(line2);
                if (len1 > 0U && line1[len1 - 1U] == '\n') {
                    line1[len1 - 1U] = '\0';
                }
                if (len2 > 0U && line2[len2 - 1U] == '\n') {
                    line2[len2 - 1U] = '\0';
                }
                out->normalized_address = runtime_address;
            }
            pclose(fp);
        }
    }

    if (strcmp(line1, "??") == 0 || strcmp(line2, "??:0") == 0 || strcmp(line2, "??:?") == 0) {
        out->status = CRASHVAULT_SYMBOL_NOT_FOUND;
        return 0;
    }

    strncpy(out->function, line1, sizeof(out->function) - 1U);

    {
        char *colon = strrchr(line2, ':');
        if (colon != NULL) {
            *colon = '\0';
            strncpy(out->source_file, line2, sizeof(out->source_file) - 1U);
            out->source_line = atoi(colon + 1);
        } else {
            strncpy(out->source_file, line2, sizeof(out->source_file) - 1U);
            out->source_line = 0;
        }
    }

    out->status = CRASHVAULT_SYMBOL_OK;
    return 0;
}
