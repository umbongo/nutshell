#include "selection.h"
#include "string_utils.h"
#include <string.h>

void selection_pixel_to_cell(int px, int py, int char_w, int char_h,
                             int y_offset, int term_rows, int term_cols,
                             int *out_row, int *out_col)
{
    int col = (char_w > 0) ? (px / char_w) : 0;
    int row = (char_h > 0) ? ((py - y_offset) / char_h) : 0;

    if (row < 0) row = 0;
    if (row >= term_rows) row = term_rows - 1;
    if (col < 0) col = 0;
    if (col >= term_cols) col = term_cols - 1;

    if (out_row) *out_row = row;
    if (out_col) *out_col = col;
}

void selection_normalise(const Selection *sel, int *r0, int *c0, int *r1, int *c1)
{
    if (!sel) return;

    int sr = sel->start_row, sc = sel->start_col;
    int er = sel->end_row,   ec = sel->end_col;

    if (sr > er || (sr == er && sc > ec)) {
        /* Swap */
        *r0 = er; *c0 = ec;
        *r1 = sr; *c1 = sc;
    } else {
        *r0 = sr; *c0 = sc;
        *r1 = er; *c1 = ec;
    }
}

size_t selection_extract_text(const Selection *sel, const Terminal *term,
                              char *buf, size_t buf_size)
{
    if (!sel || !term || !buf || buf_size == 0) {
        if (buf && buf_size > 0) buf[0] = '\0';
        return 0;
    }

    int r0, c0, r1, c1;
    selection_normalise(sel, &r0, &c0, &r1, &c1);

    /* Convert visible row index to logical index */
    int vis_start = (term->lines_count >= term->rows)
                  ? (term->lines_count - term->rows) : 0;

    size_t pos = 0;

    for (int row = r0; row <= r1; row++) {
        int logical = vis_start + row;
        /* term_logical_rows(), not lines_count directly: a selection can
         * cover a row in the sparse gap right after a resize (lines_count
         * < rows, but every row of the new screen is real and written
         * to -- see term.h) just as easily as older, already-scrolled
         * content, and Ctrl+C copying that row as blank would be the
         * user-visible version of the bug that left term_at_prompt()
         * false forever in the same situation. */
        if (logical < 0 || logical >= term_logical_rows(term)) continue;
        int physical = (term->lines_start + logical) % term->lines_capacity;
        TermRow *trow = term->lines[physical];
        if (!trow) continue;

        int col_start = (row == r0) ? c0 : 0;
        int col_end   = (row == r1) ? c1 : (trow->len > 0 ? trow->len - 1 : 0);

        /* Find last non-space cell in the selected range to trim trailing spaces.
         * A cell holding a stray control codepoint counts as blank here too --
         * cell_text_codepoint() is what actually gets emitted below. */
        int last_content = -1;
        for (int c = col_start; c <= col_end && c < trow->len; c++) {
            uint32_t cp = cell_text_codepoint(trow->cells[c].codepoint);
            if (cp != ' ') last_content = c;
        }

        /* If the row has no content in the range, still add newline between rows */
        int effective_end = (last_content >= col_start) ? last_content : -1;

        for (int c = col_start; c <= effective_end && c < trow->len; c++) {
            uint32_t cp = cell_text_codepoint(trow->cells[c].codepoint);
            char u8[4];
            int n = utf8_encode(cp, u8);
            if (pos + (size_t)n >= buf_size) goto done;
            memcpy(buf + pos, u8, (size_t)n);
            pos += (size_t)n;
        }

        /* Add newline between rows */
        if (row < r1) {
            if (pos + 1 >= buf_size) goto done;
            buf[pos++] = '\n';
        }
    }

done:
    buf[pos] = '\0';
    return pos;
}
