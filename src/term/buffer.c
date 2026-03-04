#include "term.h"
#include "xmalloc.h"
#include <string.h>
#include <stdlib.h>

static TermRow *term_row_alloc(int cols) {
    TermRow *row = xmalloc(sizeof(TermRow));
    row->cells = xcalloc((size_t)cols, sizeof(TermCell));
    row->len = 0;
    row->dirty = true;
    row->wrapped = false;
    return row;
}

static void term_row_free(TermRow *row) {
    if (row) {
        free(row->cells);
        free(row);
    }
}

static void term_row_fill(TermRow *row, int cols, TermAttr attr) {
    for (int i = 0; i < cols; i++) {
        row->cells[i].codepoint = 0; // Empty
        row->cells[i].attr = attr;
    }
    row->len = 0;
    row->dirty = true;
    row->wrapped = false;
}

Terminal *term_init(int rows, int cols, int max_scrollback) {
    Terminal *term = xmalloc(sizeof(Terminal));
    term->rows = rows;
    term->cols = cols;
    term->max_scrollback = max_scrollback;
    term->lines_capacity = rows + max_scrollback;
    term->lines_start = 0;
    term->lines_count = rows; // Start with a blank screen
    term->scrollback_offset = 0;
    
    term->cursor.row = 0;
    term->cursor.col = 0;
    term->cursor.visible = true;
    
    memset(&term->current_attr, 0, sizeof(TermAttr));
    /* fg_mode = COLOR_DEFAULT (0) and bg_mode = COLOR_DEFAULT (0) from memset —
     * the renderer will substitute the configured scheme colours. */

    term->state = TERM_STATE_NORMAL;
    term->csi_param_count = 0;
    term->csi_private = false;
    term->osc_len = 0;

    term->saved_cursor.row = 0;
    term->saved_cursor.col = 0;
    term->saved_cursor.visible = true;

    term->utf8_codepoint = 0;
    term->utf8_remaining = 0;

    term->title[0]         = '\0';
    term->app_cursor_keys  = false;
    term->insert_mode      = false;
    term->alt_screen_active = false;
    term->primary_lines    = NULL;

    term->lines = xcalloc((size_t)term->lines_capacity, sizeof(TermRow *));
    
    // Pre-allocate all rows to avoid allocation during runtime
    for (int i = 0; i < term->lines_capacity; i++) {
        term->lines[i] = term_row_alloc(cols);
        term_row_fill(term->lines[i], cols, term->current_attr);
    }

    return term;
}

void term_free(Terminal *term) {
    if (!term) return;

    if (term->lines) {
        for (int i = 0; i < term->lines_capacity; i++)
            term_row_free(term->lines[i]);
        free(term->lines);
    }
    /* When alt screen is active, primary_lines holds the saved primary buffer */
    if (term->primary_lines) {
        for (int i = 0; i < term->primary_lines_capacity; i++)
            term_row_free(term->primary_lines[i]);
        free(term->primary_lines);
    }
    free(term);
}

/* Helper to get a pointer to a specific logical line index (0 = oldest) */
static TermRow *get_logical_row(Terminal *term, int logical_idx) {
    if (logical_idx < 0 || logical_idx >= term->lines_count) return NULL;
    int physical_idx = (term->lines_start + logical_idx) % term->lines_capacity;
    return term->lines[physical_idx];
}

