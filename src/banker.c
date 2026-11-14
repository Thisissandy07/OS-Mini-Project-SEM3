#include <ctype.h>
#include <errno.h>
#include "banker.h"
#include "logger.h"

#define DEFAULT_BANKER_FILE "tests/banker_safe.txt"
#define MAX_TOKENS (2 + MAX_RES + 2 * MAX_PROCS * MAX_RES)
#define MAX_VALUE 100000

static const char *vec_str(char *buf, size_t size, const int *v, int m)
{
    size_t off = 0;

    off += (size_t)snprintf(buf + off, size - off, "(");
    for (int j = 0; j < m && off < size; j++)
        off += (size_t)snprintf(buf + off, size - off, j ? " %d" : "%d", v[j]);
    if (off < size)
        snprintf(buf + off, size - off, ")");
    return buf;
}

static int vec_leq(const int *a, const int *b, int m)
{
    for (int j = 0; j < m; j++)
        if (a[j] > b[j])
            return 0;
    return 1;
}

int banker_load_file(const char *path, banker_t *b)
{
    FILE *fp = fopen(path, "r");
    char line[512];
    int tokens[MAX_TOKENS];
    int count = 0;
    int lineno = 0;
    int n;
    int m;
    int expected;

    if (fp == NULL) {
        fprintf(stderr, "error: cannot open state file '%s': %s\n", path, strerror(errno));
        return -1;
    }
    while (fgets(line, sizeof line, fp) != NULL) {
        char *p = line;

        lineno++;
        if (strchr(line, '\n') == NULL && !feof(fp)) {
            fprintf(stderr, "error: %s line %d: line too long\n", path, lineno);
            fclose(fp);
            return -1;
        }
        for (;;) {
            char *end;
            long v;

            while (isspace((unsigned char)*p))
                p++;
            if (*p == '\0' || *p == '#')
                break;
            errno = 0;
            v = strtol(p, &end, 10);
            if (end == p || errno != 0 || (*end != '\0' && !isspace((unsigned char)*end))) {
                fprintf(stderr, "error: %s line %d: '%.12s' is not a valid integer\n", path, lineno, p);
                fclose(fp);
                return -1;
            }
            if (v < 0) {
                fprintf(stderr, "error: %s line %d: negative values are not allowed\n", path, lineno);
                fclose(fp);
                return -1;
            }
            if (v > MAX_VALUE) {
                fprintf(stderr, "error: %s line %d: value %ld exceeds %d\n", path, lineno, v, MAX_VALUE);
                fclose(fp);
                return -1;
            }
            if (count >= MAX_TOKENS) {
                fprintf(stderr, "error: %s: too many values for the supported size\n", path);
                fclose(fp);
                return -1;
            }
            tokens[count++] = (int)v;
            p = end;
        }
    }
    fclose(fp);

    if (count < 2) {
        fprintf(stderr, "error: %s: first line must give the number of processes and resource types\n", path);
        return -1;
    }
    n = tokens[0];
    m = tokens[1];
    if (n < 1 || n > MAX_PROCS || m < 1 || m > MAX_RES) {
        fprintf(stderr, "error: %s: processes must be 1-%d and resource types 1-%d\n", path, MAX_PROCS, MAX_RES);
        return -1;
    }
    expected = 2 + m + 2 * n * m;
    if (count != expected) {
        fprintf(stderr, "error: %s: expected %d values (n m, Available, %d Allocation rows, %d Maximum rows) but found %d\n",
                path, expected, n, n, count);
        return -1;
    }

    memset(b, 0, sizeof *b);
    b->n = n;
    b->m = m;
    for (int j = 0; j < m; j++)
        b->available[j] = tokens[2 + j];
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < m; j++) {
            b->allocation[i][j] = tokens[2 + m + i * m + j];
            b->maximum[i][j] = tokens[2 + m + n * m + i * m + j];
            if (b->allocation[i][j] > b->maximum[i][j]) {
                fprintf(stderr, "error: %s: P%d R%d allocation (%d) exceeds its maximum claim (%d)\n",
                        path, i, j, b->allocation[i][j], b->maximum[i][j]);
                return -1;
            }
        }
    }
    banker_compute_need(b);
    return 0;
}

void banker_compute_need(banker_t *b)
{
    for (int i = 0; i < b->n; i++)
        for (int j = 0; j < b->m; j++)
            b->need[i][j] = b->maximum[i][j] - b->allocation[i][j];
}

