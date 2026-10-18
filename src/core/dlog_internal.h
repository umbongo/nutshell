/* src/core/dlog_internal.h -- shared between dlog.c and dlog_format.c only.
 * Nothing outside src/core/dlog*.c may include it. */
#ifndef NUTSHELL_CORE_DLOG_INTERNAL_H
#define NUTSHELL_CORE_DLOG_INTERNAL_H

#include <stdarg.h>
#include <stddef.h>

/* A bounded output cursor. Units (an escape, a UTF-8 sequence, a literal run)
 * are all-or-nothing, so a line is never cut inside one; once a unit does not
 * fit, `cut` sticks and everything later is refused. `reserve` bytes beyond
 * `limit` stay free for the closing quote, the " truncated=1" marker and LF. */
typedef struct {
    char  *buf;
    size_t cap;          /* usable bytes (excluding the NUL) */
    size_t limit;        /* cap - reserve */
    size_t len;
    int    cut;
    int    quote_open;
} DlogOut;

#define DLOG_OUT_RESERVE 14u    /* '"' + " truncated=1" + LF */

/* prefix_len bytes of buf are already filled (the timestamp etc.). */
void   dlog_out_init(DlogOut *o, char *buf, size_t cap, size_t prefix_len, size_t reserve);
void   dlog_out_event(DlogOut *o, const char *event);
/* " " + the printf-subset fields; nothing at all when fmt produces no output. */
void   dlog_out_fmt(DlogOut *o, const char *fmt, va_list ap);
/* " " + fields copied verbatim (kept whole at escapes, quotes tracked). */
void   dlog_out_raw_fields(DlogOut *o, const char *fields);
/* Closes an open quote, appends " truncated=1" if cut, then LF if newline. NUL
 * terminates. Returns the length (excluding NUL). */
size_t dlog_out_finish(DlogOut *o, int newline);

/* The "<ts> <LEVEL> <cat> <sid> <thread> " prefix. Returns its length, or 0 if
 * it does not fit in cap. */
size_t dlog_make_prefix(char *buf, size_t cap, long long unix_ms, int offset_min,
                        int ev_level, int cat, unsigned sid, const char *thread);

/* The text two lines are compared on to decide whether the second repeats the
 * first: `body` (event and fields, no prefix, no LF) with the value of every
 * volatile key (tick, us, ms, since_paint_ms, elapsed_ms, quiet_ms, anything
 * ending _ms or _us) replaced by "~". NUL-terminated; returns the length. When
 * the result does not fit it is cut, so such a line simply never repeats. */
size_t dlog_repeat_key(char *out, size_t cap, const char *body, size_t len);

/* Known-secret scrub for a stream (a terminal read split anywhere). Copies
 * src[0..n) to dst with every known secret replaced by [REDACTED], except that
 * unless `final` the longest tail of src that could still be the beginning of a
 * secret is NOT consumed: *consumed (raw bytes used, at most n) tells the caller
 * what to carry in front of the next read. dst is NUL-terminated; returns its
 * length. A secret that starts before the held tail but ends inside it is
 * still replaced. */
size_t dlog_scrub_stream(char *dst, size_t dst_cap, const char *src, size_t n, int final, size_t *consumed);

/* Held secret-prefix tails of the content streams (dlog_emit_content keeps, per thread, the end of a
 * terminal read that could be the start of a known secret). dlog_streams_wipe_tails() secure_zero()s
 * every tail of the CALLING thread; dlog_secret_remove()/dlog_secret_clear() call it when a secret's
 * last registration ends and bump dlog_secret_epoch(), which every other thread compares on its next
 * content call and wipes its own tails when it changed. dlog_streams_held_nonzero() counts the
 * non-zero bytes still sitting in this thread's held buffers (tests). */
void     dlog_streams_wipe_tails(void);
unsigned dlog_secret_epoch(void);
size_t   dlog_streams_held_nonzero(void);

/* How long the writer thread sleeps before its next pass: 250 ms, or
 * DLOG_WAIT_FOREVER (until woken) when debugging is off and there is nothing
 * queued, pending or waiting for a repeat tick. */
unsigned dlog_writer_wait_ms(void);

/* Installs an unhandled-exception filter that calls dlog_crash_note() (Windows;
 * a no-op elsewhere). Called by dlog_start() with the system platform. */
void dlog_sys_install_crash_filter(void);

#endif
