#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "cfg.h"
#include "cfg_types.h"
#include "cfg_schema.h"
#include "fw_defaults.h"
#include "nets.h"

static const char *TAG = "nnode_cfg_sta";

int nnode_bssid_parse(const char *s, uint8_t *m)
{
    return nnode_ct_mac(s, m);
}

int nnode_auth_valid(const char *auth)
{
    return auth && (strcmp(auth, "auto") == 0 || strcmp(auth, "open") == 0 ||
        strcmp(auth, "wpa") == 0 || strcmp(auth, "wpa2") == 0 ||
        strcmp(auth, "wpa3") == 0);
}

/* sta/nets packages: fixed indexed shape on the flat syntax. sta holds
 * count+last, nets holds 8 ssid/pass/bssid/auth slots. Load compacts in
 * memory, store normalizes both files. Stores write nets first: a crash
 * between the two still reads correctly, growth appends (old slots
 * untouched) and shrink leaves a padded empty slot that fixup drops. */

static void net_key(char *out, size_t len, const char *base, int idx)
{
    snprintf(out, len, "%s%d", base, idx + 1);
}

/* Parse into caller storage. nets may be NULL (entries skipped, last
 * still parsed). fixup drops ssid-less entries and repairs bssid/auth
 * the same way load always did. */
static void sta_parse_all(nnode_net_cred_t *nets, int cap, int *nnets,
    char *last, int *has_last)
{
    char b[128], key[12];
    uint8_t m[6];
    int i, count = 0, w = 0;
    if (last) {
        last[0] = 0;
    }
    if (has_last) {
        *has_last = 0;
    }
    if (nnets) {
        *nnets = 0;
    }
    if (nnode_schema_lookup(NNOODE_CFG_PKG_STA, "count", b, sizeof(b)) == 0) {
        char *end = NULL;
        long v = strtol(b, &end, 10);
        if (!end || *end || v < 0 || v > NNODE_MAX_NETS) {
            return;
        }
        count = (int)v;
    } else {
        return;
    }
    if (nnode_schema_lookup(NNOODE_CFG_PKG_STA, "last", b, sizeof(b)) == 0 &&
        b[0]) {
        if (strlen(b) > 32) {
            return;
        }
        if (last) {
            /* Length checked above (1..32): plain copy. */
            memcpy(last, b, strlen(b) + 1);
        }
        if (has_last) {
            *has_last = 1;
        }
    }
    if (!nets || cap <= 0) {
        return;
    }
    if (cap > NNODE_MAX_NETS) {
        cap = NNODE_MAX_NETS;
    }
    if (count > cap) {
        count = cap;
    }
    for (i = 0; i < count; i++) {
        nnode_net_cred_t e;
        memset(&e, 0, sizeof(e));
        net_key(key, sizeof(key), "ssid", i);
        if (nnode_schema_lookup(NNOODE_CFG_PKG_NETS, key, e.ssid,
                sizeof(e.ssid)) != 0) {
            e.ssid[0] = 0;
        }
        net_key(key, sizeof(key), "pass", i);
        if (nnode_schema_lookup(NNOODE_CFG_PKG_NETS, key, e.pass,
                sizeof(e.pass)) != 0) {
            e.pass[0] = 0;
        }
        net_key(key, sizeof(key), "bssid", i);
        if (nnode_schema_lookup(NNOODE_CFG_PKG_NETS, key, e.bssid,
                sizeof(e.bssid)) != 0) {
            e.bssid[0] = 0;
        }
        net_key(key, sizeof(key), "auth", i);
        if (nnode_schema_lookup(NNOODE_CFG_PKG_NETS, key, e.auth,
                sizeof(e.auth)) != 0) {
            snprintf(e.auth, sizeof(e.auth), "auto");
        }
        if (!e.ssid[0]) {
            continue;
        }
        if (!nnode_auth_valid(e.auth)) {
            snprintf(e.auth, sizeof(e.auth), "auto");
        }
        if (e.bssid[0] && nnode_bssid_parse(e.bssid, m) != 0) {
            e.bssid[0] = 0;
        }
        nets[w++] = e;
    }
    if (nnets) {
        *nnets = w;
    }
}

