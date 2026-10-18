/* src/ui/md_render.c — Markdown-to-GDI renderer.
 *
 * Uses the existing markdown.h inline parser (md_classify_line, md_parse_inline)
 * but renders to a GDI HDC instead of RichEdit.
 */

#ifdef _WIN32

#include "md_render.h"
#include "markdown.h"
#include "md_table.h"
#include "ns_type.h"
#include "ns_draw.h"
#include "theme.h"
#include "text_runs.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Colour helper: theme stores 0x00RRGGBB, GDI wants 0x00BBGGRR ── */

#define RGB_FROM_THEME(c) \
    RGB(((c) >> 16) & 0xFF, ((c) >> 8) & 0xFF, (c) & 0xFF)

/* Colour-by-role: resolve one of the two AI-reply intent hues (structure =
 * theme->link for headings/list markers/blockquote bar/table header text;
 * code = theme->info for inline code text/fenced-code left bar) against
 * theme->bg_primary, falling back to theme->text_main when the hue itself
 * would read below WCAG AA (4.5:1) on this theme rather than retuning the
 * theme's own palette -- see tests/test_ui_tokens.c's
 * test_ui_tokens_md_role_color_contrast for the per-theme numbers this is
 * guarding against. theme_contrast() is cheap enough to call per paint. */
static COLORREF md_role_clr(const ThemeColors *theme, unsigned int role)
{
    return RGB_FROM_THEME(
        theme_role_color(role, theme->bg_primary, theme->text_main));
}

/* ── Layout constants ────────────────────────────────────────────────── */

#define MD_LINE_SPACING     2   /* Extra pixels between lines */
#define MD_PARA_SPACING     6   /* Extra pixels between paragraphs */
#define MD_CODE_PAD_H       4   /* Horizontal padding inside code blocks */
#define MD_CODE_PAD_V       2   /* Vertical padding inside code blocks */
#define MD_CODE_BAR_WIDTH   2   /* Width of fenced code block's left bar */
#define MD_LIST_INDENT     16   /* Indent for list items */
#define MD_BLOCKQUOTE_IND  12   /* Indent for blockquotes */
#define MD_BQ_BAR_WIDTH     3   /* Width of blockquote left bar */
#define MD_HRULE_HEIGHT     1   /* Thickness of horizontal rule */
#define MD_HEADING_EXTRA_V  4   /* Extra vertical space around headings */

/* Table block: this file renders at a fixed 96-DPI pixel grid throughout
 * (MD_CODE_PAD_H etc. above are never run through ns_scale either, since
 * md_render_text/md_measure_text aren't passed a dpi) -- SP_SM/SP_XS/
 * STROKE_HAIRLINE are used here as their raw 96-DPI values for the same
 * reason, not scaled. */
#define MD_TABLE_PAD_H      SP_SM
#define MD_TABLE_PAD_V      SP_XS
#define MD_TABLE_MIN_EM      6  /* column floor, in average-char widths */
#define MD_TABLE_HEADER_FILL_ALPHA 0.12f

/* ── UTF-8 → UTF-16 (stack buffer with heap fallback) ───────────────── */

#define MD_WBUF_STACK 512

typedef struct {
    wchar_t  stack[MD_WBUF_STACK];
    wchar_t *ptr;
    int      len;   /* character count, excluding NUL */
} MdWBuf;

static void mdbuf_init(MdWBuf *b, const char *utf8, int byte_len)
{
    if (!utf8 || byte_len <= 0) {
        b->ptr = b->stack;
        b->stack[0] = L'\0';
        b->len = 0;
        return;
    }
    int need = MultiByteToWideChar(CP_UTF8, 0, utf8, byte_len, NULL, 0);
    if (need <= 0) {
        b->ptr = b->stack;
        b->stack[0] = L'\0';
        b->len = 0;
        return;
    }
    if (need < MD_WBUF_STACK) {
        b->ptr = b->stack;
    } else {
        b->ptr = (wchar_t *)malloc(((size_t)need + 1) * sizeof(wchar_t));
        if (!b->ptr) {
            b->ptr = b->stack;
            b->stack[0] = L'\0';
            b->len = 0;
            return;
        }
    }
    MultiByteToWideChar(CP_UTF8, 0, utf8, byte_len, b->ptr, need);
    b->ptr[need] = L'\0';
    b->len = need;
}

static void mdbuf_free(MdWBuf *b)
{
    if (b->ptr && b->ptr != b->stack)
        free(b->ptr);
    b->ptr = NULL;
}

/* ── Character selection: runs and highlight (spec 2026-10-18 s.3, s.6) ── */

/* The highlight: the focus hue at 30% over the colour the run sits on. */
#define MD_SEL_ALPHA 0.30f

/* Boundaries of one run, from one GetTextExtentExPointW call (stack
 * buffers, heap past MD_CUM_STACK). n == 0 when they could not be had.
 * One size for all three arrays: a token of 256 UTF-16 units / bytes has 257
 * boundaries, so 257 ints each keeps every token md_next_word yields in a
 * normal line on the stack. */
#define MD_CUM_STACK 257
typedef struct {
    int  st_dx[MD_CUM_STACK];
    int  st_cum[MD_CUM_STACK];
    int  st_cb[MD_CUM_STACK];
    int *dx, *cum, *cb;
    int  n;
} MdCum;

static void mdcum_free(MdCum *c)
{
    if (c->dx && c->dx != c->st_dx) free(c->dx);
    if (c->cum && c->cum != c->st_cum) free(c->cum);
    if (c->cb && c->cb != c->st_cb) free(c->cb);
    c->dx = c->cum = c->cb = NULL;
    c->n = 0;
}

/* Measure `wb` (the UTF-16 form of utf8[0, byte_len)) in the font selected
 * into hdc: *sz gets the extent, c the UTF-8 boundaries. */
static void mdcum_measure(HDC hdc, const MdWBuf *wb, const char *utf8,
                          int byte_len, MdCum *c, SIZE *sz)
{
    int nb = byte_len + 1;
    c->n = 0;
    c->dx = wb->len <= MD_CUM_STACK
        ? c->st_dx : (int *)malloc((size_t)wb->len * sizeof(int));
    c->cum = nb <= MD_CUM_STACK
        ? c->st_cum : (int *)malloc((size_t)nb * sizeof(int));
    c->cb = nb <= MD_CUM_STACK
        ? c->st_cb : (int *)malloc((size_t)nb * sizeof(int));
    sz->cx = sz->cy = 0;
    if (!c->dx || !c->cum || !c->cb) {
        mdcum_free(c);
        GetTextExtentPoint32W(hdc, wb->ptr, wb->len, sz);
        return;
    }
    if (!GetTextExtentExPointW(hdc, wb->ptr, wb->len, 0, NULL, c->dx, sz))
        return;
    int n = text_runs_cum_from_utf16(utf8, byte_len, c->dx, wb->len,
                                     c->cum, c->cb, nb);
    c->n = n > 0 ? n : 0;
}

static void md_fill(HDC hdc, const RECT *r, COLORREF clr)
{
    if (r->right <= r->left || r->bottom <= r->top) return;
    HBRUSH br = CreateSolidBrush(clr);
    FillRect(hdc, r, br);
    DeleteObject(br);
}

static int md_has_sel(const MdRunCtx *rc)
{
    return rc && rc->sel_start < rc->sel_end;
}

