#include "term.h"
#include "xmalloc.h"
#include "shell_prompt.h"
#include "dlog.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>

/* The allocator behind every allocation that may fail without ending the
 * process: a scrollback row, a reflow, the alternate screen. term.h explains
 * the hook. term_init() and the first screen of rows stay on xmalloc/xcalloc,
 * which end the process when memory runs out, as every allocation made when a
 * tab opens does. */
static TermCallocFn s_calloc;

void term_set_calloc(TermCallocFn fn) {
    s_calloc = fn;
}

static TermRow *term_row_alloc(int cols) {
    TermRow *row = xmalloc(sizeof(TermRow));
    row->cells = xcalloc((size_t)cols, sizeof(TermCell));
    row->len = 0;
    row->dirty = true;
    row->wrapped = false;
    row->gen = 0;
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

/* A new row, filled as term_row_fill() fills one, through the replaceable
 * allocator: the TermRow, then its cells. NULL when either fails; the first
 * is freed when the second does, so a failure leaks nothing. */
static TermRow *term_row_try_alloc(int cols, TermAttr attr) {
    TermRow *row = (s_calloc ? s_calloc : calloc)(1, sizeof(TermRow));
    if (!row) return NULL;
    row->cells = (s_calloc ? s_calloc : calloc)((size_t)cols, sizeof(TermCell));
    if (!row->cells) {
        free(row);
        return NULL;
    }
    term_row_fill(row, cols, attr);
    return row;
}

/* Free every non-NULL slot of a whole ring array, then the array: a test may
 * have left rows outside the live part, so the live part is not assumed. */
static void term_ring_free(TermRow **lines, int capacity) {
    if (!lines) return;
    for (int i = 0; i < capacity; i++)
        term_row_free(lines[i]);
    free(lines);
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
    term->csi_sub_mask = 0;
    term->csi_overflow = false;
    term->csi_private = false;
    term->osc_len = 0;

    term->saved_cursor.row = 0;
    term->saved_cursor.col = 0;
    term->saved_cursor.visible = true;

    term->utf8_codepoint = 0;
    term->utf8_remaining = 0;
    term->utf8_lo = 0x80;
    term->utf8_hi = 0xBF;

    term->title[0]         = '\0';
    term->app_cursor_keys  = false;
    term->insert_mode      = false;
    term->scroll_top       = 0;
    term->scroll_bot       = rows - 1;
    term->alt_screen_active   = false;
    term->primary_lines       = NULL;
    term->full_redraw_needed  = false;
    term->bracketed_paste_mode = false;
    term->write_seq            = 0;
    term->log_id               = 0;
    term->lines_recycled         = 0;
    term->primary_lines_recycled = 0;
    term->epoch                  = 0;
    term->reflow_seq             = 0;
    term->region_scroll_seq      = 0;
    term->erase_seq              = 0;
    term->write_gen              = 0;
    term->grow_failing           = false;
    term->resize_failing         = false;

    /* The slot array starts all NULL. Only the live part holds rows: the
     * screen's, here; term_scroll() adds one per line kept. */
    term->lines = xcalloc((size_t)term->lines_capacity, sizeof(TermRow *));
    for (int i = 0; i < rows; i++) {
        term->lines[i] = term_row_alloc(cols);
        term_row_fill(term->lines[i], cols, term->current_attr);
    }

    return term;
}

void term_free(Terminal *term) {
    if (!term) return;

    term_ring_free(term->lines, term->lines_capacity);
    /* When alt screen is active, primary_lines holds the saved primary buffer */
    term_ring_free(term->primary_lines, term->primary_lines_capacity);
    free(term);
}

/* Reflow old_lines (circular buffer, old_cols wide) into a newly-allocated
 * buffer at new_rows × new_cols.  Reads old_lines and never frees or changes
 * it: term_resize() frees it once every ring involved has been reflowed.
 * Cursor position is passed as screen-relative (row from top of visible area)
 * and returned as screen-relative in the new geometry.
 *
 * All or nothing. The counting pass knows how many rows the text needs; the
 * slot array and exactly max(needed, new_rows) rows are then allocated, before
 * the writing pass, through the replaceable allocator. On a failure whatever
 * was obtained is freed, false is returned and no out parameter is written.
 * On success the live part [out_start, out_start + out_count) holds rows and
 * every other slot is NULL (term.h, term_logical_rows()).
 *
 * When the reflowed text needs more rows than the new buffer holds (narrowing
 * wraps lines into more rows), the OLDEST rows are dropped, as scrolling does,
 * never the newest: a first pass counts the output rows, the second writes
 * only those that fit. *out_dropped is how many were dropped; the caller adds
 * it to the ring's recycled counter so absolute line numbers stay monotonic. */
static bool term_reflow_buffer(
    TermRow **old_lines, int old_capacity, int old_start, int old_count,
    int old_cols, int old_rows,
    int cur_row, int cur_col,        /* screen-relative cursor in */
    int new_rows, int new_cols, int max_scrollback,
    TermAttr fill_attr,
    TermRow ***out_lines, int *out_capacity, int *out_count, int *out_start,
    int *out_cursor_row, int *out_cursor_col, uint64_t *out_dropped)
{
    int new_capacity = new_rows + max_scrollback;
    TermRow **new_lines = NULL;

    int cur_new_row = 0;
    int cur_new_col = 0;
    int new_cursor_row = 0;
    int new_cursor_col = 0;
    bool cursor_after_full_row = false;
    int dropped = 0;
    int needed = 0;      /* output rows the text needs, after the dropped ones */
    int allocated = 0;   /* rows allocated: slots 0 .. allocated - 1 */

    /* Convert screen-relative cursor to buffer-absolute logical index */
    int old_screen_top = (old_count >= old_rows) ? (old_count - old_rows) : 0;
    int cursor_logical = old_screen_top + cur_row;

    /* Pass 0 counts (nothing written, no capacity limit); pass 1 writes the
     * rows from output row `dropped` on. Output rows are numbered from 0 in
     * both; row r lives at new_lines[r - dropped]. */
    for (int pass = 0; pass < 2; pass++) {
        bool write = (pass == 1);
        int limit = write ? new_capacity : INT_MAX;
        int last_content_row = -1;
        cur_new_row = 0;
        cur_new_col = 0;
        new_cursor_row = 0;
        new_cursor_col = 0;
        cursor_after_full_row = false;

        if (write) {
            allocated = needed > new_rows ? needed : new_rows;
            new_lines = (s_calloc ? s_calloc : calloc)((size_t)new_capacity, sizeof(TermRow *));
            if (!new_lines) return false;
            for (int i = 0; i < allocated; i++) {
                new_lines[i] = term_row_try_alloc(new_cols, fill_attr);
                if (!new_lines[i]) {
                    term_ring_free(new_lines, new_capacity);
                    return false;
                }
            }
        }

#define OUT_ROW(r) (new_lines[(r) - dropped])
#define OUT_IDX(r) ((r) - dropped)

        for (int i = 0; i < old_count; i++) {
            int pi = (old_start + i) % old_capacity;
            TermRow *old_row = old_lines[pi];
            if (!old_row) continue;

            if (!old_row->wrapped && i > 0) {
                cur_new_row++;
                cur_new_col = 0;
            }
            if (OUT_IDX(cur_new_row) >= limit) break;

            /* Only reflow up to the written content width */
            int cell_limit = old_row->len < old_cols ? old_row->len : old_cols;

            /* A continuation row that holds the cursor but no cells still gets
             * its own output row when the row before it ended exactly at
             * new_cols, so the cursor keeps its row (spec section 2). */
            if (old_row->wrapped && i > 0 && i == cursor_logical &&
                cell_limit == 0 && cur_new_col >= new_cols) {
                cur_new_row++;
                cur_new_col = 0;
                if (OUT_IDX(cur_new_row) >= limit) break;
                if (write && OUT_IDX(cur_new_row) >= 0) OUT_ROW(cur_new_row)->wrapped = true;
            }

            for (int c = 0; c < cell_limit; c++) {
                /* Exact width (spec section 2): wrap when the next cell of the
                 * same logical line arrives, never after the last one, so a row
                 * that ends exactly at new_cols gains no blank wrapped row. */
                if (cur_new_col >= new_cols) {
                    cur_new_row++;
                    cur_new_col = 0;
                    if (OUT_IDX(cur_new_row) >= limit) break;
                    if (write && OUT_IDX(cur_new_row) >= 0) OUT_ROW(cur_new_row)->wrapped = true;
                }
                if (i == cursor_logical && c == cur_col) {
                    new_cursor_row = cur_new_row;
                    new_cursor_col = cur_new_col;
                }
                if (write && OUT_IDX(cur_new_row) >= 0) {
                    OUT_ROW(cur_new_row)->cells[cur_new_col] = old_row->cells[c];
                    OUT_ROW(cur_new_row)->len = cur_new_col + 1;
                }
                last_content_row = cur_new_row;
                cur_new_col++;
            }
            /* A cursor past the content keeps its distance from it (spec
             * section 2); the clamp to new_cols - 1 below bounds it, except that
             * a cursor whose row ends exactly at new_cols keeps the pending-wrap
             * column instead of landing on content (the distance is lost there). */
            if (i == cursor_logical && cur_col >= cell_limit) {
                new_cursor_row = cur_new_row;
                cursor_after_full_row = (cur_new_col == new_cols);
                new_cursor_col = cursor_after_full_row
                               ? cur_new_col
                               : cur_new_col + (cur_col - cell_limit);
            }
        }

#undef OUT_ROW
#undef OUT_IDX

        if (!write) {
            /* Rows the text needs: up to the last cell written or the cursor's
             * row. Trailing empty rows are stripped below, so they must not
             * push content out. */
            int total = last_content_row + 1;
            if (new_cursor_row + 1 > total) total = new_cursor_row + 1;
            if (total < 1) total = 1;
            dropped = total > new_capacity ? total - new_capacity : 0;
            needed = total - dropped;
        }
    }

    /* Output rows are numbered from `dropped`: shift to buffer coordinates. */
    cur_new_row -= dropped;
    new_cursor_row -= dropped;
    if (new_cursor_row < 0) new_cursor_row = 0;

    int new_count = cur_new_row + 1;
    if (new_count > new_capacity) new_count = new_capacity;

    /* Strip trailing empty rows, keeping at least the cursor row. Slots in
     * [needed, allocated) hold allocated blank rows (empty); only slots at or
     * past `allocated` are NULL. */
    {
        int min_count = new_cursor_row + 1;
        if (min_count < 1) min_count = 1;
        while (new_count > min_count &&
               (!new_lines[new_count - 1] || new_lines[new_count - 1]->len == 0))
            new_count--;
    }

    /* Push content to bottom on expansion: prepend empty rows so the cursor
     * stays near the bottom rather than floating mid-screen. The padding
     * slots lie at the end of the array, past the content in slots
     * [0, new_count). A padding slot with no row takes a spare: a row from
     * [new_count, allocated) that is not itself a padding slot (with a small
     * max_scrollback the two ranges overlap). There are always enough:
     * allocated >= new_rows, and the segment needs exactly new_rows. */
    int new_start = 0;
    if (new_rows > old_rows && old_count >= old_rows && new_count < new_rows) {
        int pad = new_rows - new_count;
        new_start = (new_capacity - pad) % new_capacity;
        int spare = new_count;
        for (int i = 0; i < pad; i++) {
            int idx = (new_start + i) % new_capacity;
            if (!new_lines[idx]) {
                while (!new_lines[spare] ||
                       ((spare - new_start + new_capacity) % new_capacity) < pad)
                    spare++;
                new_lines[idx] = new_lines[spare];
                new_lines[spare] = NULL;
            }
            term_row_fill(new_lines[idx], new_cols, fill_attr);
        }
        new_cursor_row += pad;
        new_count = new_rows;
    }

    /* Restore the invariant lines_count >= rows: the visible screen must
     * lie entirely within the counted lines.  Stripping trailing blanks
     * above can leave new_count < new_rows; rows [new_count, new_rows)
     * are freshly allocated blanks, so just count them.  Without this,
     * term_scroll() "appends" into a row that is already on screen
     * (wiping it mid-screen instead of scrolling), and a later reflow
     * drops rows and the cursor that live beyond lines_count. */
    if (new_count < new_rows) new_count = new_rows;

    /* Free every row outside the new segment, so that only the live part
     * holds rows. After the strip new_count equals `needed`, and the padding
     * above consumes exactly the spares in [needed, allocated), so as the
     * code stands nothing is left to free here: this loop is unreachable
     * defensive code, kept so the invariant holds whatever the counting pass
     * decided. A slot at or past `allocated` is NULL or a padding slot, which
     * is inside the segment. On the alternate screen (max_scrollback 0) the
     * padding slots are already allocated, so nothing moves either. */
    for (int i = 0; i < allocated; i++) {
        if (new_lines[i] &&
            ((i - new_start + new_capacity) % new_capacity) >= new_count) {
            term_row_free(new_lines[i]);
            new_lines[i] = NULL;
        }
    }

    /* Convert cursor back to screen-relative */
    int screen_top = (new_count >= new_rows) ? (new_count - new_rows) : 0;
    new_cursor_row -= screen_top;
    if (new_cursor_row < 0)        new_cursor_row = 0;
    if (new_cursor_row >= new_rows) new_cursor_row = new_rows - 1;
    if (new_cursor_col < 0)        new_cursor_col = 0;
    /* The clamp never lands on a content cell. A cursor at or past its row's
     * content (any distance) whose row, after reflow, ends exactly at new_cols
     * keeps col == new_cols: the pending wrap survives the resize, so the next
     * character wraps instead of overwriting the last cell (spec section 2
     * identity). For a cursor past the content the distance is lost there. Any
     * other column past the row is clamped, and then the content ends before
     * that cell, so it is blank. */
    if (new_cursor_col >= new_cols &&
        !(cursor_after_full_row && new_cursor_col == new_cols))
        new_cursor_col = new_cols - 1;

    *out_lines      = new_lines;
    *out_capacity   = new_capacity;
    *out_count      = new_count;
    *out_start      = new_start;
    *out_cursor_row = new_cursor_row;
    *out_cursor_col = new_cursor_col;
    *out_dropped    = (uint64_t)dropped;
    return true;
}

bool term_resize(Terminal *term, int rows, int cols) {
    if (!term || rows <= 0 || cols <= 0) return true;
    if (term->rows == rows && term->cols == cols) return true;

    int old_rows = term->rows;
    int old_cols = term->cols;
    int old_lines = term->lines_count;

    /* Reflow into locals first, committing nothing: the active buffer
     * (alt-screen or primary; the alt screen has no scrollback, so it
     * allocates rows only) and, on the alt screen, the saved primary too, so
     * term_alt_screen_exit() restores it at the correct dimensions. Either
     * failing leaves the Terminal exactly as it was. */
    TermRow **new_lines = NULL, **new_primary = NULL;
    int new_cap = 0, new_count = 0, new_start = 0;
    int new_pcap = 0, new_pcount = 0, new_pstart = 0;
    TermCursor new_cursor = term->cursor;
    TermCursor new_pcursor = term->primary_cursor;
    uint64_t dropped = 0, pdropped = 0;

    bool ok = term_reflow_buffer(
        term->lines, term->lines_capacity, term->lines_start, term->lines_count,
        old_cols, old_rows,
        term->cursor.row, term->cursor.col,
        rows, cols,
        term->alt_screen_active ? 0 : term->max_scrollback,
        term->current_attr,
        &new_lines, &new_cap, &new_count, &new_start,
        &new_cursor.row, &new_cursor.col, &dropped);
    if (ok && term->alt_screen_active) {
        ok = term_reflow_buffer(
            term->primary_lines, term->primary_lines_capacity,
            term->primary_lines_start, term->primary_lines_count,
            old_cols, old_rows,
            term->primary_cursor.row, term->primary_cursor.col,
            rows, cols, term->max_scrollback,
            term->current_attr,
            &new_primary, &new_pcap, &new_pcount, &new_pstart,
            &new_pcursor.row, &new_pcursor.col, &pdropped);
        if (!ok) term_ring_free(new_lines, new_cap);
    }
    if (!ok) {
        /* One log line per failing streak, not one per WM_SIZE of a drag. */
        if (!term->resize_failing) {
            term->resize_failing = true;
            DLOG(DLOG_TERM, DLOG_WARN, term->log_id, "term.resize.alloc_failed",
                 "rows=%d cols=%d alt=%d", rows, cols,
                 term->alt_screen_active ? 1 : 0);
        }
        return false;
    }

    /* Commit. Free the old rings slot by slot over the whole array. */
    term_ring_free(term->lines, term->lines_capacity);
    term->lines          = new_lines;
    term->lines_capacity = new_cap;
    term->lines_count    = new_count;
    term->lines_start    = new_start;
    term->cursor         = new_cursor;
    term->lines_recycled += dropped;
    if (term->alt_screen_active) {
        term_ring_free(term->primary_lines, term->primary_lines_capacity);
        term->primary_lines          = new_primary;
        term->primary_lines_capacity = new_pcap;
        term->primary_lines_count    = new_pcount;
        term->primary_lines_start    = new_pstart;
        term->primary_cursor         = new_pcursor;
        term->primary_lines_recycled += pdropped;
    }
    term->resize_failing = false;
    term->grow_failing = false;

    /* Reflow renumbered every row: a selection's line numbers no longer apply. */
    term->epoch++;
    term->reflow_seq++;

    term->rows = rows;
    term->cols = cols;
    term->scroll_top = 0;
    term->scroll_bot = rows - 1;

    /* A view scrolled back further than the reflowed history holds would
     * show rows that do not exist (spec 2026-10-20 section 2.1). */
    {
        int max_off = term_max_scroll_offset(term);
        if (term->scrollback_offset > max_off) term->scrollback_offset = max_off;
        if (term->scrollback_offset < 0) term->scrollback_offset = 0;
    }

    DLOG(DLOG_TERM, DLOG_DEBUG, term->log_id, "term.resize",
         "rows_before=%d rows=%d cols_before=%d cols=%d lines_before=%d lines=%d alt=%d",
         old_rows, rows, old_cols, cols, old_lines, term->lines_count,
         term->alt_screen_active ? 1 : 0);
    return true;
}

void term_scroll(Terminal *term) {
    if (term->lines_count < term->lines_capacity) {
        /* Buffer not full: append new line. The slot past the live part is
         * NULL, and a row is allocated into it -- the one place a scrollback
         * row gets its cells. A slot that already holds a row (only a sparse
         * state a test forced) is reused as it always was, allocating
         * nothing. */
        int new_idx = (term->lines_start + term->lines_count) % term->lines_capacity;
        TermRow *row = term->lines[new_idx];
        if (row) {
            term_row_fill(row, term->cols, term->current_attr);
            term->lines_count++;
            term_row_stamp(term, row);
        } else {
            row = term_row_try_alloc(term->cols, term->current_attr);
            if (row) {
                term->lines[new_idx] = row;
                term->lines_count++;
                term->grow_failing = false;
                term_row_stamp(term, row);
            } else {
                /* No memory: do what a full ring does, plus one pointer move.
                 * The oldest row (the live part holds at least `rows` rows,
                 * so lines_start is one) is cleared and moved into the slot
                 * past the live part, its own slot left NULL. lines_count
                 * stays; the tab keeps the history it already holds. The next
                 * line tries again. */
                int recycle_idx = term->lines_start;
                row = term->lines[recycle_idx];
                term_row_fill(row, term->cols, term->current_attr);
                term->lines[new_idx] = row;
                term->lines[recycle_idx] = NULL;
                term->lines_start = (term->lines_start + 1) % term->lines_capacity;
                term->lines_recycled++;
                term_row_stamp(term, row);
                if (!term->grow_failing) {
                    term->grow_failing = true;
                    DLOG(DLOG_TERM, DLOG_WARN, term->log_id, "term.scrollback.alloc_failed",
                         "lines=%d capacity=%d", term->lines_count,
                         term->lines_capacity);
                }
            }
        }
    } else {
        /* Buffer full: recycle oldest line. Count it first, so the stamp
         * belongs to the row's new number, not the one it held. */
        int recycle_idx = term->lines_start;
        term_row_fill(term->lines[recycle_idx], term->cols, term->current_attr);
        term->lines_start = (term->lines_start + 1) % term->lines_capacity;
        term->lines_recycled++;
        term_row_stamp(term, term->lines[recycle_idx]);
    }

    /* Smart scrolling: a view that is scrolled back stays anchored on the
     * same history lines while new output arrives -- the offset counts
     * lines from the bottom, so every line scrolled in moves it up by one,
     * clamped to the history the buffer still holds. A view at the bottom
     * (offset 0) keeps following; a keypress resets the offset (window.c). */
    if (term->scrollback_offset > 0) {
        int max_off = term_max_scroll_offset(term);
        term->scrollback_offset++;
        if (term->scrollback_offset > max_off) term->scrollback_offset = max_off;
    }

    /* Mark all visible rows dirty: each screen position now maps to a
     * different logical row, so the renderer must repaint every row.
     * Only the live part of the ring holds rows (term.h, term_logical_rows())
     * and lines_count >= rows, so the visible rows all lie inside it; a slot
     * outside it is NULL and the guard below skips it. */
    int vis_start = term->lines_count - term->rows;
    if (vis_start < 0) vis_start = 0;
    int vis_end = vis_start + term->rows;
    if (vis_end > term->lines_capacity) vis_end = term->lines_capacity;
    for (int i = vis_start; i < vis_end; i++) {
        int idx = (term->lines_start + i) % term->lines_capacity;
        if (term->lines[idx]) term->lines[idx]->dirty = true;
    }
}

/* Convert screen row (0-based) to physical ring-buffer index. Declared in
 * term.h (see there for the full rationale) so every screen-row consumer
 * -- inside this file and out -- shares this exact mapping instead of
 * keeping a copy that can drift out of step, which is exactly what
 * produced the bug that comment describes. const Terminal* so
 * term_cursor_row_text() below, which only ever sees one, can call it
 * too. */
int term_screen_to_phys(const Terminal *term, int screen_row) {
    int top = (term->lines_count >= term->rows)
            ? (term->lines_count - term->rows) : 0;
    return (term->lines_start + top + screen_row) % term->lines_capacity;
}

uint64_t term_abs_cursor_line(const Terminal *term)
{
    if (!term) return 0;
    int top = (term->lines_count >= term->rows)
            ? (term->lines_count - term->rows) : 0;
    return term->lines_recycled + (uint64_t)top + (uint64_t)term->cursor.row;
}

int term_view_to_logical(const Terminal *t, int screen_row)
{
    if (!t || screen_row < 0 || screen_row >= t->rows) return -1;
    int off = t->scrollback_offset > 0 ? t->scrollback_offset : 0;
    int top = (t->lines_count >= t->rows) ? (t->lines_count - t->rows - off) : -off;
    if (top < 0) top = 0;
    return top + screen_row;
}

int term_max_scroll_offset(const Terminal *t)
{
    if (!t) return 0;
    int max_off = t->lines_count - t->rows;
    if (max_off > t->max_scrollback) max_off = t->max_scrollback;
    return max_off > 0 ? max_off : 0;
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

    /* Rows shift in place from here: on the primary screen absolute line
     * numbers stop meaning the same rows (term.h, region_scroll_seq). The
     * alternate screen has rows of its own, so its scrolls are not counted. */
    if (!term->alt_screen_active) term->region_scroll_seq++;

    /* Region scroll (or alt screen): pointer-swap within screen rows.
     * H10: n can exceed 64 for a scroll region taller than 64 rows (the
     * old code asserted n <= 64 against a fixed saved[64] and shipped
     * with asserts live -- a remote host reaching this with n > 64 could
     * abort() the process). Process the shift in chunks of at most 64 --
     * scrolling up by n is equivalent to scrolling up by each chunk in
     * turn, since every chunk is itself a complete, correctly-cleared
     * scroll-up of the same region. */
    int remaining = n;
    while (remaining > 0) {
        int chunk = (remaining > 64) ? 64 : remaining;

        /* Save the chunk row pointers that will be recycled */
        TermRow *saved[64];
        for (int i = 0; i < chunk; i++)
            saved[i] = term->lines[term_screen_to_phys(term, top + i)];

        /* Shift rows [top+chunk .. bot] up to [top .. bot-chunk] */
        for (int i = top; i <= bot - chunk; i++) {
            int dst = term_screen_to_phys(term, i);
            int src = term_screen_to_phys(term, i + chunk);
            term->lines[dst] = term->lines[src];
        }

        /* Place recycled (cleared) rows at [bot-chunk+1 .. bot] */
        for (int i = 0; i < chunk; i++) {
            int idx = term_screen_to_phys(term, bot - chunk + 1 + i);
            term->lines[idx] = saved[i];
            if (saved[i]) term_row_fill(saved[i], term->cols, term->current_attr);
        }

        remaining -= chunk;
    }

    /* Every row in the region moved or was cleared: dirty and stamped. A NULL
     * row (unreachable while the live part covers the screen) was moved like
     * any other pointer, but is not refilled, marked or stamped. */
    for (int i = top; i <= bot; i++) {
        TermRow *moved = term->lines[term_screen_to_phys(term, i)];
        if (!moved) continue;
        moved->dirty = true;
        term_row_stamp(term, moved);
    }
}

void term_scroll_down(Terminal *term, int top, int bot, int n) {
    if (!term || top < 0 || bot >= term->rows || top >= bot || n <= 0)
        return;
    if (n > bot - top + 1) n = bot - top + 1;

    /* Rows always shift in place when scrolling down (counted on the primary
     * screen only, as in term_scroll_up()). */
    if (!term->alt_screen_active) term->region_scroll_seq++;

    /* H10: chunk in groups of at most 64 -- see the matching comment in
     * term_scroll_up(). Scrolling down by n is equivalent to scrolling
     * down by each chunk in turn. */
    int remaining = n;
    while (remaining > 0) {
        int chunk = (remaining > 64) ? 64 : remaining;

        /* Save the chunk row pointers that will be recycled (bottom of region) */
        TermRow *saved[64];
        for (int i = 0; i < chunk; i++)
            saved[i] = term->lines[term_screen_to_phys(term, bot - chunk + 1 + i)];

        /* Shift rows [top .. bot-chunk] down to [top+chunk .. bot] */
        for (int i = bot - chunk; i >= top; i--) {
            int dst = term_screen_to_phys(term, i + chunk);
            int src = term_screen_to_phys(term, i);
            term->lines[dst] = term->lines[src];
        }

        /* Place recycled (cleared) rows at [top .. top+chunk-1] */
        for (int i = 0; i < chunk; i++) {
            int idx = term_screen_to_phys(term, top + i);
            term->lines[idx] = saved[i];
            if (saved[i]) term_row_fill(saved[i], term->cols, term->current_attr);
        }

        remaining -= chunk;
    }

    /* Every row in the region moved or was cleared: dirty and stamped. A NULL
     * row (unreachable while the live part covers the screen) was moved like
     * any other pointer, but is not refilled, marked or stamped. */
    for (int i = top; i <= bot; i++) {
        TermRow *moved = term->lines[term_screen_to_phys(term, i)];
        if (!moved) continue;
        moved->dirty = true;
        term_row_stamp(term, moved);
    }
}

void term_clear_dirty(Terminal *term)
{
    if (!term) return;
    for (int i = 0; i < term->rows; i++) {
        int idx = term_screen_to_phys(term, i);
        if (idx >= 0 && idx < term->lines_capacity && term->lines[idx])
            term->lines[idx]->dirty = false;
    }
}

int term_dirty_row_flags(const Terminal *term, unsigned char *flags, int max)
{
    if (!term) return 0;
    int n = 0;
    for (int i = 0; i < term->rows; i++) {
        int idx = term_screen_to_phys(term, i);
        int dirty = (idx >= 0 && idx < term->lines_capacity &&
                     term->lines[idx] && term->lines[idx]->dirty) ? 1 : 0;
        if (flags && i < max) flags[i] = (unsigned char)dirty;
        n += dirty;
    }
    return n;
}

bool term_has_dirty_rows(Terminal *term)
{
    if (!term) return false;
    for (int i = 0; i < term->rows; i++) {
        int idx = term_screen_to_phys(term, i);
        if (idx >= 0 && idx < term->lines_capacity &&
            term->lines[idx] && term->lines[idx]->dirty)
            return true;
    }
    return false;
}

void term_mark_all_dirty(Terminal *term)
{
    if (!term) return;
    for (int i = 0; i < term->rows; i++) {
        int idx = term_screen_to_phys(term, i);
        if (idx >= 0 && idx < term->lines_capacity && term->lines[idx])
            term->lines[idx]->dirty = true;
    }
}

void term_alt_screen_enter(Terminal *term)
{
    if (!term || term->alt_screen_active) return;

    /* Allocate a fresh alt-screen buffer (rows only, no scrollback) before
     * saving anything: on a failure the terminal stays on the primary screen,
     * every field unchanged, and the matching ?1049l does nothing. */
    int cap = term->rows;
    TermRow **alt = (s_calloc ? s_calloc : calloc)((size_t)cap, sizeof(TermRow *));
    bool alt_ok = alt != NULL;
    for (int i = 0; alt_ok && i < cap; i++) {
        alt[i] = term_row_try_alloc(term->cols, term->current_attr);
        if (!alt[i]) alt_ok = false;
    }
    if (!alt_ok) {
        term_ring_free(alt, cap);
        DLOG(DLOG_TERM, DLOG_WARN, term->log_id, "term.alt_screen.alloc_failed",
             "rows=%d cols=%d", term->rows, term->cols);
        return;
    }

    /* Save primary buffer state */
    term->primary_lines           = term->lines;
    term->primary_lines_capacity  = term->lines_capacity;
    term->primary_lines_count     = term->lines_count;
    term->primary_lines_start     = term->lines_start;
    term->primary_cursor          = term->cursor;
    term->primary_lines_recycled  = term->lines_recycled;
    term->lines_recycled          = 0;
    term->epoch++;

    term->lines           = alt;
    term->lines_capacity  = cap;
    term->lines_count     = cap;
    term->lines_start     = 0;
    term->scrollback_offset = 0;
    term->cursor.row      = 0;
    term->cursor.col      = 0;
    term->scroll_top      = 0;
    term->scroll_bot      = term->rows - 1;
    term->alt_screen_active = true;

    /* Same signal as term_alt_screen_exit(): the whole screen changed
     * under the renderer. Besides invalidating the display-buffer shadow,
     * window.c's poll loop treats this as "critical" and repaints at once
     * instead of waiting out the 16 ms cooldown -- which is the window in
     * which, on 2026-10-14, a partial-width gutter repaint (the scrollbar
     * hiding for the alternate screen) was the only paint to run and left
     * the previous screen's text on view. */
    term->full_redraw_needed = true;

    DLOG(DLOG_TERM, DLOG_DEBUG, term->log_id, "term.alt_screen.enter",
         "rows=%d cols=%d primary_lines=%d", term->rows, term->cols,
         term->primary_lines_count);
}

void term_alt_screen_exit(Terminal *term)
{
    if (!term || !term->alt_screen_active) return;

    /* Free the alt-screen lines */
    term_ring_free(term->lines, term->lines_capacity);

    /* Restore primary buffer */
    term->lines           = term->primary_lines;
    term->lines_capacity  = term->primary_lines_capacity;
    term->lines_count     = term->primary_lines_count;
    term->lines_start     = term->primary_lines_start;
    term->cursor          = term->primary_cursor;
    term->lines_recycled  = term->primary_lines_recycled;
    term->epoch++;
    term->primary_lines   = NULL;
    term->alt_screen_active = false;
    term->scrollback_offset = 0;
    term->scroll_top = 0;
    term->scroll_bot = term->rows - 1;

    /* Reset SGR attributes so text after alt screen exit uses defaults,
     * not whatever colors the alt-screen application (e.g. man/less) set. */
    memset(&term->current_attr, 0, sizeof(term->current_attr));

    /* Mark all visible screen rows dirty — not just 0..lines_count-1.
     * When lines_count < rows (sparse buffer), rows beyond lines_count
     * are still visible on screen and must be repainted. */
    for (int i = 0; i < term->rows; i++) {
        int idx = term_screen_to_phys(term, i);
        if (idx >= 0 && idx < term->lines_capacity && term->lines[idx])
            term->lines[idx]->dirty = true;
    }

    /* Signal the renderer to invalidate its display buffer shadow.
     * The shadow still holds the alt-screen content; cell-level dirty checks
     * would skip cells that happen to match, leaving stale pixels on screen. */
    term->full_redraw_needed = true;

    DLOG(DLOG_TERM, DLOG_DEBUG, term->log_id, "term.alt_screen.exit",
         "rows=%d cols=%d lines_count=%d", term->rows, term->cols,
         term->lines_count);
}

/* Shared by term_at_prompt() and term_at_continuation_prompt(): every guard
 * and the row-text build, leaving only which shell_prompt_*() call to make
 * to the two callers. Returns 1 with `buf` filled (the row's text up to the
 * cursor column) when the terminal could plausibly be sitting at a prompt
 * of some kind, or 0 when it cannot be (in which case `buf` is untouched
 * and callers must not use it). */
static int term_cursor_row_text(const Terminal *term, char *buf, size_t buf_size)
{
    if (!term) return 0;
    if (term->alt_screen_active) return 0;
    if (term->cursor.row < 0 || term->cursor.row >= term->rows) return 0;
    if (term->cursor.col < 0) return 0;

    /* term_screen_to_phys() -- the exact mapping every other screen-row
     * consumer in this file uses -- not a second copy of it. No
     * `logical < lines_count` check: the row's slot is guaranteed
     * allocated and non-NULL for the whole visible screen regardless of
     * lines_count (the live part of the ring always covers the screen:
     * term_init() allocates the screen's rows and term_reflow_buffer()
     * restores lines_count >= rows), so requiring it was always wrong, not
     * just incomplete -- see term_screen_to_phys()'s own comment. */
    int physical = term_screen_to_phys(term, term->cursor.row);
    if (physical < 0 || physical >= term->lines_capacity) return 0;
    TermRow *row = term->lines[physical];
    if (!row) return 0;

    int col = term->cursor.col;
    if (col > term->cols) col = term->cols;

    /* Nothing may follow the cursor on this row -- a non-blank cell past
     * the cursor means text was typed ahead of a prompt that hasn't
     * caught up yet (or the prompt hasn't actually returned). */
    for (int c = col; c < row->len && c < term->cols; c++) {
        uint32_t cp = row->cells[c].codepoint;
        if (cp != 0 && cp != ' ') return 0;
    }

    /* Build the row's text up to the cursor column (ASCII as-is, other
     * codepoints as '?') and hand it back for a shell-prompt test. A huge
     * cursor column (implausible for a real terminal) is clamped to the
     * buffer, keeping the tail -- which is all shell_prompt_line() and
     * shell_prompt_is_continuation() look at -- intact. */
    int n = col;
    if (n > (int)buf_size - 1) n = (int)buf_size - 1;
    int start = col - n;
    for (int i = 0; i < n; i++) {
        uint32_t cp = row->cells[start + i].codepoint;
        if (cp == 0) buf[i] = ' ';
        else if (cp < 0x80) buf[i] = (char)cp;
        else buf[i] = '?';
    }
    buf[n] = '\0';

    return 1;
}

int term_cursor_row_prompt_text(const Terminal *term, char *buf, size_t buf_size)
{
    return term_cursor_row_text(term, buf, buf_size);
}

int term_at_prompt(const Terminal *term)
{
    char buf[1024];
    if (!term_cursor_row_text(term, buf, sizeof(buf))) return 0;
    return shell_prompt_line(buf);
}

int term_at_continuation_prompt(const Terminal *term)
{
    char buf[1024];
    if (!term_cursor_row_text(term, buf, sizeof(buf))) return 0;
    return shell_prompt_is_continuation(buf);
}

int term_at_unambiguous_prompt(const Terminal *term)
{
    char buf[1024];
    if (!term_cursor_row_text(term, buf, sizeof(buf))) return 0;
    return shell_prompt_line_unambiguous(buf);
}

int term_cursor_row_is_windows_prompt(const Terminal *term)
{
    char buf[1024];
    if (!term_cursor_row_text(term, buf, sizeof(buf))) return 0;
    return shell_prompt_is_windows(buf);
}