#ifndef NUTSHELL_SELECTION_H
#define NUTSHELL_SELECTION_H

#include "term.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* Terminal text selection, PuTTY style (spec
 * docs/superpowers/specs/2026-10-20-putty-selection-design.md). Pure: it
 * reads a const Terminal and never draws or touches the clipboard; window.c
 * owns the mouse capture, the timer and the clipboard.
 *
 * Points are buffer-absolute: `line` is the terminal's absolute line number
 * (lines_recycled + logical index, term.h), stable while the terminal's epoch
 * is, so a selection survives scrolling the view and new output scrolling
 * the text. */
typedef struct { uint64_t line; int col; } SelPoint;
typedef enum { SEL_STREAM = 0, SEL_WORD = 1, SEL_LINE = 2 } SelMode;

typedef struct {
    SelPoint anchor, anchor_end; /* the unit under the press (a cell for STREAM) */
    SelPoint start, end;         /* normalised, both inclusive */
    SelMode  mode;
    uint32_t epoch;              /* the terminal's at the last change */
    uint64_t gen;                /* the terminal's write_gen at the last change */
    bool     active;             /* button held */
    bool     valid;              /* a non-empty selection exists */
    int      press_x, press_y;   /* client pixels of the press, drag threshold */
    int      last_x, last_y;     /* last reported pointer, auto-scroll */
} Selection;

/* Client pixel (px, py) to a screen cell. px is relative to the terminal's
 * left edge, py to the client top (y_offset is where the grid starts).
 * Signed: a captured drag above or left of the window gives negative values.
 * Clamped to [0, rows) x [0, cols); a zero cell size gives cell 0. */
void selection_pixel_to_cell(int px, int py, int char_w, int char_h,
                             int y_offset, int rows, int cols,
                             int *out_row, int *out_col);

/* Lines the view should scroll this tick for a drag whose stored pointer is
 * at py: negative (back into history) above top_px, positive at or below
 * bottom_px (exclusive), 0 inside; magnitude 1 + distance / char_h. */
int  selection_autoscroll_step(int py, int top_px, int bottom_px, int char_h);

/* The press: clears s, then starts a selection at the screen cell
 * (click_count 1 a cell, 2 a word, 3 a line) and records the press pixel. */
void selection_begin(Selection *s, const Terminal *t, int screen_row, int col,
                     int click_count, int px, int py);
/* Shift+click / middle-click: keep the selection and its mode, move one end to
 * the screen cell and make it a live drag again (active = true, press and last
 * pixels reset, no drag threshold). Which end moves is PuTTY's rule: the click
 * before start moves start; after end moves end; inside moves the nearer end
 * (nearer by line, then by column). The other end becomes the anchor unit.
 * With no valid selection (or a different epoch) it is selection_begin() with
 * click_count 1. */
void selection_extend_from(Selection *s, const Terminal *t, int screen_row, int col,
                           int px, int py);
/* The click count for a press: `within` is true when the press is inside the
 * double-click time and rectangle of the previous press; the count cycles
 * 1, 2, 3, 1, ... (PuTTY), so a fourth rapid click selects a cell again. */
int selection_click_count(int prev_count, bool within);
/* A move while the button is held. A STREAM press becomes a selection only
 * once the pointer has moved drag_cx/drag_cy pixels on either axis. */
void selection_extend(Selection *s, const Terminal *t, int screen_row, int col,
                      int px, int py, int drag_cx, int drag_cy);
/* The release: ends the drag and takes the terminal's stamps, so what is on
 * screen now is what is copied. Returns valid. */
bool selection_finish(Selection *s, const Terminal *t);
void selection_clear(Selection *s);
/* From the oldest line held to the last non-empty line; valid = false on an
 * empty terminal. */
void selection_select_all(Selection *s, const Terminal *t);

/* Bring a selection up to date with the terminal (spec section 2.3). Trims
 * the four points below lines_recycled; clears the selection when the epoch
 * changed (ending an active drag), when every selected line is recycled, or
 * -- after the release only -- when a selected row was written since. The
 * window calls it every timer tick and before every copy. Returns true on
 * any change. */
bool selection_sync(Selection *s, const Terminal *t);
/* Is the cell at screen (row, col), in the view as scrolled, selected? */
bool selection_contains(const Selection *s, const Terminal *t, int screen_row, int col);
/* True when a valid selection intersects the rows in view (renderer hint). */
bool selection_in_view(const Selection *s, const Terminal *t);
/* The selected text: a soft-wrapped line is one line, trailing blanks trimmed
 * per logical line, trailing empty rows dropped, CRLF between lines and none
 * after the last. Always a heap string the caller frees ("" at least). */
char *selection_copy_text(const Selection *s, const Terminal *t, size_t *out_len);

/* Word classes for a double-click: 0 blank (space, empty cell, control
 * codepoint); 1 word (letters, digits, _ - . / ~ : @); 2 + cp otherwise, so
 * every other codepoint is a class of its own. "Letters" are ASCII plus
 * Latin-1 Supplement and Latin Extended-A/B (not U+00D7 or U+00F7), IPA,
 * Greek and Cyrillic; CJK and the rest stand alone (one codepoint per cell,
 * no wide-cell model). */
int  selection_char_class(uint32_t cp);
/* Combining marks: U+0300-036F, U+1AB0-1AFF, U+1DC0-1DFF, U+20D0-20FF and
 * U+FE20-FE2F. Each sits in a cell of its own, but a double-click treats it
 * as part of the cell before it. selection_char_class() is unchanged: a mark
 * alone keeps its own class. */
bool selection_is_combining(uint32_t cp);

#endif /* NUTSHELL_SELECTION_H */