static int sta_store_arrays(const nnode_net_cred_t *nets, int nnets,
    const char *last, int has_last)
{
    /* Heap maps: httpd workers have 8 KB stacks, main task less. */
    char (*nkeys)[NNODE_SCHEMA_ID_MAX + 1] = malloc(sizeof(*nkeys) * 32);
    char (*nvals)[128] = malloc(sizeof(*nvals) * 32);
    char skeys[2][NNODE_SCHEMA_ID_MAX + 1];
    char svals[2][128];
    char num[8], key[12];
    int i, rc = -1;
    if (!nkeys || !nvals) {
        goto out;
    }
    if (!nets && nnets > 0) {
        goto out;
    }
    if (nnets < 0) {
        nnets = 0;
    }
    if (nnets > NNODE_MAX_NETS) {
        nnets = NNODE_MAX_NETS;
    }
    for (i = 0; i < NNODE_MAX_NETS; i++) {
        const nnode_net_cred_t *e = i < nnets ? &nets[i] : NULL;
        size_t n;
        net_key(key, sizeof(key), "ssid", i);
        memcpy(nkeys[4 * i], key, strlen(key) + 1);
        n = e ? strlen(e->ssid) : 0;
        if (n >= sizeof(nvals[0])) {
            goto out;
        }
        memcpy(nvals[4 * i], e ? e->ssid : "", n + 1);
        net_key(key, sizeof(key), "pass", i);
        memcpy(nkeys[4 * i + 1], key, strlen(key) + 1);
        n = e ? strlen(e->pass) : 0;
        if (n >= sizeof(nvals[0])) {
            goto out;
        }
        memcpy(nvals[4 * i + 1], e ? e->pass : "", n + 1);
        net_key(key, sizeof(key), "bssid", i);
        memcpy(nkeys[4 * i + 2], key, strlen(key) + 1);
        n = e ? strlen(e->bssid) : 0;
        if (n >= sizeof(nvals[0])) {
            goto out;
        }
        memcpy(nvals[4 * i + 2], e ? e->bssid : "", n + 1);
        net_key(key, sizeof(key), "auth", i);
        memcpy(nkeys[4 * i + 3], key, strlen(key) + 1);
        if (e && e->auth[0]) {
            n = strlen(e->auth);
            if (n >= sizeof(nvals[0])) {
                goto out;
            }
            memcpy(nvals[4 * i + 3], e->auth, n + 1);
        } else {
            memcpy(nvals[4 * i + 3], "auto", 5);
        }
    }
    snprintf(num, sizeof(num), "%d", nnets);
    memcpy(skeys[0], "count", 6);
    memcpy(svals[0], num, strlen(num) + 1);
    memcpy(skeys[1], "last", 5);
    if (has_last && last && last[0]) {
        if (strlen(last) > 32) {
            goto out;
        }
        memcpy(svals[1], last, strlen(last) + 1);
    } else {
        svals[1][0] = 0;
    }
    if (nnode_schema_store(nnode_fw_defaults(NNOODE_CFG_PKG_NETS),
            nkeys, nvals, 32) != 0) {
        goto out;
    }
    rc = nnode_schema_store(nnode_fw_defaults(NNOODE_CFG_PKG_STA),
        skeys, svals, 2);
out:
    free(nkeys);
    free(nvals);
    return rc;
}

int nnode_nets_load(nnode_net_cred_t *out, int max)
{
    int n = 0;
    if (!out || max <= 0) {
        return 0;
    }
    sta_parse_all(out, max, &n, NULL, NULL);
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
    sta_parse_all(NULL, 0, NULL, last, &has_last);
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
    sta_parse_all(nets, NNODE_MAX_NETS, &nnets, last, &has_last);
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
    sta_parse_all(nets, NNODE_MAX_NETS, &nnets, last, &has_last);
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
    sta_parse_all(nets, NNODE_MAX_NETS, &nnets, NULL, NULL);
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
    sta_parse_all(nets, NNODE_MAX_NETS, &nnets, last, &has_last);
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
    sta_parse_all(NULL, 0, NULL, last, &has_last);
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
    sta_parse_all(nets, NNODE_MAX_NETS, &nnets, last, &has_last);
    snprintf(last, sizeof(last), "%s", ssid);
    has_last = 1;
    return sta_store_arrays(nets, nnets, last, has_last);
}

