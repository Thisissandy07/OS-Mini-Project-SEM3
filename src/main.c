#include "common.h"
#include "scheduler.h"
#include "banker.h"
#include "logger.h"
#include "ipc.h"
#include "worker.h"
#include "rlimits.h"

typedef struct {
    const char *name;
    cmd_fn fn;
    const char *help;
} command_t;

static int help_cmd(int argc, char **argv);
static int demo_cmd(int argc, char **argv);

static const command_t commands[] = {
    {"sched",  sched_cmd,  "sched <fcfs|rr|priority|compare> <jobs_file> [quantum]"},
    {"banker", banker_cmd, "banker <state_file>"},
    {"logger", logger_cmd, "logger [producers] [items_each]"},
    {"ipc",    ipc_cmd,    "ipc"},
    {"worker", worker_cmd, "worker <normal|fail>"},
    {"limit",  limit_cmd,  "limit"},
    {"demo",   demo_cmd,   "demo (runs everything)"},
    {"help",   help_cmd,   "help"},
};

#define NUM_COMMANDS (sizeof(commands) / sizeof(commands[0]))

static int help_cmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("CampusCompute commands:");
    for (size_t i = 0; i < NUM_COMMANDS; i++)
        printf("  %s\n", commands[i].help);
    return 0;
}

static int demo_cmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("[stub] demo not implemented");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "error: no command given\n");
        help_cmd(0, NULL);
        return 1;
    }
    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(argv[1], commands[i].name) == 0)
            return commands[i].fn(argc - 2, argv + 2);
    }
    fprintf(stderr, "error: unknown command '%s'\n", argv[1]);
    help_cmd(0, NULL);
    return 1;
}
