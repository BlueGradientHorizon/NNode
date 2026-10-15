#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "lwip/lwip_napt.h"
#include "lwip/inet.h"
#include "esp_netif_net_stack.h"
#include "wifi.h"
#include "net.h"
#include "syscfg.h"

static const char *TAG = "nnode_wifi";
static EventGroupHandle_t s_ev;
static esp_netif_t *s_sta;
static esp_netif_t *s_ap;
static int s_ap_up;
static int s_ap_always;
static int s_nat_on;
static int s_manual;
static char s_cur[33];
static int64_t s_sta_ip_us;
static int64_t s_ap_up_us;
static esp_timer_handle_t s_ap_timer;
#define GOT_BIT BIT0

static void ap_enable(void);
static void ap_disable(void);
static void nat_dns_apply(void);

static void ap_enable(void)
{
    if (s_ap_up) {
        return;
    }
    ESP_LOGI(TAG, "enabling AP fallback");
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (s_nat_on) {
        nat_dns_apply();
    }
    esp_wifi_start();
    s_ap_up = 1;
    s_ap_up_us = esp_timer_get_time();
}

static void ap_disable(void)
{
    if (!s_ap_up || s_ap_always) {
        return;
    }
    ESP_LOGI(TAG, "STA up, disabling AP");
    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    s_ap_up = 0;
    s_ap_up_us = 0;
}

static void ap_timer_cb(void *a)
{
    esp_netif_ip_info_t ip;
    (void)a;
    memset(&ip, 0, sizeof(ip));
    if (s_sta && esp_netif_get_ip_info(s_sta, &ip) == ESP_OK && ip.ip.addr) {
        return;
    }
    /* STA is down: keep trying to bring it back, and make sure AP is up */
    esp_wifi_connect();
    ap_enable();
}

static void on_wifi(void *a, esp_event_base_t b, int32_t id, void *d)
{
    if (id == WIFI_EVENT_STA_START) {
        if (!s_manual) {
            esp_wifi_connect();
        }
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = d;
        ESP_LOGW(TAG, "STA disconnected reason=%d", ev ? ev->reason : -1);
        s_sta_ip_us = 0;
        if (s_manual) {
            ap_enable();
        } else {
            esp_timer_stop(s_ap_timer);
            esp_timer_start_once(s_ap_timer, 10 * 1000000);
        }
    }
}

/* Push DNS server address into the AP DHCP offer, and flip the offer
 * flag. Address write works anytime; the flag is only accepted while
 * the DHCP server is stopped, so bounce it when it is running. */
