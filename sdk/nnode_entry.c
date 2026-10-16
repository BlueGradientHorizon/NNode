/* NNode module entry shim. Linked into every module.
 *
 * Provides the ELF entry point (app_main, selected via -e app_main in
 * elf_loader.cmake) so user code only defines nnode_main/nnode_term.
 * Registration stores per-instance pointers in the firmware slot,
 * no reverse symbol lookup is needed (the .elf is stripped).
 */
#include "nnode_sdk.h"

/* User definitions (link error if missing). */
extern int nnode_main(int argc, char *argv[]);
extern void nnode_term(void);

int app_main(int argc, char *argv[])
{
    /* Weak import: NULL only if the loader did not bind firmware symbols,
     * then run fails with bad_abi instead of jumping to zero. */
    if (nnode_register) {
        nnode_register((void *)nnode_main, (void *)nnode_term);
    }
    return nnode_main(argc, argv);
}
