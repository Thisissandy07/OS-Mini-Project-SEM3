#include <errno.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "rlimits.h"
#include "logger.h"

#define MAX_FDS 1024
#define NOFILE_SOFT 16
#define AS_LIMIT_BYTES (128UL * 1024UL * 1024UL)
#define SMALL_BYTES (8UL * 1024UL * 1024UL)
#define BIG_BYTES (512UL * 1024UL * 1024UL)

static const char *fmt_limit(rlim_t v, char *buf, size_t n)
{
    if (v == RLIM_INFINITY)
        snprintf(buf, n, "unlimited");
    else
        snprintf(buf, n, "%lu", (unsigned long)v);
    return buf;
}

static int demo_open_files(void)
{
    struct rlimit rl;
    int fds[MAX_FDS];
    int opened = 0;
    int err = 0;
    char a[32];
    char b[32];

    if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
        perror("limit: getrlimit(RLIMIT_NOFILE)");
        return -1;
    }
    printf("RLIMIT_NOFILE before: soft=%s hard=%s\n", fmt_limit(rl.rlim_cur, a, sizeof a), fmt_limit(rl.rlim_max, b, sizeof b));
    if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < NOFILE_SOFT) {
        fprintf(stderr, "limit: hard limit too low for this demo\n");
        return -1;
    }
    rl.rlim_cur = NOFILE_SOFT;
    if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
        perror("limit: setrlimit(RLIMIT_NOFILE)");
        return -1;
    }
    printf("RLIMIT_NOFILE applied: soft=%d\n", NOFILE_SOFT);

    while (opened < MAX_FDS) {
        int fd = open("/dev/null", O_RDONLY);

        if (fd < 0) {
            err = errno;
            break;
        }
        fds[opened++] = fd;
    }
    for (int i = 0; i < opened; i++)
        close(fds[i]);
    if (err == 0) {
        printf("observed effect: none, opened %d files without failure\n", opened);
        return -1;
    }
    printf("observed effect: opened %d extra files, then open() failed with \"%s\" (errno %d)\n", opened, strerror(err), err);
    return 0;
}

static int demo_address_space(void)
{
    struct rlimit rl;
    void *volatile small;
    void *volatile big;
    int err;
    char a[32];
    char b[32];

    if (getrlimit(RLIMIT_AS, &rl) != 0) {
        perror("limit: getrlimit(RLIMIT_AS)");
        return -1;
    }
    printf("RLIMIT_AS before: soft=%s hard=%s\n", fmt_limit(rl.rlim_cur, a, sizeof a), fmt_limit(rl.rlim_max, b, sizeof b));
    if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < AS_LIMIT_BYTES) {
        fprintf(stderr, "limit: hard limit too low for this demo\n");
        return -1;
    }
    rl.rlim_cur = AS_LIMIT_BYTES;
    if (setrlimit(RLIMIT_AS, &rl) != 0) {
        perror("limit: setrlimit(RLIMIT_AS)");
        return -1;
    }
    printf("RLIMIT_AS applied: soft=%lu bytes (128 MB)\n", AS_LIMIT_BYTES);

    small = malloc(SMALL_BYTES);
    if (small != NULL) {
        printf("observed effect: malloc(8 MB) succeeded, within the limit\n");
        free(small);
    } else {
        printf("observed effect: malloc(8 MB) failed unexpectedly\n");
    }
    big = malloc(BIG_BYTES);
    err = errno;
    if (big == NULL) {
        printf("observed effect: malloc(512 MB) failed with \"%s\" (errno %d)\n", strerror(err), err);
        return 0;
    }
    printf("observed effect: malloc(512 MB) succeeded, limit had no effect\n");
    free(big);
    return -1;
}

int limit_run_demo(void)
{
    pid_t pid;
    int status = 0;
    struct rlimit rl;

    printf("limit demo: limits are applied inside a child process so the parent keeps its normal limits\n");
    fflush(stdout);
    pid = fork();
    if (pid < 0) {
        perror("limit: fork");
        return -1;
    }
    if (pid == 0) {
        int r1 = demo_open_files();
        int r2 = demo_address_space();

        fflush(stdout);
        _exit(r1 == 0 && r2 == 0 ? 0 : 1);
    }
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    if (WIFEXITED(status))
        printf("limit demo: child pid %d exited with status %d\n", (int)pid, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        printf("limit demo: child pid %d killed by signal %d\n", (int)pid, WTERMSIG(status));
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0)
        printf("limit demo: parent RLIMIT_NOFILE soft is still %lu (unchanged)\n", (unsigned long)rl.rlim_cur);
    logger_log("limit", "resource limit demo finished");
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

int limit_cmd(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        fprintf(stderr, "usage: limit\n");
        return 1;
    }
    return limit_run_demo() == 0 ? 0 : 1;
}