void term_resize(Terminal *term, int rows, int cols) {
    if (!term || rows <= 0 || cols <= 0) return;
    if (term->rows == rows && term->cols == cols) return;

    /* 1. Create a new line buffer */
    int new_capacity = rows + term->max_scrollback;
    TermRow **new_lines = xcalloc((size_t)new_capacity, sizeof(TermRow *));
    
    /* Pre-allocate rows in the new buffer */
    for (int i = 0; i < new_capacity; i++) {
        new_lines[i] = term_row_alloc(cols);
        term_row_fill(new_lines[i], cols, term->current_attr);
    }

    /* 2. Reflow content from old buffer to new buffer */
    int new_count = 0;
    int current_new_row_idx = 0;
    int current_new_col_idx = 0;

    /* We need to track the cursor's new position */
    int old_cursor_row = term->cursor.row;
    int old_cursor_col = term->cursor.col;
    /* Calculate logical index of the cursor row in the old buffer */
    /* The screen is at the end of the buffer. 
       Top of screen = lines_count - term->rows (if full) or 0. */
    int old_screen_top = (term->lines_count >= term->rows) ? (term->lines_count - term->rows) : 0;
    int cursor_logical_idx = old_screen_top + old_cursor_row;
    
    int new_cursor_row = 0;
    int new_cursor_col = 0;

    for (int i = 0; i < term->lines_count; i++) {
        TermRow *old_row = get_logical_row(term, i);
        if (!old_row) continue;

        /* If this old row was NOT wrapped, it starts a new line in the new buffer.
           Otherwise, we continue appending to the current new line. */
        if (!old_row->wrapped && i > 0) {
            current_new_row_idx++;
            current_new_col_idx = 0;
        }

        /* Ensure we don't overflow the new buffer capacity (drop oldest lines if needed) */
        if (current_new_row_idx >= new_capacity) {
            /* Shift everything up? Or just circular buffer logic? 
               Since we are rebuilding linearly, let's just clamp. 
               In a real reflow, we might drop history. */
             // For simplicity in this pass, we won't implement complex history dropping during resize
             // beyond capacity. We'll just stop filling if full, or wrap around if we implemented ring logic here.
             // But since we are allocating a fresh linear buffer, let's just stop.
             break; 
        }

        /* Only copy actual content cells — trailing empty cells (codepoint==0 beyond
         * the row's written length) must not be reflowed, as they would create
         * spurious wraps and incorrect row counts when resizing. */
        int cell_limit = old_row->len < term->cols ? old_row->len : term->cols;

        /* Copy cells */
        for (int c = 0; c < cell_limit; c++) {
            /* Check if we hit the cursor position */
            if (i == cursor_logical_idx && c == old_cursor_col) {
                new_cursor_row = current_new_row_idx;
                new_cursor_col = current_new_col_idx;
            }

            const TermCell *cell = &old_row->cells[c];

            /* Append to new buffer */
            new_lines[current_new_row_idx]->cells[current_new_col_idx] = *cell;
            new_lines[current_new_row_idx]->len = current_new_col_idx + 1;

            current_new_col_idx++;

            /* Wrap if we hit new width */
            if (current_new_col_idx >= cols) {
                current_new_row_idx++;
                current_new_col_idx = 0;
                if (current_new_row_idx >= new_capacity) break;
                new_lines[current_new_row_idx]->wrapped = true; // Continuation line
            }
        }

        /* Check if cursor was at/past the content end (not caught in loop) */
        if (i == cursor_logical_idx && old_cursor_col >= cell_limit) {
            new_cursor_row = current_new_row_idx;
            new_cursor_col = current_new_col_idx;
        }
    }
    new_count = current_new_row_idx + 1;
    if (current_new_col_idx == 0 && new_count > 0) new_count--; // Trailing empty row

    /* 3. Swap buffers */
    for (int i = 0; i < term->lines_capacity; i++) {
        term_row_free(term->lines[i]);
    }
    free(term->lines);

    term->lines = new_lines;
    term->lines_capacity = new_capacity;
    term->lines_start = 0; // We rebuilt it linearly starting at 0
    term->lines_count = (new_count > new_capacity) ? new_capacity : new_count;
    
    term->rows = rows;
    term->cols = cols;
    
    /* 4. Update cursor */
    /* The new cursor row is relative to the start of the buffer. 
       We need to convert it to screen coordinates (relative to top of visible screen). */
    int new_screen_top = (term->lines_count >= term->rows) ? (term->lines_count - term->rows) : 0;
    term->cursor.row = new_cursor_row - new_screen_top;
    term->cursor.col = new_cursor_col;
    
    /* Clamp cursor to be safe */
    if (term->cursor.row < 0) term->cursor.row = 0;
    if (term->cursor.row >= term->rows) term->cursor.row = term->rows - 1;
    if (term->cursor.col < 0) term->cursor.col = 0;
    if (term->cursor.col >= term->cols) term->cursor.col = term->cols - 1;
}

void term_scroll(Terminal *term) {
    if (term->lines_count < term->lines_capacity) {
        /* Buffer not full: append new line */
        int new_idx = (term->lines_start + term->lines_count) % term->lines_capacity;
        term_row_fill(term->lines[new_idx], term->cols, term->current_attr);
        term->lines_count++;
    } else {
        /* Buffer full: recycle oldest line */
        int recycle_idx = term->lines_start;
        term_row_fill(term->lines[recycle_idx], term->cols, term->current_attr);
        term->lines_start = (term->lines_start + 1) % term->lines_capacity;
    }
}

void term_alt_screen_enter(Terminal *term)
{
    if (!term || term->alt_screen_active) return;

    /* Save primary buffer state */
    term->primary_lines           = term->lines;
    term->primary_lines_capacity  = term->lines_capacity;
    term->primary_lines_count     = term->lines_count;
    term->primary_lines_start     = term->lines_start;
    term->primary_cursor          = term->cursor;

    /* Allocate a fresh alt-screen buffer (rows only, no scrollback) */
    int cap = term->rows;
    term->lines = xcalloc((size_t)cap, sizeof(TermRow *));
    for (int i = 0; i < cap; i++) {
        term->lines[i] = term_row_alloc(term->cols);
        term_row_fill(term->lines[i], term->cols, term->current_attr);
    }
    term->lines_capacity  = cap;
    term->lines_count     = cap;
    term->lines_start     = 0;
    term->cursor.row      = 0;
    term->cursor.col      = 0;
    term->alt_screen_active = true;
}

void term_alt_screen_exit(Terminal *term)
{
    if (!term || !term->alt_screen_active) return;

    /* Free the alt-screen lines */
    for (int i = 0; i < term->lines_capacity; i++)
        term_row_free(term->lines[i]);
    free(term->lines);

    /* Restore primary buffer */
    term->lines           = term->primary_lines;
    term->lines_capacity  = term->primary_lines_capacity;
    term->lines_count     = term->primary_lines_count;
    term->lines_start     = term->primary_lines_start;
    term->cursor          = term->primary_cursor;
    term->primary_lines   = NULL;
    term->alt_screen_active = false;
}