#pragma once
#include <stddef.h>
/* Levels: 0 V, 1 D, 2 I, 3 W, 4 E. Filter keeps lvl >= min. */
void nnode_log_start(void);
void nnode_log_clear(void);
int nnode_log_json(char *buf, size_t len, int min_level, int max_n);
