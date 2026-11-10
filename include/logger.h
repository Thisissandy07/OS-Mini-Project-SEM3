#ifndef LOGGER_H
#define LOGGER_H

#include "common.h"

int logger_start(const char *path, int capacity);
int logger_log(const char *source, const char *fmt, ...);
int logger_stop(void);
int logger_cmd(int argc, char **argv);

#endif