static void nat_dns_apply(void)
{
    esp_netif_dns_info_t dns;
    uint8_t one;
    if (!s_ap) {
        return;
    }
    memset(&dns, 0, sizeof(dns));
    if (s_nat_on) {
        if (s_sta &&
            esp_netif_get_dns_info(s_sta, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK &&
            dns.ip.u_addr.ip4.addr != 0) {
            /* keep STA DNS */
        } else {
            ip4_addr_t tmp;
            ip4addr_aton("8.8.8.8", &tmp);
            dns.ip.u_addr.ip4.addr = tmp.addr;
        }
        esp_netif_set_dns_info(s_ap, ESP_NETIF_DNS_MAIN, &dns);
    }
    one = s_nat_on ? 1 : 0;
    if (esp_netif_dhcps_option(s_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
            &one, sizeof(one)) != ESP_OK) {
        esp_netif_dhcps_stop(s_ap);
        esp_netif_dhcps_option(s_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
            &one, sizeof(one));
        esp_netif_dhcps_start(s_ap);
    }
}

static void nat_refresh(void)
{
    /* NOTE: the napt flag belongs on the INTERNAL (AP) interface:
     * ip_forward() masquerades only packets arriving on a flagged
     * input netif, and skips forwarding out a flagged output netif.
     * Flag on STA (uplink) disables masquerading entirely. */
    esp_netif_ip_info_t ip;
    struct netif *lwip_ap, *lwip_sta;
    if (!s_nat_on || !s_sta || !s_ap) {
        return;
    }
    memset(&ip, 0, sizeof(ip));
    if (esp_netif_get_ip_info(s_sta, &ip) != ESP_OK || !ip.ip.addr) {
        return;
    }
    lwip_ap = esp_netif_get_netif_impl(s_ap);
    lwip_sta = esp_netif_get_netif_impl(s_sta);
    if (!lwip_ap || !lwip_sta) {
        ESP_LOGE(TAG, "NAT on failed: no lwip netif");
        return;
    }
    ip_napt_enable_netif(lwip_sta, 0);
    if (ip_napt_enable_netif(lwip_ap, 1)) {
        ESP_LOGI(TAG, "NAT on (uplink " IPSTR ")", IP2STR(&ip.ip));
    } else {
        ESP_LOGE(TAG, "NAT on failed: napt enable");
    }
    nat_dns_apply();
}

static void nat_off(void)
{
    struct netif *lwip;
    if (!s_ap) {
        return;
    }
    lwip = esp_netif_get_netif_impl(s_ap);
    if (lwip) {
        ip_napt_enable_netif(lwip, 0);
    }
    nat_dns_apply();
}

static void on_ip(void *a, esp_event_base_t b, int32_t id, void *d)
{
    if (id == IP_EVENT_STA_GOT_IP) {
        s_manual = 0;
        s_sta_ip_us = esp_timer_get_time();
        esp_timer_stop(s_ap_timer);
        nnode_last_set(s_cur);
        xEventGroupSetBits(s_ev, GOT_BIT);
        nat_refresh();
        ap_disable();
    }
}

static wifi_auth_mode_t sta_auth_floor(const nnode_net_cred_t *c)
{
    if (!c) {
        return WIFI_AUTH_OPEN;
    }
    if (strcmp(c->auth, "open") == 0) {
        return WIFI_AUTH_OPEN;
    }
    if (strcmp(c->auth, "wpa") == 0) {
        return WIFI_AUTH_WPA_PSK;
    }
    if (strcmp(c->auth, "wpa2") == 0) {
        return WIFI_AUTH_WPA2_PSK;
    }
    if (strcmp(c->auth, "wpa3") == 0) {
        return WIFI_AUTH_WPA3_PSK;
    }
    return c->pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
}

static void sta_config_for(const nnode_net_cred_t *c)
{
    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    strncpy((char *)wc.sta.ssid, c->ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, c->pass, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = sta_auth_floor(c);
    if (c->bssid[0] && nnode_bssid_parse(c->bssid, wc.sta.bssid) == 0) {
        wc.sta.bssid_set = 1;
    }
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    strncpy(s_cur, c->ssid, sizeof(s_cur) - 1);
    s_cur[sizeof(s_cur) - 1] = 0;
}

static wifi_auth_mode_t ap_auth_mode(const char *a)
{
    if (strcmp(a, "wpa2") == 0) {
        return WIFI_AUTH_WPA2_PSK;
    }
    if (strcmp(a, "wpa_wpa2") == 0) {
        return WIFI_AUTH_WPA_WPA2_PSK;
    }
    return WIFI_AUTH_OPEN;
}

static void ap_apply_cfg(const nnode_ap_cfg_t *c)
{
    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));
    strncpy((char *)wc.ap.ssid, c->ssid, sizeof(wc.ap.ssid) - 1);
    wc.ap.ssid_len = strlen(c->ssid);
    wc.ap.authmode = ap_auth_mode(c->auth);
    strncpy((char *)wc.ap.password, c->pass, sizeof(wc.ap.password) - 1);
    wc.ap.max_connection = 4;
    esp_wifi_set_config(WIFI_IF_AP, &wc);
}

