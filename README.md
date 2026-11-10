# CampusCompute

## Prerequisites
Ubuntu or WSL Ubuntu, gcc, make.

## Build
make

## Run
./campuscompute help
./campuscompute sched fcfs tests/jobs.txt
./campuscompute banker tests/banker_safe.txt

## Cleanup
make clean

## Input formats
jobs file: one job per line as `id arrival burst priority`
banker file: `n m`, Available row, n Allocation rows, n Maximum rows

## Ownership
Person A: scheduler, banker, main, Makefile, README
Person B: logger, ipc, worker, rlimits
