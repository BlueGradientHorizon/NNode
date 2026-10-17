#pragma once
#include <stddef.h>
#include <stdint.h>

/* NNode typed value validators (no ESP dependencies).
 * Every function returns 0 when the NUL-terminated value is valid,
 * -1 otherwise. Parsers never write through NULL out pointers. */

int nnode_ct_bool(const char *v);
int nnode_ct_int(const char *v, long *out);
int nnode_ct_int_range(const char *v, long lo, int has_lo,
    long hi, int has_hi, long *out);
int nnode_ct_port(const char *v, long *out);
int nnode_ct_ip4(const char *v, uint8_t out[4]);
int nnode_ct_netmask(const char *v, uint8_t out[4]);
int nnode_ct_cidr4(const char *v, uint8_t addr[4], int *prefix);
int nnode_ct_mac(const char *v, uint8_t out[6]);
int nnode_ct_hostname(const char *v, size_t maxlen);
int nnode_ct_range(const char *v, long glo_min, int has_glo_min,
    long glo_max, int has_glo_max, long *lo, long *hi);
int nnode_ct_wpakey(const char *v);
int nnode_ct_string(const char *v, long maxlen);
/* Flat enum table: element i starts at vals + i * stride. */
int nnode_ct_enum(const char *v, const char *vals, int nvals, size_t stride);
/* Comma-separated single-line list. Empty string is an empty list.
 * Each item is checked by sub (one of string, hostname, ip4addr,
 * macaddr, port, int). maxlen applies to string items only. */
int nnode_ct_list(const char *v, const char *sub, int maxitems,
    long maxlen);