int banker_is_safe(const banker_t *b, int *safe_seq, int *seq_len, int verbose)
{
    int work[MAX_RES];
    int finish[MAX_PROCS] = { 0 };
    int count = 0;
    int progress = 1;
    char a[96];
    char c[96];
    char d[96];

    memcpy(work, b->available, sizeof work);
    if (verbose)
        printf("\nSafety algorithm trace (work starts as Available = %s)\n", vec_str(a, sizeof a, work, b->m));

    while (count < b->n && progress) {
        progress = 0;
        for (int i = 0; i < b->n; i++) {
            if (finish[i] || !vec_leq(b->need[i], work, b->m))
                continue;
            if (verbose)
                printf("  Step %d: P%d need %s <= work %s -> can finish, releases %s",
                       count + 1, i, vec_str(a, sizeof a, b->need[i], b->m),
                       vec_str(c, sizeof c, work, b->m), vec_str(d, sizeof d, b->allocation[i], b->m));
            for (int j = 0; j < b->m; j++)
                work[j] += b->allocation[i][j];
            if (verbose)
                printf(", work = %s\n", vec_str(a, sizeof a, work, b->m));
            finish[i] = 1;
            safe_seq[count++] = i;
            progress = 1;
        }
    }
    *seq_len = count;

    if (verbose && count < b->n) {
        printf("  No remaining process can finish with work = %s\n", vec_str(a, sizeof a, work, b->m));
        for (int i = 0; i < b->n; i++)
            if (!finish[i])
                printf("  P%d is stuck: need %s exceeds work\n", i, vec_str(a, sizeof a, b->need[i], b->m));
    }
    return count == b->n;
}

void banker_print(const banker_t *b)
{
    char x[96];
    char y[96];
    char z[96];

    printf("\nProcesses: %d   Resource types: %d\n", b->n, b->m);
    printf("Available: %s\n\n", vec_str(x, sizeof x, b->available, b->m));
    printf("%-6s %-24s %-24s %-24s\n", "Proc", "Allocation", "Maximum", "Need (Max - Alloc)");
    for (int i = 0; i < b->n; i++)
        printf("P%-5d %-24s %-24s %-24s\n", i,
               vec_str(x, sizeof x, b->allocation[i], b->m),
               vec_str(y, sizeof y, b->maximum[i], b->m),
               vec_str(z, sizeof z, b->need[i], b->m));
}

static int verify_sequence(const banker_t *b, const int *seq, int len)
{
    int work[MAX_RES];

    memcpy(work, b->available, sizeof work);
    for (int k = 0; k < len; k++) {
        int i = seq[k];

        if (!vec_leq(b->need[i], work, b->m))
            return 0;
        for (int j = 0; j < b->m; j++)
            work[j] += b->allocation[i][j];
    }
    return 1;
}

int banker_analyze_file(const char *path, int *is_safe)
{
    banker_t b;
    int seq[MAX_PROCS];
    int finished[MAX_PROCS] = { 0 };
    int len = 0;
    int safe;

    if (banker_load_file(path, &b) != 0)
        return -1;
    printf("Banker's algorithm input: %s\n", path);
    banker_print(&b);
    safe = banker_is_safe(&b, seq, &len, 1);

    if (safe) {
        printf("\nRESULT: SAFE STATE. Safe sequence: ");
        for (int k = 0; k < len; k++)
            printf("P%d%s", seq[k], k + 1 < len ? " -> " : "\n");
        printf("Sequence re-checked against Need and Allocation: %s\n", verify_sequence(&b, seq, len) ? "valid" : "INVALID");
        logger_log("banker", "%s is SAFE, sequence length %d", path, len);
    } else {
        for (int k = 0; k < len; k++)
            finished[seq[k]] = 1;
        printf("\nRESULT: UNSAFE STATE. Only %d of %d processes can finish. Processes that may deadlock:", len, b.n);
        for (int i = 0; i < b.n; i++)
            if (!finished[i])
                printf(" P%d", i);
        putchar('\n');
        logger_log("banker", "%s is UNSAFE, %d of %d processes can finish", path, len, b.n);
    }
    if (is_safe != NULL)
        *is_safe = safe;
    return 0;
}

int banker_cmd(int argc, char **argv)
{
    const char *path = DEFAULT_BANKER_FILE;

    if (argc > 1) {
        fprintf(stderr, "usage: banker [state_file]\n");
        return 1;
    }
    if (argc == 1)
        path = argv[0];
    return banker_analyze_file(path, NULL) == 0 ? 0 : 1;
}