/* The selected part of the run [s, e) drawn at (px, py), h tall, clipped
 * at max_right -- painted before the run's text. */
static void md_sel_fill(HDC hdc, const MdRunCtx *rc, int s, int e,
                        int px, int py, int h, int max_right,
                        const MdCum *cm, COLORREF under)
{
    if (!md_has_sel(rc) || cm->n < 2) return;
    int x0 = 0, x1 = 0;
    if (!text_runs_span_px(s, e, cm->cum, cm->cb, cm->n,
                           rc->sel_start, rc->sel_end, &x0, &x1))
        return;
    RECT r = { px + x0, py, px + x1, py + h };
    if (r.right > max_right) r.right = max_right;
    md_fill(hdc, &r, rgb_alpha(rc->focus, under, MD_SEL_ALPHA));
}

/* A bullet or number over its marker's source bytes [s, e): filled whole
 * when the selection covers a byte of the marker, as the copy then carries
 * it. */
static void md_synth_fill(HDC hdc, const MdRunCtx *rc, int s, int e,
                          int x, int y, int w, int h, COLORREF under)
{
    if (!md_has_sel(rc) || !(s < rc->sel_end && e > rc->sel_start)) return;
    RECT r = { x, y, x + w, y + h };
    md_fill(hdc, &r, rgb_alpha(rc->focus, under, MD_SEL_ALPHA));
}

/* The fill from the end of a line's text to the content's right edge when
 * the selection runs past that line. `strict` (a soft wrap at offset
 * `off`, where the next visual line starts) needs the selection to start
 * before it; a source line end (`off` is its '\n') only needs it to start
 * at or before it. */
static void md_eol_fill(HDC hdc, const MdRunCtx *rc, int off, int strict,
                        int x0, int y, int x1, int h, COLORREF under)
{
    if (!md_has_sel(rc) || x1 <= x0) return;
    int starts = strict ? rc->sel_start < off : rc->sel_start <= off;
    if (!starts || off >= rc->sel_end) return;
    RECT r = { x0, y, x1, y + h };
    md_fill(hdc, &r, rgb_alpha(rc->focus, under, MD_SEL_ALPHA));
}

/* Append a run when painting with a run list, and only when its rect meets
 * the clip band (the Thinking body is painted at full height under a clip). */
static void md_record(MdRunCtx *rc, int x, int y, int w, int h, int s, int e,
                      const MdCum *cm, int flags, const char *synth)
{
    if (!rc || !rc->runs) return;
    if (rc->clip_top < rc->clip_bottom &&
        (y + h <= rc->clip_top || y >= rc->clip_bottom))
        return;
    TextRun t;
    memset(&t, 0, sizeof(t));
    t.x = x; t.y = y; t.w = w; t.h = h;
    t.start = s; t.end = e;
    t.part = rc->part;
    t.flags = flags;
    if (synth) {
        (void)snprintf(t.synthetic, sizeof(t.synthetic), "%s", synth);
        text_runs_append(rc->runs, &t, NULL, NULL, 0);
    } else if (cm && cm->n > 0) {
        text_runs_append(rc->runs, &t, cm->cum, cm->cb, cm->n);
    } else {
        int z[1] = { 0 };
        text_runs_append(rc->runs, &t, z, z, 1);
    }
}

/* A zero-width run for a line that draws no word (an empty line, an empty
 * code line), so a click on it lands on that line. */
static void md_record_empty(MdRunCtx *rc, int x, int y, int h, int off)
{
    md_record(rc, x, y, 0, h, off, off, NULL, 0, NULL);
}

/* MD_MODE_PLAIN: how many bytes of s[0, len) fit in `avail` px in the font
 * selected into hdc -- whole characters, never fewer than one. */
static int plain_fit_bytes(HDC hdc, const char *s, int len, int avail)
{
    MdWBuf wb;
    mdbuf_init(&wb, s, len);
    int fit = 0;
    SIZE sz = { 0, 0 };
    if (avail < 0) avail = 0;
    GetTextExtentExPointW(hdc, wb.ptr, wb.len, avail, &fit, NULL, &sz);
    mdbuf_free(&wb);
    int nb = text_runs_utf8_prefix_for_utf16(s, len, fit);
    if (nb <= 0) nb = text_runs_utf8_prefix_for_utf16(s, len, 1);
    if (nb <= 0) nb = text_runs_utf8_prefix_for_utf16(s, len, 2);
    if (nb <= 0) nb = 1;
    return nb;
}

/* ── Internal: measure a single word in a span's font ────────────────── */

/* Returns pixel width of [text+byte_off .. text+byte_off+byte_len) when
 * rendered in the span's font. Also returns line height via *out_h. */
static int measure_word(HDC hdc, const char *text, int byte_off, int byte_len,
                        const MdSpan *span,
                        HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                        int *out_h)
{
    if (byte_len <= 0) {
        *out_h = 0;
        return 0;
    }

    HFONT sel_font = hFont;
    HFONT created = NULL;
    /* Font selection per span type. KEEP IN SYNC with paint_word. */
    switch (span->type) {
    case MD_SPAN_BOLD:        sel_font = hBoldFont; break;
    case MD_SPAN_BOLD_ITALIC: {
        LOGFONT lf; GetObject(hBoldFont, sizeof(lf), &lf);
        lf.lfItalic = TRUE;
        created = CreateFontIndirect(&lf);
        sel_font = created ? created : hBoldFont;
        break;
    }
    case MD_SPAN_ITALIC: {
        LOGFONT lf; GetObject(hFont, sizeof(lf), &lf);
        lf.lfItalic = TRUE;
        created = CreateFontIndirect(&lf);
        sel_font = created ? created : hFont;
        break;
    }
    case MD_SPAN_CODE:        sel_font = hMonoFont; break;
    default:                  sel_font = hFont;     break;
    }

    HFONT old_font = (HFONT)SelectObject(hdc, sel_font);

    MdWBuf wb;
    mdbuf_init(&wb, text + byte_off, byte_len);

    SIZE sz = { 0, 0 };
    GetTextExtentPoint32W(hdc, wb.ptr, wb.len, &sz);

    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);

    SelectObject(hdc, old_font);
    if (created) DeleteObject(created);
    mdbuf_free(&wb);

    *out_h = tm.tmHeight;
    return sz.cx;
}

/* ── Internal: paint a single word at (px, py) in a span's font ──────── */

/* Paints text[byte_off, byte_off + byte_len) and, with a run context,
 * fills its selected part first (over `bg`, or over the inline-code
 * background for a code span) and records it as a run whose source offset
 * is `src`. One GetTextExtentExPointW gives both its size and its
 * boundaries. */
