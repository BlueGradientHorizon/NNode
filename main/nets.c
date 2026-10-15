#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "nets.h"

#define NS "nnode"

/* JSON string escaping for " and \ (ssid/pass range) */
static size_t esc(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    for (; *in && o + 2 < cap; in++) {
        if (*in == '"' || *in == '\\') {
            out[o++] = '\\';
        }
        out[o++] = *in;
    }
    out[o] = 0;
    return o;
}

/* Parse "key":"value" with unescape, value into out. Returns 0 on success. */
static int unesc_val(const char *s, const char *key, char *out, size_t cap)
{
    char pat[40];
    const char *p;
    size_t o = 0;
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    p = strstr(s, pat);
    if (!p) {
        return -1;
    }
    p += strlen(pat);
    while (*p && o + 1 < cap) {
        if (*p == '\\' && p[1]) {
            p++;
            out[o++] = *p++;
        } else if (*p == '"') {
            out[o] = 0;
            return 0;
        } else {
            out[o++] = *p++;
        }
    }
    out[o] = 0;
    return *p == '"' ? 0 : -1;
}

static int blob_get(const char *key, char *out, size_t cap)
{
    nvs_handle_t h;
    size_t len = cap;
    esp_err_t r;
    r = nvs_open(NS, NVS_READONLY, &h);
    if (r != ESP_OK) {
        ESP_LOGE("nnode_nvs", "open(%s)=%d", NS, (int)r);
        return -1;
    }
    r = nvs_get_blob(h, key, out, &len);
    nvs_close(h);
    if (r != ESP_OK) {
        ESP_LOGE("nnode_nvs", "get(%s)=%d", key, (int)r);
        return -1;
    }
    if (len >= cap) {
        len = cap - 1;
    }
    out[len] = 0;
    return 0;
}

static int blob_set(const char *key, const char *val)
{
    nvs_handle_t h;
    esp_err_t r;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return -1;
    }
    r = nvs_set_blob(h, key, val, strlen(val) + 1);
    if (r == ESP_OK) {
        r = nvs_commit(h);
    }
    nvs_close(h);
    return r == ESP_OK ? 0 : -1;
}

int nnode_bssid_parse(const char *s, uint8_t *m)
{
    unsigned int v[6];
    int i;
    if (!s || !m || sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x",
            &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
        return -1;
    }
    if (s[17] != 0) {
        return -1;
    }
    for (i = 0; i < 6; i++) {
        m[i] = (uint8_t)v[i];
    }
    return 0;
}

int nnode_auth_valid(const char *auth)
{
    return auth && (strcmp(auth, "auto") == 0 || strcmp(auth, "open") == 0 ||
        strcmp(auth, "wpa") == 0 || strcmp(auth, "wpa2") == 0 ||
        strcmp(auth, "wpa3") == 0);
}

int nnode_nets_load(nnode_net_cred_t *out, int max)
{
    char *blob = malloc(2048);
    const char *p;
    int n = 0;
    if (!blob) {
        return 0;
    }
    if (!out || max <= 0) {
        free(blob);
        return 0;
    }
    if (blob_get("nets", blob, 2048) != 0) {
        free(blob);
        return 0;
    }
    p = blob;
    while (n < max) {
        const char *o = strchr(p, '{');
        const char *e;
        char item[160];
        if (!o) {
            break;
        }
        e = strchr(o, '}');
        if (!e || (size_t)(e - o) >= sizeof(item)) {
            break;
        }
        memcpy(item, o, (size_t)(e - o) + 1);
        item[e - o + 1] = 0;
        if (unesc_val(item, "ssid", out[n].ssid, sizeof(out[n].ssid)) == 0 &&
            unesc_val(item, "pass", out[n].pass, sizeof(out[n].pass)) == 0 &&
            out[n].ssid[0]) {
            /* bssid/auth появились позже: у старых записей их нет */
            if (unesc_val(item, "bssid", out[n].bssid, sizeof(out[n].bssid)) != 0) {
                out[n].bssid[0] = 0;
            }
            if (unesc_val(item, "auth", out[n].auth, sizeof(out[n].auth)) != 0 ||
                !nnode_auth_valid(out[n].auth)) {
                strncpy(out[n].auth, "auto", sizeof(out[n].auth) - 1);
            }
            n++;
        }
        p = e + 1;
    }
    free(blob);
    return n;
}

