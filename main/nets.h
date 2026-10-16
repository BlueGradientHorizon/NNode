#pragma once
#include <stdint.h>
#define NNODE_MAX_NETS 8
/* auth: "auto" (by password: present means WPA2, absent means open),
 * "open", "wpa", "wpa2", "wpa3". bssid "" = any (driver's choice). */
typedef struct { char ssid[33]; char pass[64]; char bssid[18]; char auth[8]; } nnode_net_cred_t;
int nnode_nets_load(nnode_net_cred_t *out, int max);
int nnode_nets_save(const nnode_net_cred_t *nets, int n);
int nnode_nets_add(const char *ssid, const char *pass, const char *bssid, const char *auth);
int nnode_nets_forget(const char *ssid);
int nnode_nets_get(const char *ssid, nnode_net_cred_t *out);
int nnode_nets_update(const char *old, const char *new_ssid,
    const char *pass, int set_pass,
    const char *bssid, int set_bssid,
    const char *auth, int set_auth);
/* 0 ok, xx:xx:xx:xx:xx:xx strings to bytes */
int nnode_bssid_parse(const char *s, uint8_t *m);
int nnode_auth_valid(const char *auth);
int nnode_last_get(char *ssid, unsigned len);
int nnode_last_set(const char *ssid);
typedef struct { char ssid[33]; char auth[8]; char pass[64]; char mode[8]; int nat; } nnode_ap_cfg_t;
int nnode_ap_load(nnode_ap_cfg_t *out);
int nnode_ap_save(const nnode_ap_cfg_t *cfg);
int nnode_ap_clear(void);
/* Validate a sta/ap package text before committing it. */
int nnode_sta_validate(const char *text);
int nnode_ap_validate(const char *text);
