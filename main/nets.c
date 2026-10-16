#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "cfg.h"
#include "nets.h"

static const char *TAG = "nnode_cfg_sta";

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

/* Parse state. net entries go straight into the caller's array, so no
 * big struct ever sits on the stack (main task stack is ~3.5 KB). */
typedef struct {
    nnode_net_cred_t *nets;
    int cap;
    int nnets;
    char *last;
    int *has_last;
    int in_net;
    int in_last;
    int cur;
    int strict;
    int total;
} sta_ctx_t;

static int sta_section(void *ctx, const char *type, const char *name, int has_name)
{
    sta_ctx_t *c = ctx;
    (void)name;
    (void)has_name;
    c->in_net = 0;
    c->in_last = 0;
    if (strcmp(type, "net") == 0) {
        c->total++;
        if (c->nnets >= c->cap) {
            /* More sections than slots: extra ones are ignored. */
            c->cur = -1;
            c->in_net = 1;
            return 0;
        }
        c->cur = c->nnets++;
        if (c->nets) {
            memset(&c->nets[c->cur], 0, sizeof(c->nets[c->cur]));
            strncpy(c->nets[c->cur].auth, "auto", sizeof(c->nets[c->cur].auth) - 1);
        }
        c->in_net = 1;
    } else if (strcmp(type, "last") == 0) {
        c->in_last = 1;
    } else if (c->strict) {
        return -1;
    }
    return 0;
}

static int sta_option(void *ctx, const char *key, const char *value)
{
    sta_ctx_t *c = ctx;
    nnode_net_cred_t tmp_entry;
    nnode_net_cred_t *e;
    if (c->in_net) {
        if (c->cur < 0) {
            return 0;
        }
        /* Entries beyond the caller's array are still syntax-checked
         * into a throwaway slot. */
        if (c->nets) {
            e = &c->nets[c->cur];
        } else {
            memset(&tmp_entry, 0, sizeof(tmp_entry));
            e = &tmp_entry;
        }
        if (strcmp(key, "ssid") == 0) {
            if (strlen(value) > 32) {
                return -1;
            }
            snprintf(e->ssid, sizeof(e->ssid), "%s", value);
        } else if (strcmp(key, "pass") == 0) {
            if (strlen(value) > 63) {
                return -1;
            }
            snprintf(e->pass, sizeof(e->pass), "%s", value);
        } else if (strcmp(key, "bssid") == 0) {
            if (strlen(value) >= sizeof(e->bssid)) {
                return -1;
            }
            snprintf(e->bssid, sizeof(e->bssid), "%s", value);
        } else if (strcmp(key, "auth") == 0) {
            if (strlen(value) >= sizeof(e->auth)) {
                return -1;
            }
            snprintf(e->auth, sizeof(e->auth), "%s", value);
        }
    } else if (c->in_last) {
        if (strcmp(key, "ssid") == 0) {
            if (strlen(value) > 32) {
                return -1;
            }
            if (c->last) {
                snprintf(c->last, 33, "%s", value);
            }
            if (c->has_last) {
                *c->has_last = 1;
            }
        }
    }
    return 0;
}

static const nnode_cfg_cb_t sta_cb = { sta_section, sta_option, NULL };

/* Parse the sta package into caller storage. nets may be NULL (entries
 * skipped, last still parsed). fixup drops ssid-less entries and
 * repairs bssid/auth the same way load always did. */
