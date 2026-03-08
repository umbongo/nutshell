#ifndef NUTSHELL_DISPLAY_BUFFER_H
#define NUTSHELL_DISPLAY_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

/* A snapshot of one cell as last painted.  Used for cell-level dirty
 * detection: if the current cell matches the shadow, skip redrawing. */
typedef struct {
    uint32_t codepoint;
    uint32_t fg;       /* resolved COLORREF (0x00BBGGRR) */
    uint32_t bg;       /* resolved COLORREF (0x00BBGGRR) */
    uint8_t  flags;    /* attribute flags (bold, reverse, etc.) */
} DisplayCell;

typedef struct {
    DisplayCell *cells;   /* rows * cols flat array (row-major) */
    int rows;
    int cols;
} DisplayBuffer;

/* Initialise a display buffer for the given dimensions.
 * All cells are zeroed (will compare as dirty against any real content). */
void dispbuf_init(DisplayBuffer *db, int rows, int cols);

/* Free the cells array and zero the struct. */
void dispbuf_free(DisplayBuffer *db);

/* Resize the buffer, zeroing all cells (forces full repaint). */
void dispbuf_resize(DisplayBuffer *db, int rows, int cols);

/* Mark the entire buffer as dirty by setting all cells to a sentinel
 * that cannot match real content (codepoint = 0xFFFFFFFF). */
void dispbuf_invalidate(DisplayBuffer *db);

/* Compare a cell against the shadow.  Returns true if the cell is clean
 * (i.e., codepoint, fg, bg, and flags all match). */
static inline bool dispbuf_cell_clean(const DisplayBuffer *db,
                                       int row, int col,
                                       uint32_t codepoint,
                                       uint32_t fg, uint32_t bg,
                                       uint8_t flags)
{
    const DisplayCell *c = &db->cells[row * db->cols + col];
    return c->codepoint == codepoint && c->fg == fg &&
           c->bg == bg && c->flags == flags;
}

/* Update the shadow after drawing a cell. */
static inline void dispbuf_cell_update(DisplayBuffer *db,
                                        int row, int col,
                                        uint32_t codepoint,
                                        uint32_t fg, uint32_t bg,
                                        uint8_t flags)
{
    DisplayCell *c = &db->cells[row * db->cols + col];
    c->codepoint = codepoint;
    c->fg = fg;
    c->bg = bg;
    c->flags = flags;
}

#endif
