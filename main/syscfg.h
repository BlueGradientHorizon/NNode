#pragma once
#include <stddef.h>
#define NNODE_NTP_MAX 3
#define NNODE_NTP_SRV_LEN 64
#define NNODE_HOSTNAME_MAX 32
#define NNODE_TZ_MAX 64
int nnode_tz_get(char *out, size_t len);
int nnode_tz_save(const char *tz);
int nnode_zonename_get(char *out, size_t len);
int nnode_zonename_save(const char *name);
void nnode_tz_apply(void);
int nnode_hostname_get(char *out, size_t len);
int nnode_hostname_save(const char *name);
void nnode_hostname_apply(void);
void nnode_hostname_apply_deferred(void *arg);
int nnode_hostname_set(const char *name);
int nnode_ntp_get(int *enabled, char servers[][NNODE_NTP_SRV_LEN], int max);
int nnode_ntp_save(int enabled, const char *servers_blob);
void nnode_ntp_apply(void);
void nnode_ntp_sync_now(void);
int nnode_ntp_synced(void);
void nnode_sys_apply(void);
/* Stable 4-char device id [0-9a-zA-Z], generated once and kept in NVS. */
void nnode_devid(char out[5]);
/* Defaults sharing the same suffix: "nnode-XXXX" and "NNode-XXXX". */
void nnode_hostname_default(char *out, size_t len);
void nnode_ap_default(char *out, size_t len);
/* Wipe /data and all nnode NVS. 0 ok, -1 error, -2 modules loaded. */
int nnode_factory_reset(void);
