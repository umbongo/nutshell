#ifndef NUTSHELL_PASTE_WRITE_H
#define NUTSHELL_PASTE_WRITE_H

#include <stddef.h>
#include "write_stats.h"

/*
 * Writes pasted text to a transport in chunks instead of one byte per call.
 * A paste over SSH used to be one channel write per byte; each write flips
 * the session to blocking mode and back (src/term/ssh_channel.c), so a large
 * paste cost thousands of those round trips.
 *
 * Line ends are mapped the way paste_chunk_write() always did: '\r' is
 * dropped, and '\n' becomes '\r' when `local` is set (a console child reads
 * Enter, not LF). Bracketed-paste markers and the control filter are the
 * caller's business and unchanged. Pure: no platform types.
 */

/* Largest single write the paste path makes. */
#define PASTE_WRITE_CHUNK 4096u

typedef int (*PasteWriteFn)(void *ctx, const char *data, size_t len);

/* What a paste write did: bytes accepted by the transport, bytes the whole
 * text translates to, and whether the paste stopped before all were sent. */
typedef struct {
    size_t accepted;
    size_t expected;
    int    stopped;   /* 1 when a write failed and the rest was dropped */
} PasteWriteResult;

/*
 * Translates in[0..in_len) and writes the result through write_fn in chunks
 * of at most max_chunk bytes (values under 4 are raised to 4, so a whole
 * UTF-8 sequence always fits). A chunk never ends inside a UTF-8 sequence
 * that is complete in the input. Each write call is noted in agg
 * (rc, bytes wanted); a short write continues with the rest and counts as a
 * failure only if it ends the paste, a write that
 * returns a negative value stops the paste at once, and one that returns 0
 * stops it after 8 retries in a row (the rest would fail the same way).
 *
 * Returns the number of bytes handed to write_fn and accepted. The _ex form
 * also reports the expected total and whether the paste stopped; a stopped
 * paste drops the rest of the text, so a caller sending line by line must not
 * send the next line.
 * NULL write_fn or NULL in with in_len > 0 writes nothing and returns 0.
 */
PasteWriteResult paste_write_chunked_ex(const char *in, size_t in_len,
                                        int local, PasteWriteFn write_fn,
                                        void *ctx, size_t max_chunk,
                                        WriteAgg *agg);

/* The same, returning only the accepted byte count. */
size_t paste_write_chunked(const char *in, size_t in_len, int local,
                           PasteWriteFn write_fn, void *ctx,
                           size_t max_chunk, WriteAgg *agg);

#endif
