#pragma once
#include <stddef.h>
/* Home for loadable ELF modules. Created at boot when missing. */
#define NNOODE_PROGRAMS_DIR "programs"
/* Create the programs dir; move stray root ELF files into it. */
void nnode_programs_init(void);
/* List dir ("" = root) as JSON. Directories carry "dir":1. */
int nnode_files_json(char *buf, size_t len, const char *dir);
int nnode_file_delete(const char *name);
/* Create a subdirectory (parents must exist). Returns 0, else -1. */
int nnode_dir_create(const char *path);
int nnode_elf_run(const char *name, char *errbuf, size_t errlen);
/* Returns 0 ok, -1 bad name, -2 not loaded, -3 slot has live tasks. */
int nnode_elf_unload(const char *name);
/* Kill the slot's tasks, then unload. Returns 0 ok, -1 bad name, -2 not loaded. */
int nnode_elf_stop(const char *name);
int nnode_elf_loaded(const char *name);
int nnode_elf_any_loaded(void);
/* Single segment (no slashes), used for module names and backup. */
int nnode_file_valid(const char *name);
/* Relative multi-segment path inside /data (no leading slash,
 * no dot segments). */
int nnode_path_valid(const char *path);
