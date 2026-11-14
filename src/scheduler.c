#include <ctype.h>
#include <errno.h>
#include "scheduler.h"
#include "logger.h"

#define MAX_ID 9999
#define MAX_ARRIVAL 10000
#define MAX_BURST 1000
#define MAX_PRIORITY 99
#define MAX_QUANTUM 100
#define GANTT_WRAP 10
#define ORDER_WRAP 6
#define DEFAULT_JOBS_FILE "tests/jobs.txt"

typedef enum { POLICY_FCFS, POLICY_RR, POLICY_PRIORITY } policy_t;

static const char *status_name(job_status_t s)
{
    switch (s) {
    case JOB_NEW: return "NEW";
    case JOB_READY: return "READY";
    case JOB_RUNNING: return "RUNNING";
    case JOB_DONE: return "DONE";
    }
    return "UNKNOWN";
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

static const char *validate_reason(const job_t *j)
{
    if (j->id <= 0 || j->id > MAX_ID)
        return "job ID must be between 1 and 9999";
    if (j->arrival < 0 || j->arrival > MAX_ARRIVAL)
        return "arrival time must be between 0 and 10000";
    if (j->burst <= 0 || j->burst > MAX_BURST)
        return "CPU burst must be between 1 and 1000";
    if (j->priority < 1 || j->priority > MAX_PRIORITY)
        return "priority must be between 1 and 99 (1 = highest)";
    return NULL;
}

int job_validate(const job_t *j)
{
    return validate_reason(j) == NULL ? 0 : -1;
}

int jobs_load_file(const char *path, job_t *jobs, int max_jobs)
{
    FILE *fp = fopen(path, "r");
    char line[256];
    int n = 0;
    int lineno = 0;

    if (fp == NULL) {
        fprintf(stderr, "error: cannot open job file '%s': %s\n", path, strerror(errno));
        return -1;
    }
    while (fgets(line, sizeof line, fp) != NULL) {
        job_t j;
        char extra[64];
        char *p = line;
        const char *reason;
        int fields;

        lineno++;
        if (strchr(line, '\n') == NULL && !feof(fp)) {
            fprintf(stderr, "error: %s line %d: line too long\n", path, lineno);
            fclose(fp);
            return -1;
        }
        while (isspace((unsigned char)*p))
            p++;
        if (*p == '\0' || *p == '#')
            continue;

        memset(&j, 0, sizeof j);
        fields = sscanf(p, "%d %d %d %d %63s", &j.id, &j.arrival, &j.burst, &j.priority, extra);
        if (fields != 4) {
            fprintf(stderr, "error: %s line %d: expected 4 integers (id arrival burst priority)\n", path, lineno);
            fclose(fp);
            return -1;
        }
        reason = validate_reason(&j);
        if (reason != NULL) {
            fprintf(stderr, "error: %s line %d: %s\n", path, lineno, reason);
            fclose(fp);
            return -1;
        }
        for (int i = 0; i < n; i++) {
            if (jobs[i].id == j.id) {
                fprintf(stderr, "error: %s line %d: duplicate job ID %d\n", path, lineno, j.id);
                fclose(fp);
                return -1;
            }
        }
        if (n >= max_jobs) {
            fprintf(stderr, "error: %s: too many jobs (maximum %d)\n", path, max_jobs);
            fclose(fp);
            return -1;
        }
        j.status = JOB_NEW;
        j.remaining = j.burst;
        j.start = -1;
        jobs[n++] = j;
    }
    fclose(fp);
    if (n == 0) {
        fprintf(stderr, "error: %s: no jobs found\n", path);
        return -1;
    }
    return n;
}

static void reset_jobs(job_t *jobs, int n)
{
    for (int i = 0; i < n; i++) {
        jobs[i].status = JOB_NEW;
        jobs[i].remaining = jobs[i].burst;
        jobs[i].start = -1;
        jobs[i].completion = 0;
        jobs[i].turnaround = 0;
        jobs[i].waiting = 0;
    }
}

static void set_status(const char *tag, job_t *j, job_status_t to, int t)
{
    logger_log(tag, "t=%d job %d %s -> %s", t, j->id, status_name(j->status), status_name(to));
    j->status = to;
}

static int gantt_add(gantt_slot_t *g, int *len, int job_id, int start, int end)
{
    if (*len > 0 && g[*len - 1].job_id == job_id && g[*len - 1].end == start) {
        g[*len - 1].end = end;
        return 0;
    }
    if (*len >= MAX_SLOTS) {
        fprintf(stderr, "error: timeline too long (more than %d segments)\n", MAX_SLOTS);
        return -1;
    }
    g[*len].job_id = job_id;
    g[*len].start = start;
    g[*len].end = end;
    (*len)++;
    return 0;
}

static void sort_by_arrival(const job_t *jobs, int n, int *order)
{
    for (int i = 0; i < n; i++)
        order[i] = i;
    for (int i = 1; i < n; i++) {
        int key = order[i];
        int k = i - 1;

        while (k >= 0 && (jobs[order[k]].arrival > jobs[key].arrival ||
               (jobs[order[k]].arrival == jobs[key].arrival && jobs[order[k]].id > jobs[key].id))) {
            order[k + 1] = order[k];
            k--;
        }
        order[k + 1] = key;
    }
}

static int admit_arrivals(job_t *jobs, const int *order, int n, int *next, int t, const char *tag, int *out)
{
    int count = 0;

    while (*next < n && jobs[order[*next]].arrival <= t) {
        job_t *j = &jobs[order[*next]];

        set_status(tag, j, JOB_READY, j->arrival);
        out[count++] = order[*next];
        (*next)++;
    }
    return count;
}

static void finish_job(const char *tag, job_t *j, int t)
{
    j->completion = t;
    j->turnaround = t - j->arrival;
    j->waiting = j->turnaround - j->burst;
    set_status(tag, j, JOB_DONE, t);
}

static int higher_precedence(const job_t *a, const job_t *b, int by_priority)
{
    if (by_priority && a->priority != b->priority)
        return a->priority < b->priority;
    if (a->arrival != b->arrival)
        return a->arrival < b->arrival;
    return a->id < b->id;
}

static int run_nonpreemptive(job_t *jobs, int n, gantt_slot_t *g, int *glen, int by_priority, const char *tag)
{
    int order[MAX_JOBS];
    int admitted[MAX_JOBS];
    int next = 0;
    int done = 0;
    int t = 0;

    reset_jobs(jobs, n);
    sort_by_arrival(jobs, n, order);
    *glen = 0;

    while (done < n) {
        int pick = -1;

        admit_arrivals(jobs, order, n, &next, t, tag, admitted);
        for (int i = 0; i < n; i++) {
            if (jobs[i].status != JOB_READY)
                continue;
            if (pick < 0 || higher_precedence(&jobs[i], &jobs[pick], by_priority))
                pick = i;
        }
        if (pick < 0) {
            int wake = jobs[order[next]].arrival;

            if (gantt_add(g, glen, 0, t, wake) != 0)
                return -1;
            t = wake;
            continue;
        }
        set_status(tag, &jobs[pick], JOB_RUNNING, t);
        jobs[pick].start = t;
        if (gantt_add(g, glen, jobs[pick].id, t, t + jobs[pick].burst) != 0)
            return -1;
        t += jobs[pick].burst;
        jobs[pick].remaining = 0;
        finish_job(tag, &jobs[pick], t);
        done++;
    }
    return 0;
}

int sched_fcfs(job_t *jobs, int n, gantt_slot_t *gantt, int *gantt_len)
{
    if (jobs == NULL || gantt == NULL || gantt_len == NULL || n <= 0)
        return -1;
    return run_nonpreemptive(jobs, n, gantt, gantt_len, 0, "sched-fcfs");
}

int sched_priority(job_t *jobs, int n, gantt_slot_t *gantt, int *gantt_len)
{
    if (jobs == NULL || gantt == NULL || gantt_len == NULL || n <= 0)
        return -1;
    return run_nonpreemptive(jobs, n, gantt, gantt_len, 1, "sched-priority");
}

int sched_rr(job_t *jobs, int n, int quantum, gantt_slot_t *gantt, int *gantt_len)
{
    const char *tag = "sched-rr";
    int order[MAX_JOBS];
    int admitted[MAX_JOBS];
    int queue[MAX_JOBS];
    int qhead = 0;
    int qcount = 0;
    int next = 0;
    int done = 0;
    int t = 0;

    if (jobs == NULL || gantt == NULL || gantt_len == NULL || n <= 0 || quantum < 1)
        return -1;
    reset_jobs(jobs, n);
    sort_by_arrival(jobs, n, order);
    *gantt_len = 0;

    while (done < n) {
        int got = admit_arrivals(jobs, order, n, &next, t, tag, admitted);
        int idx;
        int run;

        for (int i = 0; i < got; i++)
            queue[(qhead + qcount++) % MAX_JOBS] = admitted[i];
        if (qcount == 0) {
            int wake = jobs[order[next]].arrival;

            if (gantt_add(gantt, gantt_len, 0, t, wake) != 0)
                return -1;
            t = wake;
            continue;
        }
        idx = queue[qhead];
        qhead = (qhead + 1) % MAX_JOBS;
        qcount--;

        set_status(tag, &jobs[idx], JOB_RUNNING, t);
        if (jobs[idx].start < 0)
            jobs[idx].start = t;
        run = jobs[idx].remaining < quantum ? jobs[idx].remaining : quantum;
        if (gantt_add(gantt, gantt_len, jobs[idx].id, t, t + run) != 0)
            return -1;
        t += run;
        jobs[idx].remaining -= run;

        got = admit_arrivals(jobs, order, n, &next, t, tag, admitted);
        for (int i = 0; i < got; i++)
            queue[(qhead + qcount++) % MAX_JOBS] = admitted[i];

        if (jobs[idx].remaining == 0) {
            finish_job(tag, &jobs[idx], t);
            done++;
        } else {
            set_status(tag, &jobs[idx], JOB_READY, t);
            queue[(qhead + qcount++) % MAX_JOBS] = idx;
        }
    }
    return 0;
}

static void print_order(const gantt_slot_t *g, int len, int indent)
{
    for (int i = 0; i < len; i++) {
        if (i > 0) {
            if (i % ORDER_WRAP == 0)
                printf(" ->\n%*s", indent, "");
            else
                printf(" -> ");
        }
        if (g[i].job_id == 0)
            printf("idle[%d-%d]", g[i].start, g[i].end);
        else
            printf("P%d[%d-%d]", g[i].job_id, g[i].start, g[i].end);
    }
    putchar('\n');
}

static void print_gantt(const gantt_slot_t *g, int len)
{
    for (int base = 0; base < len; base += GANTT_WRAP) {
        int end = base + GANTT_WRAP < len ? base + GANTT_WRAP : len;

        for (int i = base; i < end; i++)
            printf("+------");
        puts("+");
        for (int i = base; i < end; i++) {
            if (g[i].job_id == 0)
                printf("| idle ");
            else
                printf("| P%-4d", g[i].job_id);
        }
        puts("|");
        for (int i = base; i < end; i++)
            printf("+------");
        puts("+");
        for (int i = base; i < end; i++)
            printf("%-7d", g[i].start);
        printf("%d\n", g[end - 1].end);
    }
}

static void averages(const job_t *jobs, int n, double *avg_tat, double *avg_wt, int *makespan)
{
    double tat = 0;
    double wt = 0;
    int last = 0;

    for (int i = 0; i < n; i++) {
        tat += jobs[i].turnaround;
        wt += jobs[i].waiting;
        if (jobs[i].completion > last)
            last = jobs[i].completion;
    }
    *avg_tat = tat / n;
    *avg_wt = wt / n;
    *makespan = last;
}

void sched_print(const char *name, const job_t *jobs, int n, const gantt_slot_t *gantt, int gantt_len)
{
    double avg_tat;
    double avg_wt;
    int makespan;
    int busy = 0;

    averages(jobs, n, &avg_tat, &avg_wt, &makespan);
    printf("\n=== %s ===\n", name);
    printf("Execution order: ");
    print_order(gantt, gantt_len, 17);
    printf("\nGantt chart (time units):\n");
    print_gantt(gantt, gantt_len);

    printf("\n%-5s %-8s %-6s %-5s %-6s %-11s %-11s %-8s %s\n",
           "Job", "Arrival", "Burst", "Prio", "Start", "Completion", "Turnaround", "Waiting", "Status");
    for (int i = 0; i < n; i++) {
        printf("P%-4d %-8d %-6d %-5d %-6d %-11d %-11d %-8d %s\n",
               jobs[i].id, jobs[i].arrival, jobs[i].burst, jobs[i].priority, jobs[i].start,
               jobs[i].completion, jobs[i].turnaround, jobs[i].waiting, status_name(jobs[i].status));
        busy += jobs[i].burst;
    }
    printf("\nAverage turnaround time: %.2f\n", avg_tat);
    printf("Average waiting time   : %.2f\n", avg_wt);
    printf("Total elapsed time     : %d\n", makespan);
    printf("CPU utilization        : %.1f%%\n", makespan > 0 ? busy * 100.0 / makespan : 0.0);
}

static void policy_title(policy_t p, int quantum, char *buf, size_t size)
{
    switch (p) {
    case POLICY_FCFS:
        snprintf(buf, size, "FCFS");
        break;
    case POLICY_RR:
        snprintf(buf, size, "Round Robin (quantum = %d)", quantum);
        break;
    default:
        snprintf(buf, size, "Priority (non-preemptive)");
        break;
    }
}

static const char *policy_short(policy_t p)
{
    return p == POLICY_FCFS ? "FCFS" : (p == POLICY_RR ? "Round Robin" : "Priority");
}

static const char *policy_rule(policy_t p)
{
    switch (p) {
    case POLICY_FCFS:
        return "jobs run in order of arrival; equal arrival times are broken by lower job ID";
    case POLICY_RR:
        return "FIFO ready queue; a job arriving during a time slice is queued before the preempted job";
    default:
        return "lower priority number runs first; ties broken by earlier arrival, then lower job ID";
    }
}

static int execute_policy(policy_t p, job_t *jobs, int n, int quantum, gantt_slot_t *g, int *glen)
{
    switch (p) {
    case POLICY_FCFS:
        return sched_fcfs(jobs, n, g, glen);
    case POLICY_RR:
        return sched_rr(jobs, n, quantum, g, glen);
    default:
        return sched_priority(jobs, n, g, glen);
    }
}

static void print_job_set(const job_t *jobs, int n)
{
    printf("\n%-5s %-8s %-6s %-5s\n", "Job", "Arrival", "Burst", "Prio");
    for (int i = 0; i < n; i++)
        printf("P%-4d %-8d %-6d %-5d\n", jobs[i].id, jobs[i].arrival, jobs[i].burst, jobs[i].priority);
}

static void best_names(const double *vals, int count, const policy_t *ps, char *out, size_t size)
{
    double best = vals[0];
    size_t off = 0;

    for (int i = 1; i < count; i++)
        if (vals[i] < best)
            best = vals[i];
    out[0] = '\0';
    for (int i = 0; i < count; i++) {
        if (vals[i] - best < 1e-9 && off < size) {
            off += (size_t)snprintf(out + off, size - off, "%s%s", off ? " / " : "", policy_short(ps[i]));
        }
    }
}

int sched_compare(const job_t *jobs, int n, int quantum)
{
    static const policy_t ps[3] = { POLICY_FCFS, POLICY_RR, POLICY_PRIORITY };
    job_t work[MAX_JOBS];
    gantt_slot_t g[MAX_SLOTS];
    char titles[3][48];
    double tat[3];
    double wt[3];
    int span[3];
    int dispatches[3];
    char best[96];

    if (jobs == NULL || n <= 0 || quantum < 1)
        return -1;
    printf("\n=== Scheduling comparison (same job set, quantum = %d) ===\n", quantum);
    for (int k = 0; k < 3; k++) {
        int glen = 0;

        memcpy(work, jobs, (size_t)n * sizeof *jobs);
        if (execute_policy(ps[k], work, n, quantum, g, &glen) != 0)
            return -1;
        policy_title(ps[k], quantum, titles[k], sizeof titles[k]);
        averages(work, n, &tat[k], &wt[k], &span[k]);
        dispatches[k] = 0;
        for (int i = 0; i < glen; i++)
            if (g[i].job_id != 0)
                dispatches[k]++;
        printf("\n%s\n  order: ", titles[k]);
        print_order(g, glen, 9);
        printf("  rule : %s\n", policy_rule(ps[k]));
    }

    printf("\n%-27s %-16s %-13s %-12s %s\n", "Policy", "Avg turnaround", "Avg waiting", "Total time", "Dispatches");
    for (int k = 0; k < 3; k++)
        printf("%-27s %-16.2f %-13.2f %-12d %d\n", titles[k], tat[k], wt[k], span[k], dispatches[k]);

    best_names(wt, 3, ps, best, sizeof best);
    printf("\nLowest average waiting time   : %s\n", best);
    best_names(tat, 3, ps, best, sizeof best);
    printf("Lowest average turnaround time: %s\n", best);
    logger_log("sched", "comparison finished for %d jobs, quantum %d", n, quantum);
    return 0;
}

int sched_cmd(int argc, char **argv)
{
    static const char *usage = "usage: sched <fcfs|rr|priority|compare> [jobs_file] [quantum 1-100]";
    job_t jobs[MAX_JOBS];
    gantt_slot_t g[MAX_SLOTS];
    char title[48];
    const char *path = DEFAULT_JOBS_FILE;
    policy_t p = POLICY_FCFS;
    int quantum = DEFAULT_QUANTUM;
    int compare = 0;
    int glen = 0;
    int n;

    if (argc < 1 || argc > 3) {
        fprintf(stderr, "%s\n", usage);
        return 1;
    }
    if (strcmp(argv[0], "fcfs") == 0) {
        p = POLICY_FCFS;
    } else if (strcmp(argv[0], "rr") == 0) {
        p = POLICY_RR;
    } else if (strcmp(argv[0], "priority") == 0) {
        p = POLICY_PRIORITY;
    } else if (strcmp(argv[0], "compare") == 0) {
        compare = 1;
    } else {
        fprintf(stderr, "error: unknown scheduling policy '%s'\n%s\n", argv[0], usage);
        return 1;
    }
    if (argc >= 2)
        path = argv[1];
    if (argc == 3) {
        if (!compare && p != POLICY_RR) {
            fprintf(stderr, "error: a quantum only applies to 'rr' and 'compare'\n%s\n", usage);
            return 1;
        }
        if (parse_int(argv[2], 1, MAX_QUANTUM, &quantum) != 0) {
            fprintf(stderr, "error: quantum must be an integer from 1 to %d\n", MAX_QUANTUM);
            return 1;
        }
    }

    n = jobs_load_file(path, jobs, MAX_JOBS);
    if (n < 0)
        return 1;
    printf("Loaded %d jobs from %s\n", n, path);
    print_job_set(jobs, n);

    if (compare)
        return sched_compare(jobs, n, quantum) == 0 ? 0 : 1;

    policy_title(p, quantum, title, sizeof title);
    printf("\nPolicy: %s\nRule  : %s\n", title, policy_rule(p));
    if (execute_policy(p, jobs, n, quantum, g, &glen) != 0)
        return 1;
    sched_print(title, jobs, n, g, glen);
    logger_log("sched", "%s finished for %d jobs", title, n);
    return 0;
}
