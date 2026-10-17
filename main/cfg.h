#pragma once
#include <stddef.h>

/* NNode config store: text files under /data/configs, one file per package.
 * Single flat syntax for firmware and module packages alike:
 *   package <name>
 *   version <N>
 *   <key> <value>
 * Lines starting with '#' and blank lines are ignored. Values holding
 * spaces are single-quoted; inside quotes backslash, quote, newline, CR
 * and tab are escaped as \\ \' \n \r \t. All other bytes (incl. UTF-8)
 * pass through. Bare unquoted tokens without spaces are accepted on read.
 * Field types and defaults live in the schema (compiled-in defaults for
 * firmware packages, .nnode_default section for modules); this unit only
 * stores bytes. Files are written atomically (tmp + rename). */

#define NNOODE_CFG_DIR "/data/configs"
#define NNOODE_CFG_PKG_MAX 24
#define NNOODE_CFG_KEY_MAX 32
#define NNOODE_CFG_VAL_MAX 128
#define NNOODE_CFG_LINE_MAX 256

/* Firmware-owned packages. Modules must not claim them. Anything else
 * found in the dir is a module package and is backed up the same way. */
#define NNOODE_CFG_PKG_DEVICE "device"
#define NNOODE_CFG_PKG_TIME "time"
#define NNOODE_CFG_PKG_STA "sta"
#define NNOODE_CFG_PKG_NETS "nets"
#define NNOODE_CFG_PKG_AP "ap"
#define NNOODE_CFG_PKG_AUTOSTART "autostart"

/* Create the config dir if missing. Returns 0 on success. */
int nnode_cfg_init(void);
/* Package name charset: [a-z0-9_], 1..24 chars. */
int nnode_cfg_valid_pkg(const char *pkg);
/* 1 when the name belongs to the firmware itself (not for modules). */
int nnode_cfg_reserved(const char *pkg);
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
