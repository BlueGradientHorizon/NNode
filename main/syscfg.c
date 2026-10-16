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
    char sectype[NNOODE_CFG_TYPE_MAX];
    char secname[NNOODE_CFG_NAME_MAX];
    int in_device;
} device_ctx_t;

static int device_section(void *ctx, const char *type, const char *name, int has_name)
{
    device_ctx_t *c = ctx;
    snprintf(c->sectype, sizeof(c->sectype), "%s", type);
    snprintf(c->secname, sizeof(c->secname), "%s", has_name ? name : "");
    c->in_device = strcmp(type, "device") == 0;
    return 0;
}

static int device_option(void *ctx, const char *key, const char *value)
{
    device_ctx_t *c = ctx;
    if (!c->in_device) {
        return 0;
    }
    if (strcmp(key, "devid") == 0) {
        if (strlen(value) >= sizeof(c->devid)) {
            return -1;
        }
        snprintf(c->devid, sizeof(c->devid), "%s", value);
        c->has_devid = 1;
    } else if (strcmp(key, "hostname") == 0) {
        if (strlen(value) >= sizeof(c->hostname)) {
            return -1;
        }
        snprintf(c->hostname, sizeof(c->hostname), "%s", value);
        c->has_hostname = 1;
    }
    return 0;
}

static const nnode_cfg_cb_t device_cb = { device_section, device_option, NULL };

static int device_load(device_ctx_t *c)
{
    memset(c, 0, sizeof(*c));
    if (nnode_cfg_parse(NNOODE_CFG_PKG_DEVICE, &device_cb, c) != 0) {
        memset(c, 0, sizeof(*c));
        return -1;
    }
    return 0;
}

static int device_store(const char *devid, const char *hostname)
{
    char ed[16], eh[NNODE_HOSTNAME_MAX * 2 + 8];
    char text[256];
    if (nnode_cfg_escape(devid, ed, sizeof(ed)) != 0 ||
        nnode_cfg_escape(hostname, eh, sizeof(eh)) != 0) {
        return -1;
    }
    snprintf(text, sizeof(text),
        "package device\n"
        "config device 'device'\n"
        "\toption devid '%s'\n"
        "\toption hostname '%s'\n",
        ed, eh);
    return nnode_cfg_write(NNOODE_CFG_PKG_DEVICE, text);
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
    int has_enabled;
    char servers[NNODE_NTP_MAX][NNODE_NTP_SRV_LEN];
    int nservers;
    char tz[NNODE_TZ_MAX + 1];
    char zonename[NNODE_TZ_MAX + 1];
    int has_tz;
    int has_zonename;
    char sectype[NNOODE_CFG_TYPE_MAX];
} time_ctx_t;

static int time_section(void *ctx, const char *type, const char *name, int has_name)
{
    time_ctx_t *c = ctx;
    (void)name;
    (void)has_name;
    snprintf(c->sectype, sizeof(c->sectype), "%s", type);
    return 0;
}

static int time_option(void *ctx, const char *key, const char *value)
{
    time_ctx_t *c = ctx;
    if (strcmp(c->sectype, "clock") == 0) {
        if (strcmp(key, "tz") == 0) {
            if (strlen(value) > NNODE_TZ_MAX) {
                return -1;
            }
            snprintf(c->tz, sizeof(c->tz), "%s", value);
            c->has_tz = 1;
        } else if (strcmp(key, "zonename") == 0) {
            if (strlen(value) > NNODE_TZ_MAX) {
                return -1;
            }
            snprintf(c->zonename, sizeof(c->zonename), "%s", value);
            c->has_zonename = 1;
        }
    } else if (strcmp(c->sectype, "ntp") == 0) {
        if (strcmp(key, "enabled") == 0) {
            c->enabled = value[0] == '1' ? 1 : 0;
            c->has_enabled = 1;
        }
    }
    return 0;
}

static int time_list(void *ctx, const char *key, const char *value)
{
    time_ctx_t *c = ctx;
    if (strcmp(c->sectype, "ntp") == 0 && strcmp(key, "server") == 0) {
        if (c->nservers >= NNODE_NTP_MAX || strlen(value) >= NNODE_NTP_SRV_LEN) {
            return -1;
        }
        snprintf(c->servers[c->nservers], sizeof(c->servers[0]), "%s", value);
        c->nservers++;
    }
    return 0;
}

static const nnode_cfg_cb_t time_cb = { time_section, time_option, time_list };

static void time_load(time_ctx_t *c)
{
    memset(c, 0, sizeof(*c));
    if (nnode_cfg_parse(NNOODE_CFG_PKG_TIME, &time_cb, c) != 0) {
        memset(c, 0, sizeof(*c));
    }
}

