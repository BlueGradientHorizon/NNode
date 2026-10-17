#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "cfg.h"
#include "cfg_types.h"
#include "cfg_schema.h"

static const char *TAG = "nnode_schema";

#define NNODE_SCHEMA_LINE_MAX 256
#define NNODE_SCHEMA_FILE_MAX 8192
#define NNODE_SCHEMA_SECTION_MAX NNODE_SCHEMA_DEFAULT_MAX

static int id_valid(const char *s)
{
    size_t n;
    if (!s || (n = strlen(s)) == 0 || n > NNODE_SCHEMA_ID_MAX) {
        return 0;
    }
    while (*s) {
        char c = *s++;
        if ((c < 'a' || c > 'z') && (c < '0' || c > '9') && c != '_') {
            return 0;
        }
    }
    return 1;
}

/* Strict decimal parse, no trailing junk. */
static int parse_long(const char *s, long *out)
{
    char *end = NULL;
    long v;
    if (!s || !s[0]) {
        return -1;
    }
    v = strtol(s, &end, 10);
    if (!end || *end != 0) {
        return -1;
    }
    if (out) {
        *out = v;
    }
    return 0;
}

/* #@field attributes, parsed streaming straight off the line: no pair
 * array, so arbitrarily long vals= lists need no intermediate buffer.
 * Grammar per token: key=value or key="quoted value" (\" and \\ escape
 * to the bare char). */

typedef struct {
    const char *v; /* raw value bytes (still escaped when quoted) */
    size_t n;
    int quoted;
} attr_val_t;

/* Next k=v token. 1 token, 0 end of string, -1 malformed. */
static int attr_next(const char **pp, char *key, size_t kcap,
    attr_val_t *val)
{
    const char *p = *pp;
    size_t kn = 0;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (!*p) {
        *pp = p;
        return 0;
    }
    while (*p && *p != ' ' && *p != '\t' && *p != '=') {
        if (kn + 1 >= kcap) {
            return -1;
        }
        key[kn++] = *p++;
    }
    key[kn] = 0;
    if (kn == 0 || *p != '=') {
        return -1;
    }
    p++;
    if (*p == '"') {
        const char *vs = p + 1;
        p++;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) {
                p += 2;
            } else {
                p++;
            }
        }
        if (*p != '"') {
            return -1;
        }
        val->v = vs;
        val->n = (size_t)(p - vs);
        val->quoted = 1;
        p++;
    } else {
        const char *vs = p;
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
        val->v = vs;
        val->n = (size_t)(p - vs);
        val->quoted = 0;
    }
    *pp = p;
    return 1;
}

/* Decoded value of the first k= match. 1 found, 0 missing, -1 broken. */
static int attr_get(const char *attrs, const char *key, char *out,
    size_t cap)
{
    const char *p = attrs;
    for (;;) {
        char k[16];
        attr_val_t v;
        size_t o = 0, i;
        int r = attr_next(&p, k, sizeof(k), &v);
        if (r <= 0) {
            return r;
        }
        if (strcmp(k, key) != 0) {
            continue;
        }
        for (i = 0; i < v.n; i++) {
            char c = v.v[i];
            if (v.quoted && c == '\\' && i + 1 < v.n) {
                c = v.v[++i];
            }
            if (o + 1 >= cap) {
                return -1;
            }
            out[o++] = c;
        }
        out[o] = 0;
        return 1;
    }
}

/* vals= of an enum, streamed comma by comma into the field (no middle
 * buffer). 0 ok, -1 missing/malformed/duplicate/overfull. */
static int attr_enum_vals(const char *attrs, nnode_schema_field_t *f)
{
    const char *p = attrs;
    int found = 0;
    for (;;) {
        char k[16];
        attr_val_t v;
        const char *q;
        size_t left, i, o;
        char tmp[256];
        int r = attr_next(&p, k, sizeof(k), &v);
        if (r <= 0) {
            return found ? 0 : -1;
        }
        if (strcmp(k, "vals") != 0) {
            continue;
        }
        if (found) {
            return -1;
        }
        found = 1;
        /* Decode escapes into scratch: enum members are ids, the whole
         * list still fits a line. */
        o = 0;
        for (i = 0; i < v.n; i++) {
            char c = v.v[i];
            if (v.quoted && c == '\\' && i + 1 < v.n) {
                c = v.v[++i];
            }
            if (o + 1 >= sizeof(tmp)) {
                return -1;
            }
            tmp[o++] = c;
        }
        tmp[o] = 0;
        q = tmp;
        left = o;
        while (left > 0) {
            const char *c = memchr(q, ',', left);
            size_t len = c ? (size_t)(c - q) : left;
            int k2;
            if (len == 0 || len > NNODE_SCHEMA_ID_MAX ||
                f->nvals >= NNODE_SCHEMA_ENUMVALS) {
                return -1;
            }
            memcpy(f->vals[f->nvals], q, len);
            f->vals[f->nvals][len] = 0;
            if (!id_valid(f->vals[f->nvals])) {
                return -1;
            }
            for (k2 = 0; k2 < f->nvals; k2++) {
                if (strcmp(f->vals[k2], f->vals[f->nvals]) == 0) {
                    return -1;
                }
            }
            f->nvals++;
            if (!c) {
                break;
            }
            q = c + 1;
            left -= len + 1;
        }
        if (f->nvals == 0) {
            return -1;
        }
    }
}

static int type_known(const char *t)
{
    return strcmp(t, "bool") == 0 || strcmp(t, "int") == 0 ||
        strcmp(t, "list") == 0 || strcmp(t, "ip4addr") == 0 ||
        strcmp(t, "netmask") == 0 || strcmp(t, "cidr4") == 0 ||
        strcmp(t, "macaddr") == 0 || strcmp(t, "port") == 0 ||
        strcmp(t, "hostname") == 0 || strcmp(t, "range") == 0 ||
        strcmp(t, "wpakey") == 0 || strcmp(t, "string") == 0 ||
        strcmp(t, "enum") == 0;
}

static int list_sub_known(const char *t)
{
    return strcmp(t, "string") == 0 || strcmp(t, "hostname") == 0 ||
        strcmp(t, "ip4addr") == 0 || strcmp(t, "macaddr") == 0 ||
        strcmp(t, "port") == 0 || strcmp(t, "int") == 0;
}

