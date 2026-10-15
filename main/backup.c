/* NNode backup in CBOR. Schema (int keys, definite lengths only):
 *
 * map {
 *   0: uint version (=1),
 *   1: tstr hostname,
 *   2: map { 0: bool ntp_on, 1: [tstr] servers },
 *   3: tstr last-ok-ssid,
 *   4: [ map {0: tstr ssid, 1: tstr pass} ],      // saved STA nets
 *   5: map {0: tstr ssid, 1: tstr auth, 2: tstr pass,
 *            3: tstr mode, 4: bool nat}           // empty map = no AP saved
 *   6: [ map {0: tstr name, 1: uint size, 2: bstr data} ]  // /data files
 *   7: tstr tz (POSIX),
 *   8: tstr zonename (IANA, may be empty)
 * }
 *
 * Files ride as raw bytes (no base64 tax). Both directions stream with
 * constant RAM: emit reads files in 2 KB chunks, restore writes them
 * straight to /data the same way.
 *
 * Restore = full replacement: when the files section arrives, /data is
 * wiped first; NVS (hostname/NTP/nets/AP) is committed only after every
 * byte lands, then the caller reboots. A failed restore never reboots and
 * never touches NVS. Unknown map keys are skipped (forward compatible).
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "cbor.h"
#include "files.h"
#include "fs.h"
#include "nets.h"
#include "syscfg.h"
#include "backup.h"

static const char *TAG = "nnode_backup";
#define BACKUP_VERSION 1
#define CHUNK_FILE 2048

#define WCHK(x) do { if (x) { return -1; } } while (0)

int nnode_backup_emit(cbor_emit_fn emit, void *ctx)
{
    cbor_w_t w;
    char host[40], last[40];
    char tz[NNODE_TZ_MAX + 1], zn[NNODE_TZ_MAX + 1];
    char srvs[NNODE_NTP_MAX][NNODE_NTP_SRV_LEN];
    nnode_net_cred_t nets[NNODE_MAX_NETS];
    nnode_ap_cfg_t ap;
    int en = 0, nn = 0, i, nsrvs = 0, nf = 0, has_ap;
    DIR *d;
    struct dirent *e;

    nnode_hostname_get(host, sizeof(host));
    nnode_ntp_get(&en, srvs, NNODE_NTP_MAX);
    nnode_tz_get(tz, sizeof(tz));
    nnode_zonename_get(zn, sizeof(zn));
    last[0] = 0;
    nnode_last_get(last, sizeof(last));
    nn = nnode_nets_load(nets, NNODE_MAX_NETS);
    has_ap = nnode_ap_load(&ap) == 0;
    for (i = 0; i < NNODE_NTP_MAX; i++) {
        if (srvs[i][0]) {
            nsrvs++;
        }
    }

    /* File count first: arrays are definite-length, so no second
     * chances. A file vanishing mid-emit aborts the download. */
    d = opendir("/data");
    if (d) {
        while ((e = readdir(d)) != NULL) {
            struct stat st;
            char path[80];
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
                continue;
            }
            snprintf(path, sizeof(path), "/data/%.64s", e->d_name);
            memset(&st, 0, sizeof(st));
            if (stat(path, &st) == 0 && st.st_size >= 0) {
                nf++;
            }
        }
        closedir(d);
    }

    cbor_w_init(&w, emit, ctx);
    WCHK(cbor_w_map(&w, 9));
    WCHK(cbor_w_uint(&w, 0));
    WCHK(cbor_w_uint(&w, BACKUP_VERSION));
    WCHK(cbor_w_uint(&w, 1));
    WCHK(cbor_w_tstr(&w, host));
    WCHK(cbor_w_uint(&w, 2));
    WCHK(cbor_w_map(&w, 2));
    WCHK(cbor_w_uint(&w, 0));
    WCHK(cbor_w_bool(&w, en));
    WCHK(cbor_w_uint(&w, 1));
    WCHK(cbor_w_array(&w, (uint64_t)nsrvs));
    for (i = 0; i < NNODE_NTP_MAX; i++) {
        if (srvs[i][0]) {
            WCHK(cbor_w_tstr(&w, srvs[i]));
        }
    }
    WCHK(cbor_w_uint(&w, 3));
    WCHK(cbor_w_tstr(&w, last));
    WCHK(cbor_w_uint(&w, 4));
    WCHK(cbor_w_array(&w, (uint64_t)nn));
    for (i = 0; i < nn; i++) {
        WCHK(cbor_w_map(&w, 2));
        WCHK(cbor_w_uint(&w, 0));
        WCHK(cbor_w_tstr(&w, nets[i].ssid));
        WCHK(cbor_w_uint(&w, 1));
        WCHK(cbor_w_tstr(&w, nets[i].pass));
    }
    WCHK(cbor_w_uint(&w, 5));
    if (has_ap) {
        WCHK(cbor_w_map(&w, 5));
        WCHK(cbor_w_uint(&w, 0));
        WCHK(cbor_w_tstr(&w, ap.ssid));
        WCHK(cbor_w_uint(&w, 1));
        WCHK(cbor_w_tstr(&w, ap.auth));
        WCHK(cbor_w_uint(&w, 2));
        WCHK(cbor_w_tstr(&w, ap.pass));
        WCHK(cbor_w_uint(&w, 3));
        WCHK(cbor_w_tstr(&w, ap.mode));
        WCHK(cbor_w_uint(&w, 4));
        WCHK(cbor_w_bool(&w, ap.nat));
    } else {
        WCHK(cbor_w_map(&w, 0));
    }
    WCHK(cbor_w_uint(&w, 6));
    WCHK(cbor_w_array(&w, (uint64_t)nf));
    d = opendir("/data");
    if (d) {
        while ((e = readdir(d)) != NULL) {
            struct stat st;
            char path[80];
            FILE *f;
            size_t left;
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
                continue;
            }
            snprintf(path, sizeof(path), "/data/%.64s", e->d_name);
            memset(&st, 0, sizeof(st));
            if (stat(path, &st) != 0 || st.st_size < 0) {
                continue;
            }
            WCHK(cbor_w_map(&w, 3));
            WCHK(cbor_w_uint(&w, 0));
            WCHK(cbor_w_tstr(&w, e->d_name));
            WCHK(cbor_w_uint(&w, 1));
            WCHK(cbor_w_uint(&w, (uint64_t)st.st_size));
            WCHK(cbor_w_uint(&w, 2));
            WCHK(cbor_w_bstr_head(&w, (uint64_t)st.st_size));
            unsigned char *raw = malloc(CHUNK_FILE);
            f = fopen(path, "rb");
            if (!f || !raw) {
                free(raw);
                if (f) {
                    fclose(f);
                }
                closedir(d);
                return -1;
            }
            left = (size_t)st.st_size;
            while (left > 0) {
                size_t want = left > CHUNK_FILE ? CHUNK_FILE : left;
                size_t got = fread(raw, 1, want, f);
                if (!got) {
                    break;
                }
                if (cbor_w_raw(&w, raw, got)) {
                    break;
                }
                left -= got;
            }
            free(raw);
            fclose(f);
            if (left != 0) {
                closedir(d);
                return -1;
            }
        }
        closedir(d);
    }
    WCHK(cbor_w_uint(&w, 7));
    WCHK(cbor_w_tstr(&w, tz));
    WCHK(cbor_w_uint(&w, 8));
    WCHK(cbor_w_tstr(&w, zn));
    WCHK(cbor_w_flush(&w));
    return w.failed ? -1 : 0;
}