static void paint_word(HDC hdc, const char *text, int byte_off, int byte_len,
                       const MdSpan *span, int px, int py,
                       HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                       const ThemeColors *theme,
                       COLORREF default_clr, COLORREF code_clr,
                       MdRunCtx *rc, int src, COLORREF bg)
{
    if (byte_len <= 0) return;

    HFONT sel_font = hFont;
    HFONT created = NULL;
    /* Font selection per span type. KEEP IN SYNC with measure_word. */
    switch (span->type) {
    case MD_SPAN_BOLD:        sel_font = hBoldFont; break;
    case MD_SPAN_BOLD_ITALIC: {
        LOGFONT lf; GetObject(hBoldFont, sizeof(lf), &lf);
        lf.lfItalic = TRUE;
        created = CreateFontIndirect(&lf);
        sel_font = created ? created : hBoldFont;
        break;
    }
    case MD_SPAN_ITALIC: {
        LOGFONT lf; GetObject(hFont, sizeof(lf), &lf);
        lf.lfItalic = TRUE;
        created = CreateFontIndirect(&lf);
        sel_font = created ? created : hFont;
        break;
    }
    case MD_SPAN_CODE:        sel_font = hMonoFont; break;
    default:                  sel_font = hFont;     break;
    }

    HFONT old_font = (HFONT)SelectObject(hdc, sel_font);

    MdWBuf wb;
    mdbuf_init(&wb, text + byte_off, byte_len);

    SIZE sz = { 0, 0 };
    MdCum cm;
    mdcum_measure(hdc, &wb, text + byte_off, byte_len, &cm, &sz);

    /* Code background */
    COLORREF under = bg;
    if (span->type == MD_SPAN_CODE) {
        RECT cbg = { px - 1, py, px + sz.cx + 1, py + sz.cy };
        under = RGB_FROM_THEME(theme->chat.cmd_bg);
        HBRUSH br = CreateSolidBrush(under);
        FillRect(hdc, &cbg, br);
        DeleteObject(br);
    }

    /* Selection highlight: after every background, before the text. */
    md_sel_fill(hdc, rc, src, src + byte_len, px, py, sz.cy, INT_MAX, &cm,
                under);

    COLORREF clr = (span->type == MD_SPAN_CODE) ? code_clr : default_clr;
    SetTextColor(hdc, clr);

    TextOutW(hdc, px, py, wb.ptr, wb.len);

    if (span->type == MD_SPAN_STRIKETHROUGH) {
        TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
        int mid_y = py + tm.tmHeight / 2;
        HPEN pen = CreatePen(PS_SOLID, 1, default_clr);
        HPEN old_pen = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, px, mid_y, NULL);
        LineTo(hdc, px + sz.cx, mid_y);
        SelectObject(hdc, old_pen);
        DeleteObject(pen);
    }

    md_record(rc, px, py, sz.cx, sz.cy, src, src + byte_len, &cm, 0, NULL);

    SelectObject(hdc, old_font);
    if (created) DeleteObject(created);
    mdcum_free(&cm);
    mdbuf_free(&wb);
}

/* ── Table cell measurement (used for column fitting + alignment) ────── */

/* Natural (unwrapped) pixel width of one table cell's inline content: the
 * sum of every word's width, laid out on a single line. `text`/`len` is
 * already trimmed by md_table_split_row, so no leading/trailing
 * whitespace token throws this off. */
static int measure_cell_natural_width(HDC hdc, const char *text, int len,
                                      HFONT hFont, HFONT hMonoFont,
                                      HFONT hBoldFont)
{
    if (len <= 0) return 0;

    MdSpan spans[MD_MAX_SPANS];
    int span_count = md_parse_inline(text, len, spans);
    int width = 0;

    for (int s = 0; s < span_count; s++) {
        const MdSpan *span = &spans[s];
        int off = span->start, span_end = span->end;
        while (off < span_end) {
            int wstart = 0, wend = 0;
            if (!md_next_word(text, span_end, off, &wstart, &wend)) break;
            int word_h = 0;
            width += measure_word(hdc, text, wstart, wend - wstart, span,
                                  hFont, hMonoFont, hBoldFont, &word_h);
            off = wend;
        }
    }
    return width;
}

/* Widest single word in a table cell -- the floor a column can never
 * shrink below (md_table_fit_columns' `minimum`). md_next_word groups a
 * word with any whitespace that immediately follows it, so that trailing
 * run is trimmed off before measuring each token. */
static int measure_cell_widest_word(HDC hdc, const char *text, int len,
                                    HFONT hFont, HFONT hMonoFont,
                                    HFONT hBoldFont)
{
    if (len <= 0) return 0;

    MdSpan spans[MD_MAX_SPANS];
    int span_count = md_parse_inline(text, len, spans);
    int maxw = 0;

    for (int s = 0; s < span_count; s++) {
        const MdSpan *span = &spans[s];
        int off = span->start, span_end = span->end;
        while (off < span_end) {
            int wstart = 0, wend = 0;
            if (!md_next_word(text, span_end, off, &wstart, &wend)) break;
            if (text[wstart] != ' ' && text[wstart] != '\t') {
                int word_end = wend;
                while (word_end > wstart &&
                       (text[word_end - 1] == ' ' || text[word_end - 1] == '\t'))
                    word_end--;
                int word_h = 0;
                int w = measure_word(hdc, text, wstart, word_end - wstart, span,
                                     hFont, hMonoFont, hBoldFont, &word_h);
                if (w > maxw) maxw = w;
            }
            off = wend;
        }
    }
    return maxw;
}

/* ── Word-by-word inline layouter ────────────────────────────────────── */

/* Lays out parsed spans on one or more visual lines starting at (x, y),
 * wrapping to column `x` when a token won't fit at `cur_x`. Returns the
 * total height consumed (including the last line's height).
 *
 * `src` is line[0]'s source offset and `bg` the colour under the text, for
 * the run context `rc` (may be NULL). `plain` (MD_MODE_PLAIN) takes the
 * whole line as one text span and applies the two plain-only rules: the
 * whitespace a source line starts with is kept (until the first soft wrap),
 * and a word wider than the whole line is broken at a character boundary.
 * `fill_right` lets the selection fill run to x + max_width at the end of a
 * visual line the selection continues past (not for table cells). */
