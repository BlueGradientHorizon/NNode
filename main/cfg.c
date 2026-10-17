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

/* Packages owned by the firmware: loadable modules must not claim them
 * for their own settings (their declared config name is rejected). */
int nnode_cfg_reserved(const char *pkg)
{
    static const char *const kReserved[] = {
        NNOODE_CFG_PKG_DEVICE,
        NNOODE_CFG_PKG_TIME,
        NNOODE_CFG_PKG_STA,
        NNOODE_CFG_PKG_NETS,
        NNOODE_CFG_PKG_AP,
        NNOODE_CFG_PKG_AUTOSTART,
        NULL,
    };
    int i;
    if (!pkg) {
        return 1;
    }
    for (i = 0; kReserved[i]; i++) {
        if (strcmp(pkg, kReserved[i]) == 0) {
            return 1;
        }
    }
    return 0;
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
    /* One-time move from the old location: same flat syntax, no
     * conversion needed. A failed rename falls through to a fresh dir
     * (missing files heal from defaults on first sight). */
    memset(&st, 0, sizeof(st));
    if (stat("/data/config", &st) == 0 &&
        rename("/data/config", NNOODE_CFG_DIR) == 0) {
        ESP_LOGI(TAG, "moved /data/config to %s", NNOODE_CFG_DIR);
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
