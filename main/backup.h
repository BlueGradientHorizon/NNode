#pragma once
#include <stddef.h>
#include "cbor.h"
/* Streams the full backup in CBOR. Returns 0 on success. */
int nnode_backup_emit(cbor_emit_fn emit, void *ctx);
/* Streams the text bundle of all config packages (concatenated files,
 * same shape as the per-package files on disk). Returns 0 on success. */
int nnode_config_export(cbor_emit_fn emit, void *ctx);
/* Streaming restore with full-replacement semantics: /data files are
 * wiped when the files section arrives, configs are committed only
 * after each package validates. RAM use stays small; the limit is
 * free flash, not heap.
 * Returns 0, else -1 with reason in err. Reboot is left to the caller. */
int nnode_backup_restore_stream(cbor_read_fn read, void *ctx, int total_len,
    char *err, size_t errlen);
