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

/* ---- words and lines ---------------------------------------------------- */

static uint32_t sel_cp(const Terminal *t, SelPoint p)
{
    const TermRow *r = sel_row(t, p.line);
    if (!r || p.col < 0 || p.col >= t->cols) return 0;
    return r->cells[p.col].codepoint;
}

/* Step to the previous cell of the same logical line, across a soft wrap. */
static bool sel_prev_cell(const Terminal *t, SelPoint *p)
{
    if (p->col > 0) { p->col--; return true; }
    const TermRow *r = sel_row(t, p->line);
    if (!r || !r->wrapped || p->line == 0 || !sel_row(t, p->line - 1)) return false;
    p->line--;
    p->col = t->cols - 1;
    return true;
}

/* Step to the next cell of the same logical line, across a soft wrap. */
static bool sel_next_cell(const Terminal *t, SelPoint *p)
{
    if (p->col < t->cols - 1) { p->col++; return true; }
    const TermRow *n = sel_row(t, p->line + 1);
    if (!n || !n->wrapped) return false;
    p->line++;
    p->col = 0;
    return true;
}

/* The maximal run of cells of p's class, following soft wraps. */
static void sel_word_bounds(const Terminal *t, SelPoint p, SelPoint *first, SelPoint *last)
{
    int cls = selection_char_class(sel_cp(t, p));
    SelPoint a = p, b = p, q;
    for (;;) {
        q = a;
        if (!sel_prev_cell(t, &q) || selection_char_class(sel_cp(t, q)) != cls) break;
        a = q;
    }
    for (;;) {
        q = b;
        if (!sel_next_cell(t, &q) || selection_char_class(sel_cp(t, q)) != cls) break;
        b = q;
    }
    *first = a;
    *last = b;
}

/* The whole logical line through `line`: back to the first row that is not a
 * continuation (bounded by the oldest row held), forward over every row whose
 * wrapped flag is set. */
static void sel_line_bounds(const Terminal *t, uint64_t line, SelPoint *first, SelPoint *last)
{
    uint64_t a = line, b = line;
    for (;;) {
        const TermRow *r = sel_row(t, a);
        if (!r || !r->wrapped || a <= t->lines_recycled) break;
        a--;
    }
    for (;;) {
        const TermRow *n = sel_row(t, b + 1);
        if (!n || !n->wrapped) break;
        b++;
    }
    first->line = a;
    first->col = 0;
    last->line = b;
    last->col = t->cols - 1;
}

/* The unit under p for the selection's mode: a cell, a word or a line. */
static void sel_unit(const Selection *s, const Terminal *t, SelPoint p,
                     SelPoint *first, SelPoint *last)
{
    if (s->mode == SEL_WORD) sel_word_bounds(t, p, first, last);
    else if (s->mode == SEL_LINE) sel_line_bounds(t, p.line, first, last);
    else { *first = p; *last = p; }
}

/* ---- write stamps (spec section 2.3) ------------------------------------ */

/* Wrap-safe: write_gen is a 32-bit counter that wraps after 2^32 writes. */
static bool sel_gen_newer(uint32_t row_gen, uint32_t sel_gen)
{
    return (int32_t)(row_gen - sel_gen) > 0;
}

/* Was any selected row written after the selection's stamp? */
static bool sel_rows_newer(const Selection *s, const Terminal *t)
{
    if (t->write_gen == s->gen) return false;                 /* nothing written since */
    uint64_t ln = (s->start.line < t->lines_recycled) ? t->lines_recycled : s->start.line;
    for (; ln <= s->end.line; ln++) {
        const TermRow *r = sel_row(t, ln);
        if (!r) break;
        if (sel_gen_newer(r->gen, s->gen)) return true;
    }
    return false;
}

/* Cut the four points to the oldest line held; true when any moved. */
static bool sel_trim(Selection *s, const Terminal *t)
{
    SelPoint *pts[4] = { &s->anchor, &s->anchor_end, &s->start, &s->end };
    bool moved = false;
    for (int i = 0; i < 4; i++) {
        if (pts[i]->line < t->lines_recycled) {
            pts[i]->line = t->lines_recycled;
            pts[i]->col = 0;
            moved = true;
        }
    }
    return moved;
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
    s->mode = (click_count >= 3) ? SEL_LINE : (click_count == 2 ? SEL_WORD : SEL_STREAM);
    sel_unit(s, t, sel_point_at(t, screen_row, col), &s->anchor, &s->anchor_end);
    s->start = s->anchor;
    s->end = s->anchor_end;
    /* A press alone selects nothing; a double or triple click selects its
     * word or line at once (PuTTY highlights it on the press). */
    s->valid = (s->mode != SEL_STREAM);
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
    if (s->epoch != t->epoch) return;              /* selection_sync() ends the drag */
    (void)sel_trim(s, t);                          /* a recycled anchor cannot come back */
    if (s->mode == SEL_STREAM && !s->valid &&
        abs(px - s->press_x) < drag_cx && abs(py - s->press_y) < drag_cy)
        return;                                    /* still a click */
    SelPoint first, last;
    sel_unit(s, t, sel_point_at(t, screen_row, col), &first, &last);
    s->start = (sel_cmp(first, s->anchor) < 0) ? first : s->anchor;
    s->end = (sel_cmp(last, s->anchor_end) > 0) ? last : s->anchor_end;
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
    if (!s->active) {
        /* Written since the release: no longer what was selected, so the
         * highlight goes row by row as the output overwrites it. */
        const TermRow *r = sel_row(t, line);
        if (!r || sel_gen_newer(r->gen, s->gen)) return false;
    }
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
    /* Stale: copy nothing. The const API cannot clear it; selection_sync(),
     * which the window calls before every copy, does. */
    if (!s->active && sel_rows_newer(s, t)) return b.p;

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

bool selection_sync(Selection *s, const Terminal *t)
{
    if (!s || !t || (!s->valid && !s->active)) return false;
    if (s->epoch != t->epoch || s->end.line < t->lines_recycled) {
        selection_clear(s);              /* renumbered or replaced, or all recycled */
        return true;
    }
    bool changed = sel_trim(s, t);
    if (!s->active) {
        if (sel_rows_newer(s, t)) {
            selection_clear(s);
            return true;
        }
        /* No selected row is newer than the stamp, so none is newer than
         * now: compare later writes against now, and the next call with
         * nothing written is two compares. */
        s->gen = t->write_gen;
    }
    return changed;
}

int selection_char_class(uint32_t cp)
{
    if (cell_text_codepoint(cp) == (uint32_t)' ') return 0;
    if ((cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
        cp == '_' || cp == '-' || cp == '.' || cp == '/' || cp == '~' || cp == ':' || cp == '@')
        return 1;
    if ((cp >= 0xC0u && cp <= 0x2AFu && cp != 0xD7u && cp != 0xF7u) ||
        (cp >= 0x370u && cp <= 0x52Fu))
        return 1;
    return 2 + (int)cp;
}
