#ifndef NUTSHELL_TERM_TERM_H
#define NUTSHELL_TERM_TERM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TERM_ATTR_BOLD      (1 << 0)
#define TERM_ATTR_UNDERLINE (1 << 1)
#define TERM_ATTR_REVERSE   (1 << 2)
#define TERM_ATTR_BLINK     (1 << 3)
#define TERM_ATTR_DIM       (1 << 4) /* SGR 2; cleared by 22 and 0. Painted only on a
                                      * theme with a contrast floor (term_colors.h). */

#define TERM_MAX_CSI_PARAMS 16

typedef enum {
    TERM_STATE_NORMAL,
    TERM_STATE_ESC,
    TERM_STATE_CSI,
    TERM_STATE_OSC,
    TERM_STATE_SCS     /* consuming one byte after ESC ( ) * + # */
} TermState;

/* How a cell's fg/bg colour was specified. */
typedef enum {
    COLOR_DEFAULT = 0, /* use the terminal's configured default */
    COLOR_ANSI16,      /* one of the 16 classic ANSI palette entries */
    COLOR_256,         /* xterm 256-colour palette (fg_index / bg_index) */
    COLOR_RGB          /* 24-bit truecolor */
} ColorMode;

typedef struct {
    uint32_t fg;       /* resolved 0xRRGGBB; 0 = black when mode != COLOR_DEFAULT */
    uint32_t bg;       /* resolved 0xRRGGBB */
    uint8_t  fg_mode;  /* ColorMode */
    uint8_t  bg_mode;  /* ColorMode */
    uint8_t  fg_index; /* palette index (0-255) when fg_mode is COLOR_ANSI16 or COLOR_256 */
    uint8_t  bg_index; /* palette index (0-255) when bg_mode is COLOR_ANSI16 or COLOR_256 */
    uint8_t  flags;    /* TERM_ATTR_* bitmask */
} TermAttr;

typedef struct {
    uint32_t codepoint;
    TermAttr attr;
} TermCell;

typedef struct {
    TermCell *cells;
    int width;
    int len;
    bool dirty;
    bool wrapped;
    uint64_t gen;      /* Terminal.write_gen at this row's last write (term_row_stamp) */
} TermRow;

typedef struct {
    int row;
    int col;
    bool visible;
} TermCursor;