int nnode_nets_save(const nnode_net_cred_t *nets, int n)
{
    char *blob = malloc(2048);
    char es[130];
    size_t o = 0;
    int i, rc;
    if (!blob) {
        return -1;
    }
    if (n < 0) {
        n = 0;
    }
    if (n > NNODE_MAX_NETS) {
        n = NNODE_MAX_NETS;
    }
    o += snprintf(blob + o, 2048 - o, "[");
    for (i = 0; i < n && o < 2048; i++) {
        esc(nets[i].ssid, es, sizeof(es));
        o += snprintf(blob + o, 2048 - o, "%s{\"ssid\":\"%s\",", i ? "," : "", es);
        esc(nets[i].pass, es, sizeof(es));
        o += snprintf(blob + o, 2048 - o, "\"pass\":\"%s\",", es);
        esc(nets[i].bssid, es, sizeof(es));
        o += snprintf(blob + o, 2048 - o, "\"bssid\":\"%s\",", es);
        esc(nnode_auth_valid(nets[i].auth) ? nets[i].auth : "auto", es, sizeof(es));
        o += snprintf(blob + o, 2048 - o, "\"auth\":\"%s\"}", es);
    }
    if (o < 2048) {
        o += snprintf(blob + o, 2048 - o, "]");
    }
    rc = blob_set("nets", blob);
    free(blob);
    return rc;
}

/* bssid NULL/"" = любой, auth NULL/"" = "auto".
 * open требует пустой пароль, wpa/wpa2/wpa3 - непустой (до 63). */
static int cred_fix_bssid_auth(nnode_net_cred_t *c, const char *bssid, const char *auth)
{
    if (bssid && bssid[0]) {
        uint8_t m[6];
        if (nnode_bssid_parse(bssid, m) != 0) {
            return -1;
        }
        strncpy(c->bssid, bssid, sizeof(c->bssid) - 1);
        c->bssid[sizeof(c->bssid) - 1] = 0;
    } else if (bssid) {
        c->bssid[0] = 0;
    }
    if (auth && auth[0]) {
        if (!nnode_auth_valid(auth)) {
            return -1;
        }
        strncpy(c->auth, auth, sizeof(c->auth) - 1);
        c->auth[sizeof(c->auth) - 1] = 0;
    } else if (auth) {
        strncpy(c->auth, "auto", sizeof(c->auth) - 1);
    }
    if (strcmp(c->auth, "open") == 0 && c->pass[0]) {
        return -1;
    }
    if ((strcmp(c->auth, "wpa") == 0 || strcmp(c->auth, "wpa2") == 0 ||
            strcmp(c->auth, "wpa3") == 0) && !c->pass[0]) {
        return -1;
    }
    return 0;
}

int nnode_nets_add(const char *ssid, const char *pass, const char *bssid, const char *auth)
{
    nnode_net_cred_t nets[NNODE_MAX_NETS];
    int n, i;
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || !pass || strlen(pass) > 63) {
        return -1;
    }
    n = nnode_nets_load(nets, NNODE_MAX_NETS);
    for (i = 0; i < n; i++) {
        if (strcmp(nets[i].ssid, ssid) == 0) {
            strncpy(nets[i].pass, pass, sizeof(nets[i].pass) - 1);
            nets[i].pass[sizeof(nets[i].pass) - 1] = 0;
            if (cred_fix_bssid_auth(&nets[i], bssid, auth) != 0) {
                return -1;
            }
            return nnode_nets_save(nets, n) == 0 ? n : -1;
        }
    }
    if (n >= NNODE_MAX_NETS) {
        return -1;
    }
    strncpy(nets[n].ssid, ssid, sizeof(nets[n].ssid) - 1);
    nets[n].ssid[sizeof(nets[n].ssid) - 1] = 0;
    strncpy(nets[n].pass, pass, sizeof(nets[n].pass) - 1);
    nets[n].pass[sizeof(nets[n].pass) - 1] = 0;
    nets[n].bssid[0] = 0;
    strncpy(nets[n].auth, "auto", sizeof(nets[n].auth) - 1);
    if (cred_fix_bssid_auth(&nets[n], bssid, auth) != 0) {
        return -1;
    }
    return nnode_nets_save(nets, n + 1) == 0 ? n + 1 : -1;
}

int nnode_nets_forget(const char *ssid)
{
    nnode_net_cred_t nets[NNODE_MAX_NETS];
    int n, i, j;
    if (!ssid) {
        return -1;
    }
    n = nnode_nets_load(nets, NNODE_MAX_NETS);
    for (i = 0; i < n; i++) {
        if (strcmp(nets[i].ssid, ssid) == 0) {
            for (j = i; j + 1 < n; j++) {
                nets[j] = nets[j + 1];
            }
            return nnode_nets_save(nets, n - 1);
        }
    }
    return -1;
}

int nnode_nets_get(const char *ssid, nnode_net_cred_t *out)
{
    nnode_net_cred_t nets[NNODE_MAX_NETS];
    int n, i;
    if (!ssid || !out) {
        return -1;
    }
    n = nnode_nets_load(nets, NNODE_MAX_NETS);
    for (i = 0; i < n; i++) {
        if (strcmp(nets[i].ssid, ssid) == 0) {
            *out = nets[i];
            return 0;
        }
    }
    return -1;
}

