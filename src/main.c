#include <errno.h>
#include <sys/wait.h>
#include "common.h"
#include "scheduler.h"
#include "banker.h"
#include "logger.h"
#include "ipc.h"
#include "worker.h"
#include "rlimits.h"

#define JOBS_FILE "tests/jobs.txt"
#define BANKER_SAFE_FILE "tests/banker_safe.txt"
#define BANKER_UNSAFE_FILE "tests/banker_unsafe.txt"
#define DEMO_LOG_CAPACITY 16
#define TAIL_LINES 5
#define NUM_STAGES 8

typedef struct {
    const char *name;
    cmd_fn fn;
    const char *help;
} command_t;

static int help_cmd(int argc, char **argv);
static int demo_cmd(int argc, char **argv);

static const command_t commands[] = {
    { "sched",  sched_cmd,  "sched <fcfs|rr|priority|compare> [jobs_file] [quantum]   CPU scheduling" },
    { "banker", banker_cmd, "banker [state_file]                                     Banker's safety check" },
    { "logger", logger_cmd, "logger [producers 1-8] [items_each 1-50]                Threaded logger demo" },
    { "ipc",    ipc_cmd,    "ipc [cleanup]                                           Shared memory + semaphore" },
    { "worker", worker_cmd, "worker <normal|fail>                                    fork/waitpid monitoring" },
    { "limit",  limit_cmd,  "limit                                                   setrlimit demonstration" },
    { "demo",   demo_cmd,   "demo                                                    Integrated run of everything" },
    { "help",   help_cmd,   "help                                                    Show this message" },
};

#define NUM_COMMANDS (sizeof(commands) / sizeof(commands[0]))

static void print_usage(FILE *out)
{
    fprintf(out, "CampusCompute - job scheduling and resource management prototype\n");
    fprintf(out, "usage: ./campuscompute <command> [arguments]\n\ncommands:\n");
    for (size_t i = 0; i < NUM_COMMANDS; i++)
        fprintf(out, "  %s\n", commands[i].help);
    fprintf(out, "\nrun from the project directory so tests/ and logs/ resolve correctly.\n");
}

static int help_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    print_usage(stdout);
    return 0;
}

static void banner(const char *title)
{
    printf("\n########## %s ##########\n", title);
    fflush(stdout);
}

static void print_log_tail(const char *path)
{
    char ring[TAIL_LINES][256];
    char line[256];
    FILE *fp = fopen(path, "r");
    int total = 0;

    if (fp == NULL) {
        printf("(log file %s could not be read)\n", path);
        return;
    }
    while (fgets(line, sizeof line, fp) != NULL)
        snprintf(ring[total++ % TAIL_LINES], sizeof ring[0], "%s", line);
    fclose(fp);
    printf("Last %d of %d log lines:\n", total < TAIL_LINES ? total : TAIL_LINES, total);
    for (int k = total < TAIL_LINES ? 0 : total - TAIL_LINES; k < total; k++)
        printf("  %s", ring[k % TAIL_LINES]);
}

static int demo_cmd(int argc, char **argv)
{
    static const char *names[NUM_STAGES] = {
        "CPU scheduling: FCFS, Round Robin, Priority and comparison",
        "Banker's algorithm on a safe state",
        "Banker's algorithm on an unsafe state",
        "Threaded producer-consumer logger",
        "Shared-memory IPC with named semaphore",
        "Worker process, normal completion",
        "Worker process, simulated failures",
        "Resource limits with setrlimit",
    };
    int ok[NUM_STAGES] = { 0 };
    char *sched_args[] = { "compare", JOBS_FILE, "2" };
    char *logger_args[] = { "3", "3" };
    int safe = -1;
    int passed = 0;
    int status;
    int children_left;
    pid_t reaped;

    (void)argc;
    (void)argv;
    if (logger_start(LOG_FILE, DEMO_LOG_CAPACITY) != 0) {
        fprintf(stderr, "error: could not start the logger\n");
        return 1;
    }
    logger_log("demo", "integrated demonstration started");
    printf("CampusCompute integrated demonstration (8 stages)\n");

    banner("Stage 1/8: CPU scheduling");
    {
        char *a1[] = { "fcfs", JOBS_FILE };
        char *a2[] = { "rr", JOBS_FILE, "2" };
        char *a3[] = { "priority", JOBS_FILE };

        ok[0] = sched_cmd(2, a1) == 0 && sched_cmd(3, a2) == 0 && sched_cmd(2, a3) == 0 &&
                sched_cmd(3, sched_args) == 0;
    }

    banner("Stage 2/8: Banker's algorithm, safe state");
    ok[1] = banker_analyze_file(BANKER_SAFE_FILE, &safe) == 0 && safe == 1;

    banner("Stage 3/8: Banker's algorithm, unsafe state");
    safe = -1;
    ok[2] = banker_analyze_file(BANKER_UNSAFE_FILE, &safe) == 0 && safe == 0;

    banner("Stage 4/8: Threaded producer-consumer logger");
    ok[3] = logger_cmd(2, logger_args) == 0;

    banner("Stage 5/8: Shared-memory IPC");
    ok[4] = ipc_run_demo() == 0;

    banner("Stage 6/8: Worker processes (normal)");
    ok[5] = worker_run(0) == 0;

    banner("Stage 7/8: Worker processes (simulated failures)");
    ok[6] = worker_run(1) == 0;

    banner("Stage 8/8: Resource limits");
    ok[7] = limit_run_demo() == 0;

    logger_log("demo", "integrated demonstration finished");
    logger_stop();

    reaped = waitpid(-1, &status, WNOHANG);
    children_left = (reaped >= 0) ? 1 : 0;
    if (reaped < 0 && errno != ECHILD)
        children_left = 1;

    banner("SUMMARY");
    for (int i = 0; i < NUM_STAGES; i++) {
        printf(" %d. %-60s %s\n", i + 1, names[i], ok[i] ? "PASS" : "FAIL");
        passed += ok[i];
    }
    printf("\n Log file                          : %s (%lu entries written)\n", LOG_FILE, logger_entries_written());
    printf(" Logger thread still running       : %s\n", logger_is_running() ? "yes" : "no");
    printf(" Child processes still alive       : %s\n", children_left ? "yes" : "none");
    printf(" Program IPC objects remaining     : %d\n\n", ipc_resources_present());
    print_log_tail(LOG_FILE);
    printf("\n Result: %d of %d stages passed\n", passed, NUM_STAGES);
    return (passed == NUM_STAGES && !children_left && ipc_resources_present() == 0) ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "error: no command given\n");
        print_usage(stderr);
        return 1;
    }
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
        return help_cmd(0, NULL);
    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(argv[1], commands[i].name) == 0)
            return commands[i].fn(argc - 2, argv + 2);
    }
    fprintf(stderr, "error: unknown command '%s'\n", argv[1]);
    print_usage(stderr);
    return 1;
}
