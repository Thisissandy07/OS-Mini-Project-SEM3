#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "logger.h"

#define ENTRY_LEN 256
#define MAX_QUEUE 64
#define MAX_PRODUCERS 8
#define MAX_ITEMS 50
#define DEMO_CAPACITY 4

typedef struct {
    char entries[MAX_QUEUE][ENTRY_LEN];
    int head;
    int tail;
    int count;
    int capacity;
    int running;
    int stopping;
    int verbose;
    unsigned long written;
    FILE *fp;
    pthread_t consumer;
    pthread_mutex_t lock;
    pthread_cond_t not_full;
    pthread_cond_t not_empty;
} logger_state_t;

static logger_state_t L = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .not_full = PTHREAD_COND_INITIALIZER,
    .not_empty = PTHREAD_COND_INITIALIZER,
};

typedef struct {
    int id;
    int items;
} producer_arg_t;

static void ensure_parent_dir(const char *path)
{
    char dir[256];
    const char *slash = strrchr(path, '/');
    size_t len;

    if (slash == NULL || slash == path)
        return;
    len = (size_t)(slash - path);
    if (len >= sizeof dir)
        return;
    memcpy(dir, path, len);
    dir[len] = '\0';
    mkdir(dir, 0755);
}

static void *consumer_main(void *arg)
{
    (void)arg;
    for (;;) {
        char line[ENTRY_LEN];
        int depth;
        int verbose;

        pthread_mutex_lock(&L.lock);
        while (L.count == 0 && !L.stopping)
            pthread_cond_wait(&L.not_empty, &L.lock);
        if (L.count == 0 && L.stopping) {
            pthread_mutex_unlock(&L.lock);
            break;
        }
        snprintf(line, sizeof line, "%s", L.entries[L.head]);
        L.head = (L.head + 1) % L.capacity;
        L.count--;
        depth = L.count;
        verbose = L.verbose;
        pthread_cond_signal(&L.not_full);
        pthread_mutex_unlock(&L.lock);

        fputs(line, L.fp);
        fputc('\n', L.fp);
        fflush(L.fp);
        L.written++;
        if (verbose) {
            printf("[consumer] wrote entry #%lu to log (queue depth now %d)\n", L.written, depth);
            fflush(stdout);
            usleep(60000);
        }
    }
    return NULL;
}

