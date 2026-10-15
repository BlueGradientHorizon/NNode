#pragma once
#include <stddef.h>
#include <stdint.h>
int nnode_api_summary(char *buf, size_t len);
int nnode_api_sys(char *buf, size_t len);
int nnode_api_time_set(int64_t ts);