static int render_inline_spans(HDC hdc, const char *line, int line_len,
                               int x, int y, int max_width,
                               HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                               const ThemeColors *theme, int paint,
                               COLORREF default_clr, COLORREF code_clr,
                               MdRunCtx *rc, int src, COLORREF bg,
                               int plain, int fill_right)
{
    /* Default line height (used for empty lines and as min line height). */
    int default_lh;
    {
        HFONT old = (HFONT)SelectObject(hdc, hFont);
        TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
        default_lh = tm.tmHeight;
        SelectObject(hdc, old);
    }

    MdSpan spans[MD_MAX_SPANS];
    int span_count = 0;
    if (line_len > 0) {
        if (plain) {
            spans[0].type = MD_SPAN_TEXT;
            spans[0].start = 0;
            spans[0].end = line_len;
            span_count = 1;
        } else {
            span_count = md_parse_inline(line, line_len, spans);
        }
    }

    int max_right = x + max_width;
    int cur_x = x;
    int cur_y = y;
    int line_h = default_lh;
    int at_line_start = 1;   /* true if cur_x == x (drop leading whitespace) */
    int keep_ws = plain;     /* plain: keep the source line's leading spaces */
    int placed = 0;          /* tokens painted or measured on this call */

    for (int s = 0; s < span_count; s++) {
        const MdSpan *span = &spans[s];
        int span_end = span->end;
        int off = span->start;

        while (off < span_end) {
            int wstart = 0, wend = 0;
            if (!md_next_word(line, span_end, off, &wstart, &wend)) break;
            int w_byte_len = wend - wstart;
            int is_ws = (line[wstart] == ' ' || line[wstart] == '\t');

            int word_h = 0;
            int word_w = measure_word(hdc, line, wstart, w_byte_len,
                                      span, hFont, hMonoFont, hBoldFont,
                                      &word_h);

            /* Drop leading whitespace at the start of a wrapped line. */
            if (is_ws && at_line_start && !keep_ws) {
                off = wend;
                continue;
            }

            /* If this is whitespace that would push past max_right, just
             * end the line here (don't emit trailing whitespace before wrap). */
            if (is_ws && cur_x + word_w > max_right) {
                if (paint && fill_right)
                    md_eol_fill(hdc, rc, src + wend, 1, cur_x, cur_y,
                                max_right, line_h, bg);
                cur_y += line_h;
                cur_x = x;
                line_h = default_lh;
                at_line_start = 1;
                keep_ws = 0;
                off = wend;   /* consume the whitespace */
                continue;
            }

            /* Word doesn't fit on current line and we're not at line start —
             * wrap before placing it. */
            if (!is_ws && cur_x + word_w > max_right && !at_line_start) {
                if (paint && fill_right)
                    md_eol_fill(hdc, rc, src + wstart, 1, cur_x, cur_y,
                                max_right, line_h, bg);
                cur_y += line_h;
                cur_x = x;
                line_h = default_lh;
                at_line_start = 1;
                keep_ws = 0;
            }

            /* MD_MODE_PLAIN: a word wider than the whole line is broken at
             * a character boundary instead of overflowing. */
            if (plain && !is_ws) {
                int word_end = wend;
                while (word_end > wstart && (line[word_end - 1] == ' ' ||
                                             line[word_end - 1] == '\t'))
                    word_end--;
                int core_h = 0;
                int core_w = measure_word(hdc, line, wstart, word_end - wstart,
                                          span, hFont, hMonoFont, hBoldFont,
                                          &core_h);
                if (core_w > max_width) {
                    HFONT oldf = (HFONT)SelectObject(hdc, hFont);
                    int ps = wstart;
                    while (ps < wend) {
                        int nb = plain_fit_bytes(hdc, line + ps, wend - ps,
                                                 max_right - cur_x);
                        int ph = 0;
                        int pw = measure_word(hdc, line, ps, nb, span, hFont,
                                              hMonoFont, hBoldFont, &ph);
                        if (paint)
                            paint_word(hdc, line, ps, nb, span, cur_x, cur_y,
                                       hFont, hMonoFont, hBoldFont, theme,
                                       default_clr, code_clr, rc, src + ps, bg);
                        cur_x += pw;
                        if (ph > line_h) line_h = ph;
                        placed++;
                        ps += nb;
                        if (ps < wend) {
                            if (paint && fill_right)
                                md_eol_fill(hdc, rc, src + ps, 1, cur_x, cur_y,
                                            max_right, line_h, bg);
                            cur_y += line_h;
                            cur_x = x;
                            line_h = default_lh;
                        }
                    }
                    SelectObject(hdc, oldf);
                    at_line_start = 0;
                    keep_ws = 0;
                    off = wend;
                    continue;
                }
            }

            /* Place the token. */
            if (paint) {
                paint_word(hdc, line, wstart, w_byte_len, span,
                           cur_x, cur_y,
                           hFont, hMonoFont, hBoldFont, theme,
                           default_clr, code_clr, rc, src + wstart, bg);
            }
            cur_x += word_w;
            if (word_h > line_h) line_h = word_h;
            at_line_start = 0;
            placed++;
            off = wend;
        }
    }

    if (paint) {
        /* A line that drew no word still gets a run, so a click lands on
         * it; the selection fill continues past its end like any line. */
        if (placed == 0)
            md_record_empty(rc, x, y, default_lh, src + line_len);
        if (fill_right)
            md_eol_fill(hdc, rc, src + line_len, 0, cur_x, cur_y, max_right,
                        line_h, bg);
    }

    /* Account for the final line. */
    return (cur_y - y) + line_h;
}

/* ── Table block ──────────────────────────────────────────────────────── */

/* One row's NUL-terminated line text, owned (malloc'd) by the table block
 * for the duration of measure+paint. */
typedef struct {
    char *text;
    int   len;
    int   src;    /* source offset of the row's first byte (runs) */
} MdTableRow;

/* Cell `idx` of a split row, or an empty view past the row's actual cell
 * count (a ragged row with fewer cells than the header). */
static void md_table_cell_at(const MdTableCell *cells, int count, int idx,
                             const char **out_text, int *out_len)
{
    if (idx >= 0 && idx < count) {
        *out_text = cells[idx].start;
        *out_len = cells[idx].len;
    } else {
        *out_text = "";
        *out_len = 0;
    }
}

/* Gathers the run of consecutive MD_LINE_TABLE lines starting with
 * `first_line` (already classified by the caller) as one table block,
 * lays out its columns (natural widths that shrink-widest-first down to
 * a per-column minimum via md_table_fit_columns), and measures or paints
 * it. `*p_next` must already point just past `first_line` in `text`
 * (i.e. right after its newline, or at the terminating NUL); on return it
 * is advanced past every row consumed into the block, so the caller's
 * own line loop can resume there. Returns the total height consumed.
 *
 * Runs: `first_src` is the first row's source offset; a following row's is
 * base + (its position - text0), text0 being the text the caller walks.
 * Each cell records its runs at the row's offset plus the cell's offset in
 * the row; header cells sit on the header fill, body cells on rc->surface. */