static void sta_parse_all(nnode_net_cred_t *nets, int cap, int *nnets,
    char *last, int *has_last, int strict)
{
    sta_ctx_t c;
    int i;
    memset(&c, 0, sizeof(c));
    c.nets = nets;
    c.cap = cap < 0 ? 0 : cap;
    c.last = last;
    c.has_last = has_last;
    c.cur = -1;
    c.strict = strict;
    if (last) {
        last[0] = 0;
    }
    if (has_last) {
        *has_last = 0;
    }
    if (nnode_cfg_parse(NNOODE_CFG_PKG_STA, &sta_cb, &c) != 0) {
        if (nnets) {
            *nnets = 0;
        }
        if (last) {
            last[0] = 0;
        }
        if (has_last) {
            *has_last = 0;
        }
        return;
    }
    if (nets) {
        int w = 0;
        for (i = 0; i < c.nnets; i++) {
            if (!nets[i].ssid[0]) {
                continue;
            }
            if (!nnode_auth_valid(nets[i].auth)) {
                strncpy(nets[i].auth, "auto", sizeof(nets[i].auth) - 1);
            }
            if (nets[i].bssid[0]) {
                uint8_t m[6];
                if (nnode_bssid_parse(nets[i].bssid, m) != 0) {
                    nets[i].bssid[0] = 0;
                }
            }
            if (w != i) {
                nets[w] = nets[i];
            }
            w++;
        }
        if (nnets) {
            *nnets = w;
        }
    } else if (nnets) {
        *nnets = 0;
    }
}

static int sta_store_arrays(const nnode_net_cred_t *nets, int nnets,
    const char *last, int has_last)
{
    /* Heap buffer: httpd workers have 8 KB stacks, main task less. */
    char *text = malloc(4096);
    char e[160];
    size_t o = 0;
    int i;
    if (!text) {
        return -1;
    }
    o += snprintf(text + o, 4096 - o, "package sta\n");
    for (i = 0; i < nnets && o < 4096; i++) {
        o += snprintf(text + o, 4096 - o, "config net\n");
        if (nnode_cfg_escape(nets[i].ssid, e, sizeof(e)) != 0) {
            break;
        }
        o += snprintf(text + o, 4096 - o, "\toption ssid '%s'\n", e);
        if (nnode_cfg_escape(nets[i].pass, e, sizeof(e)) != 0) {
            break;
        }
        o += snprintf(text + o, 4096 - o, "\toption pass '%s'\n", e);
        if (nnode_cfg_escape(nets[i].bssid, e, sizeof(e)) != 0) {
            break;
        }
        o += snprintf(text + o, 4096 - o, "\toption bssid '%s'\n", e);
        if (nnode_cfg_escape(nnode_auth_valid(nets[i].auth) ?
                nets[i].auth : "auto", e, sizeof(e)) != 0) {
            break;
        }
        o += snprintf(text + o, 4096 - o, "\toption auth '%s'\n", e);
    }
    if (o < 4096 && has_last && last) {
        o += snprintf(text + o, 4096 - o, "config last 'last'\n");
        if (nnode_cfg_escape(last, e, sizeof(e)) != 0) {
            free(text);
            return -1;
        }
        o += snprintf(text + o, 4096 - o, "\toption ssid '%s'\n", e);
    }
    if (o >= 4096 || i < nnets) {
        free(text);
        return -1;
    }
    {
        int rc = nnode_cfg_write(NNOODE_CFG_PKG_STA, text);
        free(text);
        return rc;
    }
}

int nnode_nets_load(nnode_net_cred_t *out, int max)
{
    int n = 0;
    if (!out || max <= 0) {
        return 0;
    }
    sta_parse_all(out, max, &n, NULL, NULL, 0);
    return n;
}

int nnode_nets_save(const nnode_net_cred_t *nets, int n)
{
    char last[33];
    int has_last = 0;
    if (!nets && n > 0) {
        return -1;
    }
    if (n < 0) {
        n = 0;
    }
    if (n > NNODE_MAX_NETS) {
        n = NNODE_MAX_NETS;
    }
    /* last section is preserved as stored. */
    sta_parse_all(NULL, 0, NULL, last, &has_last, 0);
    return sta_store_arrays(nets, n, last, has_last);
}

