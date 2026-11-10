#include "banker.h"

int banker_load_file(const char *path, banker_t *b) { (void)path; (void)b; return -1; }
void banker_compute_need(banker_t *b) { (void)b; }
int banker_is_safe(const banker_t *b, int *safe_seq) { (void)b; (void)safe_seq; return -1; }
void banker_print(const banker_t *b) { (void)b; }

int banker_cmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("[stub] banker not implemented");
    return 0;
}
