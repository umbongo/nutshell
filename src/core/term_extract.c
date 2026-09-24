#include "term_extract.h"
#include "string_utils.h"
#include <string.h>
#include <stdlib.h>

/* Returns the trimmed UTF-8 byte length of logical row `logical` (0 if the
 * row doesn't exist). Uses the same trimming rule as the writer below:
 * trailing NUL/space cells are dropped; a NUL cell counts as one space byte. */
static size_t row_byte_len(const Terminal *term, int logical)
{
    if (logical < 0 || logical >= term_logical_rows(term)) return 0;
    int physical = (term->lines_start + logical) % term->lines_capacity;
    TermRow *row = term->lines[physical];
    if (!row) return 0;

    int last = -1;
    for (int c = 0; c < row->len; c++) {
        uint32_t cp = cell_text_codepoint(row->cells[c].codepoint);
        if (cp != ' ') last = c;
    }

    size_t len = 0;
    char u8[4];
    for (int c = 0; c <= last; c++) {
        uint32_t cp = cell_text_codepoint(row->cells[c].codepoint);
        len += (size_t)utf8_encode(cp, u8);
    }
    return len;
}

/* Extract rows from logical index range [start_logical, start_logical+count)
 * into buf. Returns bytes written (excluding NUL).
 * When the buffer is too small to hold every row, the OLDEST rows are
 * dropped first so the most recent terminal output is preserved. */
static size_t extract_rows(const Terminal *term, int start_logical, int count,
                           char *buf, size_t buf_size)
{
    if (!term || !buf || buf_size == 0) return 0;

    /* First pass: find the last row with actual content to avoid trailing
     * empty rows generating spurious newlines. */
    int last_nonempty = -1;
    for (int r = 0; r < count; r++) {
        int logical = start_logical + r;
        if (logical < 0 || logical >= term_logical_rows(term)) continue;
        int physical = (term->lines_start + logical) % term->lines_capacity;
        TermRow *row = term->lines[physical];
        if (row && row->len > 0) last_nonempty = r;
    }

    if (last_nonempty < 0) {
        buf[0] = '\0';
        return 0;
    }

    /* Second pass: if [first..last_nonempty] doesn't fit in buf_size, drop
     * rows from the front (oldest first) until it does. */
    int first = 0;
    size_t total = 1; /* NUL terminator */
    for (int r = 0; r <= last_nonempty; r++) {
        total += row_byte_len(term, start_logical + r);
        if (r < last_nonempty) total += 1; /* newline */
    }
    while (total > buf_size && first < last_nonempty) {
        total -= row_byte_len(term, start_logical + first);
        total -= 1; /* the newline that followed this row */
        first++;
    }

    size_t pos = 0;

    for (int r = first; r <= last_nonempty; r++) {
        int logical = start_logical + r;
        if (logical < 0 || logical >= term_logical_rows(term)) continue;

        int physical = (term->lines_start + logical) % term->lines_capacity;
        TermRow *row = term->lines[physical];
        if (!row) continue;

        /* Find last non-space, non-NUL cell to trim trailing whitespace.
         * A cell holding a stray control codepoint is blank here too. */
        int last = -1;
        for (int c = 0; c < row->len; c++) {
            uint32_t cp = cell_text_codepoint(row->cells[c].codepoint);
            if (cp != ' ') last = c;
        }

        /* Write cells [0..last] */
        for (int c = 0; c <= last; c++) {
            uint32_t cp = cell_text_codepoint(row->cells[c].codepoint);

            char u8[4];
            int n = utf8_encode(cp, u8);
            if (pos + (size_t)n >= buf_size) goto done; /* leave room for NUL */
            memcpy(buf + pos, u8, (size_t)n);
            pos += (size_t)n;
        }

        /* Add newline between rows (not after last non-empty) */
        if (r < last_nonempty) {
            if (pos + 1 >= buf_size) goto done;
            buf[pos++] = '\n';
        }
    }

done:
    buf[pos] = '\0';
    return pos;
}

size_t term_extract_visible(const Terminal *term, char *buf, size_t buf_size)
{
    if (!term || !buf || buf_size == 0) return 0;

    int top = (term->lines_count >= term->rows)
            ? (term->lines_count - term->rows) : 0;

    return extract_rows(term, top, term->rows, buf, buf_size);
}

/* The logical row range term_extract_last_n() covers: `count` rows ending
 * at the last row with content. Returns 0 (nothing to extract) when every
 * row is blank. */
static int last_n_range(const Terminal *term, int n, int *start, int *count)
{
    /* Anchor at the last row that actually has content — lines_count is
     * fixed at term->rows for a screen that has never scrolled, so blank
     * rows below the cursor must not count toward "the last N lines".
     * The scan itself must run over term_logical_rows(term), not
     * lines_count directly: in the sparse state right after a resize
     * (lines_count < rows), the newest output can sit in a row past
     * lines_count -- scanning only [0, lines_count) missed exactly that
     * output, which is what could leave the model looking at a stale
     * screen in a freshly resized tab. */
    int end_row = -1;
    for (int r = term_logical_rows(term) - 1; r >= 0; r--) {
        if (row_byte_len(term, r) > 0) { end_row = r; break; }
    }
    if (end_row < 0) return 0;

    int c = n;
    if (c > end_row + 1) c = end_row + 1;
    *start = end_row + 1 - c;
    *count = c;
    return 1;
}

size_t term_extract_last_n(const Terminal *term, int n, char *buf, size_t buf_size)
{
    if (!term || !buf || buf_size == 0 || n <= 0) return 0;

    int start, count;
    if (!last_n_range(term, n, &start, &count)) {
        buf[0] = '\0';
        return 0;
    }
    return extract_rows(term, start, count, buf, buf_size);
}

char *term_extract_last_n_dup(const Terminal *term, int n, size_t *len_out)
{
    if (len_out) *len_out = 0;
    if (!term || n <= 0) return NULL;

    int start, count;
    if (!last_n_range(term, n, &start, &count)) return NULL;

    /* Every row's bytes, a newline between rows, and the NUL: exactly what
     * extract_rows() writes, so its drop-the-oldest path never triggers. */
    size_t cap = 1;
    for (int r = 0; r < count; r++)
        cap += row_byte_len(term, start + r) + 1;

    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t len = extract_rows(term, start, count, buf, cap);
    if (len_out) *len_out = len;
    return buf;
}
