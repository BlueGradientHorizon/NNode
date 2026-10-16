#pragma once
#include <stddef.h>

/* NNode config store: text files under /data/config, one file per package.
 * Syntax per file:
 *   package <name>
 *   config <type> ['<name>']
 *           option <key> '<value>'
 *           list <key> '<value>'
 * Lines starting with '#' and blank lines are ignored. Values are
 * single-quoted; inside quotes backslash, quote, newline, CR and tab
 * are escaped as \\ \' \n \r \t. All other bytes (incl. UTF-8) pass
 * through. Bare unquoted tokens without spaces are accepted on read.
 * Files are written atomically (tmp + rename). */

#define NNOODE_CFG_DIR "/data/config"
#define NNOODE_CFG_PKG_MAX 24
#define NNOODE_CFG_KEY_MAX 32
#define NNOODE_CFG_TYPE_MAX 32
#define NNOODE_CFG_NAME_MAX 64
#define NNOODE_CFG_VAL_MAX 128
#define NNOODE_CFG_LINE_MAX 256

/* Known packages. Anything else found in the dir is treated as a
 * third-party package and is backed up the same way. */
#define NNOODE_CFG_PKG_DEVICE "device"
#define NNOODE_CFG_PKG_TIME "time"
#define NNOODE_CFG_PKG_STA "sta"
#define NNOODE_CFG_PKG_AP "ap"

/* Create the config dir if missing. Returns 0 on success. */
int nnode_cfg_init(void);
/* Package name charset: [a-z0-9_], 1..24 chars. */
int nnode_cfg_valid_pkg(const char *pkg);
int nnode_cfg_exists(const char *pkg);
int nnode_cfg_remove(const char *pkg);
/* Read whole package file into buf (NUL-terminated). */
int nnode_cfg_read(const char *pkg, char *buf, size_t cap, size_t *out_len);
/* Atomically replace package file with NUL-terminated text. */
int nnode_cfg_write(const char *pkg, const char *text);
/* List package files (tmp files skipped). Returns count. */
int nnode_cfg_list(char names[][NNOODE_CFG_PKG_MAX], int max);

/* Escape raw value into quoted form (without the quotes). */
int nnode_cfg_escape(const char *in, char *out, size_t cap);
/* Decode a possibly-quoted value token into raw form. */
int nnode_cfg_unescape(const char *in, char *out, size_t cap);

typedef struct {
    /* Return nonzero to abort the parse with that code. */
    int (*section)(void *ctx, const char *type, const char *name, int has_name);
    int (*option)(void *ctx, const char *key, const char *value);
    int (*list)(void *ctx, const char *key, const char *value);
} nnode_cfg_cb_t;

/* Parse package file from disk. First directive must be
 * "package <same name>". Returns 0 on success. */
int nnode_cfg_parse(const char *pkg, const nnode_cfg_cb_t *cb, void *ctx);
/* Same, but over an in-memory NUL-terminated text (for tests).
 * A NULL cb means syntax check only. */
int nnode_cfg_parse_text(const char *pkg, const char *text,
    const nnode_cfg_cb_t *cb, void *ctx);
