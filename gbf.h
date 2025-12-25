/*MIT License

Copyright (c) 2025 huwwa

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.*/
#ifndef GBF_H_
#define GBF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef BUF_DEBUG
#include <assert.h>
#endif

#define LIB_FUNC static

#ifndef BUF_INIT_SIZE
#define BUF_INIT_SIZE 1024
#endif

typedef uint8_t u8;

typedef struct {
    size_t gap_start;
    size_t gap_end;
    size_t capacity;
    u8 *data;
} Buffer;

typedef struct {
    const u8 *ptr;
    size_t len;
} buf_slice;

void buf_new(Buffer *b);
void buf_reset(Buffer *b);
void buf_free(Buffer *b);

size_t buf_len(const Buffer *b);
size_t buf_cursor(const Buffer *b);

int buf_cursor_set(Buffer *b, size_t pos);
int buf_cursor_move(Buffer *b, ptrdiff_t delta);
int buf_ccat(Buffer *b, u8 c);
int buf_cat(Buffer *b, const u8 *s, size_t n);
int buf_delete(Buffer *b, ptrdiff_t delta);

size_t buf_read(const Buffer *b, size_t pos, u8 *dst, size_t n);

/* The buffer uses a gap, so [pos, pos+n) may be split:
 *   out[0]  bytes before the gap
 *   out[1]  bytes after the gap.
 * If contiguous, out[1] is zeroed.
 * Slices remain valid until the buffer is modified or freed. */
size_t buf_view(const Buffer *b, size_t pos, size_t n, buf_slice out[2]);

/* Materialize the entire buffer into a NULL-terminated string.
 * Allocates; caller owns result.
 * O(n). Intended for debugging, I/O, and interp only. */
u8 *buf_flatten(const Buffer *b);

#ifdef __cplusplus
}
#endif

#ifdef GBF_IMPLEMENTATION

#ifdef BUF_DEBUG
/* Invariants:
 *   0 <= gap_start <= gap_end <= capacity
 *   Text length = capacity - (gap_end - gap_start)
 * pos position is always at gap_start.
 * All operations are byte-based (no UTF-8 awareness yet). */
static inline void buf_assert(const Buffer *b) {
    assert(b);
    assert(b->gap_start <= b->gap_end);
    assert(b->gap_end <= b->capacity);
    assert(b->data || b->capacity == 0);
}
#else
#define buf_assert(b) ((void)0)
#endif

void buf_new(Buffer *b)
{
    if (!b)
        return;
    memset(b, 0, sizeof(*b));
}

void buf_reset(Buffer *b)
{
    if (!b)
        return;
    b->gap_start = 0;
    b->gap_end = b->capacity;
}

void buf_free(Buffer *b)
{
    if (!b)
        return;
    free(b->data);
    b->data = NULL;
    b->capacity = 0;
}

size_t buf_len(const Buffer *b)
{
    return b ? b->capacity - (b->gap_end - b->gap_start) : 0;
}

size_t buf_cursor(const Buffer *b)
{
    return b ? b->gap_start : 0;
}
/*---------------------------------------------------------------------------*/

LIB_FUNC inline size_t buf_gap_len(const Buffer *b) {
    return b->gap_end - b->gap_start;
}

LIB_FUNC void buf_move_gap(Buffer *b, size_t pos)
{
    size_t n;
    if (pos == b->gap_start)
        return;

    if (pos < b->gap_start) {
        n = b->gap_start - pos;
        memmove(b->data + b->gap_end - n, b->data + pos, n);
        b->gap_end -= n;
        b->gap_start -= n;
    } else {
        n = pos - b->gap_start;
        memmove(b->data + b->gap_start, b->data + b->gap_end, n);
        b->gap_start += n;
        b->gap_end += n;
    }
}

LIB_FUNC int buf_reserve(Buffer *b, size_t new_size)
{
    size_t ncap;
    u8 *p;

    if (buf_gap_len(b) >= new_size)
        return 1;
    size_t buflen = buf_len(b);
    ncap = b->capacity ? b->capacity : BUF_INIT_SIZE;
    while ((ncap - buflen) < new_size)
        ncap *= 2;
    p = realloc(b->data, ncap);
    if (!p)
        return 0;
    b->data = p;

    size_t n, nend;
    n = b->capacity - b->gap_end;
    nend = ncap - n;
    memmove(b->data + nend, b->data + b->gap_end, n);
    b->gap_end = nend;
    b->capacity = ncap;

    return 1;
}
/*---------------------------------------------------------------------------*/

