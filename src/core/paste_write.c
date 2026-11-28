#include "paste_write.h"

#include <string.h>

#define PW_BUF_MAX PASTE_WRITE_CHUNK

/* Length of the UTF-8 sequence that starts with lead byte b, as the lead
 * byte declares it; 1 for ASCII and for a stray continuation byte. */
static size_t utf8_declared_len(unsigned char b)
{
    if (b >= 0xF0u) return 4u;
    if (b >= 0xE0u) return 3u;
    if (b >= 0xC0u) return 2u;
    return 1u;
}

/* One write of buf[0..len), continuing after a short write. Returns 0 when
 * the paste may go on, 1 when it must stop. */
static int flush_buf(const char *buf, size_t len, PasteWriteFn write_fn,
                     void *ctx, WriteAgg *agg, size_t *accepted)
{
    size_t off = 0;
    while (off < len) {
        int rc = write_fn(ctx, buf + off, len - off);
        write_agg_add(agg, rc, len - off);
        if (rc <= 0) return 1;
        if ((size_t)rc > len - off) rc = (int)(len - off);
        off += (size_t)rc;
        *accepted += (size_t)rc;
    }
    return 0;
}

PasteWriteResult paste_write_chunked_ex(const char *in, size_t in_len,
                                        int local, PasteWriteFn write_fn,
                                        void *ctx, size_t max_chunk,
                                        WriteAgg *agg)
{
    PasteWriteResult res = { 0, 0, 0 };
    if (!write_fn || (!in && in_len > 0)) return res;
    if (max_chunk < 4u) max_chunk = 4u;
    if (max_chunk > PW_BUF_MAX) max_chunk = PW_BUF_MAX;

    char buf[PW_BUF_MAX];
    size_t n = 0, accepted = 0, expected = 0, i = 0;
    int stopped = 0;

    while (i < in_len) {
        unsigned char b = (unsigned char)in[i];
        if (b == '\r') { i++; continue; }

        char unit[4];
        size_t ulen = 0;
        if (b == '\n') {
            unit[ulen++] = local ? '\r' : '\n';
            i++;
        } else {
            /* The lead byte plus the continuation bytes that really follow. */
            size_t want = utf8_declared_len(b);
            unit[ulen++] = (char)b;
            i++;
            while (ulen < want && i < in_len &&
                   ((unsigned char)in[i] & 0xC0u) == 0x80u) {
                unit[ulen++] = in[i++];
            }
        }
        expected += ulen;
        if (stopped) continue;   /* keep counting, so the report knows the total */
        if (n + ulen > max_chunk) {
            if (flush_buf(buf, n, write_fn, ctx, agg, &accepted)) {
                stopped = 1;
                continue;
            }
            n = 0;
        }
        memcpy(buf + n, unit, ulen);
        n += ulen;
    }
    if (!stopped && n > 0 && flush_buf(buf, n, write_fn, ctx, agg, &accepted))
        stopped = 1;
    res.accepted = accepted;
    res.expected = expected;
    res.stopped = stopped;
    return res;
}

size_t paste_write_chunked(const char *in, size_t in_len, int local,
                           PasteWriteFn write_fn, void *ctx,
                           size_t max_chunk, WriteAgg *agg)
{
    return paste_write_chunked_ex(in, in_len, local, write_fn, ctx, max_chunk,
                                  agg).accepted;
}
