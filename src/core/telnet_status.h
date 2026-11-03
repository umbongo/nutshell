#ifndef NUTSHELL_TELNET_STATUS_H
#define NUTSHELL_TELNET_STATUS_H

#include <stddef.h>

/*
 * telnet_status — the telnet tab's status-line text and the merge of
 * per-address connect outcomes (core, Win32-free).
 */

/* Outcome of one connect attempt, declared in order of preference. */
typedef enum {
    TN_CONN_OK = 0,
    TN_CONN_REFUSED,
    TN_CONN_TIMEOUT,
    TN_CONN_OTHER,
    TN_CONN_RESOLVE
} TnConnOutcome;

/* Best of two outcomes: OK > REFUSED > TIMEOUT > OTHER > RESOLVE. */
TnConnOutcome tn_conn_merge(TnConnOutcome acc, TnConnOutcome next);

typedef enum {
    TN_LINE_CONNECTING,
    TN_LINE_CONNECTED,
    TN_LINE_REFUSED,
    TN_LINE_TIMEOUT,
    TN_LINE_RESOLVE,
    TN_LINE_OTHER,
    TN_LINE_CLOSED
} TnLine;

/* OK -> CONNECTED, REFUSED -> REFUSED, TIMEOUT -> TIMEOUT, OTHER -> OTHER,
 * RESOLVE -> RESOLVE. */
TnLine tn_line_for_outcome(TnConnOutcome o);

/* The line's text, no CR/LF framing; NUL-terminated, truncated to cap (never
 * in the middle of a UTF-8 sequence). host:port is printed [host]:port when
 * host contains ':'. A NULL host prints "?". cap 0 leaves buf untouched.
 * Returns strlen of the result. */
size_t tn_status_line(TnLine which, const char *host, int port,
                      char *buf, size_t cap);

#endif