int nnode_ap_load(nnode_ap_cfg_t *out)
{
    nnode_ap_cfg_t c;
    if (!out) {
        return -1;
    }
    memset(&c, 0, sizeof(c));
    if (nnode_schema_lookup(NNOODE_CFG_PKG_AP, "ssid", c.ssid,
            sizeof(c.ssid)) != 0 || !c.ssid[0]) {
        return -1;
    }
    if (nnode_schema_lookup(NNOODE_CFG_PKG_AP, "auth", c.auth,
            sizeof(c.auth)) != 0 || !c.auth[0]) {
        return -1;
    }
    if (nnode_schema_lookup(NNOODE_CFG_PKG_AP, "pass", c.pass,
            sizeof(c.pass)) != 0) {
        c.pass[0] = 0;
    }
    if (nnode_schema_lookup(NNOODE_CFG_PKG_AP, "mode", c.mode,
            sizeof(c.mode)) != 0 || !c.mode[0]) {
        snprintf(c.mode, sizeof(c.mode), "auto");
    }
    {
        char b[8];
        c.nat = nnode_schema_lookup(NNOODE_CFG_PKG_AP, "nat", b, sizeof(b)) == 0 &&
            b[0] == '1' ? 1 : 0;
    }
    *out = c;
    ESP_LOGI(TAG, "ap load ssid=%s", c.ssid);
    return 0;
}

int nnode_ap_save(const nnode_ap_cfg_t *cfg)
{
    /* Heap: runs on the main task at seed, its stack is tiny. */
    char (*keys)[NNODE_SCHEMA_ID_MAX + 1] = malloc(sizeof(*keys) * 5);
    char (*vals)[128] = malloc(sizeof(*vals) * 5);
    int rc = -1;
    if (!keys || !vals) {
        goto out;
    }
    if (!cfg || !cfg->ssid[0]) {
        goto out;
    }
    memcpy(keys[0], "ssid", 5);
    if (strlen(cfg->ssid) > 32) {
        goto out;
    }
    memcpy(vals[0], cfg->ssid, strlen(cfg->ssid) + 1);
    memcpy(keys[1], "auth", 5);
    if (!cfg->auth[0] || strlen(cfg->auth) >= sizeof(vals[0])) {
        goto out;
    }
    memcpy(vals[1], cfg->auth, strlen(cfg->auth) + 1);
    memcpy(keys[2], "pass", 5);
    if (strlen(cfg->pass) > 63) {
        goto out;
    }
    memcpy(vals[2], cfg->pass, strlen(cfg->pass) + 1);
    memcpy(keys[3], "mode", 5);
    {
        const char *mode = cfg->mode[0] ? cfg->mode : "auto";
        memcpy(vals[3], mode, strlen(mode) + 1);
    }
    memcpy(keys[4], "nat", 4);
    memcpy(vals[4], cfg->nat ? "1" : "0", 2);
    rc = nnode_schema_store(nnode_fw_defaults(NNOODE_CFG_PKG_AP),
        keys, vals, 5);
out:
    free(keys);
    free(vals);
    return rc;
}

int nnode_sta_validate(const char *text)
{
    return nnode_schema_validate(nnode_fw_defaults(NNOODE_CFG_PKG_STA), text);
}