typedef struct {
    int rows;
    int cols;

    /* Ring buffer of TermRow*. lines_capacity slots, indexed modulo that; but
     * only the live part, the term_logical_rows() slots from lines_start,
     * holds rows -- every other slot is NULL (see buffer.c). */
    TermRow **lines;
    int lines_capacity;
    int lines_count;
    int lines_start;

    int scrollback_offset;
    int max_scrollback;

    TermCursor cursor;
    TermAttr current_attr;

    TermState state;
    int csi_params[TERM_MAX_CSI_PARAMS];
    int csi_param_count;
    /* Bit i set: csi_params[i] was introduced by ':' (a sub-parameter of
     * csi_params[i-1]) rather than ';'. */
    uint16_t csi_sub_mask;
    /* A ';' or ':' arrived with all TERM_MAX_CSI_PARAMS slots in use: the
     * rest of the sequence's digits are dropped instead of merging into
     * the last slot. */
    bool csi_overflow;
    bool csi_private;     /* true when CSI sequence has '?' prefix */
    char osc_buffer[256];
    int osc_len;
    TermCursor saved_cursor;

    uint32_t utf8_codepoint;
    int utf8_remaining;
    /* Hardening: bounds the NEXT continuation byte. Only the byte right
     * after the lead byte needs a restricted range (it excludes overlong
     * encodings, UTF-16 surrogates and codepoints past U+10FFFF); every
     * continuation byte after that is the generic 0x80-0xBF. Set together
     * with utf8_remaining whenever a new sequence starts. */
    unsigned char utf8_lo;
    unsigned char utf8_hi;

    /* Window title (set by OSC 0/2) */
    char title[128];

    /* Terminal modes */
    bool app_cursor_keys; /* ?1h/l  — application cursor key sequences */
    bool insert_mode;     /* 4h/l   — insert vs. replace mode           */

    /* Scroll region (0-based, inclusive).  Default: 0 .. rows-1. */
    int scroll_top;
    int scroll_bot;

    /* Alternate screen buffer (?1049h/l) */
    bool alt_screen_active;
    TermRow  **primary_lines;
    int        primary_lines_capacity;
    int        primary_lines_count;
    int        primary_lines_start;
    TermCursor primary_cursor;
    /* Set when the buffer swaps (e.g. alt-screen exit) so the renderer knows
     * the display buffer shadow is stale and must be fully invalidated. */
    bool full_redraw_needed;

    /* Allocation failures that left the terminal working (buffer.c). grow_failing:
     * the last attempt to add a row of scrollback failed, so the oldest row was
     * recycled instead; cleared by the next success. resize_failing: the last
     * term_resize() could not allocate; cleared by the next success. Each only
     * gates a once-per-streak log line. */
    bool grow_failing;
    bool resize_failing;

    /* Bracketed paste mode (?2004h/l): when true, wrap paste with \033[200~/201~ */
    bool bracketed_paste_mode;

    /* Incremented in term_process() whenever it is called with len > 0.
     * Cheap "did anything arrive" signal for callers (e.g. the AI command
     * dispatcher) that need to detect terminal quiescence without diffing
     * screen content. */
    unsigned long write_seq;

    /* Selection support (spec docs/superpowers/specs/2026-10-20-putty-selection-design.md
     * section 2.2). lines_recycled: rows dropped from the front of this ring,
     * ever -- per ring: the alternate screen counts its own while the
     * primary's waits in primary_lines_recycled. The absolute line number of
     * logical index i is lines_recycled + i, stable while epoch is. epoch:
     * bumped by reflow and by alt-screen enter/leave, the events that renumber
     * or replace the rows. write_gen: bumped by every row write; the row
     * written takes the new value (term_row_stamp()). 64 bits: it does not
     * wrap in practice, so stamps compare with a plain `>`. */
    uint64_t lines_recycled;
    uint64_t primary_lines_recycled;
    uint32_t epoch;
    /* Counters the AI command capture compares between a command's start and
     * its end (run_command, spec decision 10). reflow_seq: bumped by every
     * term_resize() that commits, beside epoch -- a failed resize leaves it,
     * and the alternate screen does not touch it. region_scroll_seq: bumped
     * whenever PRIMARY-screen rows shift in place instead of being recycled
     * into scrollback (a scroll region, every scroll down), the events after
     * which absolute line numbers stop meaning the same rows. Scrolls on the
     * alternate screen are not counted: it has rows of its own, and the
     * capture reads the alternate-screen flag itself. */
    uint32_t reflow_seq;
    uint32_t region_scroll_seq;
    uint64_t write_gen;

    /* The debug log's session id (the "s3" in a log line; 0 prints "-").
     * Set by whoever owns the session (window.c) right after term_init();
     * the buffer and parser events (term.resize, term.alt_screen.*,
     * term.clear) carry it. */
    unsigned log_id;
} Terminal;

Terminal *term_init(int rows, int cols, int max_scrollback);
void term_free(Terminal *term);
/* Resize to rows x cols, reflowing the text. Returns false only when an
 * allocation failed, and then the Terminal is exactly as it was (callers keep
 * their display buffer and the remote end at the old size too). A NULL term, a
 * size of 0 or less, and the size it already has return true: nothing to do. */
bool term_resize(Terminal *term, int rows, int cols);
void term_process(Terminal *term, const char *data, size_t len);
void term_scroll(Terminal *term);
void term_scroll_up(Terminal *term, int top, int bot, int n);
void term_scroll_down(Terminal *term, int top, int bot, int n);
/* Switch to the alternate screen. Fallible: when its rows cannot be allocated
 * the terminal stays on the primary screen, unchanged, and the matching
 * term_alt_screen_exit() does nothing. */