static int time_store(const time_ctx_t *c)
{
    char text[768];
    size_t o = 0;
    char e[NNODE_TZ_MAX * 2 + 8];
    int i;
    o += snprintf(text + o, sizeof(text) - o,
        "package time\n"
        "config clock 'clock'\n");
    if (nnode_cfg_escape(c->tz[0] ? c->tz : "UTC0", e, sizeof(e)) != 0) {
        return -1;
    }
    o += snprintf(text + o, sizeof(text) - o, "\toption tz '%s'\n", e);
    if (nnode_cfg_escape(c->zonename, e, sizeof(e)) != 0) {
        return -1;
    }
    o += snprintf(text + o, sizeof(text) - o, "\toption zonename '%s'\n", e);
    o += snprintf(text + o, sizeof(text) - o,
        "config ntp 'ntp'\n"
        "\toption enabled '%d'\n", c->enabled ? 1 : 0);
    for (i = 0; i < c->nservers && o < sizeof(text); i++) {
        if (nnode_cfg_escape(c->servers[i], e, sizeof(e)) != 0) {
            return -1;
        }
        o += snprintf(text + o, sizeof(text) - o, "\tlist server '%s'\n", e);
    }
    if (o >= sizeof(text)) {
        return -1;
    }
    return nnode_cfg_write(NNOODE_CFG_PKG_TIME, text);
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
    if (!c.has_tz) {
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
    if (!c.has_tz || !valid_tz(c.tz)) {
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
    c.has_tz = 1;
    if (!c.has_zonename) {
        c.zonename[0] = 0;
    }
    return time_store(&c);
}

int nnode_zonename_get(char *out, size_t len)
{
    time_ctx_t c;
    if (!out || !len) {
        return -1;
    }
    time_load(&c);
    if (!c.has_zonename || !valid_tz(c.zonename)) {
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
    c.has_zonename = 1;
    if (!c.has_tz) {
        snprintf(c.tz, sizeof(c.tz), "%s", TZ_DEFAULT);
        c.has_tz = 1;
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

int nnode_device_validate(const char *text)
{
    device_ctx_t c;
    static const nnode_cfg_cb_t cb = { device_section, device_option, NULL };
    if (!text) {
        return -1;
    }
    memset(&c, 0, sizeof(c));
    if (nnode_cfg_parse_text(NNOODE_CFG_PKG_DEVICE, text, &cb, &c) != 0) {
        return -1;
    }
    if (!c.has_devid || !valid_devid(c.devid)) {
        return -1;
    }
    if (!c.has_hostname || !valid_hostname(c.hostname, NNODE_HOSTNAME_MAX)) {
        return -1;
    }
    return 0;
}

typedef struct {
    char sectype[NNOODE_CFG_TYPE_MAX];
    char tz[NNODE_TZ_MAX + 1];
    char zonename[NNODE_TZ_MAX + 1];
    int has_tz;
    int has_zonename;
    int has_enabled;
    int enabled_ok;
    char servers[NNODE_NTP_MAX][NNODE_NTP_SRV_LEN];
    int nservers;
    int bad;
} time_check_t;

static int time_check_section(void *ctx, const char *type, const char *name, int has)
{
    time_check_t *c = ctx;
    (void)name;
    (void)has;
    if (strcmp(type, "clock") != 0 && strcmp(type, "ntp") != 0) {
        c->bad = 1;
        return -1;
    }
    snprintf(c->sectype, sizeof(c->sectype), "%s", type);
    return 0;
}

static int time_check_option(void *ctx, const char *key, const char *value)
{
    time_check_t *c = ctx;
    if (strcmp(c->sectype, "clock") == 0) {
        if (strcmp(key, "tz") == 0) {
            if (strlen(value) > NNODE_TZ_MAX || !valid_tz(value)) {
                return -1;
            }
            snprintf(c->tz, sizeof(c->tz), "%s", value);
            c->has_tz = 1;
        } else if (strcmp(key, "zonename") == 0) {
            if (strlen(value) > NNODE_TZ_MAX ||
                (value[0] && !valid_tz(value))) {
                return -1;
            }
            snprintf(c->zonename, sizeof(c->zonename), "%s", value);
            c->has_zonename = 1;
        }
    } else if (strcmp(c->sectype, "ntp") == 0) {
        if (strcmp(key, "enabled") == 0) {
            if ((value[0] != '0' && value[0] != '1') || value[1] != 0) {
                return -1;
            }
            c->has_enabled = 1;
            c->enabled_ok = 1;
        }
    }
    return 0;
}

static int time_check_list(void *ctx, const char *key, const char *value)
{
    time_check_t *c = ctx;
    if (strcmp(c->sectype, "ntp") == 0 && strcmp(key, "server") == 0) {
        if (c->nservers >= NNODE_NTP_MAX || strlen(value) >= NNODE_NTP_SRV_LEN ||
            !valid_hostname(value, NNODE_NTP_SRV_LEN - 1)) {
            return -1;
        }
        snprintf(c->servers[c->nservers], sizeof(c->servers[0]), "%s", value);
        c->nservers++;
        return 0;
    }
    return -1;
}

int nnode_time_validate(const char *text)
{
    time_check_t c;
    static const nnode_cfg_cb_t cb =
        { time_check_section, time_check_option, time_check_list };
    if (!text) {
        return -1;
    }
    memset(&c, 0, sizeof(c));
    if (nnode_cfg_parse_text(NNOODE_CFG_PKG_TIME, text, &cb, &c) != 0 || c.bad) {
        return -1;
    }
    if (c.has_tz && !valid_tz(c.tz)) {
        return -1;
    }
    if (!c.has_enabled) {
        return -1;
    }
    return 0;
}

void nnode_cfg_seed_defaults(void)
{
    char id[5];
    /* Generates and stores the id on first boot; leaves the file
     * alone when a valid id is already there. */
    nnode_devid(id);
    if (!nnode_cfg_exists(NNOODE_CFG_PKG_TIME)) {
        time_ctx_t c;
        memset(&c, 0, sizeof(c));
        snprintf(c.tz, sizeof(c.tz), "UTC0");
        c.has_tz = 1;
        if (time_store(&c) != 0) {
            ESP_LOGW(TAG, "seed time failed");
        }
    }
    if (!nnode_cfg_exists(NNOODE_CFG_PKG_STA)) {
        if (nnode_cfg_write(NNOODE_CFG_PKG_STA, "package sta\n") != 0) {
            ESP_LOGW(TAG, "seed sta failed");
        }
    }
    if (!nnode_cfg_exists(NNOODE_CFG_PKG_AP)) {
        nnode_ap_cfg_t ap;
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
