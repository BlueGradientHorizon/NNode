#pragma once
#include <stddef.h>
#include <stdint.h>
/* Home for loadable ELF modules. Created at boot when missing. */
#define NNOODE_PROGRAMS_DIR "programs"
/* Create the programs dir; move stray root ELF files into it. */
void nnode_programs_init(void);
/* List dir ("" = root) as JSON. Directories carry "dir":1. */
int nnode_files_json(char *buf, size_t len, const char *dir);
int nnode_file_delete(const char *name);
/* Create a subdirectory (parents must exist). Returns 0, else -1. */
int nnode_dir_create(const char *path);
int nnode_elf_run(const char *name, int *id_out, char *errbuf, size_t errlen);
/* Stop (SIGTERM: term hook + grace for tasks to exit) or kill (SIGKILL:
 * immediate, no hook) one instance. id >= 0 selects by instance, else
 * name selects the first match. Returns 0 ok, -1 bad args, -2 not found. */
int nnode_elf_stop(int id, const char *name);
int nnode_elf_kill(int id, const char *name);
/* JSON list of running instances for the UI:
 * {"instances":[{"id":1,"name":"blink.elf","state":"running","tasks":1}]}. */
int nnode_elf_instances_json(char *buf, size_t len);
/* Returns 0 ok, -1 bad name, -2 not loaded, -3 slot has live tasks. */
int nnode_elf_unload(const char *name);
int nnode_elf_loaded(const char *name);
int nnode_elf_any_loaded(void);
/* Lifecycle contract (see sdk/nnode_sdk.h): modules register their
 * nnode_main/nnode_term via the entry shim. Resolver hook below. */
void nnode_register(void *main_fn, void *term_fn);
int nnode_stop_requested(void);
uintptr_t nnode_lifecycle_resolve(const char *name);
/* Single segment (no slashes), used for module names and backup. */
int nnode_file_valid(const char *name);
/* Relative multi-segment path inside /data (no leading slash,
 * no dot segments). */
int nnode_path_valid(const char *path);