static const char *nets_kv(char keys[][NNODE_SCHEMA_ID_MAX + 1],
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

int nnode_nets_validate(const char *text)
{
    const char *def = nnode_fw_defaults(NNOODE_CFG_PKG_NETS);
    /* Heap: 5 KB of pairs does not belong on a task stack. */
    char (*keys)[NNODE_SCHEMA_ID_MAX + 1] = malloc(sizeof(*keys) * 32);
    char (*vals)[128] = malloc(sizeof(*vals) * 32);
    int n = 0, i, rc = -1;
    uint8_t m[6];
    char key[12];
    if (!keys || !vals) {
        goto out;
    }
    if (!text || nnode_schema_validate(def, text) != 0 ||
        nnode_schema_kv(NNOODE_CFG_PKG_NETS, text, keys, vals, &n) != 0) {
        goto out;
    }
    for (i = 0; i < NNODE_MAX_NETS; i++) {
        const char *ssid, *pass, *bssid, *auth;
        net_key(key, sizeof(key), "ssid", i);
        ssid = nets_kv(keys, vals, n, key);
        net_key(key, sizeof(key), "pass", i);
        pass = nets_kv(keys, vals, n, key);
        net_key(key, sizeof(key), "bssid", i);
        bssid = nets_kv(keys, vals, n, key);
        net_key(key, sizeof(key), "auth", i);
        auth = nets_kv(keys, vals, n, key);
        if (!ssid || !pass || !bssid || !auth) {
            goto out;
        }
        if (!ssid[0]) {
            /* Padding slot: must be fully empty. */
            if (pass[0] || bssid[0]) {
                goto out;
            }
            continue;
        }
        if (strlen(pass) > 63 || (bssid[0] && nnode_bssid_parse(bssid, m) != 0)) {
            goto out;
        }
        if (!nnode_auth_valid(auth)) {
            goto out;
        }
        if (strcmp(auth, "open") == 0 && pass[0]) {
            goto out;
        }
        if ((strcmp(auth, "wpa") == 0 || strcmp(auth, "wpa2") == 0 ||
                strcmp(auth, "wpa3") == 0) && !pass[0]) {
            goto out;
        }
    }
    rc = 0;
out:
    free(keys);
    free(vals);
    return rc;
}

int nnode_ap_validate(const char *text)
{
    const char *def = nnode_fw_defaults(NNOODE_CFG_PKG_AP);
    /* Heap: runs on httpd workers next to 8 KB staged texts. */
    char (*keys)[NNODE_SCHEMA_ID_MAX + 1] = malloc(sizeof(*keys) * 8);
    char (*vals)[128] = malloc(sizeof(*vals) * 8);
    const char *ssid, *auth, *pass, *mode;
    int n = 0, rc = -1;
    if (!keys || !vals) {
        goto out;
    }
    if (!text || nnode_schema_validate(def, text) != 0 ||
        nnode_schema_kv(NNOODE_CFG_PKG_AP, text, keys, vals, &n) != 0) {
        goto out;
    }
    ssid = nets_kv(keys, vals, n, "ssid");
    auth = nets_kv(keys, vals, n, "auth");
    pass = nets_kv(keys, vals, n, "pass");
    mode = nets_kv(keys, vals, n, "mode");
    if (!ssid || !ssid[0] || strlen(ssid) > 32) {
        return -1;
    }
    if (!auth || (strcmp(auth, "open") != 0 && strcmp(auth, "wpa2") != 0 &&
            strcmp(auth, "wpa_wpa2") != 0)) {
        return -1;
    }
    if (!mode || (strcmp(mode, "auto") != 0 && strcmp(mode, "always") != 0)) {
        return -1;
    }
    if (!pass) {
        goto out;
    }
    if (strcmp(auth, "open") == 0) {
        if (pass[0] != 0) {
            goto out;
        }
    } else if (strlen(pass) < 8 || strlen(pass) > 63) {
        goto out;
    }
    rc = 0;
out:
    free(keys);
    free(vals);
    return rc;
}

int nnode_ap_clear(void)
{
    /* Missing file means "no AP saved": same as before with the key. */
    if (!nnode_cfg_exists(NNOODE_CFG_PKG_AP)) {
        return 0;
    }
    return nnode_cfg_remove(NNOODE_CFG_PKG_AP);
}
