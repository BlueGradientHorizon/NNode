#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "cfg.h"

static const char *TAG = "nnode_cfg";

int nnode_cfg_valid_pkg(const char *pkg)
{
    size_t n;
    if (!pkg || (n = strlen(pkg)) == 0 || n >= NNOODE_CFG_PKG_MAX) {
        return 0;
    }
    while (*pkg) {
        char c = *pkg++;
        if ((c < 'a' || c > 'z') && (c < '0' || c > '9') && c != '_') {
            return 0;
        }
    }
    return 1;
}

static void pkg_path(const char *pkg, char *out, size_t cap, const char *suffix)
{
    if (suffix) {
        snprintf(out, cap, "%s/.%s%s", NNOODE_CFG_DIR, pkg, suffix);
    } else {
        snprintf(out, cap, "%s/%s", NNOODE_CFG_DIR, pkg);
    }
}

int nnode_cfg_init(void)
{
    struct stat st;
    memset(&st, 0, sizeof(st));
    if (stat(NNOODE_CFG_DIR, &st) == 0) {
        return 0;
    }
    if (mkdir(NNOODE_CFG_DIR, 0755) == 0) {
        return 0;
    }
    ESP_LOGE(TAG, "mkdir %s failed", NNOODE_CFG_DIR);
    return -1;
}

int nnode_cfg_exists(const char *pkg)
{
    char path[64];
    struct stat st;
    if (!nnode_cfg_valid_pkg(pkg)) {
        return 0;
    }
    pkg_path(pkg, path, sizeof(path), NULL);
    memset(&st, 0, sizeof(st));
    return stat(path, &st) == 0;
}

int nnode_cfg_remove(const char *pkg)
{
    char path[64];
    if (!nnode_cfg_valid_pkg(pkg)) {
        return -1;
    }
    pkg_path(pkg, path, sizeof(path), NULL);
    return unlink(path);
}

