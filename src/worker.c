#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include "worker.h"
#include "logger.h"

#define NUM_WORKERS 3

static void do_work(int id)
{
    volatile unsigned long sum = 0;

    for (unsigned long i = 1; i <= 2000000UL * (unsigned long)id; i++)
        sum += i;
    usleep(100000U * (unsigned)id);
}

static void worker_body(int id, int simulate_failure)
{
    printf("[worker %d pid %d] started\n", id, getpid());
    fflush(stdout);
    do_work(id);

    if (simulate_failure && id == 2) {
        printf("[worker %d pid %d] SIMULATED failure: exiting with status 42\n", id, getpid());
        fflush(stdout);
        _exit(42);
    }
    if (simulate_failure && id == 3) {
        printf("[worker %d pid %d] SIMULATED failure: terminating itself with SIGUSR1\n", id, getpid());
        fflush(stdout);
        raise(SIGUSR1);
        _exit(99);
    }
    printf("[worker %d pid %d] finished normally\n", id, getpid());
    fflush(stdout);
    _exit(0);
}

int worker_run(int simulate_failure)
{
    pid_t pids[NUM_WORKERS] = {0};
    int started = 0;
    int reaped = 0;
    int ok = 0;
    int failed = 0;
    int status;
    const char *tag = simulate_failure ? " (simulated)" : "";

    printf("[parent pid %d] starting %d workers%s\n", getpid(), NUM_WORKERS,
           simulate_failure ? ", workers 2 and 3 will SIMULATE failures" : "");
    fflush(stdout);

    for (int i = 0; i < NUM_WORKERS; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("worker: fork");
            break;
        }
        if (pid == 0)
            worker_body(i + 1, simulate_failure);
        pids[i] = pid;
        started++;
        logger_log("worker", "started worker %d as pid %d", i + 1, (int)pid);
    }

    while (reaped < started) {
        pid_t pid = waitpid(-1, &status, 0);
        int id = 0;

        if (pid < 0) {
            if (errno == EINTR)
                continue;
            perror("worker: waitpid");
            break;
        }
        reaped++;
        for (int j = 0; j < NUM_WORKERS; j++)
            if (pids[j] == pid)
                id = j + 1;

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("[parent] worker %d (pid %d) completed normally, exit status 0\n", id, (int)pid);
            logger_log("worker", "worker %d pid %d completed normally", id, (int)pid);
            ok++;
        } else if (WIFEXITED(status)) {
            printf("[parent] worker %d (pid %d) FAILED%s: exit status %d\n", id, (int)pid, tag, WEXITSTATUS(status));
            logger_log("worker", "worker %d pid %d failed%s with exit status %d", id, (int)pid, tag, WEXITSTATUS(status));
            failed++;
        } else if (WIFSIGNALED(status)) {
            printf("[parent] worker %d (pid %d) FAILED%s: killed by signal %d (%s)\n", id, (int)pid, tag, WTERMSIG(status), strsignal(WTERMSIG(status)));
            logger_log("worker", "worker %d pid %d failed%s, signal %d", id, (int)pid, tag, WTERMSIG(status));
            failed++;
        }
    }

    printf("[parent] summary: %d started, %d completed normally, %d failed\n", started, ok, failed);
    if (waitpid(-1, &status, WNOHANG) < 0 && errno == ECHILD)
        printf("[parent] no child processes remaining, all reaped\n");
    return started == NUM_WORKERS ? 0 : -1;
}

int worker_cmd(int argc, char **argv)
{
    if (argc == 1 && strcmp(argv[0], "normal") == 0)
        return worker_run(0) == 0 ? 0 : 1;
    if (argc == 1 && strcmp(argv[0], "fail") == 0)
        return worker_run(1) == 0 ? 0 : 1;
    fprintf(stderr, "usage: worker <normal|fail>\n");
    return 1;
}
