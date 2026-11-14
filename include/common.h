#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_JOBS 32
#define MAX_SLOTS 1024
#define MAX_PROCS 16
#define MAX_RES 8
#define DEFAULT_QUANTUM 2
#define LOG_FILE "logs/campuscompute.log"
#define SHM_NAME "/campuscompute_shm"
#define SEM_NAME "/campuscompute_sem"

typedef enum { JOB_NEW, JOB_READY, JOB_RUNNING, JOB_DONE } job_status_t;

typedef struct {
    int id;
    int arrival;
    int burst;
    int priority;
    job_status_t status;
    int remaining;
    int start;
    int completion;
    int turnaround;
    int waiting;
} job_t;

typedef struct {
    int job_id;
    int start;
    int end;
} gantt_slot_t;

typedef int (*cmd_fn)(int argc, char **argv);

#endif