int nnode_cfg_read(const char *pkg, char *buf, size_t cap, size_t *out_len)
{
    char path[64];
    FILE *f;
    size_t n = 0;
    if (!nnode_cfg_valid_pkg(pkg) || !buf || cap < 2) {
        return -1;
    }
    pkg_path(pkg, path, sizeof(path), NULL);
    f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    n = fread(buf, 1, cap - 1, f);
    if (ferror(f)) {
        fclose(f);
        return -1;
    }
    /* A file that does not fit the buffer is rejected: configs are
     * small by design, truncation would silently drop settings. */
    if (!feof(f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    buf[n] = 0;
    if (out_len) {
        *out_len = n;
    }
    return 0;
}

int nnode_cfg_write(const char *pkg, const char *text)
{
    char path[64], tmp[72];
    FILE *f;
    size_t n, w;
    if (!nnode_cfg_valid_pkg(pkg) || !text) {
        return -1;
    }
    n = strlen(text);
    /* Sanity cap: a single package file must stay small enough to
     * parse in RAM and to fit the backup buffers. */
    if (n > 8192) {
        return -1;
    }
    nnode_cfg_init();
    pkg_path(pkg, path, sizeof(path), NULL);
    pkg_path(pkg, tmp, sizeof(tmp), ".tmp");
    f = fopen(tmp, "w");
    if (!f) {
        return -1;
    }
    w = fwrite(text, 1, n, f);
    if (w != n || fflush(f) != 0 || ferror(f)) {
        fclose(f);
        unlink(tmp);
        return -1;
    }
    if (fclose(f) != 0) {
        unlink(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

int nnode_cfg_list(char names[][NNOODE_CFG_PKG_MAX], int max)
{
    DIR *d;
    struct dirent *e;
    int n = 0;
    if (!names || max <= 0) {
        return 0;
    }
    d = opendir(NNOODE_CFG_DIR);
    if (!d) {
        return 0;
    }
    while ((e = readdir(d)) != NULL && n < max) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        /* Tmp files from an interrupted write are never packages. */
        if (e->d_name[0] == '.') {
            continue;
        }
        if (strlen(e->d_name) >= NNOODE_CFG_PKG_MAX) {
            continue;
        }
        if (!nnode_cfg_valid_pkg(e->d_name)) {
            continue;
        }
        {
            size_t dl = strlen(e->d_name);
            memcpy(names[n], e->d_name, dl + 1);
        }
        n++;
    }
    closedir(d);
    return n;
}

int nnode_cfg_escape(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    if (!in || !out || cap < 1) {
        return -1;
    }
    while (*in) {
        const char *esc = NULL;
        char c = *in++;
        if (c == '\\') {
            esc = "\\\\";
        } else if (c == '\'') {
            esc = "\\'";
        } else if (c == '\n') {
            esc = "\\n";
        } else if (c == '\r') {
            esc = "\\r";
        } else if (c == '\t') {
            esc = "\\t";
        }
        if (esc) {
            if (o + 2 >= cap) {
                return -1;
            }
            out[o++] = esc[0];
            out[o++] = esc[1];
        } else {
            if (o + 1 >= cap) {
                return -1;
            }
            out[o++] = c;
        }
    }
    out[o] = 0;
    return 0;
}

/* Token may be 'quoted' or bare. Bare form must not contain spaces,
 * quotes or backslashes. */
int nnode_cfg_unescape(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    if (!in || !out || cap < 1) {
        return -1;
    }
    if (in[0] != '\'') {
        while (*in) {
            char c = *in++;
            if (c == '\'' || c == '\\' || isspace((unsigned char)c)) {
                return -1;
            }
            if (o + 1 >= cap) {
                return -1;
            }
            out[o++] = c;
        }
        out[o] = 0;
        return 0;
    }
    in++;
    for (;;) {
        char c = *in++;
        if (c == 0) {
            return -1;
        }
        if (c == '\'') {
            if (*in != 0) {
                return -1;
            }
            out[o] = 0;
            return 0;
        }
        if (c == '\\') {
            char e = *in++;
            if (e == '\\') {
                c = '\\';
            } else if (e == '\'') {
                c = '\'';
            } else if (e == 'n') {
                c = '\n';
            } else if (e == 'r') {
                c = '\r';
            } else if (e == 't') {
                c = '\t';
            } else {
                return -1;
            }
        }
        if (o + 1 >= cap) {
            return -1;
        }
        out[o++] = c;
    }
}

static int is_token(const char *s, size_t maxlen)
{
    size_t n = strlen(s);
    if (n == 0 || n >= maxlen) {
        return 0;
    }
    while (*s) {
        char c = *s++;
        if (!isalnum((unsigned char)c) && c != '_' && c != '-' && c != '.') {
            return 0;
        }
    }
    return 1;
}

typedef struct {
    const char *pkg;
    const nnode_cfg_cb_t *cb;
    void *ctx;
    int lineno;
    int got_package;
    int in_section;
} parse_st_t;

/* Split "word rest..." into first word and pointer to the rest. */
static const char *next_word(const char *p, char *word, size_t cap)
{
    size_t o = 0;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    while (*p && *p != ' ' && *p != '\t') {
        if (o + 1 >= cap) {
            return NULL;
        }
        word[o++] = *p++;
    }
    word[o] = 0;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

static int parse_line(parse_st_t *st, char *line)
{
    char w[NNOODE_CFG_LINE_MAX];
    const char *p = line;
    /* Strip trailing whitespace and newline. */
    size_t n = strlen(p);
    while (n > 0 && (p[n - 1] == '\n' || p[n - 1] == '\r' ||
            p[n - 1] == ' ' || p[n - 1] == '\t')) {
        n--;
    }
    line[n] = 0;
    p = line;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (!*p || *p == '#') {
        return 0;
    }
    p = next_word(p, w, sizeof(w));
    if (!p) {
        return -1;
    }
    if (strcmp(w, "package") == 0) {
        char name[NNOODE_CFG_PKG_MAX];
        if (st->got_package) {
            return -1;
        }
        p = next_word(p, name, sizeof(name));
        if (!p || *p || !nnode_cfg_valid_pkg(name) || strcmp(name, st->pkg) != 0) {
            return -1;
        }
        st->got_package = 1;
        return 0;
    }
    if (!st->got_package) {
        return -1;
    }
    if (strcmp(w, "config") == 0) {
        char type[NNOODE_CFG_TYPE_MAX], raw[NNOODE_CFG_LINE_MAX];
        char name[NNOODE_CFG_NAME_MAX];
        int has_name = 0;
        const char *r;
        p = next_word(p, type, sizeof(type));
        if (!p || !is_token(type, sizeof(type))) {
            return -1;
        }
        r = p;
        if (*r) {
            /* Optional section name: rest of line is one value token. */
            if (strlen(r) >= sizeof(raw)) {
                return -1;
            }
            strcpy(raw, r);
            if (nnode_cfg_unescape(raw, name, sizeof(name)) != 0 ||
                !is_token(name, sizeof(name))) {
                return -1;
            }
            has_name = 1;
        }
        st->in_section = 1;
        if (st->cb && st->cb->section) {
            return st->cb->section(st->ctx, type, has_name ? name : "", has_name);
        }
        return 0;
    }
    if (strcmp(w, "option") == 0 || strcmp(w, "list") == 0) {
        int is_list = strcmp(w, "list") == 0;
        char key[NNOODE_CFG_KEY_MAX], val[NNOODE_CFG_VAL_MAX];
        char raw[NNOODE_CFG_LINE_MAX];
        const char *sp;
        if (!st->in_section) {
            return -1;
        }
        p = next_word(p, key, sizeof(key));
        if (!p || !*p || !is_token(key, sizeof(key))) {
            return -1;
        }
        /* Value is the rest of the line (may contain spaces in quotes). */
        sp = p;
        while (*sp == ' ' || *sp == '\t') {
            sp++;
        }
        if (!*sp || strlen(sp) >= sizeof(raw)) {
            return -1;
        }
        strcpy(raw, sp);
        /* Trim trailing spaces outside quotes: find last non-space and
         * check it is a quote or bare token char. Simpler: rstrip then
         * unescape must consume everything. */
        {
            size_t m = strlen(raw);
            while (m > 0 && (raw[m - 1] == ' ' || raw[m - 1] == '\t')) {
                raw[--m] = 0;
            }
        }
        if (nnode_cfg_unescape(raw, val, sizeof(val)) != 0) {
            return -1;
        }
        if (is_list) {
            if (st->cb && st->cb->list) {
                return st->cb->list(st->ctx, key, val);
            }
        } else {
            if (st->cb && st->cb->option) {
                return st->cb->option(st->ctx, key, val);
            }
        }
        return 0;
    }
    return -1;
}

static int parse_stream(FILE *f, parse_st_t *st)
{
    char line[NNOODE_CFG_LINE_MAX + 2];
    st->lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        st->lineno++;
        /* Overlong line: buffer filled without newline. */
        if (!strchr(line, '\n') && !feof(f)) {
            return -1;
        }
        if (parse_line(st, line) != 0) {
            ESP_LOGW(TAG, "parse %s line %d failed", st->pkg, st->lineno);
            return -1;
        }
    }
    if (ferror(f)) {
        return -1;
    }
    return st->got_package ? 0 : -1;
}

int nnode_cfg_parse(const char *pkg, const nnode_cfg_cb_t *cb, void *ctx)
{
    char path[64];
    FILE *f;
    parse_st_t st;
    int rc;
    if (!nnode_cfg_valid_pkg(pkg)) {
        return -1;
    }
    pkg_path(pkg, path, sizeof(path), NULL);
    f = fopen(path, "r");
    if (!f) {
        return -1;
    }
    memset(&st, 0, sizeof(st));
    st.pkg = pkg;
    st.cb = cb;
    st.ctx = ctx;
    rc = parse_stream(f, &st);
    fclose(f);
    return rc;
}

int nnode_cfg_parse_text(const char *pkg, const char *text,
    const nnode_cfg_cb_t *cb, void *ctx)
{
    parse_st_t st;
    const char *p;
    if (!nnode_cfg_valid_pkg(pkg) || !text) {
        return -1;
    }
    memset(&st, 0, sizeof(st));
    st.pkg = pkg;
    st.cb = cb;
    st.ctx = ctx;
    p = text;
    while (*p) {
        char line[NNOODE_CFG_LINE_MAX + 2];
        size_t o = 0;
        while (*p && *p != '\n' && o + 1 < sizeof(line)) {
            line[o++] = *p++;
        }
        if (*p && *p != '\n') {
            return -1;
        }
        if (*p == '\n') {
            line[o++] = *p++;
        }
        line[o] = 0;
        st.lineno++;
        if (parse_line(&st, line) != 0) {
            return -1;
        }
    }
    return st.got_package ? 0 : -1;
}