int nnode_nets_update(const char *old, const char *new_ssid,
    const char *pass, int set_pass,
    const char *bssid, int set_bssid,
    const char *auth, int set_auth)
{
    nnode_net_cred_t nets[NNODE_MAX_NETS];
    int n, i, idx = -1;
    if (!old || !old[0]) {
        return -1;
    }
    n = nnode_nets_load(nets, NNODE_MAX_NETS);
    for (i = 0; i < n; i++) {
        if (strcmp(nets[i].ssid, old) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return -1;
    }
    if (new_ssid && new_ssid[0] && strcmp(new_ssid, old) != 0) {
        int j;
        if (strlen(new_ssid) > 32) {
            return -1;
        }
        for (j = 0; j < n; j++) {
            if (j != idx && strcmp(nets[j].ssid, new_ssid) == 0) {
                return -2;
            }
        }
        strncpy(nets[idx].ssid, new_ssid, sizeof(nets[idx].ssid) - 1);
        nets[idx].ssid[sizeof(nets[idx].ssid) - 1] = 0;
    }
    if (set_pass) {
        if (!pass || strlen(pass) > 63) {
            return -1;
        }
        strncpy(nets[idx].pass, pass, sizeof(nets[idx].pass) - 1);
        nets[idx].pass[sizeof(nets[idx].pass) - 1] = 0;
    }
    if (cred_fix_bssid_auth(&nets[idx],
            set_bssid ? bssid : NULL, set_auth ? auth : NULL) != 0) {
        return -1;
    }
    return nnode_nets_save(nets, n);
}

int nnode_last_get(char *ssid, unsigned len)
{
    nvs_handle_t h;
    size_t l = len;
    if (!ssid || !len) {
        return -1;
    }
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return -1;
    }
    if (nvs_get_str(h, "last", ssid, &l) != ESP_OK) {
        nvs_close(h);
        return -1;
    }
    nvs_close(h);
    return 0;
}

int nnode_last_set(const char *ssid)
{
    nvs_handle_t h;
    esp_err_t r;
    if (!ssid) {
        return -1;
    }
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return -1;
    }
    r = nvs_set_str(h, "last", ssid);
    if (r == ESP_OK) {
        r = nvs_commit(h);
    }
    nvs_close(h);
    return r == ESP_OK ? 0 : -1;
}

int nnode_ap_load(nnode_ap_cfg_t *out)
{
    char blob[256];
    if (!out) {
        return -1;
    }
    if (blob_get("ap", blob, sizeof(blob)) != 0) {
        return -1;
    }
    if (unesc_val(blob, "ssid", out->ssid, sizeof(out->ssid)) != 0 || !out->ssid[0]) {
        return -1;
    }
    if (unesc_val(blob, "auth", out->auth, sizeof(out->auth)) != 0) {
        return -1;
    }
    if (unesc_val(blob, "pass", out->pass, sizeof(out->pass)) != 0) {
        return -1;
    }
    if (unesc_val(blob, "mode", out->mode, sizeof(out->mode)) != 0) {
        strncpy(out->mode, "auto", sizeof(out->mode) - 1);
    }
    {
        const char *p = strstr(blob, "\"nat\":");
        out->nat = (p && atoi(p + 6)) ? 1 : 0;
    }
    return 0;
}

int nnode_ap_save(const nnode_ap_cfg_t *cfg)
{
    char blob[256];
    char es[130];
    size_t o = 0;
    if (!cfg || !cfg->ssid[0]) {
        return -1;
    }
    o += snprintf(blob + o, sizeof(blob) - o, "{");
    esc(cfg->ssid, es, sizeof(es));
    o += snprintf(blob + o, sizeof(blob) - o, "\"ssid\":\"%s\",", es);
    esc(cfg->auth, es, sizeof(es));
    o += snprintf(blob + o, sizeof(blob) - o, "\"auth\":\"%s\",", es);
    esc(cfg->pass, es, sizeof(es));
    o += snprintf(blob + o, sizeof(blob) - o, "\"pass\":\"%s\",", es);
    esc(cfg->mode[0] ? cfg->mode : "auto", es, sizeof(es));
    o += snprintf(blob + o, sizeof(blob) - o, "\"mode\":\"%s\",", es);
    o += snprintf(blob + o, sizeof(blob) - o, "\"nat\":%d}", cfg->nat ? 1 : 0);
    return blob_set("ap", blob);
}

int nnode_ap_clear(void)
{
    nvs_handle_t h;
    esp_err_t r;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return -1;
    }
    r = nvs_erase_key(h, "ap");
    if (r == ESP_OK || r == ESP_ERR_NVS_NOT_FOUND) {
        r = nvs_commit(h);
    }
    nvs_close(h);
    return r == ESP_OK ? 0 : -1;
}