int nnode_schema_value_ok(const nnode_schema_field_t *f, const char *v)
{
    if (!f || !v) {
        return -1;
    }
    if (strcmp(f->type, "bool") == 0) {
        return nnode_ct_bool(v);
    }
    if (strcmp(f->type, "int") == 0) {
        return nnode_ct_int_range(v, f->min, f->has_min, f->max, f->has_max, NULL);
    }
    if (strcmp(f->type, "port") == 0) {
        return nnode_ct_port(v, NULL);
    }
    if (strcmp(f->type, "ip4addr") == 0) {
        return nnode_ct_ip4(v, NULL);
    }
    if (strcmp(f->type, "netmask") == 0) {
        return nnode_ct_netmask(v, NULL);
    }
    if (strcmp(f->type, "cidr4") == 0) {
        return nnode_ct_cidr4(v, NULL, NULL);
    }
    if (strcmp(f->type, "macaddr") == 0) {
        return nnode_ct_mac(v, NULL);
    }
    if (strcmp(f->type, "hostname") == 0) {
        return nnode_ct_hostname(v, 64);
    }
    if (strcmp(f->type, "range") == 0) {
        return nnode_ct_range(v, f->min, f->has_min, f->max, f->has_max,
            NULL, NULL);
    }
    if (strcmp(f->type, "wpakey") == 0) {
        return nnode_ct_wpakey(v);
    }
    if (strcmp(f->type, "string") == 0) {
        return nnode_ct_string(v, f->maxlen > 0 ? f->maxlen : 64);
    }
    if (strcmp(f->type, "enum") == 0) {
        return nnode_ct_enum(v, (const char *)f->vals, f->nvals,
            sizeof(f->vals[0]));
    }
    if (strcmp(f->type, "list") == 0) {
        return nnode_ct_list(v, f->sub, f->maxitems > 0 ? f->maxitems : 16,
            f->maxlen > 0 ? f->maxlen : 64);
    }
    return -1;
}

/* show_if grammar: OR of AND of comparisons, no spaces.
 * key==lit, key!=lit for bool/enum/int, plus <,<=,>,>= for int.
 * The referenced key must be an earlier field of cond-safe type.
 * The frontend evaluates this, the firmware only validates it. */
