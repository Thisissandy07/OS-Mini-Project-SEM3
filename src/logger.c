#include <stdarg.h>
#include "logger.h"

int logger_start(const char *path, int capacity) { (void)path; (void)capacity; return 0; }

int logger_log(const char *source, const char *fmt, ...)
{
    va_list ap;
    printf("[%s] ", source);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    return 0;
}

int logger_stop(void) { return 0; }

int logger_cmd(int argc, char **argv)
{
    (void)argc; (void)argv;
    puts("[stub] logger not implemented");
    return 0;
}
