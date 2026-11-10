#ifndef WORKER_H
#define WORKER_H

#include "common.h"

int worker_run(int simulate_failure);
int worker_cmd(int argc, char **argv);

#endif
