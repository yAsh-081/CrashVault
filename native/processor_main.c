#include "processor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s                     Import pending raw reports\n"
            "  %s --import <report>   Import one raw report\n"
            "  %s --list              List crash groups\n"
            "  %s --show <group-id>   Show crash group details\n",
            prog, prog, prog, prog);
}

int main(int argc, char **argv)
{
    CrashVaultProcessorConfig config = {0};
    CrashVaultProcessor *processor;
    CrashVaultImportStats stats;

    const char *addr2line = getenv("CRASHVAULT_ADDR2LINE");
    if (addr2line != NULL && addr2line[0] != '\0') {
        config.addr2line_path = addr2line;
    }

    processor = crashvault_processor_open(&config);
    if (processor == NULL) {
        fprintf(stderr, "Failed to open CrashVault processor.\n");
        return 1;
    }

    if (argc == 1) {
        memset(&stats, 0, sizeof(stats));
        if (crashvault_processor_import_pending(processor, &stats) != 0) {
            fprintf(stderr, "Import failed.\n");
            crashvault_processor_close(processor);
            return 1;
        }

        printf("CrashVault Processor\n\n");
        printf("Scanned:   %zu\n", stats.scanned);
        printf("Imported:  %zu\n", stats.imported);
        printf("Rejected:  %zu\n", stats.rejected);
        printf("Duplicate: %zu\n", stats.duplicate);
        crashvault_processor_close(processor);
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "--list") == 0) {
        int rc = crashvault_processor_list_groups(processor);
        crashvault_processor_close(processor);
        return rc == 0 ? 0 : 1;
    }

    if (argc == 3 && strcmp(argv[1], "--import") == 0) {
        memset(&stats, 0, sizeof(stats));
        if (crashvault_processor_import_file(processor, argv[2], &stats) < 0) {
            fprintf(stderr, "Import failed for %s\n", argv[2]);
            crashvault_processor_close(processor);
            return 1;
        }
        printf("Imported: %zu Rejected: %zu Duplicate: %zu\n", stats.imported, stats.rejected,
               stats.duplicate);
        crashvault_processor_close(processor);
        return 0;
    }

    if (argc == 3 && strcmp(argv[1], "--show") == 0) {
        char *end = NULL;
        long long group_id = strtoll(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0') {
            fprintf(stderr, "Invalid group id: %s\n", argv[2]);
            crashvault_processor_close(processor);
            return 1;
        }
        if (crashvault_processor_show_group(processor, group_id) != 0) {
            fprintf(stderr, "Group %lld not found.\n", group_id);
            crashvault_processor_close(processor);
            return 1;
        }
        crashvault_processor_close(processor);
        return 0;
    }

    print_usage(argv[0]);
    crashvault_processor_close(processor);
    return 1;
}
