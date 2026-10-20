#include "selection.h"
#include "string_utils.h"
#include "xmalloc.h"
#include <stdlib.h>
#include <string.h>

/* ---- points and rows ---------------------------------------------------- */

static int sel_cmp(SelPoint a, SelPoint b)
{
    if (a.line != b.line) return (a.line < b.line) ? -1 : 1;
    return (a.col > b.col) - (a.col < b.col);
}

/* The row holding absolute line `line`, or NULL when it has been recycled or
 * lies past the rows the terminal holds. */
static const TermRow *sel_row(const Terminal *t, uint64_t line)
{
    if (line < t->lines_recycled) return NULL;
    uint64_t logical = line - t->lines_recycled;
    if (logical >= (uint64_t)term_logical_rows(t)) return NULL;
    return t->lines[(t->lines_start + (int)logical) % t->lines_capacity];
}

/* The absolute point under a screen cell of the view as scrolled. */
static SelPoint sel_point_at(const Terminal *t, int screen_row, int col)
{
    SelPoint p;
    int logical = term_view_to_logical(t, screen_row);
    if (logical < 0) logical = 0;
    p.line = t->lines_recycled + (uint64_t)logical;
    p.col = (col < 0) ? 0 : (col >= t->cols ? t->cols - 1 : col);
    return p;
}

static bool sel_row_blank(const Terminal *t, const TermRow *r)
{
    for (int c = 0; c < t->cols; c++)
        if (cell_text_codepoint(r->cells[c].codepoint) != (uint32_t)' ') return false;
    return true;
}

/* ---- a growable string -------------------------------------------------- */

typedef struct { char *p; size_t len, cap; } SelBuf;

static void sb_put(SelBuf *b, const char *src, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + n + 1) cap *= 2;
        b->p = xrealloc(b->p, cap);
        b->cap = cap;
    }
    memcpy(b->p + b->len, src, n);
    b->len += n;
    b->p[b->len] = '\0';
}

static void sb_cut(SelBuf *b, size_t len)
{
    b->len = len;
    b->p[len] = '\0';
}

/* ---- the API ------------------------------------------------------------ */

void selection_pixel_to_cell(int px, int py, int char_w, int char_h,
                             int y_offset, int rows, int cols,
                             int *out_row, int *out_col)
{
    int col = (char_w > 0) ? (px / char_w) : 0;
    int row = (char_h > 0) ? ((py - y_offset) / char_h) : 0;
    if (row >= rows) row = rows - 1;
    if (col >= cols) col = cols - 1;
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    if (out_row) *out_row = row;
    if (out_col) *out_col = col;
}

int selection_autoscroll_step(int py, int top_px, int bottom_px, int char_h)
{
    int ch = (char_h > 0) ? char_h : 1;
    if (py < top_px) return -(1 + (top_px - py) / ch);
    if (py >= bottom_px) return 1 + (py - bottom_px + 1) / ch;
    return 0;
}

void selection_begin(Selection *s, const Terminal *t, int screen_row, int col,
                     int click_count, int px, int py)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    if (!t) return;
    (void)click_count;                     /* a cell for every click count, for now */
    s->mode = SEL_STREAM;
    s->anchor = sel_point_at(t, screen_row, col);
    s->anchor_end = s->anchor;
    s->start = s->anchor;
    s->end = s->anchor_end;
    s->valid = false;                      /* a press alone selects nothing */
    s->epoch = t->epoch;
    s->gen = t->write_gen;
    s->active = true;
    s->press_x = s->last_x = px;
    s->press_y = s->last_y = py;
}

void selection_extend(Selection *s, const Terminal *t, int screen_row, int col,
                      int px, int py, int drag_cx, int drag_cy)
{
    if (!s || !t || !s->active) return;
    s->last_x = px;
    s->last_y = py;
    if (s->epoch != t->epoch) return;              /* the window ends the drag */
    if (s->mode == SEL_STREAM && !s->valid &&
        abs(px - s->press_x) < drag_cx && abs(py - s->press_y) < drag_cy)
        return;                                    /* still a click */
    SelPoint p = sel_point_at(t, screen_row, col);
    s->start = (sel_cmp(p, s->anchor) < 0) ? p : s->anchor;
    s->end = (sel_cmp(p, s->anchor_end) > 0) ? p : s->anchor_end;
    s->valid = true;
    s->gen = t->write_gen;
}