void nnode_wifi_start(void)
{
    nnode_ap_cfg_t ap;
    nnode_net_cred_t nets[NNODE_MAX_NETS];
    int n, i, li;
    char last[33];
    s_ev = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta = esp_netif_create_default_wifi_sta();
    s_ap = esp_netif_create_default_wifi_ap();
    /* Per-iface счетчики пакетов: обернуть input/linkoutput lwIP netif
     * нужно до старта WiFi, дальше указатели уже не меняются. */
    nnode_net_taps_install();
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, NULL, NULL));
    {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        /* Driver must not persist STA creds on its own: the nnode NVS
         * store is the single source of truth (else "forgotten" nets
         * keep reconnecting and empty-nets boot never reaches AP). */
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    }
    {
        const esp_timer_create_args_t ta = { .callback = ap_timer_cb, .name = "ap_fallback" };
        ESP_ERROR_CHECK(esp_timer_create(&ta, &s_ap_timer));
    }
    if (nnode_ap_load(&ap) != 0) {
        nnode_ap_default(ap.ssid, sizeof(ap.ssid));
        strncpy(ap.auth, "open", sizeof(ap.auth) - 1);
        ap.pass[0] = 0;
        strncpy(ap.mode, "auto", sizeof(ap.mode) - 1);
        ap.nat = 0;
    }
    s_ap_always = strcmp(ap.mode, "always") == 0;
    s_nat_on = ap.nat ? 1 : 0;
    ESP_LOGI(TAG, "AP ssid=%s auth=%s mode=%s", ap.ssid, ap.auth, ap.mode);
    ap_apply_cfg(&ap);
    n = nnode_nets_load(nets, NNODE_MAX_NETS);
    if (n <= 0) {
        ESP_LOGI(TAG, "no saved nets, AP mode");
        s_manual = 1;
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_start());
        s_ap_up = 1;
        s_ap_up_us = esp_timer_get_time();
        return;
    }
    if (nnode_last_get(last, sizeof(last)) == 0 && last[0]) {
        for (i = 0; i < n; i++) {
            if (strcmp(nets[i].ssid, last) == 0 && i != 0) {
                nnode_net_cred_t t = nets[0];
                nets[0] = nets[i];
                nets[i] = t;
                break;
            }
        }
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(s_ap_always ? WIFI_MODE_APSTA : WIFI_MODE_STA));
    if (s_ap_always) {
        s_ap_up = 1;
        s_ap_up_us = esp_timer_get_time();
    }
    sta_config_for(&nets[0]);
    ESP_ERROR_CHECK(esp_wifi_start());
    for (i = 0; i < n; i++) {
        EventBits_t bits;
        if (i > 0) {
            sta_config_for(&nets[i]);
            esp_wifi_connect();
        }
        xEventGroupClearBits(s_ev, GOT_BIT);
        ESP_LOGI(TAG, "trying %s", nets[i].ssid);
        bits = xEventGroupWaitBits(s_ev, GOT_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(9000));
        if (bits & GOT_BIT) {
            ESP_LOGI(TAG, "connected ssid=%s", nets[i].ssid);
            return;
        }
    }
    ESP_LOGE(TAG, "all saved nets failed, AP mode");
    ap_enable();
}

