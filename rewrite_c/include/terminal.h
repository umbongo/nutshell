#ifndef TERMINAL_H
#define TERMINAL_H

/*
 * VT100/ANSI terminal emulator.
 *
 * Architecture:
 *   - buffer.c : screen grid, scrollback ring buffer, resize, scroll ops.
 *   - parser.c : byte-stream state machine → writes to the screen.
 *
 * Typical usage:
 *   Terminal t;
 *   term_init(&t, 24, 80, 3000);
 *   term_process(&t, data, len);          // feed SSH channel output
 *   const Cell *c = term_get_cell(&t, row, col);  // read for rendering
 *   term_free(&t);
 */

#include <stddef.h>
#include <stdint.h>

/* ---- Limits ---------------------------------------------------------------- */

#define TERM_SCROLLBACK_MAX  ((size_t)50000)
#define TERM_SCROLLBACK_DEF  ((size_t)3000)
#define TERM_COLS_MAX        500
#define TERM_ROWS_MAX        300

/* ---- Cell attributes ------------------------------------------------------- */

/* Sentinel value stored in Cell.fg / Cell.bg to mean "use terminal default". */
#define CELL_COLOR_DEFAULT   256u

/* Bit flags for Cell.attrs and Terminal.pen_attrs. */
#define ATTR_BOLD      ((uint8_t)(1u << 0))
#define ATTR_DIM       ((uint8_t)(1u << 1))
#define ATTR_UNDERLINE ((uint8_t)(1u << 2))
#define ATTR_BLINK     ((uint8_t)(1u << 3))
#define ATTR_REVERSE   ((uint8_t)(1u << 4))
#define ATTR_INVIS     ((uint8_t)(1u << 5))

/* Maximum number of CSI numeric parameters that are tracked. */
#define CSI_PARAMS_MAX 16

/* ---- Cell ------------------------------------------------------------------ */

/*
 * A single character cell in the terminal grid.
 *
 * ch   : Unicode code point (0 treated as space by the renderer).
 * fg   : foreground colour 0-255 (256-colour palette) or CELL_COLOR_DEFAULT.
 * bg   : background colour 0-255 or CELL_COLOR_DEFAULT.
 * attrs: ATTR_* bitmask.
 */
typedef struct {
    uint32_t ch;
    uint16_t fg;
    uint16_t bg;
    uint8_t  attrs;
} Cell;

/* ---- Scrollback row ------------------------------------------------------- */

/* One captured screen row in the scrollback ring buffer. */
typedef struct {
    Cell *cells;   /* heap-allocated array of `cols` cells */
    int   cols;    /* number of cells (may differ from current screen width) */
} TermRow;

/* ---- Terminal -------------------------------------------------------------- */

typedef struct {
    /* Active screen: screen[r] is a heap-allocated Cell[cols] array. */
    Cell **screen;
    int    rows;
    int    cols;

    /* Scrollback ring buffer (oldest at scroll_head). */
    TermRow *scroll_buf;
    size_t   scroll_head;   /* index of oldest valid entry */
    size_t   scroll_count;  /* number of valid entries */
    size_t   scroll_cap;    /* allocated capacity */

    /* Cursor position (0-based). */
    int cx, cy;
    int cx_saved, cy_saved;

    /* Scrolling region (0-based, inclusive; default = full screen). */
    int scroll_top;
    int scroll_bot;

    /* Current drawing pen. */
    uint16_t pen_fg;
    uint16_t pen_bg;
    uint8_t  pen_attrs;

    /* Mode flags. */
    int auto_wrap;   /* DECAWM: wrap at right margin (default: on) */

    /* Parser state machine (values defined in parser.c). */
    int ps;                         /* current state (PS_NORMAL=0) */
    int csi_params[CSI_PARAMS_MAX]; /* -1 = absent */
    int csi_nparams;
    int csi_intermed;               /* last intermediate/private-marker byte */

    /* OSC string accumulation. */
    char osc_buf[256];
    int  osc_len;
} Terminal;

/* ---- buffer.c API --------------------------------------------------------- */

/*
 * Initialise a terminal with the given dimensions and scrollback capacity.
 * Dimensions are clamped to [1, TERM_*_MAX]; scrollback to [1, TERM_SCROLLBACK_MAX].
 */
void term_init(Terminal *t, int rows, int cols, int scrollback);

/* Free all heap memory owned by t.  Safe to call on a zero-initialised struct. */
void term_free(Terminal *t);

/*
 * Resize the active screen to new_rows × new_cols.
 * Existing content is preserved (truncated or padded).  Cursor is clamped.
 * The scrollback buffer is unchanged.
 */
void term_resize(Terminal *t, int new_rows, int new_cols);

/*
 * Clear the active screen, reset cursor and pen to defaults, reset parser state.
 * Does NOT clear the scrollback buffer.
 */
void term_reset(Terminal *t);

/*
 * Scroll the region [top..bot] (0-based, inclusive) up by n lines.
 * Lines that fall off the top of the primary scroll region (top == 0) are
 * saved into the scrollback buffer.
 */
void term_scroll_up(Terminal *t, int top, int bot, int n);

/*
 * Scroll the region [top..bot] down by n lines.
 * No scrollback capture; vacated rows at the top are cleared.
 */
void term_scroll_down(Terminal *t, int top, int bot, int n);

/*
 * Return a pointer to the cell at (row, col) on the active screen.
 * Returns NULL if row or col is out of range.
 */
const Cell *term_get_cell(const Terminal *t, int row, int col);

/*
 * Return a pointer to scrollback line idx (0 = most recent, count-1 = oldest).
 * Returns NULL if idx >= scroll_count.
 * The returned pointer is valid until the next call that modifies the terminal.
 */
const Cell *term_get_scroll_line(const Terminal *t, size_t idx);

/* Return the column count of scrollback line idx, or 0 if out of range. */
int term_get_scroll_line_cols(const Terminal *t, size_t idx);

/* ---- parser.c API --------------------------------------------------------- */

/*
 * Feed `len` bytes of raw terminal output (from the SSH channel) into the
 * terminal state machine.  Updates the screen, cursor, and attributes.
 */
void term_process(Terminal *t, const char *data, size_t len);

#endif /* TERMINAL_H */
