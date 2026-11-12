#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "ipc.h"
#include "logger.h"

#define SLOTS 4
#define TOTAL_MSGS 8
#define WAIT_SECONDS 5

typedef struct {
    int seq;
    pid_t sender;
    char text[64];
} ipc_msg_t;

typedef struct {
    sem_t space;
    int in;
    int out;
    int total;
    ipc_msg_t slots[SLOTS];
} ipc_shared_t;

static int sem_wait_timeout(sem_t *s, int seconds)
{
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += seconds;
    while (sem_timedwait(s, &ts) != 0) {
        if (errno == EINTR)
            continue;
        return -1;
    }
    return 0;
}

static int producer_process(void)
{
    int fd;
    ipc_shared_t *sh;
    sem_t *items;

    fd = shm_open(SHM_NAME, O_RDWR, 0600);
    if (fd < 0) {
        perror("producer: shm_open");
        return 1;
    }
    sh = mmap(NULL, sizeof *sh, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (sh == MAP_FAILED) {
        perror("producer: mmap");
        return 1;
    }
    items = sem_open(SEM_NAME, 0);
    if (items == SEM_FAILED) {
        perror("producer: sem_open");
        munmap(sh, sizeof *sh);
        return 1;
    }

    for (int i = 1; i <= sh->total; i++) {
        ipc_msg_t *m;
        int slot;

        if (sem_trywait(&sh->space) != 0) {
            printf("[producer pid %d] all %d slots full, waiting on semaphore\n", getpid(), SLOTS);
            fflush(stdout);
            if (sem_wait_timeout(&sh->space, WAIT_SECONDS) != 0) {
                perror("producer: waiting for a free slot");
                sem_close(items);
                munmap(sh, sizeof *sh);
                return 1;
            }
        }
        slot = sh->in;
        m = &sh->slots[slot];
        m->seq = i;
        m->sender = getpid();
        snprintf(m->text, sizeof m->text, "job %d submitted to CampusCompute", i);
        sh->in = (sh->in + 1) % SLOTS;
        printf("[producer pid %d] wrote message %d into slot %d\n", getpid(), i, slot);
        fflush(stdout);
        sem_post(items);
        usleep(100000);
    }
    sem_close(items);
    munmap(sh, sizeof *sh);
    return 0;
}

static void verify_released(void)
{
    int fd;
    int err;
    sem_t *s;

    fd = shm_open(SHM_NAME, O_RDONLY, 0);
    err = errno;
    if (fd >= 0) {
        close(fd);
        printf("verify: %s still exists\n", SHM_NAME);
    } else if (err == ENOENT) {
        printf("verify: %s no longer exists\n", SHM_NAME);
    } else {
        printf("verify: %s check failed: %s\n", SHM_NAME, strerror(err));
    }

    s = sem_open(SEM_NAME, 0);
    err = errno;
    if (s != SEM_FAILED) {
        sem_close(s);
        printf("verify: %s still exists\n", SEM_NAME);
    } else if (err == ENOENT) {
        printf("verify: %s no longer exists\n", SEM_NAME);
    } else {
        printf("verify: %s check failed: %s\n", SEM_NAME, strerror(err));
    }
}

int ipc_run_demo(void)
{
    int fd = -1;
    int shm_created = 0;
    int sem_created = 0;
    int space_ready = 0;
    int rc = -1;
    int received = 0;
    int failed = 0;
    int status = 0;
    pid_t child = -1;
    ipc_shared_t *sh = MAP_FAILED;
    sem_t *items = SEM_FAILED;

    fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        if (errno == EEXIST)
            fprintf(stderr, "ipc: %s already exists (leftover from an earlier run?). Run: ./campuscompute ipc cleanup\n", SHM_NAME);
        else
            perror("ipc: shm_open");
        return -1;
    }
    shm_created = 1;
    if (ftruncate(fd, sizeof *sh) != 0) {
        perror("ipc: ftruncate");
        goto done;
    }
    sh = mmap(NULL, sizeof *sh, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    fd = -1;
    if (sh == MAP_FAILED) {
        perror("ipc: mmap");
        goto done;
    }
    memset(sh, 0, sizeof *sh);
    sh->total = TOTAL_MSGS;
    if (sem_init(&sh->space, 1, SLOTS) != 0) {
        perror("ipc: sem_init");
        goto done;
    }
    space_ready = 1;
    items = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0600, 0);
    if (items == SEM_FAILED) {
        if (errno == EEXIST)
            fprintf(stderr, "ipc: %s already exists (leftover from an earlier run?). Run: ./campuscompute ipc cleanup\n", SEM_NAME);
        else
            perror("ipc: sem_open");
        goto done;
    }
    sem_created = 1;

    printf("ipc demo: created shared memory %s (%zu bytes) and named semaphore %s\n", SHM_NAME, sizeof *sh, SEM_NAME);
    printf("ipc demo: ring buffer of %d slots, %d messages, consumer pid %d\n", SLOTS, TOTAL_MSGS, getpid());
    fflush(stdout);
    logger_log("ipc", "created %s and %s", SHM_NAME, SEM_NAME);

    child = fork();
    if (child < 0) {
        perror("ipc: fork");
        goto done;
    }
    if (child == 0)
        _exit(producer_process());

    for (int i = 1; i <= TOTAL_MSGS; i++) {
        ipc_msg_t m;
        int slot;

        if (sem_wait_timeout(items, WAIT_SECONDS) != 0) {
            fprintf(stderr, "ipc: consumer timed out waiting for data\n");
            failed = 1;
            break;
        }
        slot = sh->out;
        m = sh->slots[slot];
        sh->out = (sh->out + 1) % SLOTS;
        sem_post(&sh->space);
        printf("[consumer pid %d] read message %d from slot %d: \"%s\" (sender pid %d)\n", getpid(), m.seq, slot, m.text, m.sender);
        fflush(stdout);
        logger_log("ipc", "consumer read message %d from pid %d", m.seq, m.sender);
        if (m.seq != i) {
            fprintf(stderr, "ipc: out-of-order message: expected %d, got %d\n", i, m.seq);
            failed = 1;
            break;
        }
        received++;
        usleep(250000);
    }

    if (failed)
        kill(child, SIGTERM);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR)
        ;
    if (WIFEXITED(status))
        printf("ipc demo: producer exited with status %d\n", WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        printf("ipc demo: producer terminated by signal %d\n", WTERMSIG(status));
    if (!failed && received == TOTAL_MSGS && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        printf("ipc demo: all %d messages received in order\n", received);
        rc = 0;
    }

done:
    if (items != SEM_FAILED)
        sem_close(items);
    if (sem_created && sem_unlink(SEM_NAME) == 0)
        printf("cleanup: named semaphore %s unlinked\n", SEM_NAME);
    if (space_ready)
        sem_destroy(&sh->space);
    if (sh != MAP_FAILED)
        munmap(sh, sizeof *sh);
    if (fd >= 0)
        close(fd);
    if (shm_created && shm_unlink(SHM_NAME) == 0)
        printf("cleanup: shared memory %s unlinked\n", SHM_NAME);
    if (shm_created || sem_created)
        verify_released();
    return rc;
}

int ipc_cleanup(void)
{
    int rc = 0;

    if (shm_unlink(SHM_NAME) == 0) {
        printf("removed %s\n", SHM_NAME);
    } else if (errno == ENOENT) {
        printf("%s not present, nothing to remove\n", SHM_NAME);
    } else {
        perror("ipc cleanup: shm_unlink");
        rc = -1;
    }
    if (sem_unlink(SEM_NAME) == 0) {
        printf("removed %s\n", SEM_NAME);
    } else if (errno == ENOENT) {
        printf("%s not present, nothing to remove\n", SEM_NAME);
    } else {
        perror("ipc cleanup: sem_unlink");
        rc = -1;
    }
    return rc;
}

int ipc_cmd(int argc, char **argv)
{
    if (argc == 0)
        return ipc_run_demo() == 0 ? 0 : 1;
    if (argc == 1 && strcmp(argv[0], "cleanup") == 0)
        return ipc_cleanup() == 0 ? 0 : 1;
    fprintf(stderr, "usage: ipc [cleanup]\n");
    return 1;
}
