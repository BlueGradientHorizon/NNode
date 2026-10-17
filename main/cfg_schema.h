#pragma once
#include <stddef.h>
#include <stdint.h>

/* NNode config schemas: text defaults with typed fields.
 *
 * Modules ship examples/<app>/config.defaults next to their sources
 * (see tools/nnode_cfg_validate.py for the grammar); at build time
 * examples/build.py injects it verbatim as the .nnode_default ELF
 * section. Firmware-owned packages keep the same text compiled in
 * (main/fw_defaults.c). On first sight the firmware copies the value
 * lines to /data/configs/<pkg>; on upgrade it adds new keys with
 * defaults and drops orphan keys, keeping user values otherwise.
 * Validation lives in cfg_types.c, this unit only parses lines and
 * merges maps.
 *
 * Stored values file (no meta lines, key order follows the schema):
 *   package <pkg>
 *   version <N>
 *   <key> <value>
 * Values with spaces are single-quoted with cfg.c escaping.
 */

#define NNODE_SCHEMA_TABS 8
#define NNODE_SCHEMA_FIELDS 32
#define NNODE_SCHEMA_ENUMVALS 8
#define NNODE_SCHEMA_ID_MAX 32
#define NNODE_SCHEMA_DEFAULT_MAX 4096

typedef struct {
    char id[NNODE_SCHEMA_ID_MAX + 1];
    char title[65];
} nnode_schema_tab_t;

typedef struct {
    char key[NNODE_SCHEMA_ID_MAX + 1];
    char tab[NNODE_SCHEMA_ID_MAX + 1];
    char type[12];
    char label[65];
    char dflt[128];
    long min, max;
    int has_min, has_max;
    long maxlen; /* string + list(string) */
    char vals[NNODE_SCHEMA_ENUMVALS][NNODE_SCHEMA_ID_MAX + 1];
    int nvals; /* enum */
    char sub[12]; /* list element type */
    int maxitems; /* list */
    char show_if[128];
} nnode_schema_field_t;

typedef struct {
    char pkg[24];
    int version;
    nnode_schema_tab_t tabs[NNODE_SCHEMA_TABS];
    int ntabs;
    nnode_schema_field_t fields[NNODE_SCHEMA_FIELDS];
    int nfields;
} nnode_schema_t;

/* Parse a defaults blob (meta + values). 0 ok, -1 invalid.
 * allow_reserved is nonzero for firmware-owned packages only; modules
 * must not claim reserved names. */
int nnode_schema_parse(const char *text, nnode_schema_t *s,
    int allow_reserved);
/* Extract the .nnode_default section from ELF bytes or a program file.
 * 0 ok, -1 missing/invalid/too big. */
int nnode_schema_section_from_mem(const uint8_t *elf, size_t elen,
    char *out, size_t cap, size_t *outlen);
int nnode_schema_section_from_file(const char *elf_name,
    char *out, size_t cap, size_t *outlen);
/* Ensure the values file exists and matches the schema (merge on
 * upgrade: new keys added, orphans dropped, bad values reset).
 * Returns 1 when the program has valid settings, else 0. When pkg_out
 * is not NULL the package name is stored there on success. */
int nnode_schema_ensure(const char *elf_name, char *pkg_out, size_t pkg_cap);
/* Read-only schema probe for listings: 1 when the program carries a
 * valid schema, else 0. Never creates files. */
int nnode_schema_has(const char *elf_name);
/* Raw value read from a stored file. 0 ok, -1 error/not found. */
int nnode_schema_lookup(const char *pkg, const char *key,
    char *out, size_t len);
/* One field value against its schema entry. 0 ok, -1 bad. */
int nnode_schema_value_ok(const nnode_schema_field_t *f, const char *v);
/* Full settings document for the panel, bytes written (0 = no schema). */
int nnode_config_json(const char *elf_name, char *buf, size_t len);
/* Save a flat JSON body {"k":"v"}. 0 ok, -1 bad args/schema,
 * -2 bad value, -3 write failed. */
int nnode_config_save(const char *elf_name, const char *body);
/* Syntax-only check of a stored values text (backup restore path,
 * no schema at hand). 0 ok, -1 bad. */
int nnode_schema_syntax_ok(const char *pkg, const char *text);
/* Split a values text into key/value pairs. 0 ok, -1 bad. */
int nnode_schema_kv(const char *pkg, const char *text,
    char keys[][NNODE_SCHEMA_ID_MAX + 1], char vals[][128], int *n);
/* Firmware-owned package backed by compiled-in defaults text:
 * ensure creates/merges the file (1 ok, 0 bad schema), valid checks a
 * restore candidate for the exact key set and typed values (0 ok),
 * store writes a complete caller map after the same checks (0 ok). */
int nnode_schema_ensure_text(const char *defaults);
int nnode_schema_validate(const char *defaults, const char *text);
int nnode_schema_store(const char *defaults,
    char keys[][NNODE_SCHEMA_ID_MAX + 1], char vals[][128], int n);
