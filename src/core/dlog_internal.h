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

/* Installs an unhandled-exception filter that calls dlog_crash_note() (Windows;
 * a no-op elsewhere). Called by dlog_start() with the system platform. */
void dlog_sys_install_crash_filter(void);

#endif
