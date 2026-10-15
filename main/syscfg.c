#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_littlefs.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_sntp.h"
#include "wifi.h"
#include "net.h"
#include "files.h"
#include "syscfg.h"

static const char *TAG = "nnode_sys";
#define NS "nnode"

static int valid_devid(const char *s)
{
    int i;
    if (!s) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        if (!isalnum((unsigned char)s[i])) {
            return 0;
        }
    }
    return s[4] == 0;
}

void nnode_devid(char out[5])
{
    static const char ABC[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    nvs_handle_t h;
    size_t need = 5;
    char cur[8];
    int i;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        need = sizeof(cur);
        if (nvs_get_str(h, "devid", cur, &need) == ESP_OK && valid_devid(cur)) {
            nvs_close(h);
            memcpy(out, cur, 5);
            return;
        }
        nvs_close(h);
    }
    for (i = 0; i < 4; i++) {
        out[i] = ABC[esp_random() % (sizeof(ABC) - 1)];
    }
    out[4] = 0;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_str(h, "devid", out) == ESP_OK) {
            nvs_commit(h);
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "new devid %s", out);
}

void nnode_hostname_default(char *out, size_t len)
{
    char id[5];
    if (!out || !len) {
        return;
    }
    nnode_devid(id);
    snprintf(out, len, "nnode-%s", id);
}

void nnode_ap_default(char *out, size_t len)
{
    char id[5];
    if (!out || !len) {
        return;
    }
    nnode_devid(id);
    snprintf(out, len, "NNode-%s", id);
}

static int valid_hostname(const char *s, size_t maxlen)
{
    size_t n;
    if (!s || (n = strlen(s)) == 0 || n > maxlen) {
        return 0;
    }
    if (s[0] == '-' || s[n - 1] == '-') {
        return 0;
    }
    while (*s) {
        if (!isalnum((unsigned char)*s) && *s != '-' && *s != '.') {
            return 0;
        }
        s++;
    }
    return 1;
}

int nnode_hostname_get(char *out, size_t len)
{
    nvs_handle_t h;
    size_t need = len;
    char def[NNODE_HOSTNAME_MAX + 1];
    if (!out || !len) {
        return -1;
    }
    nnode_hostname_default(def, sizeof(def));
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        snprintf(out, len, "%s", def);
        return 0;
    }
    if (nvs_get_str(h, "hostname", out, &need) != ESP_OK) {
        nvs_close(h);
        snprintf(out, len, "%s", def);
        return 0;
    }
    nvs_close(h);
    if (!valid_hostname(out, NNODE_HOSTNAME_MAX)) {
        snprintf(out, len, "%s", def);
    }
    return 0;
}

int nnode_hostname_save(const char *name)
{
    nvs_handle_t h;
    if (!valid_hostname(name, NNODE_HOSTNAME_MAX)) {
        return -1;
    }
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return -1;
    }
    if (nvs_set_str(h, "hostname", name) != ESP_OK ||
        nvs_commit(h) != ESP_OK) {
        nvs_close(h);
        return -1;
    }
    nvs_close(h);
    return 0;
}

/* Applies the saved hostname and renews DHCP. Drops the station IP for
 * a moment, so call only AFTER the HTTP response is out (same rule as
 * save/renew/reboot in the WiFi handlers). */
void nnode_hostname_apply(void)
{
    char host[NNODE_HOSTNAME_MAX + 1];
    esp_netif_t *sta;
    nnode_hostname_get(host, sizeof(host));
    sta = nnode_net_sta();
    if (sta) {
        esp_netif_set_hostname(sta, host);
    }
    nnode_net_renew();
    ESP_LOGI(TAG, "hostname -> %s", host);
}

void nnode_hostname_apply_deferred(void *arg)
{
    (void)arg;
    nnode_hostname_apply();
}

int nnode_hostname_set(const char *name)
{
    if (nnode_hostname_save(name) != 0) {
        return -1;
    }
    nnode_hostname_apply();
    return 0;
}