/* bssid NULL/"" = any, auth NULL/"" = "auto".
 * open requires an empty password, wpa/wpa2/wpa3 a non-empty one (up to 63). */
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
    char last[33];
    int has_last = 0, nnets = 0, i;
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || !pass || strlen(pass) > 63) {
        return -1;
    }
    sta_parse_all(nets, NNODE_MAX_NETS, &nnets, last, &has_last, 0);
    for (i = 0; i < nnets; i++) {
        if (strcmp(nets[i].ssid, ssid) == 0) {
            strncpy(nets[i].pass, pass, sizeof(nets[i].pass) - 1);
            nets[i].pass[sizeof(nets[i].pass) - 1] = 0;
            if (cred_fix_bssid_auth(&nets[i], bssid, auth) != 0) {
                return -1;
            }
            return sta_store_arrays(nets, nnets, last, has_last) == 0 ? nnets : -1;
        }
    }
    if (nnets >= NNODE_MAX_NETS) {
        return -1;
    }
    strncpy(nets[nnets].ssid, ssid, sizeof(nets[nnets].ssid) - 1);
    nets[nnets].ssid[sizeof(nets[nnets].ssid) - 1] = 0;
    strncpy(nets[nnets].pass, pass, sizeof(nets[nnets].pass) - 1);
    nets[nnets].pass[sizeof(nets[nnets].pass) - 1] = 0;
    nets[nnets].bssid[0] = 0;
    strncpy(nets[nnets].auth, "auto", sizeof(nets[nnets].auth) - 1);
    if (cred_fix_bssid_auth(&nets[nnets], bssid, auth) != 0) {
        return -1;
    }
    nnets++;
    return sta_store_arrays(nets, nnets, last, has_last) == 0 ? nnets : -1;
}

int nnode_nets_forget(const char *ssid)
{
    nnode_net_cred_t nets[NNODE_MAX_NETS];
    char last[33];
    int has_last = 0, nnets = 0;
    int i, j;
    if (!ssid) {
        return -1;
    }
    sta_parse_all(nets, NNODE_MAX_NETS, &nnets, last, &has_last, 0);
    for (i = 0; i < nnets; i++) {
        if (strcmp(nets[i].ssid, ssid) == 0) {
            for (j = i; j + 1 < nnets; j++) {
                nets[j] = nets[j + 1];
            }
            nnets--;
            /* Forgetting the last-ok network clears the marker too. */
            if (has_last && strcmp(last, ssid) == 0) {
                has_last = 0;
                last[0] = 0;
            }
            return sta_store_arrays(nets, nnets, last, has_last);
        }
    }
    return -1;
}