int nnode_sta_connect(const char *ssid, const char *pass)
{
    nnode_net_cred_t saved;
    const char *use_pass = pass;
    char tmp[64];
    if (!ssid || !ssid[0] || strlen(ssid) > 32) {
        return -1;
    }
    if (!use_pass || !use_pass[0]) {
        if (nnode_nets_get(ssid, &saved) != 0) {
            return -2;
        }
        strncpy(tmp, saved.pass, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = 0;
        use_pass = tmp;
    }
    if (strlen(use_pass) > 63) {
        return -1;
    }
    if (nnode_nets_add(ssid, use_pass, NULL, NULL) < 0) {
        return -1;
    }
    if (nnode_nets_get(ssid, &saved) != 0) {
        return -1;
    }
    nnode_last_set(ssid);
    s_manual = 0;
    sta_config_for(&saved);
    ESP_LOGI(TAG, "user connect %s", ssid);
    return esp_wifi_connect() == ESP_OK ? 0 : -1;
}

int nnode_sta_disconnect(void)
{
    ESP_LOGI(TAG, "user disconnect");
    s_manual = 1;
    s_cur[0] = 0;
    esp_timer_stop(s_ap_timer);
    esp_wifi_disconnect();
    ap_enable();
    return 0;
}

void nnode_sta_cur(char *ssid, unsigned ssid_len, char *pass, unsigned pass_len)
{
    nnode_net_cred_t saved;
    if (ssid && ssid_len) {
        strncpy(ssid, s_cur[0] ? s_cur : "-", ssid_len - 1);
        ssid[ssid_len - 1] = 0;
    }
    if (pass && pass_len) {
        pass[0] = 0;
        if (s_cur[0] && nnode_nets_get(s_cur, &saved) == 0) {
            strncpy(pass, saved.pass, pass_len - 1);
            pass[pass_len - 1] = 0;
        }
    }
}

int nnode_ap_up(void)
{
    return s_ap_up;
}

int nnode_sta_uptime(void)
{
    if (!s_sta_ip_us) {
        return 0;
    }
    return (int)((esp_timer_get_time() - s_sta_ip_us) / 1000000);
}

int nnode_ap_uptime(void)
{
    if (!s_ap_up || !s_ap_up_us) {
        return 0;
    }
    return (int)((esp_timer_get_time() - s_ap_up_us) / 1000000);
}

int nnode_ap_get(nnode_ap_cfg_t *out)
{
    if (!out) {
        return -1;
    }
    if (nnode_ap_load(out) == 0) {
        return 0;
    }
    nnode_ap_default(out->ssid, sizeof(out->ssid));
    strncpy(out->auth, "open", sizeof(out->auth) - 1);
    out->pass[0] = 0;
    strncpy(out->mode, "auto", sizeof(out->mode) - 1);
    out->nat = 0;
    return 0;
}

int nnode_ap_set(const nnode_ap_cfg_t *cfg)
{
    size_t sl, pl;
    int always, nat;
    if (!cfg) {
        return -1;
    }
    sl = strlen(cfg->ssid);
    pl = strlen(cfg->pass);
    if (sl == 0 || sl > 32) {
        return -1;
    }
    if (strcmp(cfg->auth, "open") != 0 && strcmp(cfg->auth, "wpa2") != 0 &&
        strcmp(cfg->auth, "wpa_wpa2") != 0) {
        return -1;
    }
    if (strcmp(cfg->auth, "open") == 0) {
        if (pl != 0) {
            return -1;
        }
    } else if (pl < 8 || pl > 63) {
        return -1;
    }
    if (strcmp(cfg->mode, "auto") != 0 && strcmp(cfg->mode, "always") != 0) {
        return -1;
    }
    nat = cfg->nat ? 1 : 0;
    always = strcmp(cfg->mode, "always") == 0;
    if (nnode_ap_save(cfg) != 0) {
        return -1;
    }
    s_ap_always = always;
    ap_apply_cfg(cfg);
    if (nat && !s_nat_on) {
        s_nat_on = 1;
        nat_refresh();
    } else if (!nat && s_nat_on) {
        s_nat_on = 0;
        nat_off();
    }
    if (always && !s_ap_up) {
        ap_enable();
    } else if (!always && s_ap_up) {
        esp_netif_ip_info_t ip;
        memset(&ip, 0, sizeof(ip));
        if (s_sta && esp_netif_get_ip_info(s_sta, &ip) == ESP_OK && ip.ip.addr) {
            ap_disable();
        }
    }
    ESP_LOGI(TAG, "AP updated ssid=%s auth=%s mode=%s nat=%d",
        cfg->ssid, cfg->auth, cfg->mode, nat);
    return 0;
}

esp_netif_t *nnode_net_ap(void)
{
    return s_ap;
}

void nnode_net_ip(char *buf, unsigned len)
{
    esp_netif_ip_info_t ip;
    memset(&ip, 0, sizeof(ip));
    if (s_sta && esp_netif_get_ip_info(s_sta, &ip) == ESP_OK && ip.ip.addr) {
        snprintf(buf, len, IPSTR, IP2STR(&ip.ip));
    } else {
        snprintf(buf, len, "-");
    }
}

esp_netif_t *nnode_net_sta(void)
{
    return s_sta;
}

int nnode_net_rssi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}
