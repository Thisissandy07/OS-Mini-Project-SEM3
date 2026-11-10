#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "common.h"

int job_validate(const job_t *j);
int jobs_load_file(const char *path, job_t *jobs, int max_jobs);
int sched_fcfs(job_t *jobs, int n, gantt_slot_t *gantt, int *gantt_len);
int sched_rr(job_t *jobs, int n, int quantum, gantt_slot_t *gantt, int *gantt_len);
int sched_priority(job_t *jobs, int n, gantt_slot_t *gantt, int *gantt_len);
void sched_print(const char *name, const job_t *jobs, int n, const gantt_slot_t *gantt, int gantt_len);
int sched_compare(const job_t *jobs, int n, int quantum);
int sched_cmd(int argc, char **argv);

#endif