static int host_ok(const char *s, size_t maxlen)
{
    size_t n = strlen(s);
    if (n == 0 || n > maxlen) {
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

static int wipe_data(void)
{
    DIR *d = opendir("/data");
    struct dirent *e;
    int n = 0;
    if (!d) {
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char path[80];
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        snprintf(path, sizeof(path), "/data/%.64s", e->d_name);
        if (unlink(path) != 0) {
            closedir(d);
            return -1;
        }
        n++;
    }
    closedir(d);
    ESP_LOGI(TAG, "wiped %d files", n);
    return 0;
}

int nnode_backup_restore_stream(cbor_read_fn read, void *ctx, int total_len,
    char *err, size_t errlen)
{
    cbor_r_t r;
    int major, ntop = 0, i;
    uint64_t v, n;
    int rc = -1;
    /* Staged head: committed only after everything lands. */
    int has_host = 0, has_ntp = 0, has_last = 0, has_nets = 0, has_ap = 0;
    int has_tz = 0, has_tzname = 0;
    char tz[NNODE_TZ_MAX + 1], zn[NNODE_TZ_MAX + 1];
    char host[40];
    int ntp_en = 0;
    char ntp_srv[NNODE_NTP_MAX][NNODE_NTP_SRV_LEN];
    int ntp_n = 0;
    char last[40];
    nnode_net_cred_t nets[NNODE_MAX_NETS];
    int nnets = 0;
    int ap_empty = 1;
    nnode_ap_cfg_t ap;

    if (!read || !err || !errlen || total_len <= 0) {
        return -1;
    }
    if (cbor_r_init(&r, read, ctx, total_len)) {
        snprintf(err, errlen, "no_mem");
        return -1;
    }
    memset(nets, 0, sizeof(nets));
    memset(&ap, 0, sizeof(ap));
    memset(ntp_srv, 0, sizeof(ntp_srv));

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
        case 1:
            if (cbor_r_tstr(&r, host, sizeof(host)) ||
                !host_ok(host, 32)) {
                BK_FAIL("hostname");
            }
            has_host = 1;
            break;
        case 2: {
            uint64_t sub, j;
            if (cbor_r_head(&r, &major, &sub) || major != 5 || sub > 8) {
                BK_FAIL("ntp");
            }
            for (j = 0; j < sub; j++) {
                uint64_t sk;
                if (cbor_r_uint(&r, &sk)) {
                    BK_FAIL("ntp");
                }
                if (sk == 0) {
                    if (cbor_r_bool(&r, &ntp_en)) {
                        BK_FAIL("ntp");
                    }
                } else if (sk == 1) {
                    uint64_t ns, k;
                    if (cbor_r_head(&r, &major, &ns) || major != 4) {
                        BK_FAIL("ntp");
                    }
                    ntp_n = 0;
                    for (k = 0; k < ns; k++) {
                        if (ntp_n >= NNODE_NTP_MAX) {
                            BK_FAIL("ntp");
                        }
                        if (cbor_r_tstr(&r, ntp_srv[ntp_n],
                                sizeof(ntp_srv[0])) ||
                            !host_ok(ntp_srv[ntp_n],
                                NNODE_NTP_SRV_LEN - 1)) {
                            BK_FAIL("ntp");
                        }
                        ntp_n++;
                    }
                } else if (cbor_r_skip(&r)) {
                    BK_FAIL("ntp");
                }
            }
            has_ntp = 1;
            break;
        }
        case 3:
            if (cbor_r_tstr(&r, last, sizeof(last))) {
                BK_FAIL("parse");
            }
            has_last = 1;
            break;
        case 4: {
            uint64_t nn2, k;
            if (cbor_r_head(&r, &major, &nn2) || major != 4 ||
                nn2 > NNODE_MAX_NETS) {
                BK_FAIL("nets");
            }
            nnets = 0;
            for (k = 0; k < nn2; k++) {
                uint64_t ne, j;
                char ssid[sizeof(nets[0].ssid)];
                char pass[sizeof(nets[0].pass)];
                int got_ssid = 0, got_pass = 0;
                if (nnets >= NNODE_MAX_NETS) {
                    BK_FAIL("nets");
                }
                if (cbor_r_head(&r, &major, &ne) || major != 5) {
                    BK_FAIL("nets");
                }
                for (j = 0; j < ne; j++) {
                    uint64_t nk;
                    if (cbor_r_uint(&r, &nk)) {
                        BK_FAIL("nets");
                    }
                    if (nk == 0) {
                        if (cbor_r_tstr(&r, ssid, sizeof(ssid)) ||
                            !ssid[0] || strlen(ssid) > 32) {
                            BK_FAIL("nets");
                        }
                        got_ssid = 1;
                    } else if (nk == 1) {
                        if (cbor_r_tstr(&r, pass, sizeof(pass)) ||
                            strlen(pass) > 63) {
                            BK_FAIL("nets");
                        }
                        got_pass = 1;
                    } else if (cbor_r_skip(&r)) {
                        BK_FAIL("nets");
                    }
                }
                if (!got_ssid || !got_pass) {
                    BK_FAIL("nets");
                }
                snprintf(nets[nnets].ssid, sizeof(nets[nnets].ssid),
                    "%s", ssid);
                snprintf(nets[nnets].pass, sizeof(nets[nnets].pass),
                    "%s", pass);
                nnets++;
            }
            has_nets = 1;
            break;
        }
        case 5: {
            uint64_t na, j;
            int got[5] = { 0, 0, 0, 0, 0 };
            memset(&ap, 0, sizeof(ap));
            if (cbor_r_head(&r, &major, &na) || major != 5 || na > 8) {
                BK_FAIL("ap");
            }
            ap_empty = (na == 0);
            for (j = 0; j < na; j++) {
                uint64_t ak;
                if (cbor_r_uint(&r, &ak)) {
                    BK_FAIL("ap");
                }
                switch (ak) {
                case 0:
                    if (cbor_r_tstr(&r, ap.ssid, sizeof(ap.ssid)) ||
                        !ap.ssid[0] || strlen(ap.ssid) > 32) {
                        BK_FAIL("ap");
                    }
                    got[0] = 1;
                    break;
                case 1:
                    if (cbor_r_tstr(&r, ap.auth, sizeof(ap.auth)) ||
                        (strcmp(ap.auth, "open") &&
                         strcmp(ap.auth, "wpa2") &&
                         strcmp(ap.auth, "wpa_wpa2"))) {
                        BK_FAIL("ap");
                    }
                    got[1] = 1;
                    break;
                case 2:
                    if (cbor_r_tstr(&r, ap.pass, sizeof(ap.pass))) {
                        BK_FAIL("ap");
                    }
                    got[2] = 1;
                    break;
                case 3:
                    if (cbor_r_tstr(&r, ap.mode, sizeof(ap.mode)) ||
                        (strcmp(ap.mode, "auto") &&
                         strcmp(ap.mode, "always"))) {
                        BK_FAIL("ap");
                    }
                    got[3] = 1;
                    break;
                case 4:
                    if (cbor_r_bool(&r, &ap.nat)) {
                        BK_FAIL("ap");
                    }
                    got[4] = 1;
                    break;
                default:
                    if (cbor_r_skip(&r)) {
                        BK_FAIL("ap");
                    }
                    break;
                }
            }
            if (!ap_empty) {
                int k;
                for (k = 0; k < 5; k++) {
                    if (!got[k]) {
                        BK_FAIL("ap");
                    }
                }
                if (!strcmp(ap.auth, "open") ? ap.pass[0] != 0 :
                    (strlen(ap.pass) < 8 || strlen(ap.pass) > 63)) {
                    BK_FAIL("ap");
                }
            }
            has_ap = 1;
            break;
        }
        case 6: {
            uint64_t nf2, k;
            if (wipe_data()) {
                BK_FAIL("wipe");
            }
            if (cbor_r_head(&r, &major, &nf2) || major != 4) {
                BK_FAIL("files");
            }
            for (k = 0; k < nf2; k++) {
                uint64_t ne, j, declared;
                char name[80], path[80];
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
                        if (cbor_r_tstr(&r, name, sizeof(name)) ||
                            !nnode_file_valid(name)) {
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
                        snprintf(path, sizeof(path), "/data/%.64s", name);
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
        case 7:
            if (cbor_r_tstr(&r, tz, sizeof(tz))) {
                BK_FAIL("tz");
            }
            has_tz = 1;
            break;
        case 8:
            if (cbor_r_tstr(&r, zn, sizeof(zn))) {
                BK_FAIL("tz");
            }
            has_tzname = 1;
            break;
        }
    }
    if (has_host && nnode_hostname_save(host)) {
        BK_FAIL("hostname");
    }
    if (has_ntp) {
        char blob[NNODE_NTP_MAX * (NNODE_NTP_SRV_LEN + 1)];
        blob[0] = 0;
        for (i = 0; i < ntp_n; i++) {
            strcat(blob, ntp_srv[i]);
            if (i + 1 < ntp_n) {
                strcat(blob, " ");
            }
        }
        if (nnode_ntp_save(ntp_en, blob)) {
            BK_FAIL("ntp");
        }
    }
    if (has_last && nnode_last_set(last)) {
        BK_FAIL("last");
    }
    if (has_tz && nnode_tz_save(tz)) {
        BK_FAIL("tz");
    }
    if (has_tzname && nnode_zonename_save(zn)) {
        BK_FAIL("tz");
    }
    if (has_nets && nnode_nets_save(nets, nnets)) {
        BK_FAIL("nets");
    }
    if (has_ap) {
        if (ap_empty ? nnode_ap_clear() : nnode_ap_save(&ap)) {
            BK_FAIL("ap");
        }
    }
    ESP_LOGI(TAG, "restore ok");
    snprintf(err, errlen, "ok");
    rc = 0;
out:
    cbor_r_free(&r);
    return rc;
}
