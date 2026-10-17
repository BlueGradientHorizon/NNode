#pragma once

/* Compiled-in defaults for firmware-owned packages (single source for
 * seed, store validation and backup restore checks). Module packages
 * carry the same text in their .nnode_default ELF section instead. */

const char *nnode_fw_defaults(const char *pkg);