static int md_render_table_block(HDC hdc, const char *first_line,
                                 int first_line_len, const char **p_next,
                                 int x, int y, int max_width,
                                 HFONT hFont, HFONT hMonoFont,
                                 HFONT hBoldFont, const ThemeColors *theme,
                                 int paint,
                                 COLORREF structure_clr, COLORREF code_clr,
                                 MdRunCtx *rc, int first_src,
                                 const char *text0, int base)
{
    COLORREF body_clr = RGB_FROM_THEME(theme->text_main);
    MdTableRow rows[MD_TABLE_MAX_ROWS];
    int n_rows = 0;

    /* Row 0: the line the caller already classified as MD_LINE_TABLE. */
    rows[n_rows].text = (char *)malloc((size_t)first_line_len + 1);
    if (rows[n_rows].text) {
        memcpy(rows[n_rows].text, first_line, (size_t)first_line_len);
        rows[n_rows].text[first_line_len] = '\0';
        rows[n_rows].len = first_line_len;
        rows[n_rows].src = first_src;
        n_rows++;
    }

    /* Consume every following consecutive MD_LINE_TABLE line. */
    const char *p = *p_next;
    while (n_rows > 0 && *p && n_rows < MD_TABLE_MAX_ROWS) {
        const char *eol = p;
        while (*eol && *eol != '\n') eol++;
        int line_len = (int)(eol - p);

        char stack_buf[512];
        char *line_buf;
        int used_malloc = 0;
        if (line_len < (int)sizeof(stack_buf)) {
            line_buf = stack_buf;
        } else {
            line_buf = (char *)malloc((size_t)line_len + 1);
            if (!line_buf) break;
            used_malloc = 1;
        }
        memcpy(line_buf, p, (size_t)line_len);
        line_buf[line_len] = '\0';

        MdLineInfo info = md_classify_line(line_buf, 0);
        int is_table = (info.type == MD_LINE_TABLE);

        if (is_table) {
            rows[n_rows].text = (char *)malloc((size_t)line_len + 1);
            if (rows[n_rows].text) {
                memcpy(rows[n_rows].text, line_buf, (size_t)line_len);
                rows[n_rows].text[line_len] = '\0';
                rows[n_rows].len = line_len;
                rows[n_rows].src = base + (int)(p - text0);
                n_rows++;
            }
        }

        if (used_malloc) free(line_buf);
        if (!is_table) break;

        p = (*eol == '\n') ? eol + 1 : eol;
    }
    *p_next = p;

    if (n_rows == 0) return 0;   /* row 0's malloc failed -- bail cleanly */

    /* ---- Header/separator/body split, columns, alignment ---- */
    int align[MD_TABLE_MAX_COLS];
    for (int i = 0; i < MD_TABLE_MAX_COLS; i++) align[i] = MD_ALIGN_LEFT;

    /* The drawn column count is md_table_drawn_cols()'s, shared with the
     * hit-test and the copier; the alignments are still read here. */
    int has_sep = 0;
    int ncols = md_table_drawn_cols(rows[0].text,
                                    n_rows >= 2 ? rows[1].text : NULL,
                                    &has_sep);
    if (has_sep)
        (void)md_table_alignments(rows[1].text, align, MD_TABLE_MAX_COLS);

    MdTableCell header_cells[MD_TABLE_MAX_COLS];
    int header_n = md_table_split_row(rows[0].text, header_cells,
                                      MD_TABLE_MAX_COLS);

    int body_start = has_sep ? 2 : 1;
    int n_body = n_rows - body_start;
    if (n_body < 0) n_body = 0;
    if (n_body > MD_TABLE_MAX_ROWS) n_body = MD_TABLE_MAX_ROWS;

    MdTableCell body_cells[MD_TABLE_MAX_ROWS][MD_TABLE_MAX_COLS];
    int body_cell_count[MD_TABLE_MAX_ROWS];
    for (int r = 0; r < n_body; r++) {
        body_cell_count[r] = md_table_split_row(rows[body_start + r].text,
                                                 body_cells[r],
                                                 MD_TABLE_MAX_COLS);
    }

    /* ---- Column widths: natural + minimum (widest word, floored at a
     * fixed em count), then shrink-to-fit. ---- */
    TEXTMETRIC tm_body;
    {
        HFONT old = (HFONT)SelectObject(hdc, hFont);
        GetTextMetrics(hdc, &tm_body);
        SelectObject(hdc, old);
    }
    int default_line_h = tm_body.tmHeight;
    int min_em_w = tm_body.tmAveCharWidth * MD_TABLE_MIN_EM;

    int col_natural[MD_TABLE_MAX_COLS], col_min[MD_TABLE_MAX_COLS];
    int col_w[MD_TABLE_MAX_COLS];
    for (int j = 0; j < ncols; j++) {
        const char *ctext; int clen;

        md_table_cell_at(header_cells, header_n, j, &ctext, &clen);
        int nat = measure_cell_natural_width(hdc, ctext, clen, hBoldFont,
                                             hMonoFont, hBoldFont);
        int minw = measure_cell_widest_word(hdc, ctext, clen, hBoldFont,
                                            hMonoFont, hBoldFont);

        for (int r = 0; r < n_body; r++) {
            md_table_cell_at(body_cells[r], body_cell_count[r], j,
                             &ctext, &clen);
            int rn = measure_cell_natural_width(hdc, ctext, clen, hFont,
                                                hMonoFont, hBoldFont);
            int rm = measure_cell_widest_word(hdc, ctext, clen, hFont,
                                              hMonoFont, hBoldFont);
            if (rn > nat) nat = rn;
            if (rm > minw) minw = rm;
        }

        if (minw < min_em_w) minw = min_em_w;
        if (nat < minw) nat = minw;

        col_natural[j] = nat + 2 * MD_TABLE_PAD_H;
        col_min[j] = minw + 2 * MD_TABLE_PAD_H;
    }

    md_table_fit_columns(col_natural, col_min, ncols, max_width, col_w);

    /* ---- Row heights (measure pass; shared by measure-only callers and
     * as the layout paint uses). ---- */
    int header_h = 0;
    for (int j = 0; j < ncols; j++) {
        const char *ctext; int clen;
        md_table_cell_at(header_cells, header_n, j, &ctext, &clen);
        int content_w = col_w[j] - 2 * MD_TABLE_PAD_H;
        if (content_w < 1) content_w = 1;
        int h = clen > 0
            ? render_inline_spans(hdc, ctext, clen, 0, 0, content_w,
                                  hBoldFont, hMonoFont, hBoldFont, theme, 0,
                                  structure_clr, code_clr,
                                  NULL, 0, 0, 0, 0)
            : default_line_h;
        if (h > header_h) header_h = h;
    }
    header_h += 2 * MD_TABLE_PAD_V;

    int body_h[MD_TABLE_MAX_ROWS];
    int total_body_h = 0;
    for (int r = 0; r < n_body; r++) {
        int rh = 0;
        for (int j = 0; j < ncols; j++) {
            const char *ctext; int clen;
            md_table_cell_at(body_cells[r], body_cell_count[r], j,
                             &ctext, &clen);
            int content_w = col_w[j] - 2 * MD_TABLE_PAD_H;
            if (content_w < 1) content_w = 1;
            int h = clen > 0
                ? render_inline_spans(hdc, ctext, clen, 0, 0, content_w,
                                      hFont, hMonoFont, hBoldFont, theme, 0,
                                      body_clr, code_clr,
                                      NULL, 0, 0, 0, 0)
                : default_line_h;
            if (h > rh) rh = h;
        }
        rh += 2 * MD_TABLE_PAD_V;
        body_h[r] = rh;
        total_body_h += rh;
    }

    int table_h = header_h + total_body_h;

    /* ---- Paint ---- */
    if (paint) {
        int table_w = 0;
        for (int j = 0; j < ncols; j++) table_w += col_w[j];
        int table_right = x + table_w;

        COLORREF border_clr = RGB_FROM_THEME(theme->border);
        COLORREF dim_clr = RGB_FROM_THEME(theme->text_dim);
        COLORREF panel_clr = RGB_FROM_THEME(theme->bg_primary);
        COLORREF header_fill = rgb_alpha(dim_clr, panel_clr,
                                         MD_TABLE_HEADER_FILL_ALPHA);
        HBRUSH border_br = CreateSolidBrush(border_clr);

        int row_y = y;

        /* Header fill + text */
        RECT header_bg = { x, row_y, table_right, row_y + header_h };
        HBRUSH hb = CreateSolidBrush(header_fill);
        FillRect(hdc, &header_bg, hb);
        DeleteObject(hb);

        int col_x = x;
        for (int j = 0; j < ncols; j++) {
            const char *ctext; int clen;
            md_table_cell_at(header_cells, header_n, j, &ctext, &clen);
            int content_w = col_w[j] - 2 * MD_TABLE_PAD_H;
            if (content_w < 1) content_w = 1;
            if (clen > 0) {
                int natural = measure_cell_natural_width(hdc, ctext, clen,
                                                          hBoldFont, hMonoFont,
                                                          hBoldFont);
                int off = 0;
                if (natural <= content_w) {
                    if (align[j] == MD_ALIGN_RIGHT) off = content_w - natural;
                    else if (align[j] == MD_ALIGN_CENTER)
                        off = (content_w - natural) / 2;
                }
                render_inline_spans(hdc, ctext, clen,
                                    col_x + MD_TABLE_PAD_H + off,
                                    row_y + MD_TABLE_PAD_V,
                                    content_w - off,
                                    hBoldFont, hMonoFont, hBoldFont,
                                    theme, 1, structure_clr, code_clr,
                                    rc, rows[0].src + (int)(ctext - rows[0].text),
                                    header_fill, 0, 0);
            }
            col_x += col_w[j];
        }
        row_y += header_h;

        /* Header/body separator */
        {
            RECT sep = { x, row_y, table_right, row_y + STROKE_HAIRLINE };
            FillRect(hdc, &sep, border_br);
        }

        /* Body rows */
        for (int r = 0; r < n_body; r++) {
            col_x = x;
            for (int j = 0; j < ncols; j++) {
                const char *ctext; int clen;
                md_table_cell_at(body_cells[r], body_cell_count[r], j,
                                 &ctext, &clen);
                int content_w = col_w[j] - 2 * MD_TABLE_PAD_H;
                if (content_w < 1) content_w = 1;
                if (clen > 0) {
                    int natural = measure_cell_natural_width(hdc, ctext, clen,
                                                              hFont, hMonoFont,
                                                              hBoldFont);
                    int off = 0;
                    if (natural <= content_w) {
                        if (align[j] == MD_ALIGN_RIGHT)
                            off = content_w - natural;
                        else if (align[j] == MD_ALIGN_CENTER)
                            off = (content_w - natural) / 2;
                    }
                    const MdTableRow *brow = &rows[body_start + r];
                    render_inline_spans(hdc, ctext, clen,
                                        col_x + MD_TABLE_PAD_H + off,
                                        row_y + MD_TABLE_PAD_V,
                                        content_w - off,
                                        hFont, hMonoFont, hBoldFont,
                                        theme, 1, body_clr, code_clr,
                                        rc, brow->src + (int)(ctext - brow->text),
                                        rc ? rc->surface : panel_clr, 0, 0);
                }
                col_x += col_w[j];
            }
            row_y += body_h[r];

            if (r < n_body - 1) {
                RECT rsep = { x, row_y, table_right, row_y + STROKE_HAIRLINE };
                FillRect(hdc, &rsep, border_br);
            }
        }

        /* Outer hairline border (no vertical column lines). */
        {
            RECT top_rc = { x, y, table_right, y + STROKE_HAIRLINE };
            RECT bot_rc = { x, y + table_h - STROKE_HAIRLINE, table_right,
                            y + table_h };
            RECT left_rc = { x, y, x + STROKE_HAIRLINE, y + table_h };
            RECT right_rc = { table_right - STROKE_HAIRLINE, y, table_right,
                              y + table_h };
            FillRect(hdc, &top_rc, border_br);
            FillRect(hdc, &bot_rc, border_br);
            FillRect(hdc, &left_rc, border_br);
            FillRect(hdc, &right_rc, border_br);
        }

        DeleteObject(border_br);
    }

    for (int i = 0; i < n_rows; i++)
        if (rows[i].text) free(rows[i].text);

    return table_h;
}

