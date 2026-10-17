#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_littlefs.h"
#include "esp_system.h"
#include "esp_sntp.h"
#include "cfg.h"
#include "cfg_types.h"
#include "cfg_schema.h"
#include "fw_defaults.h"
#include "wifi.h"
#include "net.h"
#include "files.h"
#include "syscfg.h"

static const char *TAG = "nnode_sys";

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

/* In-memory image of the device package. */
typedef struct {
    char devid[8];
    char hostname[NNODE_HOSTNAME_MAX + 1];
    int has_devid;
    int has_hostname;
} device_ctx_t;

static void device_load(device_ctx_t *c)
{
    memset(c, 0, sizeof(*c));
    if (nnode_schema_lookup(NNOODE_CFG_PKG_DEVICE, "devid",
            c->devid, sizeof(c->devid)) == 0) {
        c->has_devid = 1;
    }
    if (nnode_schema_lookup(NNOODE_CFG_PKG_DEVICE, "hostname",
            c->hostname, sizeof(c->hostname)) == 0) {
        c->has_hostname = 1;
    }
}

static int device_store(const char *devid, const char *hostname)
{
    char keys[2][NNODE_SCHEMA_ID_MAX + 1];
    char vals[2][128];
    if (!devid || !hostname || strlen(devid) >= 8 || strlen(hostname) > 32) {
        return -1;
    }
    memcpy(keys[0], "devid", 6);
    memcpy(vals[0], devid, strlen(devid) + 1);
    memcpy(keys[1], "hostname", 9);
    memcpy(vals[1], hostname, strlen(hostname) + 1);
    return nnode_schema_store(nnode_fw_defaults(NNOODE_CFG_PKG_DEVICE),
        keys, vals, 2);
}

