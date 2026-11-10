#ifndef BANKER_H
#define BANKER_H

#include "common.h"

typedef struct {
    int n;
    int m;
    int available[MAX_RES];
    int allocation[MAX_PROCS][MAX_RES];
    int maximum[MAX_PROCS][MAX_RES];
    int need[MAX_PROCS][MAX_RES];
} banker_t;

int banker_load_file(const char *path, banker_t *b);
void banker_compute_need(banker_t *b);
int banker_is_safe(const banker_t *b, int *safe_seq);
void banker_print(const banker_t *b);
int banker_cmd(int argc, char **argv);

#endif
