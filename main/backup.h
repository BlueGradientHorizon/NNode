#pragma once
#include <stddef.h>
#include "cbor.h"
/* Streams the full backup in CBOR. Returns 0 on success. */
int nnode_backup_emit(cbor_emit_fn emit, void *ctx);
/* Streaming restore with full-replacement semantics: /data is wiped when
 * the files section arrives, NVS is committed only after everything
 * lands. RAM use stays ~6 KB; the limit is free flash, not heap.
 * Returns 0, else -1 with reason in err. Reboot is left to the caller. */
int nnode_backup_restore_stream(cbor_read_fn read, void *ctx, int total_len,
    char *err, size_t errlen);
