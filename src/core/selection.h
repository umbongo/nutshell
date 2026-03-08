#ifndef NUTSHELL_SELECTION_H
#define NUTSHELL_SELECTION_H

#include "term.h"
#include <stddef.h>
#include <stdbool.h>

/* Selection state for click-drag text selection */
typedef struct {
    int start_row;  /* terminal row (0-based from top of visible area) */
    int start_col;  /* terminal column */
    int end_row;
    int end_col;
    bool active;    /* true while mouse is held down */
    bool valid;     /* true if a selection exists (start != end) */
} Selection;

/* Convert pixel coordinates to terminal cell coordinates.
 * px, py:       pixel position relative to the window client area
 * char_w:       character cell width in pixels
 * char_h:       character cell height in pixels
 * y_offset:     vertical offset (e.g. TAB_HEIGHT) before terminal area
 * term_rows:    number of terminal rows
 * term_cols:    number of terminal columns
 * out_row/col:  output cell coordinates (clamped to valid range)
 */
void selection_pixel_to_cell(int px, int py, int char_w, int char_h,
                             int y_offset, int term_rows, int term_cols,
                             int *out_row, int *out_col);

/* Extract selected text from the terminal as a UTF-8 string.
 * sel:      selection (start/end row/col in visible coordinates)
 * term:     terminal to read from
 * buf:      output buffer
 * buf_size: size of output buffer
 * Returns bytes written (excluding NUL). */
size_t selection_extract_text(const Selection *sel, const Terminal *term,
                              char *buf, size_t buf_size);

/* Normalise selection so start <= end (for left-to-right extraction). */
void selection_normalise(const Selection *sel, int *r0, int *c0, int *r1, int *c1);

#endif /* NUTSHELL_SELECTION_H */