int nnode_ntp_get(int *enabled, char servers[][NNODE_NTP_SRV_LEN], int max)
{
    nvs_handle_t h;
    uint8_t en = 0;
    char blob[NNODE_NTP_MAX * (NNODE_NTP_SRV_LEN + 1)];
    size_t need = sizeof(blob);
    int n = 0;
    const char *p;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "ntp_en", &en);
        if (nvs_get_str(h, "ntp_srv", blob, &need) != ESP_OK) {
            blob[0] = 0;
        }
        nvs_close(h);
    } else {
        blob[0] = 0;
    }
    if (enabled) {
        *enabled = en ? 1 : 0;
    }
    if (servers && max > 0) {
        for (n = 0; n < max; n++) {
            servers[n][0] = 0;
        }
        n = 0;
        for (p = blob; *p && n < max;) {
            while (*p == ' ' || *p == ',') {
                p++;
            }
            if (!*p) {
                break;
            }
            {
                size_t k = 0;
                while (*p && *p != ' ' && *p != ',' && k + 1 < NNODE_NTP_SRV_LEN) {
                    servers[n][k++] = *p++;
                }
                servers[n][k] = 0;
                while (*p && *p != ' ' && *p != ',') {
                    p++;
                }
                if (valid_hostname(servers[n], NNODE_NTP_SRV_LEN - 1)) {
                    n++;
                }
            }
        }
    }
    return n;
}

int nnode_ntp_save(int enabled, const char *servers_blob)
{
    nvs_handle_t h;
    char norm[NNODE_NTP_MAX * (NNODE_NTP_SRV_LEN + 1)];
    char srvs[NNODE_NTP_MAX][NNODE_NTP_SRV_LEN];
    int i, n = 0;
    const char *p;
    if (!servers_blob) {
        servers_blob = "";
    }
    if ((int)strlen(servers_blob) >= (int)sizeof(norm)) {
        return -1;
    }
    strcpy(norm, servers_blob);
    for (p = norm; *p && n < NNODE_NTP_MAX;) {
        while (*p == ' ' || *p == ',') {
            p++;
        }
        if (!*p) {
            break;
        }
        {
            size_t k = 0;
            while (*p && *p != ' ' && *p != ',' && k + 1 < NNODE_NTP_SRV_LEN) {
                srvs[n][k++] = *p++;
            }
            srvs[n][k] = 0;
            while (*p && *p != ' ' && *p != ',') {
                p++;
            }
            if (!valid_hostname(srvs[n], NNODE_NTP_SRV_LEN - 1)) {
                return -1;
            }
            n++;
        }
    }
    norm[0] = 0;
    for (i = 0; i < n; i++) {
        strcat(norm, srvs[i]);
        if (i + 1 < n) {
            strcat(norm, " ");
        }
    }
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return -1;
    }
    if (nvs_set_u8(h, "ntp_en", enabled ? 1 : 0) != ESP_OK ||
        nvs_set_str(h, "ntp_srv", norm) != ESP_OK ||
        nvs_commit(h) != ESP_OK) {
        nvs_close(h);
        return -1;
    }
    nvs_close(h);
    nnode_ntp_apply();
    return 0;
}

static char s_srv[NNODE_NTP_MAX][NNODE_NTP_SRV_LEN];

void nnode_ntp_apply(void)
{
    int en = 0;
    char srvs[NNODE_NTP_MAX][NNODE_NTP_SRV_LEN];
    int i, n;
    esp_sntp_stop();
    n = nnode_ntp_get(&en, srvs, NNODE_NTP_MAX);
#if CONFIG_LWIP_SNTP_MAX_SERVERS < NNODE_NTP_MAX
    if (n > CONFIG_LWIP_SNTP_MAX_SERVERS) {
        n = CONFIG_LWIP_SNTP_MAX_SERVERS;
    }
#endif
    if (!en || n <= 0) {
        ESP_LOGI(TAG, "ntp off");
        return;
    }
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    for (i = 0; i < n; i++) {
        snprintf(s_srv[i], sizeof(s_srv[i]), "%s", srvs[i]);
        esp_sntp_setservername(i, s_srv[i]);
    }
    esp_sntp_init();
    ESP_LOGI(TAG, "ntp on (%d servers)", n);
}

