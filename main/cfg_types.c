#include <stdlib.h>
#include <string.h>
#include "cfg_types.h"

int nnode_ct_bool(const char *v)
{
    if (!v) {
        return -1;
    }
    return (strcmp(v, "0") == 0 || strcmp(v, "1") == 0) ? 0 : -1;
}

int nnode_ct_int(const char *v, long *out)
{
    char *end = NULL;
    long n;
    if (!v || !v[0]) {
        return -1;
    }
    n = strtol(v, &end, 10);
    if (!end || *end != 0) {
        return -1;
    }
    if (out) {
        *out = n;
    }
    return 0;
}

int nnode_ct_int_range(const char *v, long lo, int has_lo,
    long hi, int has_hi, long *out)
{
    long n;
    if (nnode_ct_int(v, &n) != 0) {
        return -1;
    }
    if (has_lo && n < lo) {
        return -1;
    }
    if (has_hi && n > hi) {
        return -1;
    }
    if (out) {
        *out = n;
    }
    return 0;
}

int nnode_ct_port(const char *v, long *out)
{
    return nnode_ct_int_range(v, 1, 1, 65535, 1, out);
}

/* Four decimal groups, no leading zeros (so 01.02.03.04 is rejected). */
int nnode_ct_ip4(const char *v, uint8_t out[4])
{
    int g, n = 0;
    const char *p;
    uint8_t tmp[4];
    if (!v || !v[0]) {
        return -1;
    }
    p = v;
    for (g = 0; g < 4; g++) {
        long val = 0;
        int digits = 0;
        if (*p == 0) {
            return -1;
        }
        while (*p >= '0' && *p <= '9') {
            if (digits == 0 && *p == '0' && *(p + 1) >= '0' && *(p + 1) <= '9') {
                return -1;
            }
            val = val * 10 + (*p - '0');
            if (val > 255 || ++digits > 3) {
                return -1;
            }
            p++;
        }
        if (digits == 0) {
            return -1;
        }
        tmp[g] = (uint8_t)val;
        n++;
        if (g < 3) {
            if (*p != '.') {
                return -1;
            }
            p++;
        }
    }
    if (*p != 0 || n != 4) {
        return -1;
    }
    if (out) {
        memcpy(out, tmp, 4);
    }
    return 0;
}

/* Dotted quad with contiguous one bits, all-zero rejected. */
int nnode_ct_netmask(const char *v, uint8_t out[4])
{
    uint8_t m[4];
    uint32_t w;
    int seen_zero = 0, i, b;
    if (nnode_ct_ip4(v, m) != 0) {
        return -1;
    }
    w = ((uint32_t)m[0] << 24) | ((uint32_t)m[1] << 16) |
        ((uint32_t)m[2] << 8) | m[3];
    if (w == 0) {
        return -1;
    }
    for (i = 31; i >= 0; i--) {
        b = (w >> i) & 1;
        if (b == 0) {
            seen_zero = 1;
        } else if (seen_zero) {
            return -1;
        }
    }
    if (out) {
        memcpy(out, m, 4);
    }
    return 0;
}

int nnode_ct_cidr4(const char *v, uint8_t addr[4], int *prefix)
{
    const char *slash;
    size_t alen;
    char abuf[20];
    long p;
    if (!v || (slash = strchr(v, '/')) == NULL) {
        return -1;
    }
    if (strchr(slash + 1, '/') != NULL) {
        return -1;
    }
    alen = (size_t)(slash - v);
    if (alen == 0 || alen >= sizeof(abuf)) {
        return -1;
    }
    memcpy(abuf, v, alen);
    abuf[alen] = 0;
    if (nnode_ct_ip4(abuf, addr) != 0) {
        return -1;
    }
    if (nnode_ct_int_range(slash + 1, 0, 1, 32, 1, &p) != 0) {
        return -1;
    }
    if (prefix) {
        *prefix = (int)p;
    }
    return 0;
}

int nnode_ct_mac(const char *v, uint8_t out[6])
{
    int g;
    const char *p;
    uint8_t tmp[6];
    if (!v) {
        return -1;
    }
    p = v;
    for (g = 0; g < 6; g++) {
        int hi, lo;
        char c0 = p[0], c1 = p[1];
        if (c0 >= '0' && c0 <= '9') {
            hi = c0 - '0';
        } else if (c0 >= 'a' && c0 <= 'f') {
            hi = c0 - 'a' + 10;
        } else if (c0 >= 'A' && c0 <= 'F') {
            hi = c0 - 'A' + 10;
        } else {
            return -1;
        }
        if (c1 >= '0' && c1 <= '9') {
            lo = c1 - '0';
        } else if (c1 >= 'a' && c1 <= 'f') {
            lo = c1 - 'a' + 10;
        } else if (c1 >= 'A' && c1 <= 'F') {
            lo = c1 - 'A' + 10;
        } else {
            return -1;
        }
        tmp[g] = (uint8_t)((hi << 4) | lo);
        p += 2;
        if (g < 5) {
            if (*p != ':') {
                return -1;
            }
            p++;
        }
    }
    if (*p != 0) {
        return -1;
    }
    if (out) {
        memcpy(out, tmp, 6);
    }
    return 0;
}

