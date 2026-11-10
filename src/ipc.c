#include "ipc.h"

int ipc_run_demo(void) { return -1; }
int ipc_cleanup(void) { return 0; }

int ipc_cmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("[stub] ipc not implemented");
    return 0;
}
