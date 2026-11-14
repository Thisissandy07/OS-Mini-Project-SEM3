# CampusCompute

A command-line prototype for a shared university compute lab. It demonstrates
CPU scheduling, deadlock-safety analysis, a threaded logger, shared-memory IPC,
worker-process monitoring and resource limits, all in C with POSIX APIs.

## Prerequisites

- Ubuntu or WSL Ubuntu
- gcc and make (`sudo apt install -y build-essential`)

## Build

    make

This produces the `campuscompute` executable. Run all commands from the project
directory so `tests/` and `logs/` resolve correctly.

## Commands

    ./campuscompute help
    ./campuscompute sched fcfs [jobs_file]
    ./campuscompute sched rr [jobs_file] [quantum]
    ./campuscompute sched priority [jobs_file]
    ./campuscompute sched compare [jobs_file] [quantum]
    ./campuscompute banker [state_file]
    ./campuscompute logger [producers 1-8] [items_each 1-50]
    ./campuscompute ipc
    ./campuscompute worker normal
    ./campuscompute worker fail
    ./campuscompute limit
    ./campuscompute demo

Defaults: jobs file `tests/jobs.txt`, quantum 2, Banker state `tests/banker_safe.txt`.
Every command exits 0 on success and 1 on invalid input, with a usage or error message.

## Scheduling rules

- FCFS: jobs run in order of arrival. Equal arrival times are broken by lower job ID.
- Round Robin: default time quantum is 2. FIFO ready queue. A job that arrives during
  a time slice is queued before the preempted job.
- Priority (non-preemptive): a lower priority number means higher priority. Ties are
  broken by earlier arrival, then lower job ID.
- Idle CPU time between jobs is shown as `idle` in the timeline.
- Waiting time = turnaround - burst. Turnaround time = completion - arrival.

## Input formats

Jobs file, one job per line (blank lines and `#` comments allowed):

    id arrival burst priority

Banker state file (whitespace-separated integers, `#` comments allowed):

    n m
    Available (m values)
    Allocation (n rows of m values)
    Maximum (n rows of m values)

Need is computed as Maximum - Allocation.

## Test inputs

    tests/jobs.txt             five-job set used for all scheduling output
    tests/jobs_idle.txt        two jobs with an idle gap between them
    tests/jobs_invalid.txt     negative burst, rejected with a line number
    tests/banker_safe.txt      safe state, sequence P1 -> P3 -> P4 -> P0 -> P2
    tests/banker_unsafe.txt    unsafe state, P0 and P2 cannot finish
    tests/banker_invalid.txt   allocation exceeds maximum, rejected

Run the self-checks with `make test`.

## Worker failures are simulated

`worker fail` makes worker 2 exit with status 42 and worker 3 terminate itself with
SIGUSR1. These are controlled, clearly labelled simulations, not real OS failures.
The parent detects both with `waitpid`.

## Cleanup

    make clean              removes build files, the executable and logs/*.log
    ./campuscompute ipc cleanup   removes the program's shared memory and semaphore
                                  if an `ipc` run was interrupted

The program only ever removes the objects it created: `/campuscompute_shm` and
`/campuscompute_sem`. Verify with `ls /dev/shm`.

## Project layout

    include/   headers
    src/       main.c, scheduler.c, banker.c, logger.c, ipc.c, worker.c, rlimits.c
    tests/     input files and run_tests.sh
    logs/      campuscompute.log is written here
