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

/* ---- Absolute-line range capture ---- */

static const TermRow *row_at(const Terminal *term, int logical)
{
    if (logical < 0 || logical >= term_logical_rows(term)) return NULL;
    return term->lines[(term->lines_start + logical) % term->lines_capacity];
}

/* Writes the UTF-8 of cells [0, upto) of row to out (NULL: count only); a
 * control cell is a space, as in extract_rows(). Returns the byte count. */
static size_t row_emit(const TermRow *row, int upto, char *out)
{
    size_t n = 0;
    for (int c = 0; c < upto; c++) {
        char u8[4];
        int k = utf8_encode(cell_text_codepoint(row->cells[c].codepoint), u8);
        if (out) memcpy(out + n, u8, (size_t)k);
        n += (size_t)k;
    }
    return n;
}

/* Cells up to and including the last that is not blank: what the writer
 * trims a row to. */
static int row_trimmed_cells(const TermRow *row)
{
    int last = -1;
    for (int c = 0; c < row->len; c++)
        if (cell_text_codepoint(row->cells[c].codepoint) != ' ') last = c;
    return last + 1;
}

char *term_extract_abs_range_dup(const Terminal *term, uint64_t first, uint64_t end,
                                 const char *echo, size_t *len_out, unsigned *flags)
{
    if (len_out) *len_out = 0;
    if (flags) *flags = 0;
    if (!term) return NULL;

    /* Clamp the range to the live part of the ring: absolute line
     * lines_recycled + i is logical row i. */
    uint64_t base = term->lines_recycled;
    uint64_t live_end = base + (uint64_t)term_logical_rows(term);
    if (first < base) {
        first = base;
        if (flags) *flags = TERM_RANGE_START_RECYCLED;
    }
    if (end > live_end) end = live_end;
    int s = 0, e = 0;
    if (first < end) {
        s = (int)(first - base);
        e = (int)(end - base);
    }

    /* One allocation, the result's: room for every row of the range
     * untrimmed plus a newline each, which also serves as the scratch the
     * echo test builds its logical line in. */
    size_t cap = 2;
    for (int r = s; r < e; r++) {
        const TermRow *row = row_at(term, r);
        if (row) cap += row_emit(row, row->len, NULL) + 1;
    }
    char *buf = malloc(cap);
    if (!buf) return NULL;
    buf[0] = '\0';

    /* The echo: the first row and the rows wrapped onto it, joined whole. */
    if (echo && *echo && s < e) {
        const TermRow *row = row_at(term, s);
        if (row && !row->wrapped) {
            size_t n = 0;
            int r = s;
            for (; r < e; r++) {
                row = row_at(term, r);
                if (r > s && (!row || !row->wrapped)) break;
                if (row) n += row_emit(row, row->len, buf + n);
            }
            buf[n] = '\0';
            if (strstr(buf, echo)) s = r;
        }
    }

    int last = -1;
    for (int r = s; r < e; r++) {
        const TermRow *row = row_at(term, r);
        if (row && row_trimmed_cells(row) > 0) last = r;
    }

    size_t pos = 0;
    for (int r = s; r <= last; r++) {
        const TermRow *row = row_at(term, r);
        if (row) pos += row_emit(row, row_trimmed_cells(row), buf + pos);
        if (r < last) buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    if (len_out) *len_out = pos;
    return buf;
}

size_t term_extract_cursor_prefix(const Terminal *term, char *out, size_t cap)
{
    if (out && cap > 0) out[0] = '\0';
    if (!term) return 0;
    if (term->cursor.row < 0 || term->cursor.row >= term->rows) return 0;
    int physical = term_screen_to_phys(term, term->cursor.row);
    if (physical < 0 || physical >= term->lines_capacity) return 0;
    const TermRow *row = term->lines[physical];
    if (!row) return 0;

    int upto = term->cursor.col;
    if (upto > term->cols) upto = term->cols;
    size_t total = 0, written = 0;
    for (int c = 0; c < upto; c++) {
        /* A cell past the written width has never held text: a space. */
        uint32_t cp = c < row->len ? cell_text_codepoint(row->cells[c].codepoint) : ' ';
        char u8[4];
        int k = utf8_encode(cp, u8);
        total += (size_t)k;
        if (out && cap > 0 && written + (size_t)k < cap && written == total - (size_t)k) {
            memcpy(out + written, u8, (size_t)k);
            written += (size_t)k;
        }
    }
    if (out && cap > 0) out[written] = '\0';
    return total;
}