int buf_cursor_set(Buffer *b, size_t pos)
{
    buf_assert(b);
    if (pos > buf_len(b))
        return 0;
    buf_move_gap(b, pos);
    return 1;
}

int buf_cursor_move(Buffer *b, ptrdiff_t delta)
{
    ptrdiff_t pos;
    buf_assert(b);
    pos = (ptrdiff_t)b->gap_start + delta;
    if (pos < 0 || (size_t)pos > buf_len(b))
        return 0;
    return buf_cursor_set(b, pos);
}

/* add a byte */
int buf_ccat(Buffer *b, u8 c)
{
    if (!b || !buf_reserve(b, 1))
        return 0;
    buf_assert(b);
    b->data[b->gap_start++] = c;
    buf_assert(b);
    return 1;
}
/* n == 0, treat as cstr */
int buf_cat(Buffer *b, const u8 *s, size_t n)
{
    if (!b || !s)
        return 0;
    buf_assert(b);
    n = n ? n : strlen((const char *)s);
    if (!buf_reserve(b, n))
        return 0;
    memcpy(b->data + b->gap_start, s, n);
    b->gap_start += n;
    buf_assert(b);
    return 1;
}

int buf_insert(Buffer *b, size_t pos, const u8 *s, size_t n)
{
    if (!b || !s)
        return 0;
    if (!buf_cursor_set(b, pos))
        return 0;
    return buf_cat(b, s, n);
}

int buf_delete(Buffer *b, ptrdiff_t delta)
{
    if (!b)
        return 0;
    buf_assert(b);
    if (delta > 0) {
        if ((size_t)delta > buf_len(b) - buf_cursor(b))
            return 0;
        b->gap_end += delta;
    } else if (delta < 0){
        if ((ptrdiff_t)b->gap_start + delta < 0)
            return 0;
        b->gap_start += delta;
    }
    buf_assert(b);
    return 1;
}
/*---------------------------------------------------------------------------*/

size_t buf_read(const Buffer *b, size_t pos, u8 *dst, size_t n)
{
    size_t buflen;
    buf_assert(b);
    buflen = buf_len(b);
    if (!b || !dst || !n || pos >= buflen)
        return 0;

    if (pos + n > buflen)
        n = buflen - pos;

    if (pos > b->gap_start) {
        memcpy(dst, b->data + pos + buf_gap_len(b), n);
    } else {
        if (pos + n <= b->gap_start)
            memcpy(dst, b->data + pos, n);
        else {
            size_t ncpy = b->gap_start - pos;
            memcpy(dst, b->data + pos, ncpy);
            memcpy(dst + ncpy, b->data + b->gap_end, n - ncpy);
        }
    }
    return n;
}

size_t buf_view(const Buffer *b, size_t pos, size_t n, buf_slice out[2])
{
    size_t buflen;
    buf_assert(b);
    buflen = buf_len(b);
    if (!b || !out || !n || pos >= buflen)
        return 0;

    if (pos + n > buflen)
        n = buflen - pos;

    memset(out, 0, sizeof(*out)*2);
    if (pos < b->gap_start) {
        if (pos + n <= b->gap_start) {
            out->len = n;
            out->ptr = b->data + pos;
        } else {
            size_t n1 = b->gap_start - pos;
            out->len = n1;
            out->ptr = b->data + pos;
            out[1].len = n - n1;
            out[1].ptr = b->data + b->gap_end;
        }
    } else {
        out->len = n;
        out->ptr = b->data + pos + buf_gap_len(b);
    }
    return n;
}

u8 *buf_flatten(const Buffer *b)
{
    u8 *buf;
    size_t h, t, buflen;
    if (!b)
        return NULL;

    buf_assert(b);
#ifdef GAP_DEBUG
    buflen = b->capacity;
#else
    buflen = buf_len(b);
#endif
    buf = malloc(buflen + 1);
    if (!buf)
        return NULL;
    h = b->gap_start;
    t = b->capacity - b->gap_end;

    memcpy(buf, b->data, h);
#ifdef GAP_DEBUG
    size_t gap = buf_gap_len(b);
    memset(buf + h, '_', gap);
    memcpy(buf + h + gap, b->data + b->gap_end, t);
#else
    memcpy(buf + h, b->data + b->gap_end, t);
#endif
    buf[buflen] = '\0';

    buf_assert(b);
    return buf;
}

#endif /* GBF_IMPLEMENTATION */
#endif /* GBF_H_ */
