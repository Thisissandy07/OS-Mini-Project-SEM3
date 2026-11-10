#include "scheduler.h"

int job_validate(const job_t *j) { (void)j; return -1; }
int jobs_load_file(const char *path, job_t *jobs, int max_jobs) { (void)path; (void)jobs; (void)max_jobs; return -1; }
int sched_fcfs(job_t *jobs, int n, gantt_slot_t *gantt, int *gantt_len) { (void)jobs; (void)n; (void)gantt; (void)gantt_len; return -1; }
int sched_rr(job_t *jobs, int n, int quantum, gantt_slot_t *gantt, int *gantt_len) { (void)jobs; (void)n; (void)quantum; (void)gantt; (void)gantt_len; return -1; }
int sched_priority(job_t *jobs, int n, gantt_slot_t *gantt, int *gantt_len) { (void)jobs; (void)n; (void)gantt; (void)gantt_len; return -1; }
void sched_print(const char *name, const job_t *jobs, int n, const gantt_slot_t *gantt, int gantt_len) { (void)name; (void)jobs; (void)n; (void)gantt; (void)gantt_len; }
int sched_compare(const job_t *jobs, int n, int quantum) { (void)jobs; (void)n; (void)quantum; return -1; }

int sched_cmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("[stub] sched not implemented");
    return 0;
}