void term_alt_screen_enter(Terminal *term);
void term_alt_screen_exit(Terminal *term);

/* Convert an xterm 256-colour palette index to 0xRRGGBB.
 * Indices 0-15:   standard ANSI colours.
 * Indices 16-231: 6×6×6 colour cube.
 * Indices 232-255: 24-step greyscale ramp.
 */
uint32_t color256_to_rgb(uint8_t index);

/* The allocator behind every allocation buffer.c lets fail without ending the
 * process: a scrollback row, a reflow, the alternate screen. Tests only (a
 * fake that counts calls and fails the n-th, as tests/fake_dpapi.c does for
 * DPAPI): no product code calls it. NULL restores calloc. The function must
 * return zeroed memory, like calloc, that free() can release: buffer.c relies
 * on a zeroed slot array being all NULL. */
typedef void *(*TermCallocFn)(size_t nmemb, size_t size);
void term_set_calloc(TermCallocFn fn);

/* Clear the dirty flag on all visible rows.  Call after painting. */
void term_clear_dirty(Terminal *term);

/* Return true if any visible row has its dirty flag set. */
bool term_has_dirty_rows(Terminal *term);

/* How many visible rows are dirty. When flags is non-NULL the first
 * min(rows, max) entries are filled, 1 = dirty (the issue snapshot and the
 * render log events use this). NULL term returns 0. */
int term_dirty_row_flags(const Terminal *term, unsigned char *flags, int max);

/* Mark every row dirty so the renderer repaints everything.
 * Call after font/zoom/resize changes that affect pixel layout. */
void term_mark_all_dirty(Terminal *term);

/* The cursor row's text up to the cursor (what term_at_prompt() tests). 1 with
 * buf filled, 0 when there is no such line (alt screen, cursor off-screen, text
 * after the cursor). */
int term_cursor_row_prompt_text(const Terminal *term, char *buf, size_t buf_size);

/* Is the terminal sitting at a shell prompt, ready for a new command?
 * True when: the primary screen is active (not the alt screen), the
 * cursor is inside the screen, nothing follows the cursor on its row,
 * and the row's text up to the cursor ends in a shell-prompt character
 * (see shell_prompt_line() in src/core/shell_prompt.h). */
int term_at_prompt(const Terminal *term);

/* Same cursor-row text as term_at_prompt(), tested against
 * shell_prompt_is_continuation() instead: true when the shell is sitting
 * at a continuation prompt (an open quote, heredoc, etc.), waiting for the
 * rest of a multi-line command rather than a new one. */
int term_at_continuation_prompt(const Terminal *term);

/* Same cursor-row text as term_at_prompt(), tested against the stricter
 * shell_prompt_line_unambiguous() instead: true only when the row is a
 * bare prompt with nothing else typed on it, safe for a caller that will
 * not clear the line first to append a command to. See
 * dispatch_line_clear.h and shell_prompt_line_unambiguous(). */
int term_at_unambiguous_prompt(const Terminal *term);

/* Same cursor-row text as term_at_prompt(), tested against
 * shell_prompt_is_windows() instead: true when the shell is currently
 * sitting at a PowerShell or cmd.exe prompt, whatever the session's kind
 * or configured shell name -- an SSH session to a Windows host, or a
 * pwsh/cmd started as a nested shell inside a local Git bash/MSYS2
 * session. Note this can be true even when term_at_unambiguous_prompt()
 * is false for the same row (a command paused mid-line under a "PS
 * ...>" prompt still "is a Windows prompt"); the two answer different
 * questions -- see dispatch_line_clear.h. */
int term_cursor_row_is_windows_prompt(const Terminal *term);