void nnode_ntp_sync_now(void)
{
    int en = 0;
    nnode_ntp_get(&en, NULL, 0);
    if (!en) {
        return;
    }
    esp_sntp_stop();
    esp_sntp_init();
}

int nnode_ntp_synced(void)
{
    int en = 0;
    char srvs[NNODE_NTP_MAX][NNODE_NTP_SRV_LEN];
    int i, n;
    n = nnode_ntp_get(&en, srvs, NNODE_NTP_MAX);
    if (!en) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (esp_sntp_getreachability(i)) {
            return 1;
        }
    }
    return 0;
}

void nnode_sys_apply(void)
{
    char host[NNODE_HOSTNAME_MAX + 1];
    esp_netif_t *sta;
    nnode_tz_apply();
    nnode_hostname_get(host, sizeof(host));
    ESP_LOGI(TAG, "hostname -> %s", host);
    sta = nnode_net_sta();
    if (sta) {
        esp_netif_set_hostname(sta, host);
    }
    nnode_ntp_apply();
}

#define TZ_DEFAULT "UTC0"

static int valid_tz(const char *s)
{
    size_t n;
    if (!s || (n = strlen(s)) == 0 || n > NNODE_TZ_MAX) {
        return 0;
    }
    while (*s) {
        char c = *s++;
        if (!isalnum((unsigned char)c) && c != '+' && c != '-' &&
            c != '_' && c != ',' && c != '.' && c != '/' && c != ':' &&
            c != '<' && c != '>') {
            return 0;
        }
    }
    return 1;
}

int nnode_tz_get(char *out, size_t len)
{
    nvs_handle_t h;
    size_t need = len;
    if (!out || !len) {
        return -1;
    }
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        snprintf(out, len, "%s", TZ_DEFAULT);
        return 0;
    }
    if (nvs_get_str(h, "tz", out, &need) != ESP_OK) {
        nvs_close(h);
        snprintf(out, len, "%s", TZ_DEFAULT);
        return 0;
    }
    nvs_close(h);
    if (!valid_tz(out)) {
        snprintf(out, len, "%s", TZ_DEFAULT);
    }
    return 0;
}

int nnode_tz_save(const char *tz)
{
    nvs_handle_t h;
    if (!valid_tz(tz)) {
        return -1;
    }
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return -1;
    }
    if (nvs_set_str(h, "tz", tz) != ESP_OK ||
        nvs_commit(h) != ESP_OK) {
        nvs_close(h);
        return -1;
    }
    nvs_close(h);
    return 0;
}

int nnode_zonename_get(char *out, size_t len)
{
    nvs_handle_t h;
    size_t need = len;
    if (!out || !len) {
        return -1;
    }
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        out[0] = 0;
        return 0;
    }
    if (nvs_get_str(h, "tz_name", out, &need) != ESP_OK) {
        nvs_close(h);
        out[0] = 0;
        return 0;
    }
    nvs_close(h);
    if (!valid_tz(out)) {
        out[0] = 0;
    }
    return 0;
}

int nnode_zonename_save(const char *name)
{
    nvs_handle_t h;
    if (!valid_tz(name)) {
        return -1;
    }
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return -1;
    }
    if (nvs_set_str(h, "tz_name", name) != ESP_OK ||
        nvs_commit(h) != ESP_OK) {
        nvs_close(h);
        return -1;
    }
    nvs_close(h);
    return 0;
}

void nnode_tz_apply(void)
{
    char tz[NNODE_TZ_MAX + 1];
    nnode_tz_get(tz, sizeof(tz));
    setenv("TZ", tz, 1);
    tzset();
    ESP_LOGI(TAG, "tz -> %s", tz);
}

int nnode_factory_reset(void)
{
    nvs_handle_t h;
    if (nnode_elf_any_loaded()) {
        return -2;
    }
    if (esp_littlefs_format("storage") != ESP_OK) {
        return -1;
    }
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return -1;
    }
    nvs_erase_all(h);
    if (nvs_commit(h) != ESP_OK) {
        nvs_close(h);
        return -1;
    }
    nvs_close(h);
    ESP_LOGI(TAG, "factory reset: /data formatted, NVS wiped");
    return 0;
}
