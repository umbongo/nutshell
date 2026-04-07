#include "term.h"
#include "xmalloc.h"
#include <assert.h>
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
    term->scroll_top       = 0;
    term->scroll_bot       = rows - 1;
    term->alt_screen_active   = false;
    term->primary_lines       = NULL;
    term->full_redraw_needed  = false;
    term->bracketed_paste_mode = false;

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

/* Reflow old_lines (circular buffer, old_cols wide) into a newly-allocated
 * linear buffer at new_rows × new_cols.  Frees old_lines.
 * Cursor position is passed as screen-relative (row from top of visible area)
 * and returned as screen-relative in the new geometry. */
static void term_reflow_buffer(
    TermRow **old_lines, int old_capacity, int old_start, int old_count,
    int old_cols, int old_rows,
    int cur_row, int cur_col,        /* screen-relative cursor in */
    int new_rows, int new_cols, int max_scrollback,
    TermAttr fill_attr,
    TermRow ***out_lines, int *out_capacity, int *out_count, int *out_start,
    int *out_cursor_row, int *out_cursor_col)
{
    int new_capacity = new_rows + max_scrollback;
    TermRow **new_lines = xcalloc((size_t)new_capacity, sizeof(TermRow *));
    for (int i = 0; i < new_capacity; i++) {
        new_lines[i] = term_row_alloc(new_cols);
        term_row_fill(new_lines[i], new_cols, fill_attr);
    }

    int cur_new_row = 0;
    int cur_new_col = 0;
    int new_cursor_row = 0;
    int new_cursor_col = 0;

    /* Convert screen-relative cursor to buffer-absolute logical index */
    int old_screen_top = (old_count >= old_rows) ? (old_count - old_rows) : 0;
    int cursor_logical = old_screen_top + cur_row;

    for (int i = 0; i < old_count; i++) {
        int pi = (old_start + i) % old_capacity;
        TermRow *old_row = old_lines[pi];
        if (!old_row) continue;

        if (!old_row->wrapped && i > 0) {
            cur_new_row++;
            cur_new_col = 0;
        }
        if (cur_new_row >= new_capacity) break;

        /* Only reflow up to the written content width */
        int cell_limit = old_row->len < old_cols ? old_row->len : old_cols;

        for (int c = 0; c < cell_limit; c++) {
            if (i == cursor_logical && c == cur_col) {
                new_cursor_row = cur_new_row;
                new_cursor_col = cur_new_col;
            }
            new_lines[cur_new_row]->cells[cur_new_col] = old_row->cells[c];
            new_lines[cur_new_row]->len = cur_new_col + 1;
            cur_new_col++;
            if (cur_new_col >= new_cols) {
                cur_new_row++;
                cur_new_col = 0;
                if (cur_new_row >= new_capacity) break;
                new_lines[cur_new_row]->wrapped = true;
            }
        }
        if (i == cursor_logical && cur_col >= cell_limit) {
            new_cursor_row = cur_new_row;
            new_cursor_col = cur_new_col;
        }
    }

    int new_count = cur_new_row + 1;
    if (new_count > new_capacity) new_count = new_capacity;

    /* Strip trailing empty rows, keeping at least the cursor row */
    {
        int min_count = new_cursor_row + 1;
        if (min_count < 1) min_count = 1;
        while (new_count > min_count && new_lines[new_count - 1]->len == 0)
            new_count--;
    }

    /* Free old buffer */
    for (int i = 0; i < old_capacity; i++)
        term_row_free(old_lines[i]);
    free(old_lines);

    /* Push content to bottom on expansion: prepend empty rows so the cursor
     * stays near the bottom rather than floating mid-screen. */
    int new_start = 0;
    if (new_rows > old_rows && old_count >= old_rows && new_count < new_rows) {
        int pad = new_rows - new_count;
        new_start = (new_capacity - pad) % new_capacity;
        for (int i = 0; i < pad; i++) {
            int idx = (new_start + i) % new_capacity;
            term_row_fill(new_lines[idx], new_cols, fill_attr);
        }
        new_cursor_row += pad;
        new_count = new_rows;
    }

    /* Convert cursor back to screen-relative */
    int screen_top = (new_count >= new_rows) ? (new_count - new_rows) : 0;
    new_cursor_row -= screen_top;
    if (new_cursor_row < 0)        new_cursor_row = 0;
    if (new_cursor_row >= new_rows) new_cursor_row = new_rows - 1;
    if (new_cursor_col < 0)        new_cursor_col = 0;
    if (new_cursor_col >= new_cols) new_cursor_col = new_cols - 1;

    *out_lines      = new_lines;
    *out_capacity   = new_capacity;
    *out_count      = new_count;
    *out_start      = new_start;
    *out_cursor_row = new_cursor_row;
    *out_cursor_col = new_cursor_col;
}

