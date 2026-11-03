/* src/core/dlog_snapshot.h -- the state the issue marker records.
 *
 * Mark Issue (Ctrl+Shift+M) fills a DlogSnapshot on the UI thread, before
 * anything is painted, and hands it to dlog_issue_mark() (dlog.h). This
 * module is the plain-struct side of that: the struct, and a pure formatter
 * that turns it into the issue.* lines (spec section 7), so it can be tested
 * without a window. Strings in the struct are borrowed, never copied, and
 * need only live until the call returns.
 */
#ifndef NUTSHELL_CORE_DLOG_SNAPSHOT_H
#define NUTSHELL_CORE_DLOG_SNAPSHOT_H

#include <stddef.h>

#define DLOG_SNAP_MAX_SESSIONS 32

/* issue.window */
typedef struct {
    int client_w, client_h;
    int dpi;
    int tab_height;
    int left_margin;
    int ai_docked;
    int ai_width;
    int scrollbar_visible;
    int iconic;
    int fullscreen;
    long ms_since_last_paint;     /* -1 when no paint yet */
    int cooldown_ms;              /* the invalidate cooldown in force */
    unsigned long long focus_hwnd;
    const char *focus_role;       /* "main", "ai_input", "ai_list", "ai_panel", "other", "none"; NULL = "-" */
    int focus_visible;            /* the focused window is visible */
    unsigned long long main_hwnd;
    int foreground;               /* Nutshell owns the foreground window */
    const char *capture;          /* mouse capture holder: "none", "main", "other"; NULL = "-" */
    int key_ctrl, key_shift, key_alt;   /* modifier down (always C+S when marked by Ctrl+Shift+M: see via) */
    const char *via;              /* how the issue was marked: "key" or "menu"; NULL = "-" */
} DlogSnapWindow;

/* issue.term, for the active session */
typedef struct {
    unsigned sid;
    const char *kind;             /* "ssh", "local", "telnet" */
    int connected;
    int rows, cols;
    int lines_count, lines_start, lines_capacity;
    int scrollback_offset;
    int cursor_row, cursor_col, cursor_visible;
    int alt_screen;
    int scroll_top, scroll_bottom;
    int full_redraw_needed;
    unsigned long long write_seq;
    long ms_since_recv;           /* ms since the session last received bytes; -1 never */
    long ms_since_key_write;      /* ms since the last keystroke/paste write; -1 never */
    long last_write_rc;           /* return code of that write (bytes, or negative error) */
    unsigned write_fail_count;    /* failed or short writes so far */
    const unsigned char *dirty;   /* one flag per row (non-zero = dirty), may be NULL */
    int dirty_n;
} DlogSnapTerm;

/* issue.render */
typedef struct {
    int char_w, char_h;
    int dispbuf_rows, dispbuf_cols;
    int prev_cursor_row, prev_cursor_col;
} DlogSnapRender;

/* issue.session, one for every session other than the active one */
typedef struct {
    unsigned sid;
    const char *kind;
    int connected;
    int dirty_rows;
    int alt_screen;
} DlogSnapSession;

/* issue.ai */
typedef struct {
    int panel_visible;
    int stream_live;
    int dispatch_batch;
    const char *policy;           /* e.g. "read/none" */
} DlogSnapAi;

typedef struct {
    DlogSnapWindow  window;
    int             has_term;     /* 0 when there is no active session */
    DlogSnapTerm    term;
    DlogSnapRender  render;
    DlogSnapSession sessions[DLOG_SNAP_MAX_SESSIONS];
    int             nsessions;
    DlogSnapAi      ai;
} DlogSnapshot;

/* One formatted line: the event name, the session id for the sid column (0 =
 * none) and the key=value fields, already quoted. */
typedef struct {
    const char *event;
    unsigned    sid;
    char        fields[640];
} DlogSnapLine;

/* Rows flagged dirty as ranges: "none", "5", "0-72", "0-3,7,10-11". A list that
 * would not fit in cap ends with ",..." . Returns the length written. */
size_t dlog_dirty_ranges(const unsigned char *flags, int n, char *out, size_t cap);

/* Format the snapshot: issue.window, issue.term (when has_term), issue.render,
 * one issue.session per other session, issue.ai, in that order. Returns the
 * number of lines written (at most max). Pure. */
size_t dlog_snapshot_format(const DlogSnapshot *s, DlogSnapLine *out, size_t max);

#endif /* NUTSHELL_CORE_DLOG_SNAPSHOT_H */
