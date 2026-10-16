/* NNode backup in CBOR. Schema v2 (int keys, definite lengths only):
 *
 * map {
 *   0: uint version (=2),
 *   1: map { tstr package-name: tstr file-text },  // one entry per file
 *   2: [ map {0: tstr rel-path, 1: uint size, 2: bstr data} ]
 * }
 *
 * Config packages ride as text, plain files as raw bytes (no base64
 * tax). File entries carry slash-separated paths relative to /data,
 * so subdirectories roundtrip as well. The config dir is covered by
 * key 1 only and is excluded from key 2. Both directions stream with
 * constant RAM: emit reads files in 2 KB chunks, restore writes them
 * straight to disk the same way.
 *
 * Restore = full replacement: when the files section arrives, the
 * /data tree (except the config dir) is wiped first; known config
 * packages are validated before any of them is committed, then the
 * caller reboots. Unknown map keys are skipped (forward compatible).
 * Version 1 backups are rejected: the store moved from NVS to files,
 * there is nothing worth migrating automatically.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "cbor.h"
#include "cfg.h"
#include "files.h"
#include "fs.h"
#include "nets.h"
#include "syscfg.h"
#include "backup.h"

static const char *TAG = "nnode_backup";
#define BACKUP_VERSION 2
#define CHUNK_FILE 2048
#define CFG_FILE_MAX 8192

#define WCHK(x) do { if (x) { return -1; } } while (0)

/* Recursive walk over /data (config dir excluded: key 1 covers it).
 * rel is "" at the top. Entry names accumulate as rel paths. */
#define BK_WALK_DEPTH 16

static void join_rel(const char *rel, const char *name, char *out, size_t cap)
{
    if (!rel || !rel[0]) {
        snprintf(out, cap, "%s", name);
    } else {
        snprintf(out, cap, "%s/%s", rel, name);
    }
}