void term_resize(Terminal *term, int rows, int cols) {
    if (!term || rows <= 0 || cols <= 0) return;
    if (term->rows == rows && term->cols == cols) return;

    int old_rows = term->rows;
    int old_cols = term->cols;

    /* Reflow the active buffer (alt-screen or primary).
     * Alt-screen has no scrollback — allocate rows only. */
    term_reflow_buffer(
        term->lines, term->lines_capacity, term->lines_start, term->lines_count,
        old_cols, old_rows,
        term->cursor.row, term->cursor.col,
        rows, cols,
        term->alt_screen_active ? 0 : term->max_scrollback,
        term->current_attr,
        &term->lines, &term->lines_capacity, &term->lines_count, &term->lines_start,
        &term->cursor.row, &term->cursor.col);

    /* When on the alt screen, also reflow the saved primary buffer so that
     * term_alt_screen_exit() restores it at the correct dimensions. */
    if (term->alt_screen_active) {
        term_reflow_buffer(
            term->primary_lines, term->primary_lines_capacity,
            term->primary_lines_start, term->primary_lines_count,
            old_cols, old_rows,
            term->primary_cursor.row, term->primary_cursor.col,
            rows, cols, term->max_scrollback,
            term->current_attr,
            &term->primary_lines, &term->primary_lines_capacity,
            &term->primary_lines_count, &term->primary_lines_start,
            &term->primary_cursor.row, &term->primary_cursor.col);
    }

    term->rows = rows;
    term->cols = cols;
    term->scroll_top = 0;
    term->scroll_bot = rows - 1;
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

    /* Mark all visible rows dirty: each screen position now maps to a
     * different logical row, so the renderer must repaint every row.
     * When lines_count < rows (sparse content after resize), we must
     * also mark the pre-allocated rows beyond lines_count that are
     * still visible on screen; otherwise they never get repainted. */
    int vis_start = term->lines_count - term->rows;
    if (vis_start < 0) vis_start = 0;
    int vis_end = vis_start + term->rows;
    if (vis_end > term->lines_capacity) vis_end = term->lines_capacity;
    for (int i = vis_start; i < vis_end; i++) {
        int idx = (term->lines_start + i) % term->lines_capacity;
        term->lines[idx]->dirty = true;
    }
}

/* Helper: convert screen row (0-based) to physical ring-buffer index */
static int screen_to_phys(Terminal *term, int screen_row) {
    int top = (term->lines_count >= term->rows)
            ? (term->lines_count - term->rows) : 0;
    return (term->lines_start + top + screen_row) % term->lines_capacity;
}