/* ── Core: shared render/measure logic ───────────────────────────────── */

/* MD_MODE_PLAIN: every source line ('\n', or CR LF) a hard break, laid out
 * by render_inline_spans' plain rules in hFont and `clr`. No spacing is
 * added between lines, as DrawText added none. */
static int md_render_plain(HDC hdc, const char *text, int x, int y,
                           int max_width, HFONT hFont,
                           const ThemeColors *theme, int paint, COLORREF clr,
                           MdRunCtx *rc)
{
    int len = (int)strlen(text);
    int base = rc ? rc->base : 0;
    COLORREF bg = rc ? rc->surface : 0;

    int cur_y = y, pos = 0, ls = 0, le = 0;
    while (text_next_line(text, len, &pos, &ls, &le)) {
        cur_y += render_inline_spans(hdc, text + ls, le - ls, x, cur_y,
                                     max_width, hFont, hFont, hFont, theme,
                                     paint, clr, clr, rc, base + ls, bg, 1, 1);
    }
    return cur_y - y;
}

static int md_render_core(HDC hdc, const char *text, int x, int y,
                          int max_width,
                          HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                          const ThemeColors *theme, int paint,
                          MdBlockOut *bo, MdRunCtx *rc)
{
    if (bo) bo->count = 0;
    if (!text || !*text || max_width <= 0)
        return 0;

    /* The two AI-reply intent hues (see md_role_clr): "structure" for
     * headings/list markers/blockquote bar/table header text, "code" for
     * inline code text/fenced-code left bar. Resolved once per render so
     * every line reuses the same (contrast-guarded) colour. */
    COLORREF structure_clr = md_role_clr(theme, theme->link);
    COLORREF code_clr = md_role_clr(theme, theme->info);
    COLORREF body_clr = RGB_FROM_THEME(theme->text_main);
    COLORREF dim_text_clr = RGB_FROM_THEME(theme->text_dim);
    COLORREF code_bg = RGB_FROM_THEME(theme->chat.cmd_bg);
    COLORREF surface = rc ? rc->surface : RGB_FROM_THEME(theme->bg_primary);
    int base = rc ? rc->base : 0;   /* source offset of text[0] */

    int cur_y = y;
    int in_code_block = 0;
    int cur_block = -1;  /* index of the fenced block being walked, or -1 */
    int olist_num = 0;   /* current ordered list number */

    /* Process line by line */
    const char *p = text;
    while (*p) {
        /* Find end of current line */
        const char *eol = p;
        while (*eol && *eol != '\n') eol++;
        int line_len = (int)(eol - p);
        int src = base + (int)(p - text);   /* this line's source offset */

        /* Make a NUL-terminated copy for md_classify_line */
        char stack_buf[512];
        char *line_buf;
        if (line_len < (int)sizeof(stack_buf)) {
            line_buf = stack_buf;
        } else {
            line_buf = (char *)malloc((size_t)line_len + 1);
            if (!line_buf) break;
        }
        memcpy(line_buf, p, (size_t)line_len);
        line_buf[line_len] = '\0';

        MdLineInfo info = md_classify_line(line_buf, in_code_block);

        /* A table is a run of consecutive MD_LINE_TABLE lines, handled as
         * one block (own column layout) rather than line-by-line like
         * every other case below -- so it's intercepted before the
         * switch, and consumes as many following lines as belong to it. */
        if (info.type == MD_LINE_TABLE) {
            olist_num = 0;
            const char *next_p = (*eol == '\n') ? eol + 1 : eol;
            int h = md_render_table_block(hdc, line_buf, line_len, &next_p,
                                          x, cur_y, max_width,
                                          hFont, hMonoFont, hBoldFont,
                                          theme, paint,
                                          structure_clr, code_clr,
                                          paint ? rc : NULL, src, text, base);
            cur_y += h + MD_LINE_SPACING;
            if (line_buf != stack_buf) free(line_buf);
            p = next_p;
            continue;
        }

        switch (info.type) {
        case MD_LINE_CODE_FENCE:
            in_code_block = !in_code_block;
            if (in_code_block) {
                /* Start of code block — add a small gap */
                cur_y += MD_CODE_PAD_V;
                /* One entry per fence opened, in the order md_code_blocks()
                 * reports them (both walk md_classify_line), so an empty
                 * body keeps its slot as a zero rect. */
                cur_block = -1;
                if (bo) {
                    int bi = bo->count++;
                    if (bi < bo->max_rects && bo->rects) {
                        bo->rects[bi].x = bo->rects[bi].y = 0;
                        bo->rects[bi].w = bo->rects[bi].h = 0;
                        cur_block = bi;
                    }
                }
            } else {
                /* End of code block */
                cur_y += MD_CODE_PAD_V;
                cur_block = -1;
            }
            break;

        case MD_LINE_CODE: {
            /* One code line: monospace on the code background, drawn with
             * ExtTextOutW clipped at text_w -- no prefix processing (so
             * "cd a && make" shows both ampersands) and one run per line. */
            MdWBuf wb;
            mdbuf_init(&wb, line_buf, line_len);

            HFONT old_font = (HFONT)SelectObject(hdc, hMonoFont);
            /* A caller that draws a copy glyph on the block asks for its
             * right-hand margin to stay free of text. Code lines never
             * wrap, so this cannot change a height. */
            int text_w = max_width;
            if (bo && bo->reserve_w > 0) {
                text_w = max_width - bo->reserve_w;
                if (text_w < 1) text_w = 1;
            }
            TEXTMETRIC tm;
            GetTextMetrics(hdc, &tm);
            int h = tm.tmHeight;

            RECT bg_rc;
            bg_rc.left   = x - MD_CODE_PAD_H;
            bg_rc.top    = cur_y;
            bg_rc.right  = x + max_width + MD_CODE_PAD_H;
            bg_rc.bottom = cur_y + h;

            /* Grow the block's rect to the union of its line backgrounds. */
            if (bo && cur_block >= 0 && cur_block < bo->max_rects) {
                NsRect *br = &bo->rects[cur_block];
                if (br->w == 0 && br->h == 0) {
                    br->x = bg_rc.left;
                    br->y = bg_rc.top;
                    br->w = bg_rc.right - bg_rc.left;
                    br->h = bg_rc.bottom - bg_rc.top;
                } else {
                    int bot = bg_rc.bottom;
                    if (br->y + br->h > bot) bot = br->y + br->h;
                    br->h = bot - br->y;
                }
            }

            if (paint) {
                /* Background rectangle */
                HBRUSH bg_br = CreateSolidBrush(code_bg);
                FillRect(hdc, &bg_rc, bg_br);
                DeleteObject(bg_br);

                /* Code hue left bar (fenced code block only -- inline
                 * code gets the hue on its text instead, see paint_word). */
                RECT bar_rc = bg_rc;
                bar_rc.right = bar_rc.left + MD_CODE_BAR_WIDTH;
                HBRUSH bar_br = CreateSolidBrush(code_clr);
                FillRect(hdc, &bar_rc, bar_br);
                DeleteObject(bar_br);

                if (line_len == 0) {
                    md_record_empty(rc, x, cur_y, h, src);
                    md_eol_fill(hdc, rc, src, 0, x, cur_y, x + text_w, h,
                                code_bg);
                } else {
                    MdCum cm;
                    SIZE sz;
                    mdcum_measure(hdc, &wb, line_buf, line_len, &cm, &sz);
                    md_sel_fill(hdc, rc, src, src + line_len, x, cur_y, h,
                                x + text_w, &cm, code_bg);
                    SetTextColor(hdc, body_clr);
                    RECT clip = { x, cur_y, x + text_w, cur_y + h };
                    ExtTextOutW(hdc, x, cur_y, ETO_CLIPPED, &clip,
                                wb.ptr, (UINT)wb.len, NULL);
                    int run_w = sz.cx < text_w ? sz.cx : text_w;
                    md_record(rc, x, cur_y, run_w, h, src, src + line_len,
                              &cm, TR_FLAG_CODE, NULL);
                    md_eol_fill(hdc, rc, src + line_len, 0, x + run_w, cur_y,
                                x + text_w, h, code_bg);
                    mdcum_free(&cm);
                }
            }

            cur_y += h;
            SelectObject(hdc, old_font);
            mdbuf_free(&wb);
            break;
        }

        case MD_LINE_HEADING: {
            cur_y += MD_HEADING_EXTRA_V;
            const char *content = line_buf + info.content_offset;
            int content_len = line_len - info.content_offset;

            /* Create a scaled bold font for headings:
             * h1 = 150%, h2 = 125%, h3 = 110% of base */
            LOGFONT lf;
            GetObject(hBoldFont, sizeof(lf), &lf);
            if (info.heading_level == 1)
                lf.lfHeight = (LONG)(lf.lfHeight * 150 / 100);
            else if (info.heading_level == 2)
                lf.lfHeight = (LONG)(lf.lfHeight * 125 / 100);
            else
                lf.lfHeight = (LONG)(lf.lfHeight * 110 / 100);

            HFONT heading_font = CreateFontIndirect(&lf);
            HFONT use_font = heading_font ? heading_font : hBoldFont;

            HFONT old_font = (HFONT)SelectObject(hdc, use_font);
            int h = render_inline_spans(hdc, content, content_len,
                                        x, cur_y, max_width,
                                        use_font, hMonoFont, use_font,
                                        theme, paint,
                                        structure_clr, code_clr,
                                        rc, src + info.content_offset,
                                        surface, 0, 1);
            cur_y += h + MD_HEADING_EXTRA_V;
            SelectObject(hdc, old_font);
            if (heading_font) DeleteObject(heading_font);
            break;
        }

        case MD_LINE_ULIST: {
            const char *content = line_buf + info.content_offset;
            int content_len = line_len - info.content_offset;

            /* Draw bullet */
            if (paint) {
                /* Bullet character; a synthetic run "- " over the marker's
                 * source bytes (the line start up to content_offset),
                 * filled when the selection touches them. */
                static const wchar_t bullet[] = L"\x2022 ";
                HFONT old_font = (HFONT)SelectObject(hdc, hFont);
                TEXTMETRIC tm;
                GetTextMetrics(hdc, &tm);
                md_synth_fill(hdc, rc, src, src + info.content_offset, x,
                              cur_y, MD_LIST_INDENT, tm.tmHeight, surface);
                SetTextColor(hdc, structure_clr);
                RECT brc;
                brc.left   = x;
                brc.top    = cur_y;
                brc.right  = x + MD_LIST_INDENT;
                brc.bottom = cur_y + 1000;
                DrawTextW(hdc, bullet, 2, &brc, DT_LEFT | DT_TOP);
                md_record(rc, x, cur_y, MD_LIST_INDENT, tm.tmHeight, src,
                          src + info.content_offset, NULL, 0, "- ");
                SelectObject(hdc, old_font);
            }

            int h = render_inline_spans(hdc, content, content_len,
                                        x + MD_LIST_INDENT, cur_y,
                                        max_width - MD_LIST_INDENT,
                                        hFont, hMonoFont, hBoldFont,
                                        theme, paint, body_clr, code_clr,
                                        rc, src + info.content_offset,
                                        surface, 0, 1);
            cur_y += h + MD_LINE_SPACING;
            break;
        }

        case MD_LINE_OLIST: {
            const char *content = line_buf + info.content_offset;
            int content_len = line_len - info.content_offset;
            olist_num++;

            /* Draw number prefix */
            if (paint) {
                char num_str[16];
                int num_len = snprintf(num_str, sizeof(num_str),
                                       "%d. ", olist_num);
                MdWBuf nb;
                mdbuf_init(&nb, num_str, num_len);
                HFONT old_font = (HFONT)SelectObject(hdc, hFont);
                TEXTMETRIC tm;
                GetTextMetrics(hdc, &tm);
                md_synth_fill(hdc, rc, src, src + info.content_offset, x,
                              cur_y, MD_LIST_INDENT, tm.tmHeight, surface);
                SetTextColor(hdc, structure_clr);
                RECT nrc;
                nrc.left   = x;
                nrc.top    = cur_y;
                nrc.right  = x + MD_LIST_INDENT;
                nrc.bottom = cur_y + 1000;
                DrawTextW(hdc, nb.ptr, nb.len, &nrc, DT_LEFT | DT_TOP);
                md_record(rc, x, cur_y, MD_LIST_INDENT, tm.tmHeight, src,
                          src + info.content_offset, NULL, 0, num_str);
                SelectObject(hdc, old_font);
                mdbuf_free(&nb);
            }

            int h = render_inline_spans(hdc, content, content_len,
                                        x + MD_LIST_INDENT, cur_y,
                                        max_width - MD_LIST_INDENT,
                                        hFont, hMonoFont, hBoldFont,
                                        theme, paint, body_clr, code_clr,
                                        rc, src + info.content_offset,
                                        surface, 0, 1);
            cur_y += h + MD_LINE_SPACING;
            break;
        }

        case MD_LINE_TABLE:
            /* Unreachable: handled as a whole block, and control flow
             * `continue`s past this switch, before this line is ever
             * classified as MD_LINE_TABLE here. Kept only so this switch
             * stays exhaustive over MdLineType. */
            break;

        case MD_LINE_BLOCKQUOTE: {
            const char *content = line_buf + info.content_offset;
            int content_len = line_len - info.content_offset;

            int h = render_inline_spans(hdc, content, content_len,
                                        x + MD_BLOCKQUOTE_IND, cur_y,
                                        max_width - MD_BLOCKQUOTE_IND,
                                        hFont, hMonoFont, hBoldFont,
                                        theme, paint, dim_text_clr, code_clr,
                                        rc, src + info.content_offset,
                                        surface, 0, 1);
            if (paint) {
                /* Draw left bar (structure hue) */
                RECT bar_rc;
                bar_rc.left   = x;
                bar_rc.top    = cur_y;
                bar_rc.right  = x + MD_BQ_BAR_WIDTH;
                bar_rc.bottom = cur_y + h;
                HBRUSH bar_br = CreateSolidBrush(structure_clr);
                FillRect(hdc, &bar_rc, bar_br);
                DeleteObject(bar_br);
            }
            cur_y += h + MD_LINE_SPACING;
            break;
        }

        case MD_LINE_HRULE: {
            cur_y += MD_PARA_SPACING;
            if (paint) {
                RECT hr_rc;
                hr_rc.left   = x;
                hr_rc.top    = cur_y;
                hr_rc.right  = x + max_width;
                hr_rc.bottom = cur_y + MD_HRULE_HEIGHT;
                HBRUSH hr_br = CreateSolidBrush(
                    RGB_FROM_THEME(theme->border));
                FillRect(hdc, &hr_rc, hr_br);
                DeleteObject(hr_br);
            }
            cur_y += MD_HRULE_HEIGHT + MD_PARA_SPACING;
            break;
        }

        case MD_LINE_EMPTY: {
            /* Reset ordered list counter on blank line */
            olist_num = 0;
            /* Real paragraph break: full font line height plus MD_LINE_SPACING.
             * MD_PARA_SPACING (6 px) made \n\n indistinguishable from \n. */
            HFONT old = (HFONT)SelectObject(hdc, hFont);
            TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
            SelectObject(hdc, old);
            if (paint) {
                md_record_empty(rc, x, cur_y, tm.tmHeight, src);
                md_eol_fill(hdc, rc, src, 0, x, cur_y, x + max_width,
                            tm.tmHeight, surface);
            }
            cur_y += tm.tmHeight + MD_LINE_SPACING;
            break;
        }

        case MD_LINE_PARAGRAPH: {
            /* Reset ordered list counter */
            olist_num = 0;
            int h = render_inline_spans(hdc, line_buf, line_len,
                                        x, cur_y, max_width,
                                        hFont, hMonoFont, hBoldFont,
                                        theme, paint, body_clr, code_clr,
                                        rc, src, surface, 0, 1);
            cur_y += h + MD_LINE_SPACING;
            break;
        }
        }

        if (line_buf != stack_buf) free(line_buf);

        /* Advance past the newline */
        if (*eol == '\n')
            p = eol + 1;
        else
            p = eol;   /* end of string */
    }

    return cur_y - y;
}