bool selection_finish(Selection *s, const Terminal *t)
{
    if (!s) return false;
    if (!s->active) return s->valid;
    s->active = false;
    if (!t || s->epoch != t->epoch) {
        selection_clear(s);
        return false;
    }
    s->gen = t->write_gen;
    return s->valid;
}

void selection_clear(Selection *s)
{
    if (s) memset(s, 0, sizeof(*s));
}

void selection_select_all(Selection *s, const Terminal *t)
{
    if (!s) return;
    selection_clear(s);
    if (!t) return;
    int last = term_logical_rows(t) - 1;
    while (last >= 0 &&
           sel_row_blank(t, t->lines[(t->lines_start + last) % t->lines_capacity]))
        last--;
    if (last < 0) return;                          /* an empty terminal: nothing */
    s->mode = SEL_STREAM;
    s->start.line = t->lines_recycled;
    s->start.col = 0;
    s->end.line = t->lines_recycled + (uint64_t)last;
    s->end.col = t->cols - 1;
    s->anchor = s->start;
    s->anchor_end = s->end;
    s->epoch = t->epoch;
    s->gen = t->write_gen;
    s->valid = true;
}

bool selection_contains(const Selection *s, const Terminal *t, int screen_row, int col)
{
    if (!s || !t || !s->valid || s->epoch != t->epoch) return false;
    int logical = term_view_to_logical(t, screen_row);
    if (logical < 0) return false;
    uint64_t line = t->lines_recycled + (uint64_t)logical;
    if (line < s->start.line || line > s->end.line) return false;
    if (line == s->start.line && col < s->start.col) return false;
    if (line == s->end.line && col > s->end.col) return false;
    return true;
}

bool selection_in_view(const Selection *s, const Terminal *t)
{
    if (!s || !t || !s->valid || s->epoch != t->epoch || t->rows <= 0) return false;
    uint64_t top = t->lines_recycled + (uint64_t)term_view_to_logical(t, 0);
    uint64_t bottom = top + (uint64_t)(t->rows - 1);
    return s->start.line <= bottom && s->end.line >= top;
}

char *selection_copy_text(const Selection *s, const Terminal *t, size_t *out_len)
{
    SelBuf b = { NULL, 0, 0 };
    sb_put(&b, "", 0);                             /* always a string */
    if (out_len) *out_len = 0;
    if (!s || !t || !s->valid || s->epoch != t->epoch) return b.p;

    uint64_t first = s->start.line;
    int first_col = s->start.col;
    if (first < t->lines_recycled) {               /* its head has been recycled */
        first = t->lines_recycled;
        first_col = 0;
    }
    size_t keep = 0;   /* end of the current logical line's last non-blank cell */
    for (uint64_t ln = first; ln <= s->end.line; ln++) {
        const TermRow *r = sel_row(t, ln);
        if (!r) break;
        if (ln != first && !r->wrapped) {          /* a new logical line */
            sb_cut(&b, keep);                      /* trim the one just finished */
            sb_put(&b, "\r\n", 2);
            keep = b.len;
        }
        int c0 = (ln == first) ? first_col : 0;
        int c1 = (ln == s->end.line) ? s->end.col : t->cols - 1;
        if (c1 > t->cols - 1) c1 = t->cols - 1;
        for (int c = c0; c <= c1; c++) {
            uint32_t cp = cell_text_codepoint(r->cells[c].codepoint);
            char u8[4];
            int n = utf8_encode(cp, u8);
            if (n <= 0) continue;
            sb_put(&b, u8, (size_t)n);
            if (cp != (uint32_t)' ') keep = b.len;
        }
    }
    sb_cut(&b, keep);
    /* Trailing empty rows are dropped: no CRLF after the last line. */
    while (b.len >= 2 && b.p[b.len - 2] == '\r' && b.p[b.len - 1] == '\n')
        sb_cut(&b, b.len - 2);
    if (out_len) *out_len = b.len;
    return b.p;
}
