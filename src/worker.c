#include "worker.h"

int worker_run(int simulate_failure) { (void)simulate_failure; return -1; }

int worker_cmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("[stub] worker not implemented");
    return 0;
}