/* ── Public API ──────────────────────────────────────────────────────── */

int md_render_ex(HDC hdc, const char *text, int x, int y, int max_width,
                 HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                 const ThemeColors *theme, int mode, int paint,
                 COLORREF plain_clr, MdBlockOut *blocks, MdRunCtx *rc)
{
    if (!hdc || !text || !theme)
        return 0;
    if (!paint) {
        x = 0;
        y = 0;
        rc = NULL;
    }

    int old_bk = 0;
    COLORREF old_color = 0;
    if (paint) {
        old_bk = SetBkMode(hdc, TRANSPARENT);
        old_color = GetTextColor(hdc);
    }
    int result;
    if (mode == MD_MODE_PLAIN) {
        if (blocks) blocks->count = 0;
        result = (*text && max_width > 0)
            ? md_render_plain(hdc, text, x, y, max_width, hFont, theme,
                              paint, plain_clr, rc)
            : 0;
    } else {
        result = md_render_core(hdc, text, x, y, max_width,
                                hFont, hMonoFont, hBoldFont,
                                theme, paint, blocks, rc);
    }
    if (paint) {
        SetTextColor(hdc, old_color);
        SetBkMode(hdc, old_bk);
    }
    return result;
}

int md_render_text(HDC hdc, const char *text, int x, int y, int max_width,
                   HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                   const ThemeColors *theme)
{
    return md_render_ex(hdc, text, x, y, max_width, hFont, hMonoFont,
                        hBoldFont, theme, MD_MODE_MARKDOWN, 1, 0, NULL, NULL);
}

int md_measure_text(HDC hdc, const char *text, int max_width,
                    HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                    const ThemeColors *theme)
{
    return md_render_ex(hdc, text, 0, 0, max_width, hFont, hMonoFont,
                        hBoldFont, theme, MD_MODE_MARKDOWN, 0, 0, NULL, NULL);
}

int md_render_text_blocks(HDC hdc, const char *text, int x, int y,
                          int max_width,
                          HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                          const ThemeColors *theme, MdBlockOut *blocks)
{
    return md_render_ex(hdc, text, x, y, max_width, hFont, hMonoFont,
                        hBoldFont, theme, MD_MODE_MARKDOWN, 1, 0, blocks,
                        NULL);
}

#endif /* _WIN32 */
