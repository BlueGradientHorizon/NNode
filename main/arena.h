#pragma once
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Accounting arenas, one per loaded ELF slot (see files.c).
 *
 * Code (.text) already lands in IRAM via MALLOC_CAP_EXEC inside the loader,
 * data (.data/.bss) in DRAM; this module tracks everything the module takes
 * afterwards through malloc/calloc/realloc (DRAM heap) plus every task it
 * spawns, so unload can release the leftovers. Arenas are dynamic: no memory
 * is reserved at boot, a slot's state appears on run and vanishes on unload.
 *
 * No module source changes needed: the ELF symbol resolver redirects the
 * module's malloc/free/task symbols to the nnode_ wrappers below.
 * No PMP isolation: a wild pointer can still corrupt outside the arena.
 */
#define NNODE_ARENA_SLOTS 8
#define NNODE_ARENA_HEAP_QUOTA (32u * 1024u)

void nnode_arena_init(void);
int nnode_arena_alloc(int slot);
int nnode_arena_free_slot(int slot);
int nnode_arena_set_active(int slot);
int nnode_arena_owner_current(void);
void nnode_arena_note_task(int slot, TaskHandle_t h);
void nnode_arena_bind_task(int slot, TaskHandle_t h, size_t stack_bytes);
void nnode_arena_forget_task(TaskHandle_t h);
int nnode_arena_kill_tasks(int slot);
int nnode_arena_slot_has_live_tasks(int slot);
int nnode_arena_slot_tasks(int slot);
size_t nnode_arena_slot_stacks(int slot);
size_t nnode_arena_heap_bytes(int slot);
