#pragma once

/* NNode module SDK (firmware imports + user contract).
 *
 * Every module must define both functions:
 *   int nnode_main(int argc, char *argv[]);
 *   void nnode_term(void);
 *
 * nnode_main behaves like the old app_main: it runs in an httpd worker,
 * must not block, spawns worker tasks and returns immediately.
 * nnode_term is the SIGTERM handler: it runs when the user presses Stop,
 * must be short and non-blocking (release GPIO, flush state with
 * write-tmp-plus-rename, signal tasks to exit). Kill never calls it.
 *
 * The entry shim (sdk/nnode_entry.c, linked into each module) provides
 * the ELF entry point app_main, registers both pointers in the firmware
 * and forwards to nnode_main. Missing definitions fail the module link
 * with "undefined reference", so the contract is enforced at build time.
 */

#define NNODE_ABI_VERSION 1

/* Firmware imports, resolved via the modsym chain at load time.
 *
 * Declared weak so the example's intermediate executable link (blink.elf,
 * a host-side artifact that is never flashed) succeeds without IDF
 * providing these symbols. The shipped image (blink.app.elf) keeps them
 * undefined and the loader binds them to the firmware on run. */
void nnode_register(void *main_fn, void *term_fn) __attribute__((weak));

/* Pollable stop flag for module tasks. Returns 1 after Stop was pressed
 * for the calling instance, 0 otherwise. */
int nnode_stop_requested(void) __attribute__((weak));

/* Besides plain libc (printf, ...) and the gpio_/FreeRTOS modsym table,
 * modules may use these file ops (missing from the loader defaults):
 * fopen, fclose, fread, fwrite, fprintf, fseek, ftell, rename, unlink.
 * Files live on LittleFS under /data. Close what you open: FILE objects
 * are not arena-tracked, a leaked handle survives Stop. */
