#pragma once
#include "esp_netif.h"
#include "nets.h"
void nnode_wifi_start(void);
void nnode_net_ip(char *buf, unsigned len);
int nnode_net_rssi(void);
esp_netif_t *nnode_net_sta(void);
int nnode_sta_connect(const char *ssid, const char *pass);
int nnode_sta_disconnect(void);
void nnode_sta_cur(char *ssid, unsigned ssid_len, char *pass, unsigned pass_len);
int nnode_ap_up(void);
int nnode_sta_uptime(void);
int nnode_ap_uptime(void);
int nnode_ap_get(nnode_ap_cfg_t *out);
int nnode_ap_set(const nnode_ap_cfg_t *cfg);
esp_netif_t *nnode_net_ap(void);
