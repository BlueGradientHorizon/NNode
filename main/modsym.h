#pragma once
#include <stdint.h>
/* Address of an extra firmware symbol for ELF modules, 0 if unknown. */
uintptr_t nnode_sym_resolve(const char *name);
/* Install the custom elf_loader resolver (own table + loader defaults). */
void nnode_sym_resolver_install(void);
