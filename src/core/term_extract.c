#include "term_extract.h"
#include "string_utils.h"
#include <string.h>

/* Extract rows from logical index range [start_logical, start_logical+count)
 * into buf. Returns bytes written (excluding NUL). */
static size_t extract_rows(const Terminal *term, int start_logical, int count,
                           char *buf, size_t buf_size)
{
    if (!term || !buf || buf_size == 0) return 0;

    /* First pass: find the last row with actual content to avoid trailing
     * empty rows generating spurious newlines. */
    int last_nonempty = -1;
    for (int r = 0; r < count; r++) {
        int logical = start_logical + r;
        if (logical < 0 || logical >= term->lines_count) continue;
        int physical = (term->lines_start + logical) % term->lines_capacity;
        TermRow *row = term->lines[physical];
        if (row && row->len > 0) last_nonempty = r;
    }

    if (last_nonempty < 0) {
        buf[0] = '\0';
        return 0;
    }

    size_t pos = 0;

    for (int r = 0; r <= last_nonempty; r++) {
        int logical = start_logical + r;
        if (logical < 0 || logical >= term->lines_count) continue;

        int physical = (term->lines_start + logical) % term->lines_capacity;
        TermRow *row = term->lines[physical];
        if (!row) continue;

        /* Find last non-space, non-NUL cell to trim trailing whitespace */
        int last = -1;
        for (int c = 0; c < row->len; c++) {
            uint32_t cp = row->cells[c].codepoint;
            if (cp != 0 && cp != ' ') last = c;
        }

        /* Write cells [0..last] */
        for (int c = 0; c <= last; c++) {
            uint32_t cp = row->cells[c].codepoint;
            if (cp == 0) cp = ' '; /* empty cell → space */

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

size_t term_extract_last_n(const Terminal *term, int n, char *buf, size_t buf_size)
{
    if (!term || !buf || buf_size == 0 || n <= 0) return 0;

    int count = n;
    if (count > term->lines_count) count = term->lines_count;
    int start = term->lines_count - count;

    return extract_rows(term, start, count, buf, buf_size);
}