static int hostname_ch(char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9')) {
        return 1;
    }
    return c == '-' || c == '.';
}

int nnode_ct_hostname(const char *v, size_t maxlen)
{
    size_t n;
    const char *p;
    if (!v || (n = strlen(v)) == 0 || n > maxlen) {
        return -1;
    }
    if (v[0] == '-' || v[n - 1] == '-') {
        return -1;
    }
    for (p = v; *p; p++) {
        if (!hostname_ch(*p)) {
            return -1;
        }
    }
    return 0;
}

/* LO-HI with strtol on both sides, so negative bounds work. */
int nnode_ct_range(const char *v, long glo_min, int has_glo_min,
    long glo_max, int has_glo_max, long *lo, long *hi)
{
    const char *sep;
    char *end = NULL;
    long a, b;
    char abuf[24];
    size_t alen;
    if (!v || !v[0]) {
        return -1;
    }
    sep = strchr(v[0] == '-' ? v + 1 : v, '-');
    if (!sep || sep == v || *(sep + 1) == 0) {
        return -1;
    }
    alen = (size_t)(sep - v);
    if (alen >= sizeof(abuf)) {
        return -1;
    }
    memcpy(abuf, v, alen);
    abuf[alen] = 0;
    a = strtol(abuf, &end, 10);
    if (!end || *end != 0) {
        return -1;
    }
    b = strtol(sep + 1, &end, 10);
    if (!end || *end != 0) {
        return -1;
    }
    if (a > b) {
        return -1;
    }
    if (has_glo_min && a < glo_min) {
        return -1;
    }
    if (has_glo_max && b > glo_max) {
        return -1;
    }
    if (lo) {
        *lo = a;
    }
    if (hi) {
        *hi = b;
    }
    return 0;
}

/* WPA PSK: 8..63 printable ASCII, or 64 hex digits. */
int nnode_ct_wpakey(const char *v)
{
    size_t n, i;
    int hex;
    if (!v || (n = strlen(v)) == 0) {
        return -1;
    }
    if (n == 64) {
        hex = 1;
        for (i = 0; i < n; i++) {
            char c = v[i];
            int ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                (c >= 'A' && c <= 'F');
            if (!ok) {
                hex = 0;
                break;
            }
        }
        if (hex) {
            return 0;
        }
    }
    if (n < 8 || n > 63) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        if ((unsigned char)v[i] < 32 || (unsigned char)v[i] > 126) {
            return -1;
        }
    }
    return 0;
}

int nnode_ct_string(const char *v, long maxlen)
{
    if (!v) {
        return -1;
    }
    if (maxlen < 1) {
        maxlen = 64;
    }
    return (long)strlen(v) <= maxlen ? 0 : -1;
}

int nnode_ct_enum(const char *v, const char *vals, int nvals, size_t stride)
{
    int i;
    if (!v || !vals || nvals < 1 || stride == 0) {
        return -1;
    }
    for (i = 0; i < nvals; i++) {
        if (strcmp(v, vals + (size_t)i * stride) == 0) {
            return 0;
        }
    }
    return -1;
}

static int list_item_ok(const char *it, const char *sub, long maxlen)
{
    if (!it || !it[0]) {
        return -1;
    }
    if (strchr(it, ' ') || strchr(it, '\t') || strchr(it, '\'') ||
        strchr(it, '"') || strchr(it, '\\')) {
        return -1;
    }
    if (strcmp(sub, "string") == 0) {
        return nnode_ct_string(it, maxlen > 0 ? maxlen : 64);
    }
    if (strcmp(sub, "hostname") == 0) {
        return nnode_ct_hostname(it, 64);
    }
    if (strcmp(sub, "ip4addr") == 0) {
        return nnode_ct_ip4(it, NULL);
    }
    if (strcmp(sub, "macaddr") == 0) {
        return nnode_ct_mac(it, NULL);
    }
    if (strcmp(sub, "port") == 0) {
        return nnode_ct_port(it, NULL);
    }
    if (strcmp(sub, "int") == 0) {
        return nnode_ct_int(it, NULL);
    }
    return -1;
}

int nnode_ct_list(const char *v, const char *sub, int maxitems,
    long maxlen)
{
    int n = 0;
    const char *p;
    if (!v || !sub) {
        return -1;
    }
    if (maxitems < 1) {
        maxitems = 16;
    }
    if (v[0] == 0) {
        return 0;
    }
    if (v[0] == ',' || v[strlen(v) - 1] == ',' || strstr(v, ",,") != NULL) {
        return -1;
    }
    p = v;
    while (*p) {
        const char *c = strchr(p, ',');
        size_t len = c ? (size_t)(c - p) : strlen(p);
        char item[128];
        if (len == 0 || len >= sizeof(item)) {
            return -1;
        }
        memcpy(item, p, len);
        item[len] = 0;
        if (list_item_ok(item, sub, maxlen) != 0) {
            return -1;
        }
        if (++n > maxitems) {
            return -1;
        }
        p = c ? c + 1 : p + len;
    }
    return 0;
}