int logger_start(const char *path, int capacity)
{
    int rc;

    if (path == NULL || capacity < 1 || capacity > MAX_QUEUE) {
        fprintf(stderr, "logger: invalid start arguments\n");
        return -1;
    }
    pthread_mutex_lock(&L.lock);
    if (L.running) {
        pthread_mutex_unlock(&L.lock);
        fprintf(stderr, "logger: already running\n");
        return -1;
    }
    ensure_parent_dir(path);
    L.fp = fopen(path, "w");
    if (L.fp == NULL) {
        pthread_mutex_unlock(&L.lock);
        fprintf(stderr, "logger: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    L.head = 0;
    L.tail = 0;
    L.count = 0;
    L.capacity = capacity;
    L.stopping = 0;
    L.written = 0;
    rc = pthread_create(&L.consumer, NULL, consumer_main, NULL);
    if (rc != 0) {
        fclose(L.fp);
        L.fp = NULL;
        pthread_mutex_unlock(&L.lock);
        fprintf(stderr, "logger: pthread_create failed: %s\n", strerror(rc));
        return -1;
    }
    L.running = 1;
    pthread_mutex_unlock(&L.lock);
    return 0;
}

int logger_log(const char *source, const char *fmt, ...)
{
    char msg[160];
    char stamp[32];
    char line[ENTRY_LEN];
    struct tm tmv;
    time_t now = time(NULL);
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    localtime_r(&now, &tmv);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tmv);
    snprintf(line, sizeof line, "%s [%.24s] %s", stamp, source, msg);

    pthread_mutex_lock(&L.lock);
    if (!L.running || L.stopping) {
        pthread_mutex_unlock(&L.lock);
        return 0;
    }
    if (L.count == L.capacity && L.verbose) {
        printf("[producer] queue full (%d/%d), %.24s must wait\n", L.count, L.capacity, source);
        fflush(stdout);
    }
    while (L.count == L.capacity && !L.stopping)
        pthread_cond_wait(&L.not_full, &L.lock);
    if (L.stopping) {
        pthread_mutex_unlock(&L.lock);
        return -1;
    }
    snprintf(L.entries[L.tail], ENTRY_LEN, "%s", line);
    L.tail = (L.tail + 1) % L.capacity;
    L.count++;
    if (L.verbose) {
        printf("[producer] %.24s enqueued: %s (queue depth %d/%d)\n", source, msg, L.count, L.capacity);
        fflush(stdout);
    }
    pthread_cond_signal(&L.not_empty);
    pthread_mutex_unlock(&L.lock);
    return 0;
}

int logger_stop(void)
{
    pthread_mutex_lock(&L.lock);
    if (!L.running) {
        pthread_mutex_unlock(&L.lock);
        return 0;
    }
    L.stopping = 1;
    pthread_cond_broadcast(&L.not_empty);
    pthread_cond_broadcast(&L.not_full);
    pthread_mutex_unlock(&L.lock);

    pthread_join(L.consumer, NULL);

    pthread_mutex_lock(&L.lock);
    fclose(L.fp);
    L.fp = NULL;
    L.running = 0;
    L.stopping = 0;
    pthread_mutex_unlock(&L.lock);
    return 0;
}

int logger_is_running(void)
{
    int r;

    pthread_mutex_lock(&L.lock);
    r = L.running;
    pthread_mutex_unlock(&L.lock);
    return r;
}

void logger_set_verbose(int on)
{
    pthread_mutex_lock(&L.lock);
    L.verbose = on;
    pthread_mutex_unlock(&L.lock);
}

unsigned long logger_entries_written(void)
{
    return L.written;
}

static void *producer_main(void *arg)
{
    producer_arg_t *a = arg;
    char name[24];

    snprintf(name, sizeof name, "producer-%d", a->id);
    for (int i = 1; i <= a->items; i++)
        logger_log(name, "event %d of %d from lab user %d", i, a->items, a->id);
    return NULL;
}

static int parse_int(const char *s, int lo, int hi, int *out)
{
    char *end;
    long v;

    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < lo || v > hi)
        return -1;
    *out = (int)v;
    return 0;
}

int logger_cmd(int argc, char **argv)
{
    int producers = 3;
    int items = 4;
    int own;
    int created = 0;
    pthread_t tids[MAX_PRODUCERS];
    producer_arg_t args[MAX_PRODUCERS];

    if (argc > 2) {
        fprintf(stderr, "usage: logger [producers 1-%d] [items_each 1-%d]\n", MAX_PRODUCERS, MAX_ITEMS);
        return 1;
    }
    if (argc >= 1 && parse_int(argv[0], 1, MAX_PRODUCERS, &producers) != 0) {
        fprintf(stderr, "error: producers must be an integer from 1 to %d\n", MAX_PRODUCERS);
        return 1;
    }
    if (argc == 2 && parse_int(argv[1], 1, MAX_ITEMS, &items) != 0) {
        fprintf(stderr, "error: items_each must be an integer from 1 to %d\n", MAX_ITEMS);
        return 1;
    }

    own = !logger_is_running();
    if (own && logger_start(LOG_FILE, DEMO_CAPACITY) != 0)
        return 1;

    printf("logger demo: %d producer threads x %d events, queue capacity %d, log file %s\n",
           producers, items, DEMO_CAPACITY, LOG_FILE);
    fflush(stdout);
    logger_set_verbose(1);

    for (int i = 0; i < producers; i++) {
        int rc;

        args[i].id = i + 1;
        args[i].items = items;
        rc = pthread_create(&tids[i], NULL, producer_main, &args[i]);
        if (rc != 0) {
            fprintf(stderr, "logger: pthread_create failed: %s\n", strerror(rc));
            break;
        }
        created++;
    }
    for (int i = 0; i < created; i++)
        pthread_join(tids[i], NULL);
    printf("all %d producer threads joined\n", created);

    logger_set_verbose(0);
    if (own) {
        logger_stop();
        printf("consumer thread joined, log file closed\n");
        printf("entries expected: %d, entries written: %lu\n", created * items, logger_entries_written());
    }
    return created == producers ? 0 : 1;
}