static int cond_valid(const nnode_schema_t *s, int idx, const char *expr)
{
    const char *p = expr;
    if (!expr || !expr[0]) {
        return -1;
    }
    if (strchr(expr, ' ') || strchr(expr, '\t')) {
        return -1;
    }
    while (*p) {
        const char *or_end = strstr(p, "||");
        size_t or_len = or_end ? (size_t)(or_end - p) : strlen(p);
        const char *q = p, *q_end = p + or_len;
        if (or_len == 0) {
            return -1;
        }
        while (q < q_end) {
            const char *a_end = strstr(q, "&&");
            size_t a_len;
            char term[128];
            const char *op = NULL;
            const char *ops[] = { "==", "!=", "<=", ">=", "<", ">", NULL };
            int oi;
            const char *key_e, *lit;
            size_t key_n;
            char key[33], litbuf[64];
            int ri, found = 0;
            if (a_end && a_end < q_end) {
                a_len = (size_t)(a_end - q);
            } else {
                a_len = (size_t)(q_end - q);
            }
            if (a_len == 0 || a_len >= sizeof(term)) {
                return -1;
            }
            memcpy(term, q, a_len);
            term[a_len] = 0;
            for (oi = 0; ops[oi]; oi++) {
                op = strstr(term, ops[oi]);
                if (op) {
                    break;
                }
            }
            if (!op) {
                return -1;
            }
            /* Two-char ops win over their one-char tails: strstr finds
             * the first ops[] hit in table order, so ==/!=/<=/>= match
             * before a lone =/</> inside them. A stray single = never
             * matches any entry and fails here. */
            key_e = op;
            lit = op + strlen(ops[oi]);
            key_n = (size_t)(key_e - term);
            if (key_n == 0 || key_n >= sizeof(key) || !lit[0] ||
                strlen(lit) >= sizeof(litbuf)) {
                return -1;
            }
            memcpy(key, term, key_n);
            key[key_n] = 0;
            /* Length checked above: plain copy. */
            memcpy(litbuf, lit, strlen(lit) + 1);
            if (!id_valid(key)) {
                return -1;
            }
            for (ri = 0; ri < idx; ri++) {
                if (strcmp(s->fields[ri].key, key) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                return -1;
            } {
                const nnode_schema_field_t *r = &s->fields[ri];
                int is_int = strcmp(r->type, "int") == 0;
                int eq = strcmp(ops[oi], "==") == 0 || strcmp(ops[oi], "!=") == 0;
                if (strcmp(r->type, "bool") != 0 && strcmp(r->type, "enum") != 0 &&
                    !is_int) {
                    return -1;
                }
                if (!eq && !is_int) {
                    return -1;
                }
                if (strcmp(r->type, "bool") == 0) {
                    if (nnode_ct_bool(litbuf) != 0) {
                        return -1;
                    }
                } else if (strcmp(r->type, "enum") == 0) {
                    if (nnode_ct_enum(litbuf, (const char *)r->vals, r->nvals,
                            sizeof(r->vals[0])) != 0) {
                        return -1;
                    }
                } else {
                    if (nnode_ct_int(litbuf, NULL) != 0) {
                        return -1;
                    }
                }
            }
            q += a_len;
            if (q < q_end) {
                q += 2; /* skip && */
            }
        }
        p += or_len;
        if (*p) {
            p += 2; /* skip || */
        }
    }
    return 0;
}

/* Next line without the newline; handles lone \n and \r\n. */
static const char *next_line(const char *p, char *out, size_t cap, size_t *len)
{
    size_t o = 0;
    while (*p && *p != '\n' && o + 1 < cap) {
        out[o++] = *p++;
    }
    if (*p && *p != '\n') {
        return NULL; /* overlong line */
    }
    if (*p == '\n') {
        p++;
    }
    out[o] = 0;
    /* Strip a trailing \r so \r\n files parse the same. */
    if (o > 0 && out[o - 1] == '\r') {
        out[--o] = 0;
    }
    if (len) {
        *len = o;
    }
    return p;
}

static int field_by_key(const nnode_schema_t *s, const char *key)
{
    int i;
    for (i = 0; i < s->nfields; i++) {
        if (strcmp(s->fields[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

/* Fill one field from its #@field attribute string. */
static int field_attrs(nnode_schema_field_t *f, const char *attrs)
{
    char v[128];
    int r;
    memset(f->tab, 0, sizeof(f->tab));
    memset(f->show_if, 0, sizeof(f->show_if));
    f->has_min = 0;
    f->has_max = 0;
    f->maxlen = 64;
    f->nvals = 0;
    f->maxitems = 16;
    memset(f->sub, 0, sizeof(f->sub));
    r = attr_get(attrs, "label", v, sizeof(v));
    if (r != 1 || !v[0] || strlen(v) > 64) {
        return -1;
    }
    memcpy(f->label, v, strlen(v) + 1);
    r = attr_get(attrs, "tab", v, sizeof(v));
    if (r < 0) {
        return -1;
    }
    if (r == 1) {
        if (!id_valid(v)) {
            return -1;
        }
        memcpy(f->tab, v, strlen(v) + 1);
    }
    r = attr_get(attrs, "show_if", v, sizeof(v));
    if (r < 0) {
        return -1;
    }
    if (r == 1) {
        if (!v[0] || strlen(v) >= sizeof(f->show_if)) {
            return -1;
        }
        memcpy(f->show_if, v, strlen(v) + 1);
    }
    if (strcmp(f->type, "int") == 0 || strcmp(f->type, "range") == 0) {
        r = attr_get(attrs, "min", v, sizeof(v));
        if (r < 0) {
            return -1;
        }
        if (r == 1) {
            if (parse_long(v, &f->min) != 0) {
                return -1;
            }
            f->has_min = 1;
        }
        r = attr_get(attrs, "max", v, sizeof(v));
        if (r < 0) {
            return -1;
        }
        if (r == 1) {
            if (parse_long(v, &f->max) != 0) {
                return -1;
            }
            f->has_max = 1;
        }
        if (f->has_min && f->has_max && f->min > f->max) {
            return -1;
        }
    }
    if (strcmp(f->type, "string") == 0) {
        long m;
        r = attr_get(attrs, "maxlen", v, sizeof(v));
        if (r < 0) {
            return -1;
        }
        if (r == 1) {
            if (parse_long(v, &m) != 0 || m < 1 || m > 127) {
                return -1;
            }
            f->maxlen = m;
        }
    }
    if (strcmp(f->type, "enum") == 0) {
        if (attr_enum_vals(attrs, f) != 0) {
            return -1;
        }
    }
    if (strcmp(f->type, "list") == 0) {
        long m;
        r = attr_get(attrs, "sub", v, sizeof(v));
        if (r != 1 || !list_sub_known(v)) {
            return -1;
        }
        /* Known literals only ("hostname" is the longest): plain copy. */
        memcpy(f->sub, v, strlen(v) + 1);
        r = attr_get(attrs, "maxitems", v, sizeof(v));
        if (r < 0) {
            return -1;
        }
        if (r == 1) {
            if (parse_long(v, &m) != 0 || m < 1 || m > 32) {
                return -1;
            }
            f->maxitems = (int)m;
        }
        r = attr_get(attrs, "maxlen", v, sizeof(v));
        if (r < 0) {
            return -1;
        }
        if (r == 1) {
            if (parse_long(v, &m) != 0 || m < 1 || m > 127) {
                return -1;
            }
            f->maxlen = m;
        }
    }
    return 0;
}

/* Attributes allowed per type (anything else is a typo), duplicates
 * included: the small seen set fits the stack easily. */
static int attrs_known(const char *type, const char *attrs)
{
    const char *p = attrs;
    char seen[12][16];
    int nseen = 0;
    for (;;) {
        char k[16];
        attr_val_t v;
        int i, ok;
        int r = attr_next(&p, k, sizeof(k), &v);
        if (r == 0) {
            return 0;
        }
        if (r < 0 || nseen >= 12) {
            return -1;
        }
        ok = strcmp(k, "label") == 0 || strcmp(k, "tab") == 0 ||
            strcmp(k, "show_if") == 0;
        if (!ok && (strcmp(type, "int") == 0 || strcmp(type, "range") == 0)) {
            ok = strcmp(k, "min") == 0 || strcmp(k, "max") == 0;
        }
        if (!ok && strcmp(type, "string") == 0) {
            ok = strcmp(k, "maxlen") == 0;
        }
        if (!ok && strcmp(type, "enum") == 0) {
            ok = strcmp(k, "vals") == 0;
        }
        if (!ok && strcmp(type, "list") == 0) {
            ok = strcmp(k, "sub") == 0 || strcmp(k, "maxitems") == 0 ||
                strcmp(k, "maxlen") == 0;
        }
        if (!ok) {
            return -1;
        }
        for (i = 0; i < nseen; i++) {
            if (strcmp(seen[i], k) == 0) {
                return -1;
            }
        }
        snprintf(seen[nseen], sizeof(seen[0]), "%s", k);
        nseen++;
    }
}

int nnode_schema_parse(const char *text, nnode_schema_t *s,
    int allow_reserved)
{
    const char *p;
    char line[NNODE_SCHEMA_LINE_MAX];
    int stage = 0; /* 0 package, 1 version, 2 body */
    int i;
    /* Value lines stash: key + raw decoded value (heap: 5 KB,
     * far beyond the main task stack). */
    typedef struct {
        char k[NNODE_SCHEMA_FIELDS][NNODE_SCHEMA_ID_MAX + 1];
        char v[NNODE_SCHEMA_FIELDS][128];
    } stage_t;
    stage_t *st = NULL;
    int nvals = 0;
    if (!text || !s) {
        goto fail;
    }
    memset(s, 0, sizeof(*s));
    st = malloc(sizeof(*st));
    if (!st) {
        goto fail;
    }
    p = text;
    while (*p) {
        const char *q;
        size_t len = 0;
        q = next_line(p, line, sizeof(line), &len);
        if (!q) {
            goto fail;
        }
        p = q;
        /* Trim leading blanks. */
        {
            char *t = line;
            while (*t == ' ' || *t == '\t') {
                t++;
            }
            if (t != line) {
                memmove(line, t, strlen(t) + 1);
            }
        }
        if (!line[0] || (line[0] == '#' && line[1] != '@')) {
            continue;
        }
        if (strncmp(line, "#@tab", 5) == 0 &&
            (line[5] == ' ' || line[5] == '\t')) {
            char *rest = line + 5;
            char *sp;
            while (*rest == ' ' || *rest == '\t') {
                rest++;
            }
            sp = strchr(rest, ' ');
            if (!sp) {
                sp = strchr(rest, '\t');
            }
            if (!sp || s->ntabs >= NNODE_SCHEMA_TABS) {
                goto fail;
            }
            *sp = 0;
            if (!id_valid(rest)) {
                goto fail;
            }
            for (i = 0; i < s->ntabs; i++) {
                if (strcmp(s->tabs[i].id, rest) == 0) {
                    goto fail;
                }
            }
            /* id_valid above caps the id at 32 chars: plain copy. */
            memcpy(s->tabs[s->ntabs].id, rest, strlen(rest) + 1);
            rest = sp + 1;
            while (*rest == ' ' || *rest == '\t') {
                rest++;
            }
            /* Trim trailing blanks of the title. */
            {
                size_t tl = strlen(rest);
                while (tl > 0 && (rest[tl - 1] == ' ' || rest[tl - 1] == '\t')) {
                    rest[--tl] = 0;
                }
            }
            if (!rest[0] || strlen(rest) > 64) {
                goto fail;
            }
            /* Length checked above (1..64): plain copy. */
            memcpy(s->tabs[s->ntabs].title, rest, strlen(rest) + 1);
            s->ntabs++;
            continue;
        }
        if (strncmp(line, "#@field", 7) == 0 &&
            (line[7] == ' ' || line[7] == '\t')) {
            char *rest = line + 7;
            char key[33], type[12];
            char *sp;
            nnode_schema_field_t *f;
            while (*rest == ' ' || *rest == '\t') {
                rest++;
            }
            sp = rest;
            while (*sp && *sp != ' ' && *sp != '\t') {
                sp++;
            }
            if ((size_t)(sp - rest) == 0 || (size_t)(sp - rest) >= sizeof(key)) {
                goto fail;
            }
            memcpy(key, rest, (size_t)(sp - rest));
            key[sp - rest] = 0;
            rest = sp;
            while (*rest == ' ' || *rest == '\t') {
                rest++;
            }
            sp = rest;
            while (*sp && *sp != ' ' && *sp != '\t') {
                sp++;
            }
            if ((size_t)(sp - rest) == 0 || (size_t)(sp - rest) >= sizeof(type)) {
                goto fail;
            }
            memcpy(type, rest, (size_t)(sp - rest));
            type[sp - rest] = 0;
            rest = sp;
            if (!id_valid(key) || !type_known(type) ||
                s->nfields >= NNODE_SCHEMA_FIELDS || field_by_key(s, key) >= 0) {
                goto fail;
            }
            if (attrs_known(type, rest) != 0) {
                goto fail;
            }
            f = &s->fields[s->nfields];
            memset(f, 0, sizeof(*f));
            snprintf(f->key, sizeof(f->key), "%s", key);
            snprintf(f->type, sizeof(f->type), "%s", type);
            if (field_attrs(f, rest) != 0) {
                goto fail;
            }
            s->nfields++;
            continue;
        }
        if (line[0] == '#' && line[1] == '@') {
            goto fail; /* unknown meta */
        }
        if (stage == 0) {
            if (strncmp(line, "package ", 8) != 0) {
                goto fail;
            }
            if (!nnode_cfg_valid_pkg(line + 8) ||
                (!allow_reserved && nnode_cfg_reserved(line + 8))) {
                goto fail;
            }
            /* valid_pkg caps the name at 23 chars: plain copy. */
            memcpy(s->pkg, line + 8, strlen(line + 8) + 1);
            stage = 1;
            continue;
        }
        if (stage == 1) {
            long v;
            if (strncmp(line, "version ", 8) != 0) {
                goto fail;
            }
            if (parse_long(line + 8, &v) != 0 || v < 1 || v > 999999) {
                goto fail;
            }
            s->version = (int)v;
            stage = 2;
            continue;
        } {
            /* Value line: key + space + token. */
            char *sp = strchr(line, ' ');
            char *tb;
            char raw[128];
            if (!sp) {
                sp = strchr(line, '\t');
            }
            if (!sp || nvals >= NNODE_SCHEMA_FIELDS) {
                goto fail;
            }
            *sp = 0;
            tb = sp + 1;
            while (*tb == ' ' || *tb == '\t') {
                tb++;
            }
            if (!id_valid(line) || !tb[0]) {
                goto fail;
            }
            for (i = 0; i < nvals; i++) {
                if (strcmp(st->k[i], line) == 0) {
                    goto fail;
                }
            }
            if (nnode_cfg_unescape(tb, raw, sizeof(raw)) != 0) {
                goto fail;
            }
            /* id_valid above caps the key at 32 chars: plain copy. */
            memcpy(st->k[nvals], line, strlen(line) + 1);
            memcpy(st->v[nvals], raw, strlen(raw) + 1);
            nvals++;
        }
    }
    if (stage != 2 || !s->pkg[0] || s->nfields == 0) {
        goto fail;
    }
    if (s->ntabs == 1) {
        goto fail;
    }
    for (i = 0; i < s->nfields; i++) {
        nnode_schema_field_t *f = &s->fields[i];
        int j, tab_ok;
        if (s->ntabs > 0) {
            tab_ok = 0;
            for (j = 0; j < s->ntabs; j++) {
                if (strcmp(s->tabs[j].id, f->tab) == 0) {
                    tab_ok = 1;
                    break;
                }
            }
            if (!tab_ok) {
                goto fail;
            }
        } else if (f->tab[0]) {
            goto fail;
        }
    }
    if (s->ntabs > 0) {
        for (i = 0; i < s->ntabs; i++) {
            int j, used = 0;
            for (j = 0; j < s->nfields; j++) {
                if (strcmp(s->fields[j].tab, s->tabs[i].id) == 0) {
                    used = 1;
                    break;
                }
            }
            if (!used) {
                goto fail;
            }
        }
    }
    /* Every field needs exactly one value line; every value line
     * needs its field. Defaults are validated as ordinary values. */
    if (nvals != s->nfields) {
        goto fail;
    }
    for (i = 0; i < s->nfields; i++) {
        int j, ok = 0;
        for (j = 0; j < nvals; j++) {
            if (strcmp(st->k[j], s->fields[i].key) == 0) {
                ok = 1;
                if (nnode_schema_value_ok(&s->fields[i], st->v[j]) != 0) {
                    goto fail;
                }
                snprintf(s->fields[i].dflt, sizeof(s->fields[i].dflt),
                    "%s", st->v[j]);
                break;
            }
        }
        if (!ok) {
            goto fail;
        }
    }
    for (i = 0; i < s->nfields; i++) {
        if (s->fields[i].show_if[0] &&
            cond_valid(s, i, s->fields[i].show_if) != 0) {
            goto fail;
        }
    }
    free(st);
    return 0;
fail:
    free(st);
    return -1;
}

/* Little-endian loads with bounds checks (file bytes are unaligned). */
static int rd16(const uint8_t *b, size_t total, size_t off, uint16_t *out)
{
    if (off + 2 > total) {
        return -1;
    }
    *out = (uint16_t)b[off] | ((uint16_t)b[off + 1] << 8);
    return 0;
}

static int rd32(const uint8_t *b, size_t total, size_t off, uint32_t *out)
{
    if (off + 4 > total) {
        return -1;
    }
    *out = (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) |
        ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
    return 0;
}

static const char kDefaultSection[] = ".nnode_default";

int nnode_schema_section_from_mem(const uint8_t *elf, size_t elen,
    char *out, size_t cap, size_t *outlen)
{
    uint32_t shoff, sh_name, sh_type, sh_off, sh_size;
    uint16_t shentsize, shnum, shstrndx;
    uint32_t str_off = 0, str_size = 0;
    uint16_t i;
    if (!elf || elen < 52 || !out || cap < 16) {
        return -1;
    }
    if (elf[0] != 0x7f || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F') {
        return -1;
    }
    if (rd32(elf, elen, 32, &shoff) != 0 || shoff == 0 || shoff >= elen) {
        return -1;
    }
    if (rd16(elf, elen, 46, &shentsize) != 0 || shentsize != 40) {
        return -1;
    }
    if (rd16(elf, elen, 48, &shnum) != 0 || shnum == 0 || shnum > 64) {
        return -1;
    }
    if (rd16(elf, elen, 50, &shstrndx) != 0 || shstrndx >= shnum) {
        return -1;
    } {
        /* String table section first, section names resolve through it. */
        size_t h = (size_t)shoff + (size_t)shstrndx * 40;
        if (h + 40 > elen) {
            return -1;
        }
        if (rd32(elf, elen, h + 16, &str_off) != 0 ||
            rd32(elf, elen, h + 20, &str_size) != 0) {
            return -1;
        }
        if (str_off + str_size > elen || str_size == 0) {
            return -1;
        }
    }
    for (i = 0; i < shnum; i++) {
        size_t h = (size_t)shoff + (size_t)i * 40;
        const char *nm;
        size_t maxn;
        if (h + 40 > elen) {
            return -1;
        }
        if (rd32(elf, elen, h, &sh_name) != 0 ||
            rd32(elf, elen, h + 4, &sh_type) != 0 ||
            rd32(elf, elen, h + 16, &sh_off) != 0 ||
            rd32(elf, elen, h + 20, &sh_size) != 0) {
            return -1;
        }
        if (sh_type != 1 || sh_size == 0 || sh_size > NNODE_SCHEMA_SECTION_MAX) {
            continue;
        }
        if (sh_name >= str_size) {
            continue;
        }
        nm = (const char *)elf + str_off + sh_name;
        maxn = (size_t)str_size - sh_name;
        if (memchr(nm, 0, maxn) == NULL ||
            strncmp(nm, kDefaultSection, sizeof(kDefaultSection)) != 0) {
            continue;
        }
        if (sh_off + sh_size > elen) {
            return -1;
        }
        if (sh_size + 1 > cap) {
            return -1;
        }
        if (memchr(elf + sh_off, 0, sh_size) != NULL) {
            return -1;
        }
        memcpy(out, elf + sh_off, sh_size);
        out[sh_size] = 0;
        if (outlen) {
            *outlen = sh_size;
        }
        return 0;
    }
    return -1;
}

static uint8_t *read_program_file(const char *elf_name, size_t *outlen)
{
    char path[80];
    FILE *f;
    long n;
    uint8_t *b;
    size_t cap = 65536;
    if (!elf_name || !elf_name[0] || strlen(elf_name) > 64 ||
        strchr(elf_name, '/') || strstr(elf_name, "..")) {
        return NULL;
    }
    snprintf(path, sizeof(path), "/data/programs/%s", elf_name);
    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    n = ftell(f);
    if (n <= 0 || (size_t)n > cap) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n);
    if (!b) {
        fclose(f);
        return NULL;
    }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (outlen) {
        *outlen = (size_t)n;
    }
    return b;
}

int nnode_schema_section_from_file(const char *elf_name,
    char *out, size_t cap, size_t *outlen)
{
    uint8_t *b;
    size_t n = 0;
    int rc;
    b = read_program_file(elf_name, &n);
    if (!b) {
        return -1;
    }
    rc = nnode_schema_section_from_mem(b, n, out, cap, outlen);
    free(b);
    return rc;
}

/* Stored values text from schema defaults or a merged map. */
static int values_text(const nnode_schema_t *s, char vals[][128],
    char *out, size_t cap)
{
    size_t o = 0;
    int i;
    int n = snprintf(out, cap, "package %s\nversion %d\n", s->pkg, s->version);
    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    o = (size_t)n;
    for (i = 0; i < s->nfields; i++) {
        const char *v = vals ? vals[i] : s->fields[i].dflt;
        char e[140];
        int k;
        if (nnode_cfg_escape(v, e, sizeof(e)) != 0) {
            return -1;
        }
        k = snprintf(out + o, cap - o, "%s '%s'\n", s->fields[i].key, e);
        if (k < 0 || (size_t)k >= cap - o) {
            return -1;
        }
        o += (size_t)k;
    }
    return 0;
}

/* Parse a stored values file. Unknown keys land in okeys with an
 * empty raw slot marker (-2 index), so the caller can drop orphans. */
static int parse_values(const char *text, const char *pkg,
    char keys[][NNODE_SCHEMA_ID_MAX + 1], char vals[][128], int *n)
{
    const char *p = text;
    char line[NNODE_SCHEMA_LINE_MAX];
    int stage = 0, cnt = 0, i;
    *n = 0;
    while (*p) {
        const char *q = next_line(p, line, sizeof(line), NULL);
        char *t = line;
        char *sp, *tb;
        char raw[128];
        if (!q) {
            return -1;
        }
        p = q;
        while (*t == ' ' || *t == '\t') {
            t++;
        }
        if (!t[0] || t[0] == '#') {
            continue;
        }
        if (stage == 0) {
            if (strncmp(t, "package ", 8) != 0 || strcmp(t + 8, pkg) != 0) {
                return -1;
            }
            stage = 1;
            continue;
        }
        if (stage == 1) {
            long v;
            if (strncmp(t, "version ", 8) != 0 || parse_long(t + 8, &v) != 0 ||
                v < 1 || v > 999999) {
                return -1;
            }
            stage = 2;
            continue;
        }
        sp = strchr(t, ' ');
        if (!sp) {
            sp = strchr(t, '\t');
        }
        if (!sp || cnt >= NNODE_SCHEMA_FIELDS) {
            return -1;
        }
        *sp = 0;
        tb = sp + 1;
        while (*tb == ' ' || *tb == '\t') {
            tb++;
        }
        if (!id_valid(t) || !tb[0]) {
            return -1;
        }
        for (i = 0; i < cnt; i++) {
            if (strcmp(keys[i], t) == 0) {
                return -1;
            }
        }
        if (nnode_cfg_unescape(tb, raw, sizeof(raw)) != 0) {
            return -1;
        }
        /* id_valid above caps the key at 32 chars: plain copy. */
        memcpy(keys[cnt], t, strlen(t) + 1);
        memcpy(vals[cnt], raw, strlen(raw) + 1);
        cnt++;
    }
    if (stage != 2) {
        return -1;
    }
    *n = cnt;
    return 0;
}

/* Merge core shared by the ELF and firmware paths: the stored map is
 * overlaid with defaults (missing added, orphans dropped, bad reset),
 * the file is rewritten only when it differs. 1 ok, 0 failure. */
static int ensure_core(nnode_schema_t *s, char *pkg_out, size_t pkg_cap)
{
    char (*merged)[128] = malloc(128 * NNODE_SCHEMA_FIELDS);
    char (*gotk)[NNODE_SCHEMA_ID_MAX + 1] = NULL;
    char (*gotv)[128] = NULL;
    char *stored = NULL;
    char *fresh = NULL;
    size_t got = 0;
    int nstored = 0, have_stored = 0, i, rc = 0;
    if (!merged) {
        goto out;
    }
    gotk = malloc(sizeof(*gotk) * NNODE_SCHEMA_FIELDS);
    gotv = malloc(sizeof(*gotv) * NNODE_SCHEMA_FIELDS);
    stored = malloc(NNODE_SCHEMA_FILE_MAX);
    fresh = malloc(NNODE_SCHEMA_FILE_MAX);
    if (!gotk || !gotv || !stored || !fresh) {
        goto out;
    }
    if (nnode_cfg_read(s->pkg, stored, NNODE_SCHEMA_FILE_MAX, &got) == 0 &&
        parse_values(stored, s->pkg, gotk, gotv, &nstored) == 0) {
        have_stored = 1;
    } else {
        nstored = 0;
        have_stored = 0;
    }
    for (i = 0; i < s->nfields; i++) {
        int j, hit = -1;
        for (j = 0; j < nstored; j++) {
            if (strcmp(gotk[j], s->fields[i].key) == 0) {
                hit = j;
                break;
            }
        }
        if (hit >= 0 && nnode_schema_value_ok(&s->fields[i], gotv[hit]) == 0) {
            snprintf(merged[i], 128, "%s", gotv[hit]);
        } else {
            if (hit >= 0) {
                ESP_LOGW(TAG, "%s: bad %s, default back", s->pkg,
                    s->fields[i].key);
            }
            snprintf(merged[i], 128, "%s", s->fields[i].dflt);
        }
    }
    if (values_text(s, merged, fresh, NNODE_SCHEMA_FILE_MAX) != 0) {
        ESP_LOGW(TAG, "%s: values build failed", s->pkg);
        goto out;
    }
    if (!have_stored || strcmp(stored, fresh) != 0) {
        if (nnode_cfg_write(s->pkg, fresh) != 0) {
            ESP_LOGW(TAG, "%s: values write failed", s->pkg);
            goto out;
        }
        ESP_LOGI(TAG, "%s: values %s", s->pkg,
            have_stored ? "merged" : "created");
    }
    if (pkg_out && pkg_cap) {
        snprintf(pkg_out, pkg_cap, "%s", s->pkg);
    }
    rc = 1;
out:
    free(merged);
    free(gotk);
    free(gotv);
    free(stored);
    free(fresh);
    return rc;
}

int nnode_schema_ensure(const char *elf_name, char *pkg_out, size_t pkg_cap)
{
    nnode_schema_t *s = malloc(sizeof(*s));
    char *def = malloc(NNODE_SCHEMA_SECTION_MAX + 1);
    int rc = 0;
    if (!s || !def) {
        goto out;
    }
    if (nnode_schema_section_from_file(elf_name, def, NNODE_SCHEMA_SECTION_MAX + 1, NULL) != 0) {
        goto out;
    }
    if (nnode_schema_parse(def, s, 0) != 0) {
        ESP_LOGW(TAG, "defaults %s: invalid", elf_name);
        goto out;
    }
    rc = ensure_core(s, pkg_out, pkg_cap);
out:
    free(s);
    free(def);
    return rc;
}

int nnode_schema_has(const char *elf_name)
{
    nnode_schema_t *s = malloc(sizeof(*s));
    char *def = malloc(NNODE_SCHEMA_SECTION_MAX + 1);
    int rc = 0;
    if (!s || !def) {
        goto out;
    }
    if (nnode_schema_section_from_file(elf_name, def, NNODE_SCHEMA_SECTION_MAX + 1, NULL) != 0) {
        goto out;
    }
    if (nnode_schema_parse(def, s, 0) != 0) {
        goto out;
    }
    rc = 1;
out:
    free(s);
    free(def);
    return rc;
}

int nnode_schema_ensure_text(const char *defaults)
{
    nnode_schema_t *s = malloc(sizeof(*s));
    int rc = 0;
    if (!s) {
        return 0;
    }
    if (!defaults || nnode_schema_parse(defaults, s, 1) != 0) {
        ESP_LOGW(TAG, "firmware defaults invalid");
        free(s);
        return 0;
    }
    rc = ensure_core(s, NULL, 0);
    free(s);
    return rc;
}

int nnode_schema_kv(const char *pkg, const char *text,
    char keys[][NNODE_SCHEMA_ID_MAX + 1], char vals[][128], int *n)
{
    if (!pkg || !text || !keys || !vals || !n) {
        return -1;
    }
    return parse_values(text, pkg, keys, vals, n);
}

/* Exact key set plus typed values against the schema. */
static int check_complete(const nnode_schema_t *s,
    char keys[][NNODE_SCHEMA_ID_MAX + 1], char vals[][128], int n)
{
    int i;
    if (n != s->nfields) {
        return -1;
    }
    for (i = 0; i < s->nfields; i++) {
        int j, hit = -1;
        for (j = 0; j < n; j++) {
            if (strcmp(keys[j], s->fields[i].key) == 0) {
                hit = j;
                break;
            }
        }
        if (hit < 0 || nnode_schema_value_ok(&s->fields[i], vals[hit]) != 0) {
            return -1;
        }
    }
    return 0;
}

int nnode_schema_validate(const char *defaults, const char *text)
{
    nnode_schema_t *s = malloc(sizeof(*s));
    char (*keys)[NNODE_SCHEMA_ID_MAX + 1] = malloc(sizeof(*keys) * NNODE_SCHEMA_FIELDS);
    char (*vals)[128] = malloc(sizeof(*vals) * NNODE_SCHEMA_FIELDS);
    int n = 0, rc = -1;
    if (!s || !keys || !vals || !defaults || !text) {
        goto out;
    }
    if (nnode_schema_parse(defaults, s, 1) != 0) {
        goto out;
    }
    if (parse_values(text, s->pkg, keys, vals, &n) != 0) {
        goto out;
    }
    rc = check_complete(s, keys, vals, n);
out:
    free(s);
    free(keys);
    free(vals);
    return rc;
}

int nnode_schema_store(const char *defaults,
    char keys[][NNODE_SCHEMA_ID_MAX + 1], char vals[][128], int n)
{
    nnode_schema_t *s = malloc(sizeof(*s));
    char (*ordered)[128] = malloc(sizeof(*ordered) * NNODE_SCHEMA_FIELDS);
    char *text = malloc(NNODE_SCHEMA_FILE_MAX);
    int i, rc = -1;
    if (!s || !ordered || !text || !defaults || !keys || !vals) {
        goto out;
    }
    if (nnode_schema_parse(defaults, s, 1) != 0) {
        goto out;
    }
    if (check_complete(s, keys, vals, n) != 0) {
        goto out;
    }
    for (i = 0; i < s->nfields; i++) {
        int j;
        for (j = 0; j < n; j++) {
            if (strcmp(keys[j], s->fields[i].key) == 0) {
                memcpy(ordered[i], vals[j], strlen(vals[j]) + 1);
                break;
            }
        }
    }
    if (values_text(s, ordered, text, NNODE_SCHEMA_FILE_MAX) != 0) {
        goto out;
    }
    rc = nnode_cfg_write(s->pkg, text);
out:
    free(s);
    free(ordered);
    free(text);
    return rc;
}

int nnode_schema_lookup(const char *pkg, const char *key, char *out, size_t len)
{
    /* Heap: 5 KB of pairs, called from the tiny main task too. */
    char *text = malloc(2048);
    char (*keys)[NNODE_SCHEMA_ID_MAX + 1] = malloc(sizeof(*keys) * NNODE_SCHEMA_FIELDS);
    char (*vals)[128] = malloc(sizeof(*vals) * NNODE_SCHEMA_FIELDS);
    size_t got = 0;
    int n = 0, i, rc = -1;
    if (!text || !keys || !vals) {
        goto out;
    }
    if (!id_valid(key) || !out || len < 2) {
        goto out;
    }
    if (nnode_cfg_read(pkg, text, 2048, &got) != 0 ||
        parse_values(text, pkg, keys, vals, &n) != 0) {
        goto out;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(keys[i], key) == 0) {
            size_t vl = strlen(vals[i]) + 1;
            if (vl <= len) {
                memcpy(out, vals[i], vl);
                rc = 0;
            }
            break;
        }
    }
out:
    free(text);
    free(keys);
    free(vals);
    return rc;
}

static size_t json_esc(const char *src, char *dst, size_t cap)
{
    size_t o = 0;
    if (!cap) {
        return 0;
    }
    for (; *src && o + 1 < cap; src++) {
        if (*src == '"' || *src == '\\') {
            if (o + 2 >= cap) {
                break;
            }
            dst[o++] = '\\';
            dst[o++] = *src;
        } else if ((unsigned char)*src < 0x20) {
            if (o + 6 >= cap) {
                break;
            }
            o += (size_t)snprintf(dst + o, cap - o, "\\u%04x", *src);
        } else {
            dst[o++] = *src;
        }
    }
    dst[o] = 0;
    return o;
}

int nnode_config_json(const char *elf_name, char *buf, size_t len)
{
    nnode_schema_t *s = malloc(sizeof(*s));
    char *def = malloc(NNODE_SCHEMA_SECTION_MAX + 1);
    /* Heap: 4 KB of values would not fit a worker stack twice. */
    char (*vals)[128] = malloc(sizeof(*vals) * NNODE_SCHEMA_FIELDS);
    char pkg[24];
    size_t o = 0;
    int i, j;
    char esc[512];
    if (!s || !def || !vals) {
        free(s);
        free(def);
        free(vals);
        return 0;
    }
    if (nnode_schema_section_from_file(elf_name, def, NNODE_SCHEMA_SECTION_MAX + 1, NULL) != 0 ||
        nnode_schema_parse(def, s, 0) != 0) {
        free(s);
        free(def);
        free(vals);
        return 0;
    }
    nnode_schema_ensure(elf_name, NULL, 0);
    snprintf(pkg, sizeof(pkg), "%s", s->pkg);
    for (i = 0; i < s->nfields; i++) {
        if (nnode_schema_lookup(pkg, s->fields[i].key, vals[i], sizeof(vals[i])) != 0) {
            snprintf(vals[i], sizeof(vals[i]), "%s", s->fields[i].dflt);
        }
    }
    o += snprintf(buf + o, len - o,
        "{\"pkg\":\"%s\",\"version\":%d,\"tabs\":[", s->pkg, s->version);
    for (i = 0; i < s->ntabs && o < len; i++) {
        json_esc(s->tabs[i].title, esc, sizeof(esc));
        o += snprintf(buf + o, len - o, "%s{\"id\":\"%s\",\"title\":\"%s\"}",
            i ? "," : "", s->tabs[i].id, esc);
    }
    o += snprintf(buf + o, len - o, "],\"fields\":[");
    for (i = 0; i < s->nfields && o < len; i++) {
        nnode_schema_field_t *f = &s->fields[i];
        char show[256], sub[32];
        json_esc(f->label, esc, sizeof(esc));
        json_esc(f->show_if, show, sizeof(show));
        json_esc(f->sub, sub, sizeof(sub));
        o += snprintf(buf + o, len - o,
            "%s{\"key\":\"%s\",\"tab\":\"%s\",\"type\":\"%s\",\"label\":\"%s\","
            "\"default\":\"%s\",\"min\":%ld,\"max\":%ld,\"maxlen\":%ld,"
            "\"has_min\":%d,\"has_max\":%d,"
            "\"sub\":\"%s\",\"maxitems\":%d,\"show_if\":\"%s\",\"options\":[",
            i ? "," : "", f->key, f->tab, f->type, esc, f->dflt,
            f->has_min ? f->min : 0, f->has_max ? f->max : 0, f->maxlen,
            f->has_min ? 1 : 0, f->has_max ? 1 : 0,
            sub, f->maxitems, show);
        for (j = 0; j < f->nvals && o < len; j++) {
            o += snprintf(buf + o, len - o, "%s\"%s\"",
                j ? "," : "", f->vals[j]);
        }
        o += snprintf(buf + o, len - o, "]}");
    }
    o += snprintf(buf + o, len - o, "],\"values\":{");
    for (i = 0; i < s->nfields && o < len; i++) {
        json_esc(vals[i], esc, sizeof(esc));
        o += snprintf(buf + o, len - o, "%s\"%s\":\"%s\"",
            i ? "," : "", s->fields[i].key, esc);
    }
    if (o < len) {
        o += snprintf(buf + o, len - o, "}}");
    }
    free(s);
    free(def);
    free(vals);
    return (int)o;
}

/* Flat JSON object parser: {"k":"v", ...} with string values only.
 * Non-string values and unknown escapes fail the pair. The callback
 * gets every decoded pair, last duplicate wins at the call site. */
typedef void (*json_pair_cb)(const char *k, const char *v, void *ctx);

static int json_flat_each(const char *body, json_pair_cb cb, void *ctx)
{
    const char *p = body;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p != '{') {
        return -1;
    }
    p++;
    for (;;) {
        char k[40], v[256];
        size_t ko = 0, vo = 0;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (*p == '}') {
            return 0;
        }
        if (*p != '"') {
            return -1;
        }
        p++;
        while (*p && *p != '"' && ko + 1 < sizeof(k)) {
            if (*p == '\\' && p[1]) {
                p++;
                k[ko++] = *p++;
            } else if (*p == '\\') {
                return -1;
            } else {
                k[ko++] = *p++;
            }
        }
        if (*p != '"') {
            return -1;
        }
        p++;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (*p != ':') {
            return -1;
        }
        p++;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (*p != '"') {
            return -1;
        }
        p++;
        while (*p && *p != '"' && vo + 1 < sizeof(v)) {
            if (*p == '\\' && p[1]) {
                char e = p[1];
                p += 2;
                if (e == 'n') {
                    v[vo++] = '\n';
                } else if (e == 'r') {
                    v[vo++] = '\r';
                } else if (e == 't') {
                    v[vo++] = '\t';
                } else if (e == 'b') {
                    v[vo++] = '\b';
                } else if (e == 'f') {
                    v[vo++] = '\f';
                } else if (e == '/' || e == '"' || e == '\\') {
                    v[vo++] = e;
                } else {
                    return -1;
                }
            } else if (*p == '\\') {
                return -1;
            } else {
                v[vo++] = *p++;
            }
        }
        if (*p != '"') {
            return -1;
        }
        p++;
        k[ko] = 0;
        v[vo] = 0;
        cb(k, v, ctx);
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            return 0;
        }
        return -1;
    }
}

typedef struct {
    char keys[NNODE_SCHEMA_FIELDS][NNODE_SCHEMA_ID_MAX + 1];
    char vals[NNODE_SCHEMA_FIELDS][256];
    int n;
} save_body_t;

static void save_pair(const char *k, const char *v, void *ctx)
{
    save_body_t *b = ctx;
    int i;
    if (!id_valid(k)) {
        return;
    }
    for (i = 0; i < b->n; i++) {
        if (strcmp(b->keys[i], k) == 0) {
            snprintf(b->vals[i], sizeof(b->vals[i]), "%s", v);
            return;
        }
    }
    if (b->n >= NNODE_SCHEMA_FIELDS) {
        return;
    }
    /* id_valid above caps the key: plain copies (v fits: vo < 256). */
    memcpy(b->keys[b->n], k, strlen(k) + 1);
    memcpy(b->vals[b->n], v, strlen(v) + 1);
    b->n++;
}

int nnode_config_save(const char *elf_name, const char *body)
{
    nnode_schema_t *s = malloc(sizeof(*s));
    char *def = malloc(NNODE_SCHEMA_SECTION_MAX + 1);
    /* Heap: values plus the posted body exceed any task stack. */
    char (*vals)[128] = malloc(sizeof(*vals) * NNODE_SCHEMA_FIELDS);
    char pkg[24];
    save_body_t *b = malloc(sizeof(*b));
    char *fresh = NULL;
    int i, rc = -1;
    if (!s || !def || !vals || !b) {
        free(s);
        free(def);
        free(vals);
        free(b);
        return -1;
    }
    if (!body || nnode_schema_section_from_file(elf_name, def,
            NNODE_SCHEMA_SECTION_MAX + 1, NULL) != 0 ||
        nnode_schema_parse(def, s, 0) != 0) {
        free(s);
        free(def);
        free(vals);
        free(b);
        return -1;
    }
    nnode_schema_ensure(elf_name, NULL, 0);
    snprintf(pkg, sizeof(pkg), "%s", s->pkg);
    for (i = 0; i < s->nfields; i++) {
        if (nnode_schema_lookup(pkg, s->fields[i].key, vals[i], sizeof(vals[i])) != 0) {
            snprintf(vals[i], sizeof(vals[i]), "%s", s->fields[i].dflt);
        }
    }
    memset(b, 0, sizeof(*b));
    if (json_flat_each(body, save_pair, b) != 0) {
        free(s);
        free(def);
        free(vals);
        free(b);
        return -2;
    }
    for (i = 0; i < s->nfields; i++) {
        int j;
        for (j = 0; j < b->n; j++) {
            if (strcmp(b->keys[j], s->fields[i].key) == 0) {
                size_t vl = strlen(b->vals[j]);
                if (vl >= sizeof(vals[i]) ||
                    nnode_schema_value_ok(&s->fields[i], b->vals[j]) != 0) {
                    ESP_LOGW(TAG, "save %s: bad %s", elf_name,
                        s->fields[i].key);
                    free(s);
                    free(def);
                    free(vals);
                    free(b);
                    return -2;
                }
                memcpy(vals[i], b->vals[j], vl + 1);
                break;
            }
        }
    }
    fresh = malloc(NNODE_SCHEMA_FILE_MAX);
    if (!fresh) {
        free(s);
        free(def);
        free(vals);
        free(b);
        return -3;
    }
    if (values_text(s, vals, fresh, NNODE_SCHEMA_FILE_MAX) != 0) {
        free(fresh);
        free(s);
        free(def);
        free(vals);
        free(b);
        return -3;
    }
    rc = nnode_cfg_write(s->pkg, fresh) == 0 ? 0 : -3;
    free(fresh);
    free(s);
    free(def);
    free(vals);
    free(b);
    return rc;
}

int nnode_schema_syntax_ok(const char *pkg, const char *text)
{
    /* Heap: 5 KB does not belong on a task stack. */
    char (*keys)[NNODE_SCHEMA_ID_MAX + 1] = malloc(sizeof(*keys) * NNODE_SCHEMA_FIELDS);
    char (*vals)[128] = malloc(sizeof(*vals) * NNODE_SCHEMA_FIELDS);
    int n = 0, rc = -1;
    if (!keys || !vals) {
        goto out;
    }
    if (!nnode_cfg_valid_pkg(pkg) || nnode_cfg_reserved(pkg) || !text) {
        goto out;
    }
    rc = parse_values(text, pkg, keys, vals, &n);
out:
    free(keys);
    free(vals);
    return rc;
}
