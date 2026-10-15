#pragma once
#include <stddef.h>
int nnode_files_json(char *buf, size_t len);
int nnode_file_delete(const char *name);
int nnode_elf_run(const char *name, char *errbuf, size_t errlen);
/* Returns 0 ok, -1 bad name, -2 not loaded, -3 slot has live tasks. */
int nnode_elf_unload(const char *name);
/* Kill the slot's tasks, then unload. Returns 0 ok, -1 bad name, -2 not loaded. */
int nnode_elf_stop(const char *name);
int nnode_elf_loaded(const char *name);
int nnode_elf_any_loaded(void);
int nnode_file_valid(const char *name);
