#pragma once
#include <stddef.h>
#include "nets.h"
void nnode_net_taps_install(void);
int nnode_net_json(char *buf, size_t len);
int nnode_net_scan(char *buf, size_t len);
typedef int (*nnode_ping_line_fn)(void *ctx, const char *line);
int nnode_net_ping_lines(const char *host, int count, int interval_ms,
    nnode_ping_line_fn emit, void *ctx);
int nnode_net_traceroute(const char *host, nnode_ping_line_fn emit, void *ctx);
int nnode_net_nslookup(const char *host, char *buf, size_t len);
int nnode_net_renew(void);
int nnode_ap_stations(char *buf, size_t len);
int nnode_ap_deauth(const char *mac);
int nnode_nets_json(char *buf, size_t len);
int nnode_ap_json(char *buf, size_t len);
int nnode_ap_set_json(const char *body);
int nnode_ap_parse_json(const char *body, nnode_ap_cfg_t *out);
