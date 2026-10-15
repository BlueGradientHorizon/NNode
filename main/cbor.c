#include <stdlib.h>
#include <string.h>
#include "cbor.h"

void cbor_w_init(cbor_w_t *w, cbor_emit_fn emit, void *ctx)
{
    w->emit = emit;
    w->ctx = ctx;
    w->n = 0;
    w->failed = 0;
}

int cbor_w_flush(cbor_w_t *w)
{
    if (w->failed) {
        return -1;
    }
    if (w->n == 0) {
        return 0;
    }
    if (w->emit(w->ctx, (const char *)w->buf, w->n)) {
        w->failed = 1;
        return -1;
    }
    w->n = 0;
    return 0;
}

static int w_put(cbor_w_t *w, const void *d, size_t n)
{
    const unsigned char *p = (const unsigned char *)d;
    if (w->failed) {
        return -1;
    }
    while (n) {
        size_t room = sizeof(w->buf) - w->n;
        size_t cp = n < room ? n : room;
        if (!cp) {
            if (cbor_w_flush(w)) {
                return -1;
            }
            continue;
        }
        memcpy(w->buf + w->n, p, cp);
        w->n += cp;
        p += cp;
        n -= cp;
    }
    return 0;
}

static int w_head(cbor_w_t *w, int major, uint64_t v)
{
    unsigned char b[9];
    int n = 0;
    if (v < 24) {
        b[n++] = (unsigned char)((major << 5) | (int)v);
    } else if (v < 256) {
        b[n++] = (unsigned char)((major << 5) | 24);
        b[n++] = (unsigned char)v;
    } else if (v < 65536) {
        b[n++] = (unsigned char)((major << 5) | 25);
        b[n++] = (unsigned char)(v >> 8);
        b[n++] = (unsigned char)v;
    } else if (v < 4294967296ULL) {
        b[n++] = (unsigned char)((major << 5) | 26);
        b[n++] = (unsigned char)(v >> 24);
        b[n++] = (unsigned char)(v >> 16);
        b[n++] = (unsigned char)(v >> 8);
        b[n++] = (unsigned char)v;
    } else {
        b[n++] = (unsigned char)((major << 5) | 27);
        b[n++] = (unsigned char)(v >> 56);
        b[n++] = (unsigned char)(v >> 48);
        b[n++] = (unsigned char)(v >> 40);
        b[n++] = (unsigned char)(v >> 32);
        b[n++] = (unsigned char)(v >> 24);
        b[n++] = (unsigned char)(v >> 16);
        b[n++] = (unsigned char)(v >> 8);
        b[n++] = (unsigned char)v;
    }
    return w_put(w, b, (size_t)n);
}

int cbor_w_uint(cbor_w_t *w, uint64_t v)
{
    return w_head(w, 0, v);
}

int cbor_w_bool(cbor_w_t *w, int v)
{
    unsigned char b = v ? 0xf5 : 0xf4;
    return w_put(w, &b, 1);
}

int cbor_w_map(cbor_w_t *w, uint64_t n)
{
    return w_head(w, 5, n);
}

int cbor_w_array(cbor_w_t *w, uint64_t n)
{
    return w_head(w, 4, n);
}

int cbor_w_tstr(cbor_w_t *w, const char *s)
{
    size_t l = s ? strlen(s) : 0;
    if (w_head(w, 3, (uint64_t)l)) {
        return -1;
    }
    return w_put(w, s, l);
}

int cbor_w_bstr_head(cbor_w_t *w, uint64_t len)
{
    return w_head(w, 2, len);
}

int cbor_w_raw(cbor_w_t *w, const void *data, size_t len)
{
    if (!len) {
        return 0;
    }
    return w_put(w, data, len);
}

int cbor_r_init(cbor_r_t *r, cbor_read_fn read, void *ctx, int total)
{
    memset(r, 0, sizeof(*r));
    r->buf = (char *)malloc(CBOR_R_BUF);
    if (!r->buf) {
        return -1;
    }
    r->read = read;
    r->ctx = ctx;
    r->left = total;
    return 0;
}

void cbor_r_free(cbor_r_t *r)
{
    free(r->buf);
    r->buf = NULL;
}

static int r_fill(cbor_r_t *r)
{
    size_t avail;
    int want, n;
    if (r->left <= 0) {
        return -1;
    }
    if (r->start) {
        avail = r->end - r->start;
        if (avail) {
            memmove(r->buf, r->buf + r->start, avail);
        }
        r->start = 0;
        r->end = avail;
    }
    want = (int)(CBOR_R_BUF - r->end);
    if (want > r->left) {
        want = r->left;
    }
    n = r->read(r->ctx, r->buf + r->end, (size_t)want);
    if (n <= 0) {
        return -1;
    }
    r->end += (size_t)n;
    r->left -= n;
    return 0;
}

