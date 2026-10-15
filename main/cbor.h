#pragma once
#include <stddef.h>
#include <stdint.h>

/* Minimal streaming CBOR (RFC 8949), definite lengths only: uint, tstr,
 * bstr, array, map, bool. No floats, tags, or indefinite lengths.
 * The writer stages through 256 B and flushes via emit; the reader keeps
 * a 2 KB window over a byte source, so multi-megabyte items stream with
 * constant RAM. Unknown map keys should be skipped (forward compat). */

typedef int (*cbor_emit_fn)(void *ctx, const char *data, size_t len);

typedef struct {
    cbor_emit_fn emit;
    void *ctx;
    unsigned char buf[256];
    size_t n;
    int failed;
} cbor_w_t;

void cbor_w_init(cbor_w_t *w, cbor_emit_fn emit, void *ctx);
int cbor_w_flush(cbor_w_t *w);
int cbor_w_uint(cbor_w_t *w, uint64_t v);
int cbor_w_bool(cbor_w_t *w, int v);
int cbor_w_map(cbor_w_t *w, uint64_t n);
int cbor_w_array(cbor_w_t *w, uint64_t n);
int cbor_w_tstr(cbor_w_t *w, const char *s);
int cbor_w_bstr_head(cbor_w_t *w, uint64_t len);
int cbor_w_raw(cbor_w_t *w, const void *data, size_t len);

typedef int (*cbor_read_fn)(void *ctx, char *buf, size_t len);

#define CBOR_R_BUF 2048

typedef struct {
    cbor_read_fn read;
    void *ctx;
    int left;
    char *buf;
    size_t start, end;
} cbor_r_t;

int cbor_r_init(cbor_r_t *r, cbor_read_fn read, void *ctx, int total);
void cbor_r_free(cbor_r_t *r);
/* Initial byte: major 0..7, argument value. Fails on indefinite (31). */
int cbor_r_head(cbor_r_t *r, int *major, uint64_t *val);
int cbor_r_uint(cbor_r_t *r, uint64_t *v);
int cbor_r_bool(cbor_r_t *r, int *v);
/* Text string incl. NUL; fails if it does not fit in cap. */
int cbor_r_tstr(cbor_r_t *r, char *out, size_t cap);
/* Exact n raw bytes or fail. */
int cbor_r_take(cbor_r_t *r, void *dst, size_t n);
/* Skip one whole item, any nesting (depth-capped). */
int cbor_r_skip(cbor_r_t *r);