void nnode_devid(char out[5])
{
    static const char ABC[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    device_ctx_t c;
    int i;
    device_load(&c);
    if (c.has_devid && valid_devid(c.devid)) {
        memcpy(out, c.devid, 5);
        return;
    }
    for (i = 0; i < 4; i++) {
        out[i] = ABC[esp_random() % (sizeof(ABC) - 1)];
    }
    out[4] = 0;
    {
        char host[NNODE_HOSTNAME_MAX + 1];
        if (!c.has_hostname || c.hostname[0] == 0) {
            snprintf(host, sizeof(host), "nnode-%s", out);
        } else {
            snprintf(host, sizeof(host), "%s", c.hostname);
        }
        if (device_store(out, host) != 0) {
            ESP_LOGW(TAG, "devid store failed");
        }
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
    device_ctx_t c;
    char def[NNODE_HOSTNAME_MAX + 1];
    if (!out || !len) {
        return -1;
    }
    nnode_hostname_default(def, sizeof(def));
    device_load(&c);
    if (!c.has_hostname || !valid_hostname(c.hostname, NNODE_HOSTNAME_MAX)) {
        snprintf(out, len, "%s", def);
        return 0;
    }
    snprintf(out, len, "%s", c.hostname);
    return 0;
}

int nnode_hostname_save(const char *name)
{
    device_ctx_t c;
    char id[5];
    if (!valid_hostname(name, NNODE_HOSTNAME_MAX)) {
        return -1;
    }
    device_load(&c);
    if (c.has_devid && valid_devid(c.devid)) {
        memcpy(id, c.devid, sizeof(id));
    } else {
        nnode_devid(id);
    }
    return device_store(id, name);
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

/* In-memory image of the time package. */
typedef struct {
    int enabled;
    char servers[NNODE_NTP_MAX][NNODE_NTP_SRV_LEN];
    int nservers;
    char tz[NNODE_TZ_MAX + 1];
    char zonename[NNODE_TZ_MAX + 1];
} time_ctx_t;

static void time_load(time_ctx_t *c)
{
    char b[16], key[8];
    int i;
    memset(c, 0, sizeof(*c));
    if (nnode_schema_lookup(NNOODE_CFG_PKG_TIME, "enabled", b, sizeof(b)) == 0) {
        c->enabled = b[0] == '1' ? 1 : 0;
    }
    if (nnode_schema_lookup(NNOODE_CFG_PKG_TIME, "tz", c->tz,
            sizeof(c->tz)) != 0) {
        c->tz[0] = 0;
    }
    if (nnode_schema_lookup(NNOODE_CFG_PKG_TIME, "zonename", c->zonename,
            sizeof(c->zonename)) != 0) {
        c->zonename[0] = 0;
    }
    for (i = 0; i < NNODE_NTP_MAX; i++) {
        snprintf(key, sizeof(key), "server%d", i + 1);
        if (nnode_schema_lookup(NNOODE_CFG_PKG_TIME, key,
                c->servers[c->nservers],
                sizeof(c->servers[c->nservers])) != 0 ||
            !c->servers[c->nservers][0]) {
            c->servers[c->nservers][0] = 0;
            continue;
        }
        c->nservers++;
    }
}

static int time_store(const time_ctx_t *c)
{
    char keys[6][NNODE_SCHEMA_ID_MAX + 1];
    char vals[6][128];
    char key[12];
    int i;
    if (!c) {
        return -1;
    }
    memcpy(keys[0], "tz", 3);
    if (c->tz[0]) {
        if (strlen(c->tz) > NNODE_TZ_MAX) {
            return -1;
        }
        memcpy(vals[0], c->tz, strlen(c->tz) + 1);
    } else {
        memcpy(vals[0], "UTC0", 5);
    }
    memcpy(keys[1], "zonename", 9);
    if (strlen(c->zonename) > NNODE_TZ_MAX) {
        return -1;
    }
    memcpy(vals[1], c->zonename, strlen(c->zonename) + 1);
    memcpy(keys[2], "enabled", 8);
    memcpy(vals[2], c->enabled ? "1" : "0", 2);
    for (i = 0; i < NNODE_NTP_MAX; i++) {
        snprintf(key, sizeof(key), "server%d", i + 1);
        memcpy(keys[3 + i], key, strlen(key) + 1);
        if (i < c->nservers && c->servers[i][0]) {
            size_t n = strlen(c->servers[i]);
            if (n >= NNODE_NTP_SRV_LEN) {
                return -1;
            }
            memcpy(vals[3 + i], c->servers[i], n + 1);
        } else {
            vals[3 + i][0] = 0;
        }
    }
    return nnode_schema_store(nnode_fw_defaults(NNOODE_CFG_PKG_TIME),
        keys, vals, 6);
}

int nnode_ntp_get(int *enabled, char servers[][NNODE_NTP_SRV_LEN], int max)
{
    time_ctx_t c;
    int i, n = 0;
    time_load(&c);
    if (enabled) {
        *enabled = c.enabled ? 1 : 0;
    }
    if (servers && max > 0) {
        for (i = 0; i < max; i++) {
            servers[i][0] = 0;
        }
        for (i = 0; i < c.nservers && n < max; i++) {
            if (valid_hostname(c.servers[i], NNODE_NTP_SRV_LEN - 1)) {
                snprintf(servers[n], NNODE_NTP_SRV_LEN, "%s", c.servers[i]);
                n++;
            }
        }
    }
    return n;
}

int nnode_ntp_save(int enabled, const char *servers_blob)
{
    time_ctx_t c;
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
    time_load(&c);
    c.enabled = enabled ? 1 : 0;
    c.nservers = 0;
    for (i = 0; i < n; i++) {
        snprintf(c.servers[i], sizeof(c.servers[i]), "%s", srvs[i]);
        c.nservers++;
    }
    if (!c.tz[0]) {
        snprintf(c.tz, sizeof(c.tz), "UTC0");
    }
    if (time_store(&c) != 0) {
        return -1;
    }
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
        /* Same-size copy; precision cap lets the compiler prove it fits. */
        snprintf(s_srv[i], sizeof(s_srv[i]), "%.*s",
            (int)sizeof(s_srv[i]) - 1, srvs[i]);
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
    time_ctx_t c;
    if (!out || !len) {
        return -1;
    }
    time_load(&c);
    if (!c.tz[0] || !valid_tz(c.tz)) {
        snprintf(out, len, "%s", TZ_DEFAULT);
        return 0;
    }
    snprintf(out, len, "%s", c.tz);
    return 0;
}

int nnode_tz_save(const char *tz)
{
    time_ctx_t c;
    if (!valid_tz(tz)) {
        return -1;
    }
    time_load(&c);
    snprintf(c.tz, sizeof(c.tz), "%s", tz);

    return time_store(&c);
}

int nnode_zonename_get(char *out, size_t len)
{
    time_ctx_t c;
    if (!out || !len) {
        return -1;
    }
    time_load(&c);
    if (!c.zonename[0] || !valid_tz(c.zonename)) {
        out[0] = 0;
        return 0;
    }
    snprintf(out, len, "%s", c.zonename);
    return 0;
}

int nnode_zonename_save(const char *name)
{
    time_ctx_t c;
    if (!valid_tz(name)) {
        return -1;
    }
    time_load(&c);
    snprintf(c.zonename, sizeof(c.zonename), "%s", name);
    if (!c.tz[0]) {
        snprintf(c.tz, sizeof(c.tz), TZ_DEFAULT);
    }
    return time_store(&c);
}

void nnode_tz_apply(void)
{
    char tz[NNODE_TZ_MAX + 1];
    nnode_tz_get(tz, sizeof(tz));
    setenv("TZ", tz, 1);
    tzset();
    ESP_LOGI(TAG, "tz -> %s", tz);
}

static const char *kv_find(char keys[][NNODE_SCHEMA_ID_MAX + 1],
    char vals[][128], int n, const char *key)
{
    int i;
    for (i = 0; i < n; i++) {
        if (strcmp(keys[i], key) == 0) {
            return vals[i];
        }
    }
    return NULL;
}

int nnode_device_validate(const char *text)
{
    const char *def = nnode_fw_defaults(NNOODE_CFG_PKG_DEVICE);
    /* Heap: restore runs next to 8 KB staged texts. */
    char (*keys)[NNODE_SCHEMA_ID_MAX + 1] = malloc(sizeof(*keys) * 4);
    char (*vals)[128] = malloc(sizeof(*vals) * 4);
    const char *v;
    int n = 0, rc = -1;
    if (!keys || !vals) {
        goto out;
    }
    if (!text || nnode_schema_validate(def, text) != 0 ||
        nnode_schema_kv(NNOODE_CFG_PKG_DEVICE, text, keys, vals, &n) != 0) {
        goto out;
    }
    v = kv_find(keys, vals, n, "devid");
    if (!v || !valid_devid(v)) {
        goto out;
    }
    v = kv_find(keys, vals, n, "hostname");
    if (!v || !valid_hostname(v, NNODE_HOSTNAME_MAX)) {
        goto out;
    }
    rc = 0;
out:
    free(keys);
    free(vals);
    return rc;
}

int nnode_time_validate(const char *text)
{
    const char *def = nnode_fw_defaults(NNOODE_CFG_PKG_TIME);
    char (*keys)[NNODE_SCHEMA_ID_MAX + 1] = malloc(sizeof(*keys) * 8);
    char (*vals)[128] = malloc(sizeof(*vals) * 8);
    const char *v;
    int n = 0, rc = -1;
    if (!keys || !vals) {
        goto out;
    }
    if (!text || nnode_schema_validate(def, text) != 0 ||
        nnode_schema_kv(NNOODE_CFG_PKG_TIME, text, keys, vals, &n) != 0) {
        goto out;
    }
    v = kv_find(keys, vals, n, "tz");
    if (!v || !valid_tz(v)) {
        goto out;
    }
    v = kv_find(keys, vals, n, "zonename");
    if (!v || (v[0] && !valid_tz(v))) {
        goto out;
    }
    {
        int i;
        char key[12];
        for (i = 1; i <= NNODE_NTP_MAX; i++) {
            snprintf(key, sizeof(key), "server%d", i);
            v = kv_find(keys, vals, n, key);
            if (!v || (v[0] &&
                    !valid_hostname(v, NNODE_NTP_SRV_LEN - 1))) {
                goto out;
            }
        }
    }
    rc = 0;
out:
    free(keys);
    free(vals);
    return rc;
}

void nnode_cfg_seed_defaults(void)
{
    char id[5];
    /* Device id first: the AP default SSID derives from it. Missing or
     * foreign files heal here: ensure recreates blank shapes, the domain
     * loaders below fill real values when the stored ones are unusable. */
    nnode_devid(id);
    nnode_schema_ensure_text(nnode_fw_defaults(NNOODE_CFG_PKG_TIME));
    nnode_schema_ensure_text(nnode_fw_defaults(NNOODE_CFG_PKG_STA));
    nnode_schema_ensure_text(nnode_fw_defaults(NNOODE_CFG_PKG_NETS));
    {
        nnode_ap_cfg_t ap;
        if (nnode_ap_load(&ap) != 0) {
            nnode_ap_default(ap.ssid, sizeof(ap.ssid));
            snprintf(ap.auth, sizeof(ap.auth), "open");
            ap.pass[0] = 0;
            snprintf(ap.mode, sizeof(ap.mode), "auto");
            ap.nat = 0;
            if (nnode_ap_save(&ap) != 0) {
                ESP_LOGW(TAG, "seed ap failed");
            }
        }
    }
}

int nnode_factory_reset(void)
{
    if (nnode_elf_any_loaded()) {
        return -2;
    }
    /* Config files live on /data, so formatting wipes settings,
     * uploads and module files in one go. No separate store left. */
    if (esp_littlefs_format("storage") != ESP_OK) {
        return -1;
    }
    ESP_LOGI(TAG, "factory reset: /data formatted");
    return 0;
}
