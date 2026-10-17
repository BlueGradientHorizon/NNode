#pragma once
#include <stddef.h>

/* A/B firmware updates over two OTA slots (ota_0/ota_1, no factory).
 * The update always lands in the inactive slot, the old one stays
 * bootable as a fallback until the next update flips the roles. */

/* Streaming read callback: returns bytes read, negative on error. */
typedef int (*nnode_ota_read_fn)(void *ctx, char *buf, size_t len);

/* JSON for GET /api/ota/status:
 * {"running":"ota_0","boot":"ota_0","slots":[
 *   {"label":"ota_0","version":"0.1.0 abc123","state":"valid",
 *    "running":1,"boot":1}]}. State is one of
 * new/pending/valid/invalid/aborted/empty/unknown. */
int nnode_ota_status_json(char *buf, size_t len);
/* Flash total bytes into the inactive slot and select it for next boot.
 * err gets a stable snake_case code (see http.c dictionary). */
int nnode_ota_upload(nnode_ota_read_fn rd, void *ctx, int total,
    char *err, size_t errlen);
/* Boot into the other slot (or label when given). Same err convention. */
int nnode_ota_rollback(const char *label, char *err, size_t errlen);
/* Confirm the running slot after a clean boot (no-op unless the
 * bootloader waits for validation of a pending slot). */
void nnode_ota_mark_valid(void);