static int r_need(cbor_r_t *r, size_t n)
{
    if (!r->buf || n > CBOR_R_BUF) {
        return -1;
    }
    while (r->end - r->start < n) {
        if (r_fill(r)) {
            return -1;
        }
    }
    return 0;
}

int cbor_r_head(cbor_r_t *r, int *major, uint64_t *val)
{
    unsigned char b;
    unsigned int ai;
    uint64_t v;
    if (r_need(r, 1)) {
        return -1;
    }
    b = (unsigned char)r->buf[r->start++];
    *major = b >> 5;
    ai = b & 31;
    if (ai < 24) {
        v = ai;
    } else if (ai == 24) {
        if (r_need(r, 1)) {
            return -1;
        }
        v = (unsigned char)r->buf[r->start++];
    } else if (ai == 25) {
        if (r_need(r, 2)) {
            return -1;
        }
        v = ((uint64_t)(unsigned char)r->buf[r->start] << 8) |
            (uint64_t)(unsigned char)r->buf[r->start + 1];
        r->start += 2;
    } else if (ai == 26) {
        if (r_need(r, 4)) {
            return -1;
        }
        v = ((uint64_t)(unsigned char)r->buf[r->start] << 24) |
            ((uint64_t)(unsigned char)r->buf[r->start + 1] << 16) |
            ((uint64_t)(unsigned char)r->buf[r->start + 2] << 8) |
            (uint64_t)(unsigned char)r->buf[r->start + 3];
        r->start += 4;
    } else if (ai == 27) {
        int i;
        if (r_need(r, 8)) {
            return -1;
        }
        v = 0;
        for (i = 0; i < 8; i++) {
            v = (v << 8) | (uint64_t)(unsigned char)r->buf[r->start + i];
        }
        r->start += 8;
    } else {
        return -1;
    }
    *val = v;
    return 0;
}

int cbor_r_uint(cbor_r_t *r, uint64_t *v)
{
    int major;
    uint64_t val;
    if (cbor_r_head(r, &major, &val) || major != 0) {
        return -1;
    }
    *v = val;
    return 0;
}

int cbor_r_bool(cbor_r_t *r, int *v)
{
    int major;
    uint64_t val;
    if (cbor_r_head(r, &major, &val) || major != 7) {
        return -1;
    }
    if (val == 20) {
        *v = 0;
        return 0;
    }
    if (val == 21) {
        *v = 1;
        return 0;
    }
    return -1;
}

int cbor_r_tstr(cbor_r_t *r, char *out, size_t cap)
{
    int major;
    uint64_t val, i;
    if (cbor_r_head(r, &major, &val) || major != 3) {
        return -1;
    }
    if (val >= cap) {
        return -1;
    }
    for (i = 0; i < val; i++) {
        if (r_need(r, 1)) {
            return -1;
        }
        out[i] = r->buf[r->start++];
    }
    out[val] = 0;
    return 0;
}

int cbor_r_take(cbor_r_t *r, void *dst, size_t n)
{
    char *p = (char *)dst;
    while (n) {
        size_t avail = r->end - r->start;
        size_t cp;
        if (!avail) {
            if (r_fill(r)) {
                return -1;
            }
            continue;
        }
        cp = n < avail ? n : avail;
        memcpy(p, r->buf + r->start, cp);
        r->start += cp;
        p += cp;
        n -= cp;
    }
    return 0;
}

static int r_discard(cbor_r_t *r, uint64_t n)
{
    char scrap[128];
    while (n) {
        size_t cp = n < sizeof(scrap) ? (size_t)n : sizeof(scrap);
        if (cbor_r_take(r, scrap, cp)) {
            return -1;
        }
        n -= cp;
    }
    return 0;
}

static int skip_depth(cbor_r_t *r, int depth)
{
    int major;
    uint64_t val, i;
    if (depth > 8) {
        return -1;
    }
    if (cbor_r_head(r, &major, &val)) {
        return -1;
    }
    switch (major) {
    case 0:
    case 1:
        return 0;
    case 7:
        if (val == 25) {
            return r_discard(r, 2);
        }
        if (val == 26) {
            return r_discard(r, 4);
        }
        if (val == 27) {
            return r_discard(r, 8);
        }
        return 0;
    case 6:
        return skip_depth(r, depth + 1);
    case 2:
    case 3:
        return r_discard(r, val);
    case 4:
        for (i = 0; i < val; i++) {
            if (skip_depth(r, depth + 1)) {
                return -1;
            }
        }
        return 0;
    case 5:
        for (i = 0; i < val; i++) {
            if (skip_depth(r, depth + 1) || skip_depth(r, depth + 1)) {
                return -1;
            }
        }
        return 0;
    default:
        return -1;
    }
}

int cbor_r_skip(cbor_r_t *r)
{
    return skip_depth(r, 0);
}