/* The physical ring-buffer index (into Terminal.lines[]) holding screen
 * row `screen_row` -- 0-based, 0 is the top of the currently visible
 * screen. The one mapping every screen-row consumer in this codebase
 * shares (src/term/buffer.c's internal callers, term_cursor_row_text()
 * behind term_at_prompt() and friends, src/term/parser.c's
 * get_screen_row()) -- or, for ai_chat_dispatch.c's dispatch-stall diagnostic,
 * should. (The renderer and the selection show the view as scrolled and
 * use term_view_to_logical() below instead.) A second, hand-written
 * copy of this mapping is exactly how term_cursor_row_text() used to
 * disagree with it (see the "sparse state" note on
 * term_logical_rows() below). Does not itself bound-check the result
 * against anything but lines_capacity (the modulo); screen_row must
 * already be known valid (0 <= screen_row < term->rows). The live part of the
 * ring (term_logical_rows() below) always covers the visible screen, so the
 * slot it names holds a row. */
int term_screen_to_phys(const Terminal *term, int screen_row);

/* The cursor's absolute line: lines_recycled plus the cursor's logical index
 * (the same top term_screen_to_phys() uses). scrollback_offset never changes
 * it. 0 for NULL. */
uint64_t term_abs_cursor_line(const Terminal *term);

/* Logical index of the row shown at screen_row in the view as scrolled
 * (scrollback_offset applied, top clamped to 0 exactly as the renderer's
 * get_visible_row did). Never -1 for 0 <= screen_row < rows; -1 outside.
 * The view mapping: the renderer and the selection use it. The parser keeps
 * term_screen_to_phys(), the live mapping -- writes always land on the live
 * screen, scrolled back or not (spec 2026-10-20-putty-selection-design.md 2.1). */
int term_view_to_logical(const Terminal *t, int screen_row);

/* The furthest the view can scroll back: the history actually held,
 * min(lines_count - rows, max_scrollback), never below 0. The wheel, the
 * scrollbar, Page Up, smart scrolling and term_resize() all clamp to it. */
int term_max_scroll_offset(const Terminal *t);

/* The number of logical rows [0, this) the terminal currently holds real,
 * readable content for: every row of scrolled history (lines_count), or,
 * in the sparse state right after a resize (lines_count < rows --
 * term_resize()/term_reflow_buffer() can leave a freshly resized,
 * not-yet-scrolled window with fewer committed rows than the screen has,
 * even though every row of the new screen is allocated, filled and
 * written to -- see term_screen_to_phys()'s comment above), every row of
 * the current screen instead, since those are real too.
 *
 * This is also the live part of the ring: the slots from lines_start, modulo
 * lines_capacity, that hold rows. Every slot in it holds a row, term->cols
 * cells wide, so the visible screen always lies inside it; every slot outside
 * it is NULL (the rows are allocated as lines are kept, not up front).
 *
 * A logical index at or past this bound has never been written, or wraps
 * (via the ring buffer's modulo) into a slot that belongs to something
 * else entirely. Every consumer that walks *logical* rows by hand --
 * scrolled history, a selection, the text handed to the AI model, "last N
 * lines" -- must use this instead of comparing against lines_count
 * directly, or it inherits the exact bug this replaced: a fresh,
 * never-scrolled window whose cursor sits past lines_count read as
 * "nothing here" forever, which is what made term_at_prompt() false
 * forever after a resize until enough output happened to scroll the
 * buffer full. (A *screen-row* consumer doesn't need this at all --
 * term_screen_to_phys() above has no such bound because it needs none: the
 * live part always covers the screen, so the slot it names holds a row.) */
static inline int term_logical_rows(const Terminal *term) {
    return term->lines_count > term->rows ? term->lines_count : term->rows;
}

/* Stamp a row as written now. The parser calls it only from get_screen_row(),
 * the one place every parser write passes through; buffer.c calls it for the
 * rows a scroll moves, clears or recycles. Cursor motion never stamps. */
static inline void term_row_stamp(Terminal *term, TermRow *row) {
    row->gen = ++term->write_gen;
}

#endif