static int walk_count(const char *rel, int depth, int *n)
{
    char base[160];
    DIR *d;
    struct dirent *e;
    if (depth > BK_WALK_DEPTH || !rel || !n) {
        return -1;
    }
    if (!rel[0]) {
        snprintf(base, sizeof(base), "/data");
    } else {
        snprintf(base, sizeof(base), "/data/%s", rel);
    }
    d = opendir(base);
    if (!d) {
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        struct stat st;
        char path[256], child[144];
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (!rel[0] && strcmp(e->d_name, "config") == 0) {
            continue;
        }
        join_rel(rel, e->d_name, child, sizeof(child));
        if (strlen(child) >= sizeof(child) - 1) {
            continue;
        }
        snprintf(path, sizeof(path), "/data/%s", child);
        memset(&st, 0, sizeof(st));
        if (stat(path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (walk_count(child, depth + 1, n) != 0) {
                closedir(d);
                return -1;
            }
        } else if (st.st_size >= 0) {
            (*n)++;
        }
    }
    closedir(d);
    return 0;
}

static int walk_emit_file(cbor_w_t *w, const char *rel, const struct stat *stp)
{
    char path[256];
    FILE *f;
    size_t left;
    unsigned char *raw;
    snprintf(path, sizeof(path), "/data/%s", rel);
    WCHK(cbor_w_map(w, 3));
    WCHK(cbor_w_uint(w, 0));
    WCHK(cbor_w_tstr(w, rel));
    WCHK(cbor_w_uint(w, 1));
    WCHK(cbor_w_uint(w, (uint64_t)stp->st_size));
    WCHK(cbor_w_uint(w, 2));
    WCHK(cbor_w_bstr_head(w, (uint64_t)stp->st_size));
    raw = malloc(CHUNK_FILE);
    f = fopen(path, "rb");
    if (!f || !raw) {
        free(raw);
        if (f) {
            fclose(f);
        }
        return -1;
    }
    left = (size_t)stp->st_size;
    while (left > 0) {
        size_t want = left > CHUNK_FILE ? CHUNK_FILE : left;
        size_t got = fread(raw, 1, want, f);
        if (!got) {
            break;
        }
        if (cbor_w_raw(w, raw, got)) {
            break;
        }
        left -= got;
    }
    free(raw);
    fclose(f);
    return left == 0 ? 0 : -1;
}

static int walk_emit(cbor_w_t *w, const char *rel, int depth)
{
    char base[160];
    DIR *d;
    struct dirent *e;
    if (depth > BK_WALK_DEPTH || !rel || w->failed) {
        return -1;
    }
    if (!rel[0]) {
        snprintf(base, sizeof(base), "/data");
    } else {
        snprintf(base, sizeof(base), "/data/%s", rel);
    }
    d = opendir(base);
    if (!d) {
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        struct stat st;
        char path[256], child[144];
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (!rel[0] && strcmp(e->d_name, "config") == 0) {
            continue;
        }
        join_rel(rel, e->d_name, child, sizeof(child));
        if (strlen(child) >= sizeof(child) - 1) {
            continue;
        }
        snprintf(path, sizeof(path), "/data/%s", child);
        memset(&st, 0, sizeof(st));
        if (stat(path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (walk_emit(w, child, depth + 1) != 0) {
                break;
            }
        } else if (st.st_size >= 0) {
            /* A file vanishing mid-emit aborts the download. */
            if (walk_emit_file(w, child, &st) != 0) {
                break;
            }
        }
        if (w->failed) {
            break;
        }
    }
    closedir(d);
    return w->failed ? -1 : 0;
}

int nnode_config_export(cbor_emit_fn emit, void *ctx)
{
    char names[16][NNOODE_CFG_PKG_MAX];
    int n, i;
    if (!emit) {
        return -1;
    }
    n = nnode_cfg_list(names, 16);
    for (i = 0; i < n; i++) {
        char *text = malloc(CFG_FILE_MAX + 2);
        size_t len = 0;
        int rc;
        if (!text) {
            return -1;
        }
        rc = nnode_cfg_read(names[i], text, CFG_FILE_MAX + 2, &len);
        if (rc != 0) {
            free(text);
            return -1;
        }
        if (len == 0 || text[len - 1] != '\n') {
            if (len + 1 >= CFG_FILE_MAX + 2) {
                free(text);
                return -1;
            }
            text[len++] = '\n';
            text[len] = 0;
        }
        if (i > 0) {
            if (emit(ctx, "\n", 1)) {
                free(text);
                return -1;
            }
        }
        if (emit(ctx, text, len)) {
            free(text);
            return -1;
        }
        free(text);
    }
    return 0;
}

int nnode_backup_emit(cbor_emit_fn emit, void *ctx)
{
    cbor_w_t w;
    char names[16][NNOODE_CFG_PKG_MAX];
    int npkgs, i, nf = 0;

    npkgs = nnode_cfg_list(names, 16);

    /* File count first: arrays are definite-length, so no second
     * chances. */
    nf = 0;
    if (walk_count("", 0, &nf) != 0) {
        return -1;
    }

    cbor_w_init(&w, emit, ctx);
    WCHK(cbor_w_map(&w, 3));
    WCHK(cbor_w_uint(&w, 0));
    WCHK(cbor_w_uint(&w, BACKUP_VERSION));
    WCHK(cbor_w_uint(&w, 1));
    WCHK(cbor_w_map(&w, (uint64_t)npkgs));
    for (i = 0; i < npkgs; i++) {
        char *text = malloc(CFG_FILE_MAX + 1);
        size_t len = 0;
        if (!text) {
            return -1;
        }
        if (nnode_cfg_read(names[i], text, CFG_FILE_MAX + 1, &len) != 0) {
            free(text);
            return -1;
        }
        WCHK(cbor_w_tstr(&w, names[i]));
        WCHK(cbor_w_tstr(&w, text));
        free(text);
        if (w.failed) {
            return -1;
        }
    }
    WCHK(cbor_w_uint(&w, 2));
    WCHK(cbor_w_array(&w, (uint64_t)nf));
    if (walk_emit(&w, "", 0) != 0) {
        return -1;
    }
    WCHK(cbor_w_flush(&w));
    return w.failed ? -1 : 0;
}

/* Typed validation for the known packages. Unknown packages only get
 * a syntax check: there are no rules to check them against. */
static int config_valid(const char *pkg, const char *text)
{
    if (!nnode_cfg_valid_pkg(pkg) || !text) {
        return -1;
    }
    if (strcmp(pkg, NNOODE_CFG_PKG_DEVICE) == 0) {
        return nnode_device_validate(text);
    }
    if (strcmp(pkg, NNOODE_CFG_PKG_TIME) == 0) {
        return nnode_time_validate(text);
    }
    if (strcmp(pkg, NNOODE_CFG_PKG_STA) == 0) {
        return nnode_sta_validate(text);
    }
    if (strcmp(pkg, NNOODE_CFG_PKG_AP) == 0) {
        return nnode_ap_validate(text);
    }
    return nnode_cfg_parse_text(pkg, text, NULL, NULL);
}

static int wipe_tree(const char *base, int depth)
{
    DIR *d = opendir(base);
    struct dirent *e;
    if (!d || depth > BK_WALK_DEPTH) {
        if (d) {
            closedir(d);
        }
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char path[256];
        struct stat st;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%.64s", base, e->d_name);
        memset(&st, 0, sizeof(st));
        if (stat(path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (wipe_tree(path, depth + 1) != 0 || rmdir(path) != 0) {
                closedir(d);
                return -1;
            }
        } else if (unlink(path) != 0) {
            closedir(d);
            return -1;
        }
    }
    closedir(d);
    return 0;
}

static int wipe_data_files(void)
{
    DIR *d = opendir("/data");
    struct dirent *e;
    int n = 0;
    if (!d) {
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char path[256];
        struct stat st;
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0 ||
            strcmp(e->d_name, "config") == 0) {
            continue;
        }
        snprintf(path, sizeof(path), "/data/%.64s", e->d_name);
        memset(&st, 0, sizeof(st));
        if (stat(path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (wipe_tree(path, 0) != 0 || rmdir(path) != 0) {
                closedir(d);
                return -1;
            }
        } else if (unlink(path) != 0) {
            closedir(d);
            return -1;
        }
        n++;
    }
    closedir(d);
    ESP_LOGI(TAG, "wiped %d entries", n);
    return 0;
}

/* Create parent dirs for a rel path (a/b/c.txt makes /data/a and
 * /data/a/b). Existing entries are fine, clashing files fail. */
static int mkdir_parents(const char *rel)
{
    const char *s = rel;
    for (;;) {
        const char *e = strchr(s, '/');
        char prefix[160];
        size_t n;
        struct stat st;
        if (!e) {
            return 0;
        }
        n = (size_t)(e - rel);
        if (n == 0 || n >= sizeof(prefix)) {
            return -1;
        }
        memcpy(prefix, rel, n);
        prefix[n] = 0;
        {
            char full[224];
            snprintf(full, sizeof(full), "/data/%s", prefix);
            memset(&st, 0, sizeof(st));
            if (stat(full, &st) == 0) {
                if (!S_ISDIR(st.st_mode)) {
                    return -1;
                }
            } else if (mkdir(full, 0755) != 0) {
                return -1;
            }
        }
        s = e + 1;
    }
}

int nnode_backup_restore_stream(cbor_read_fn read, void *ctx, int total_len,
    char *err, size_t errlen)
{
    cbor_r_t r;
    int major, ntop = 0, i;
    uint64_t v, n;
    int rc = -1;

    if (!read || !err || !errlen || total_len <= 0) {
        return -1;
    }
    if (cbor_r_init(&r, read, ctx, total_len)) {
        snprintf(err, errlen, "no_mem");
        return -1;
    }

#define BK_FAIL(s) do { snprintf(err, errlen, "%s", s); goto out; } while (0)

    /* Top-level map; the version pair must come first so garbage is
     * rejected before anything is wiped. */
    if (cbor_r_head(&r, &major, &n) || major != 5 || n < 1 || n > 16) {
        BK_FAIL("parse");
    }
    if (cbor_r_uint(&r, &v) || v != 0) {
        BK_FAIL("version");
    }
    if (cbor_r_uint(&r, &v) || v != BACKUP_VERSION) {
        BK_FAIL("version");
    }
    ntop = (int)n - 1;
    for (i = 0; i < ntop; i++) {
        uint64_t key;
        if (cbor_r_uint(&r, &key)) {
            BK_FAIL("parse");
        }
        switch (key) {
        case 1: {
            /* Known packages are staged in RAM and committed only
             * after every one of them validates. Unknown packages
             * have no rules beyond syntax and go straight to disk. */
            uint64_t np, k;
            char *staged[4] = { NULL, NULL, NULL, NULL };
            const char *known[4] = {
                NNOODE_CFG_PKG_DEVICE, NNOODE_CFG_PKG_TIME,
                NNOODE_CFG_PKG_STA, NNOODE_CFG_PKG_AP,
            };
            int ki, bad = 0;
            if (cbor_r_head(&r, &major, &np) || major != 5 || np > 16) {
                BK_FAIL("config");
            }
            for (k = 0; k < np; k++) {
                char pkg[NNOODE_CFG_PKG_MAX];
                char *text = malloc(CFG_FILE_MAX + 1);
                if (!text) {
                    bad = 1;
                    break;
                }
                if (cbor_r_tstr(&r, pkg, sizeof(pkg)) ||
                    cbor_r_tstr(&r, text, CFG_FILE_MAX + 1)) {
                    free(text);
                    bad = 1;
                    break;
                }
                if (config_valid(pkg, text) != 0) {
                    ESP_LOGW(TAG, "bad package %s", pkg);
                    free(text);
                    bad = 1;
                    break;
                }
                for (ki = 0; ki < 4; ki++) {
                    if (strcmp(pkg, known[ki]) == 0) {
                        break;
                    }
                }
                if (ki < 4) {
                    free(staged[ki]);
                    staged[ki] = text;
                } else {
                    if (nnode_cfg_write(pkg, text) != 0) {
                        free(text);
                        bad = 1;
                        break;
                    }
                    free(text);
                }
            }
            if (bad) {
                for (ki = 0; ki < 4; ki++) {
                    free(staged[ki]);
                }
                BK_FAIL("config");
            }
            for (ki = 0; ki < 4; ki++) {
                if (staged[ki]) {
                    if (nnode_cfg_write(known[ki], staged[ki]) != 0) {
                        for (ki = 0; ki < 4; ki++) {
                            free(staged[ki]);
                        }
                        BK_FAIL("config");
                    }
                    free(staged[ki]);
                    staged[ki] = NULL;
                }
            }
            break;
        }
        case 2: {
            uint64_t nf2, k;
            if (wipe_data_files()) {
                BK_FAIL("wipe");
            }
            if (cbor_r_head(&r, &major, &nf2) || major != 4) {
                BK_FAIL("files");
            }
            for (k = 0; k < nf2; k++) {
                uint64_t ne, j, declared;
                char name[144], path[224];
                int got_name = 0, got_size = 0, got_data = 0;
                struct stat st;
                size_t existing = 0;
                uint64_t fst = 0, fsu = 0, free_b;
                FILE *f;
                size_t got = 0;
                unsigned long chunks = 0;
                if (cbor_r_head(&r, &major, &ne) || major != 5) {
                    BK_FAIL("files");
                }
                for (j = 0; j < ne; j++) {
                    uint64_t fk;
                    if (cbor_r_uint(&r, &fk)) {
                        BK_FAIL("files");
                    }
                    if (fk == 0) {
                        /* Package texts own the config dir; plain file
                         * entries must not touch it. */
                        if (cbor_r_tstr(&r, name, sizeof(name)) ||
                            !nnode_path_valid(name) ||
                            (strncmp(name, "config", 6) == 0 &&
                            (name[6] == 0 || name[6] == '/'))) {
                            BK_FAIL("files");
                        }
                        got_name = 1;
                    } else if (fk == 1) {
                        if (cbor_r_uint(&r, &declared) ||
                            declared > 0xFFFFFFUL) {
                            BK_FAIL("files");
                        }
                        got_size = 1;
                    } else if (fk == 2) {
                        int fmajor;
                        uint64_t flen;
                        if (!got_name || !got_size) {
                            BK_FAIL("files");
                        }
                        if (cbor_r_head(&r, &fmajor, &flen) ||
                            fmajor != 2 || flen != declared) {
                            ESP_LOGW(TAG, "bk file %s: head m=%d len=%llu"
                                " declared=%llu", name, fmajor,
                                (unsigned long long)flen,
                                (unsigned long long)declared);
                            BK_FAIL("files");
                        }
                        snprintf(path, sizeof(path), "/data/%s", name);
                        memset(&st, 0, sizeof(st));
                        if (stat(path, &st) == 0 && st.st_size > 0) {
                            existing = (size_t)st.st_size;
                        }
                        nnode_fs_usage(&fst, &fsu);
                        free_b = fst > fsu ? fst - fsu : 0;
                        ESP_LOGI(TAG, "bk file %s declared %llu free %llu",
                            name, (unsigned long long)declared,
                            (unsigned long long)free_b);
                        if (declared > free_b + existing) {
                            BK_FAIL("space");
                        }
                        if (mkdir_parents(name) != 0) {
                            BK_FAIL("files");
                        }
                        f = fopen(path, "wb");
                        if (!f) {
                            ESP_LOGW(TAG, "bk file %s: open failed", name);
                            BK_FAIL("files");
                        }
                        while (got < (size_t)declared) {
                            char chunk[1536];
                            size_t want = (size_t)declared - got;
                            size_t cp = want < sizeof(chunk) ?
                                want : sizeof(chunk);
                            if (cbor_r_take(&r, chunk, cp)) {
                                fclose(f);
                                unlink(path);
                                BK_FAIL("files");
                            }
                            if (fwrite(chunk, 1, cp, f) != cp) {
                                fclose(f);
                                unlink(path);
                                BK_FAIL("files");
                            }
                            got += cp;
                            if (++chunks % 32 == 0) {
                                esp_task_wdt_reset();
                            }
                        }
                        if (fflush(f) != 0 || ferror(f)) {
                            ESP_LOGW(TAG, "bk file %s: flush failed", name);
                            fclose(f);
                            unlink(path);
                            BK_FAIL("files");
                        }
                        if (fclose(f) != 0) {
                            ESP_LOGW(TAG, "bk file %s: close failed", name);
                            unlink(path);
                            BK_FAIL("files");
                        }
                        ESP_LOGI(TAG, "bk file %s: wrote %u of %llu", name,
                            (unsigned)got, (unsigned long long)declared);
                        got_data = 1;
                    } else if (cbor_r_skip(&r)) {
                        BK_FAIL("files");
                    }
                }
                if (!got_name || !got_size || !got_data) {
                    BK_FAIL("files");
                }
            }
            break;
        }
        default:
            if (cbor_r_skip(&r)) {
                BK_FAIL("parse");
            }
            break;
        }
    }
    ESP_LOGI(TAG, "restore ok");
    snprintf(err, errlen, "ok");
    rc = 0;
out:
    cbor_r_free(&r);
    return rc;
}