int nnode_nets_get(const char *ssid, nnode_net_cred_t *out)
{
    nnode_net_cred_t nets[NNODE_MAX_NETS];
    int nnets = 0, i;
    if (!ssid || !out) {
        return -1;
    }
    sta_parse_all(nets, NNODE_MAX_NETS, &nnets, NULL, NULL, 0);
    for (i = 0; i < nnets; i++) {
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
    char last[33];
    int has_last = 0, nnets = 0;
    int i, idx = -1;
    if (!old || !old[0]) {
        return -1;
    }
    sta_parse_all(nets, NNODE_MAX_NETS, &nnets, last, &has_last, 0);
    for (i = 0; i < nnets; i++) {
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
        for (j = 0; j < nnets; j++) {
            if (j != idx && strcmp(nets[j].ssid, new_ssid) == 0) {
                return -2;
            }
        }
        strncpy(nets[idx].ssid, new_ssid, sizeof(nets[idx].ssid) - 1);
        nets[idx].ssid[sizeof(nets[idx].ssid) - 1] = 0;
        if (has_last && strcmp(last, old) == 0) {
            snprintf(last, sizeof(last), "%s", new_ssid);
        }
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
    return sta_store_arrays(nets, nnets, last, has_last);
}

int nnode_last_get(char *ssid, unsigned len)
{
    char last[33];
    int has_last = 0;
    if (!ssid || !len) {
        return -1;
    }
    sta_parse_all(NULL, 0, NULL, last, &has_last, 0);
    if (!has_last) {
        return -1;
    }
    snprintf(ssid, len, "%s", last);
    return 0;
}

int nnode_last_set(const char *ssid)
{
    nnode_net_cred_t nets[NNODE_MAX_NETS];
    char last[33];
    int has_last = 0, nnets = 0;
    if (!ssid || strlen(ssid) > 32) {
        return -1;
    }
    sta_parse_all(nets, NNODE_MAX_NETS, &nnets, last, &has_last, 0);
    snprintf(last, sizeof(last), "%s", ssid);
    has_last = 1;
    return sta_store_arrays(nets, nnets, last, has_last);
}

/* In-memory image of the ap package. */
typedef struct {
    nnode_ap_cfg_t ap;
    int has_ssid;
    int has_auth;
    int has_pass;
    int has_mode;
    int has_nat;
    int in_ap;
    int strict;
} ap_ctx_t;

static int ap_section(void *ctx, const char *type, const char *name, int has_name)
{
    ap_ctx_t *c = ctx;
    (void)name;
    (void)has_name;
    c->in_ap = strcmp(type, "ap") == 0;
    if (!c->in_ap && c->strict) {
        return -1;
    }
    return 0;
}

static int ap_option(void *ctx, const char *key, const char *value)
{
    ap_ctx_t *c = ctx;
    if (!c->in_ap) {
        return 0;
    }
    if (strcmp(key, "ssid") == 0) {
        if (strlen(value) > 32) {
            return -1;
        }
        snprintf(c->ap.ssid, sizeof(c->ap.ssid), "%s", value);
        c->has_ssid = 1;
    } else if (strcmp(key, "auth") == 0) {
        if (strlen(value) >= sizeof(c->ap.auth)) {
            return -1;
        }
        snprintf(c->ap.auth, sizeof(c->ap.auth), "%s", value);
        c->has_auth = 1;
    } else if (strcmp(key, "pass") == 0) {
        if (strlen(value) > 63) {
            return -1;
        }
        snprintf(c->ap.pass, sizeof(c->ap.pass), "%s", value);
        c->has_pass = 1;
    } else if (strcmp(key, "mode") == 0) {
        if (strlen(value) >= sizeof(c->ap.mode)) {
            return -1;
        }
        snprintf(c->ap.mode, sizeof(c->ap.mode), "%s", value);
        c->has_mode = 1;
    } else if (strcmp(key, "nat") == 0) {
        c->ap.nat = value[0] == '1' ? 1 : 0;
        c->has_nat = 1;
    }
    return 0;
}

static const nnode_cfg_cb_t ap_cb = { ap_section, ap_option, NULL };

int nnode_ap_load(nnode_ap_cfg_t *out)
{
    ap_ctx_t c;
    if (!out) {
        return -1;
    }
    memset(&c, 0, sizeof(c));
    if (nnode_cfg_parse(NNOODE_CFG_PKG_AP, &ap_cb, &c) != 0) {
        return -1;
    }
    if (!c.has_ssid || !c.ap.ssid[0] || !c.has_auth) {
        return -1;
    }
    if (!c.has_mode) {
        strncpy(c.ap.mode, "auto", sizeof(c.ap.mode) - 1);
    }
    *out = c.ap;
    ESP_LOGI(TAG, "ap load ssid=%s", c.ap.ssid);
    return 0;
}

int nnode_ap_save(const nnode_ap_cfg_t *cfg)
{
    char text[512];
    char e[160];
    size_t o = 0;
    if (!cfg || !cfg->ssid[0]) {
        return -1;
    }
    o += snprintf(text + o, sizeof(text) - o, "package ap\nconfig ap 'main'\n");
    if (nnode_cfg_escape(cfg->ssid, e, sizeof(e)) != 0) {
        return -1;
    }
    o += snprintf(text + o, sizeof(text) - o, "\toption ssid '%s'\n", e);
    if (nnode_cfg_escape(cfg->auth, e, sizeof(e)) != 0) {
        return -1;
    }
    o += snprintf(text + o, sizeof(text) - o, "\toption auth '%s'\n", e);
    if (nnode_cfg_escape(cfg->pass, e, sizeof(e)) != 0) {
        return -1;
    }
    o += snprintf(text + o, sizeof(text) - o, "\toption pass '%s'\n", e);
    if (nnode_cfg_escape(cfg->mode[0] ? cfg->mode : "auto", e, sizeof(e)) != 0) {
        return -1;
    }
    o += snprintf(text + o, sizeof(text) - o, "\toption mode '%s'\n", e);
    o += snprintf(text + o, sizeof(text) - o, "\toption nat '%d'\n", cfg->nat ? 1 : 0);
    if (o >= sizeof(text)) {
        return -1;
    }
    return nnode_cfg_write(NNOODE_CFG_PKG_AP, text);
}

int nnode_sta_validate(const char *text)
{
    nnode_net_cred_t nets[NNODE_MAX_NETS];
    sta_ctx_t c;
    int i, nnets;
    static const nnode_cfg_cb_t cb = { sta_section, sta_option, NULL };
    if (!text) {
        return -1;
    }
    memset(&c, 0, sizeof(c));
    memset(nets, 0, sizeof(nets));
    c.nets = nets;
    c.cap = NNODE_MAX_NETS;
    c.cur = -1;
    c.strict = 1;
    if (nnode_cfg_parse_text(NNOODE_CFG_PKG_STA, text, &cb, &c) != 0) {
        return -1;
    }
    if (c.total > NNODE_MAX_NETS) {
        return -1;
    }
    nnets = c.nnets > NNODE_MAX_NETS ? NNODE_MAX_NETS : c.nnets;
    for (i = 0; i < nnets; i++) {
        nnode_net_cred_t e = nets[i];
        uint8_t m[6];
        if (!e.ssid[0] || strlen(e.ssid) > 32 || strlen(e.pass) > 63) {
            return -1;
        }
        if (!e.auth[0]) {
            strncpy(e.auth, "auto", sizeof(e.auth) - 1);
        }
        if (!nnode_auth_valid(e.auth)) {
            return -1;
        }
        if (e.bssid[0] && nnode_bssid_parse(e.bssid, m) != 0) {
            return -1;
        }
        if (strcmp(e.auth, "open") == 0 && e.pass[0]) {
            return -1;
        }
        if ((strcmp(e.auth, "wpa") == 0 || strcmp(e.auth, "wpa2") == 0 ||
                strcmp(e.auth, "wpa3") == 0) && !e.pass[0]) {
            return -1;
        }
    }
    return 0;
}

int nnode_ap_validate(const char *text)
{
    ap_ctx_t c;
    static const nnode_cfg_cb_t cb = { ap_section, ap_option, NULL };
    if (!text) {
        return -1;
    }
    memset(&c, 0, sizeof(c));
    c.strict = 1;
    if (nnode_cfg_parse_text(NNOODE_CFG_PKG_AP, text, &cb, &c) != 0) {
        return -1;
    }
    if (!c.has_ssid || !c.ap.ssid[0] || strlen(c.ap.ssid) > 32) {
        return -1;
    }
    if (!c.has_auth || (strcmp(c.ap.auth, "open") != 0 &&
            strcmp(c.ap.auth, "wpa2") != 0 &&
            strcmp(c.ap.auth, "wpa_wpa2") != 0)) {
        return -1;
    }
    if (c.has_mode && strcmp(c.ap.mode, "auto") != 0 &&
        strcmp(c.ap.mode, "always") != 0) {
        return -1;
    }
    if (strcmp(c.ap.auth, "open") == 0) {
        if (c.ap.pass[0] != 0) {
            return -1;
        }
    } else if (strlen(c.ap.pass) < 8 || strlen(c.ap.pass) > 63) {
        return -1;
    }
    return 0;
}

int nnode_ap_clear(void)
{
    /* Missing file means "no AP saved": same as before with the key. */
    if (!nnode_cfg_exists(NNOODE_CFG_PKG_AP)) {
        return 0;
    }
    return nnode_cfg_remove(NNOODE_CFG_PKG_AP);
}