void term_scroll_up(Terminal *term, int top, int bot, int n) {
    if (!term || top < 0 || bot >= term->rows || top >= bot || n <= 0)
        return;
    if (n > bot - top + 1) n = bot - top + 1;

    /* Full-screen scroll on primary buffer: delegate to term_scroll()
     * which handles scrollback ring-buffer extension. */
    if (top == 0 && bot == term->rows - 1 && !term->alt_screen_active) {
        for (int i = 0; i < n; i++)
            term_scroll(term);
        return;
    }

    /* Region scroll (or alt screen): pointer-swap within screen rows. */
    /* Save the n row pointers that will be recycled */
    TermRow *saved[64];
    assert(n <= 64 && "scroll_up: n exceeds saved[] capacity");
    if (n > 64) n = 64;  /* safety clamp */
    for (int i = 0; i < n; i++)
        saved[i] = term->lines[screen_to_phys(term, top + i)];

    /* Shift rows [top+n .. bot] up to [top .. bot-n] */
    for (int i = top; i <= bot - n; i++) {
        int dst = screen_to_phys(term, i);
        int src = screen_to_phys(term, i + n);
        term->lines[dst] = term->lines[src];
    }

    /* Place recycled (cleared) rows at [bot-n+1 .. bot] */
    for (int i = 0; i < n; i++) {
        int idx = screen_to_phys(term, bot - n + 1 + i);
        term->lines[idx] = saved[i];
        term_row_fill(saved[i], term->cols, term->current_attr);
    }

    /* Mark all rows in the region dirty */
    for (int i = top; i <= bot; i++)
        term->lines[screen_to_phys(term, i)]->dirty = true;
}

void term_scroll_down(Terminal *term, int top, int bot, int n) {
    if (!term || top < 0 || bot >= term->rows || top >= bot || n <= 0)
        return;
    if (n > bot - top + 1) n = bot - top + 1;

    /* Save the n row pointers that will be recycled (bottom of region) */
    TermRow *saved[64];
    assert(n <= 64 && "scroll_down: n exceeds saved[] capacity");
    if (n > 64) n = 64;  /* safety clamp */
    for (int i = 0; i < n; i++)
        saved[i] = term->lines[screen_to_phys(term, bot - n + 1 + i)];

    /* Shift rows [top .. bot-n] down to [top+n .. bot] */
    for (int i = bot - n; i >= top; i--) {
        int dst = screen_to_phys(term, i + n);
        int src = screen_to_phys(term, i);
        term->lines[dst] = term->lines[src];
    }

    /* Place recycled (cleared) rows at [top .. top+n-1] */
    for (int i = 0; i < n; i++) {
        int idx = screen_to_phys(term, top + i);
        term->lines[idx] = saved[i];
        term_row_fill(saved[i], term->cols, term->current_attr);
    }

    /* Mark all rows in the region dirty */
    for (int i = top; i <= bot; i++)
        term->lines[screen_to_phys(term, i)]->dirty = true;
}

void term_clear_dirty(Terminal *term)
{
    if (!term) return;
    for (int i = 0; i < term->lines_count; i++) {
        int idx = (term->lines_start + i) % term->lines_capacity;
        if (term->lines[idx])
            term->lines[idx]->dirty = false;
    }
}

bool term_has_dirty_rows(Terminal *term)
{
    if (!term) return false;
    /* Check all rows in the buffer (visible + scrollback) */
    for (int i = 0; i < term->lines_count; i++) {
        int idx = (term->lines_start + i) % term->lines_capacity;
        if (term->lines[idx] && term->lines[idx]->dirty)
            return true;
    }
    return false;
}

void term_mark_all_dirty(Terminal *term)
{
    if (!term) return;
    for (int i = 0; i < term->lines_count; i++) {
        int idx = (term->lines_start + i) % term->lines_capacity;
        if (term->lines[idx])
            term->lines[idx]->dirty = true;
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
    term->scrollback_offset = 0;
    term->cursor.row      = 0;
    term->cursor.col      = 0;
    term->scroll_top      = 0;
    term->scroll_bot      = term->rows - 1;
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
    term->scrollback_offset = 0;
    term->scroll_top = 0;
    term->scroll_bot = term->rows - 1;

    /* Reset SGR attributes so text after alt screen exit uses defaults,
     * not whatever colors the alt-screen application (e.g. man/less) set. */
    memset(&term->current_attr, 0, sizeof(term->current_attr));

    /* Mark all restored rows dirty — screen content changed completely. */
    for (int i = 0; i < term->lines_count; i++) {
        int idx = (term->lines_start + i) % term->lines_capacity;
        if (term->lines[idx])
            term->lines[idx]->dirty = true;
    }

    /* Signal the renderer to invalidate its display buffer shadow.
     * The shadow still holds the alt-screen content; cell-level dirty checks
     * would skip cells that happen to match, leaving stale pixels on screen. */
    term->full_redraw_needed = true;
}