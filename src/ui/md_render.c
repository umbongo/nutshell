/* src/ui/md_render.c — Markdown-to-GDI renderer.
 *
 * Uses the existing markdown.h inline parser (md_classify_line, md_parse_inline)
 * but renders to a GDI HDC instead of RichEdit.
 */

#ifdef _WIN32

#include "md_render.h"
#include "markdown.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Colour helper: theme stores 0x00RRGGBB, GDI wants 0x00BBGGRR ── */

#define RGB_FROM_THEME(c) \
    RGB(((c) >> 16) & 0xFF, ((c) >> 8) & 0xFF, (c) & 0xFF)

/* ── Layout constants ────────────────────────────────────────────────── */

#define MD_LINE_SPACING     2   /* Extra pixels between lines */
#define MD_PARA_SPACING     6   /* Extra pixels between paragraphs */
#define MD_CODE_PAD_H       4   /* Horizontal padding inside code blocks */
#define MD_CODE_PAD_V       2   /* Vertical padding inside code blocks */
#define MD_LIST_INDENT     16   /* Indent for list items */
#define MD_BLOCKQUOTE_IND  12   /* Indent for blockquotes */
#define MD_BQ_BAR_WIDTH     3   /* Width of blockquote left bar */
#define MD_HRULE_HEIGHT     1   /* Thickness of horizontal rule */
#define MD_HEADING_EXTRA_V  4   /* Extra vertical space around headings */

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

/* ── Internal: render or measure a single inline span ────────────────── */

/* Returns width consumed. Advances *px. Sets *line_h to max line height. */
static int render_span(HDC hdc, const char *line, const MdSpan *span,
                       int px, int py, int max_right,
                       HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                       const ThemeColors *theme, int paint,
                       int *out_height)
{
    const char *text = line + span->start;
    int byte_len = span->end - span->start;
    if (byte_len <= 0) {
        *out_height = 0;
        return 0;
    }

    MdWBuf wb;
    mdbuf_init(&wb, text, byte_len);
    if (wb.len <= 0) {
        mdbuf_free(&wb);
        *out_height = 0;
        return 0;
    }

    /* Select font based on span type */
    HFONT sel_font = hFont;
    switch (span->type) {
    case MD_SPAN_BOLD:
    case MD_SPAN_BOLD_ITALIC:
        sel_font = hBoldFont;
        break;
    case MD_SPAN_CODE:
        sel_font = hMonoFont;
        break;
    case MD_SPAN_ITALIC:
    case MD_SPAN_STRIKETHROUGH:
    case MD_SPAN_TEXT:
        sel_font = hFont;
        break;
    }

    HFONT old_font = (HFONT)SelectObject(hdc, sel_font);

    /* Measure */
    RECT rc_measure;
    rc_measure.left = px;
    rc_measure.top = py;
    rc_measure.right = max_right;
    rc_measure.bottom = py + 1000;
    int h = DrawTextW(hdc, wb.ptr, wb.len, &rc_measure,
                      DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
    int w = rc_measure.right - rc_measure.left;

    if (paint) {
        /* Inline code background */
        if (span->type == MD_SPAN_CODE) {
            RECT bg_rc;
            bg_rc.left   = px - 1;
            bg_rc.top    = py;
            bg_rc.right  = px + w + 1;
            bg_rc.bottom = py + h;
            HBRUSH bg_br = CreateSolidBrush(
                RGB_FROM_THEME(theme->chat.cmd_bg));
            FillRect(hdc, &bg_rc, bg_br);
            DeleteObject(bg_br);
        }

        /* Set text colour */
        COLORREF text_color;
        if (span->type == MD_SPAN_CODE) {
            text_color = RGB_FROM_THEME(theme->chat.cmd_text);
        } else {
            text_color = RGB_FROM_THEME(theme->text_main);
        }
        SetTextColor(hdc, text_color);

        /* Draw text */
        RECT rc_draw;
        rc_draw.left   = px;
        rc_draw.top    = py;
        rc_draw.right  = max_right;
        rc_draw.bottom = py + h;
        DrawTextW(hdc, wb.ptr, wb.len, &rc_draw,
                  DT_LEFT | DT_TOP | DT_WORDBREAK);

        /* Strikethrough: draw a line through the middle */
        if (span->type == MD_SPAN_STRIKETHROUGH) {
            int mid_y = py + h / 2;
            HPEN pen = CreatePen(PS_SOLID, 1,
                                 RGB_FROM_THEME(theme->text_main));
            HPEN old_pen = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, px, mid_y, NULL);
            LineTo(hdc, px + w, mid_y);
            SelectObject(hdc, old_pen);
            DeleteObject(pen);
        }
    }

    SelectObject(hdc, old_font);
    mdbuf_free(&wb);

    *out_height = h;
    return w;
}

/* ── Internal: render/measure inline spans for a single line ─────────── */

/* Returns height consumed. */
static int render_inline_spans(HDC hdc, const char *line, int line_len,
                               int x, int y, int max_width,
                               HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                               const ThemeColors *theme, int paint)
{
    MdSpan spans[MD_MAX_SPANS];
    int count = md_parse_inline(line, line_len, spans);

    if (count == 0) {
        /* Empty line — still occupies one line height */
        TEXTMETRIC tm;
        HFONT old = (HFONT)SelectObject(hdc, hFont);
        GetTextMetrics(hdc, &tm);
        SelectObject(hdc, old);
        return tm.tmHeight;
    }

    int max_h = 0;
    int cur_x = x;
    int cur_y = y;
    int max_right = x + max_width;

    for (int i = 0; i < count; i++) {
        int span_h = 0;
        int span_w = render_span(hdc, line, &spans[i],
                                 cur_x, cur_y, max_right,
                                 hFont, hMonoFont, hBoldFont,
                                 theme, paint, &span_h);
        cur_x += span_w;
        if (span_h > max_h) max_h = span_h;
    }

    return max_h > 0 ? max_h : MD_LINE_SPACING;
}

/* ── Core: shared render/measure logic ───────────────────────────────── */

static int md_render_core(HDC hdc, const char *text, int x, int y,
                          int max_width,
                          HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                          const ThemeColors *theme, int paint)
{
    if (!text || !*text || max_width <= 0)
        return 0;

    int cur_y = y;
    int in_code_block = 0;
    int olist_num = 0;   /* current ordered list number */

    /* Process line by line */
    const char *p = text;
    while (*p) {
        /* Find end of current line */
        const char *eol = p;
        while (*eol && *eol != '\n') eol++;
        int line_len = (int)(eol - p);

        /* Make a NUL-terminated copy for md_classify_line */
        char *line_buf = (char *)malloc((size_t)line_len + 1);
        if (!line_buf) break;
        memcpy(line_buf, p, (size_t)line_len);
        line_buf[line_len] = '\0';

        MdLineInfo info = md_classify_line(line_buf, in_code_block);

        switch (info.type) {
        case MD_LINE_CODE_FENCE:
            in_code_block = !in_code_block;
            if (in_code_block) {
                /* Start of code block — add a small gap */
                cur_y += MD_CODE_PAD_V;
            } else {
                /* End of code block */
                cur_y += MD_CODE_PAD_V;
            }
            break;

        case MD_LINE_CODE: {
            /* Render code line with monospace font and background */
            MdWBuf wb;
            mdbuf_init(&wb, line_buf, line_len);

            HFONT old_font = (HFONT)SelectObject(hdc, hMonoFont);
            RECT rc_m;
            rc_m.left   = x;
            rc_m.top    = cur_y;
            rc_m.right  = x + max_width;
            rc_m.bottom = cur_y + 1000;
            int h = DrawTextW(hdc, wb.ptr, wb.len, &rc_m,
                              DT_LEFT | DT_TOP | DT_CALCRECT);
            if (h == 0) {
                /* Empty code line — use font metrics */
                TEXTMETRIC tm;
                GetTextMetrics(hdc, &tm);
                h = tm.tmHeight;
            }

            if (paint) {
                /* Background rectangle */
                RECT bg_rc;
                bg_rc.left   = x - MD_CODE_PAD_H;
                bg_rc.top    = cur_y;
                bg_rc.right  = x + max_width + MD_CODE_PAD_H;
                bg_rc.bottom = cur_y + h;
                HBRUSH bg_br = CreateSolidBrush(
                    RGB_FROM_THEME(theme->chat.cmd_bg));
                FillRect(hdc, &bg_rc, bg_br);
                DeleteObject(bg_br);

                /* Draw code text */
                SetTextColor(hdc, RGB_FROM_THEME(theme->chat.cmd_text));
                RECT rc_d;
                rc_d.left   = x;
                rc_d.top    = cur_y;
                rc_d.right  = x + max_width;
                rc_d.bottom = cur_y + h;
                DrawTextW(hdc, wb.ptr, wb.len, &rc_d,
                          DT_LEFT | DT_TOP);
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

            /* Use bold font for headings */
            HFONT old_font = (HFONT)SelectObject(hdc, hBoldFont);

            /* For h1/h2, scale up by adjusting the font temporarily —
             * but since we only have the fonts passed in, we render
             * headings with the bold font and add extra spacing. */
            int h = render_inline_spans(hdc, content, content_len,
                                        x, cur_y, max_width,
                                        hBoldFont, hMonoFont, hBoldFont,
                                        theme, paint);
            cur_y += h + MD_HEADING_EXTRA_V;
            SelectObject(hdc, old_font);
            break;
        }

        case MD_LINE_ULIST: {
            const char *content = line_buf + info.content_offset;
            int content_len = line_len - info.content_offset;

            /* Draw bullet */
            if (paint) {
                /* Bullet character */
                static const wchar_t bullet[] = L"\x2022 ";
                HFONT old_font = (HFONT)SelectObject(hdc, hFont);
                SetTextColor(hdc, RGB_FROM_THEME(theme->text_main));
                RECT brc;
                brc.left   = x;
                brc.top    = cur_y;
                brc.right  = x + MD_LIST_INDENT;
                brc.bottom = cur_y + 1000;
                DrawTextW(hdc, bullet, 2, &brc, DT_LEFT | DT_TOP);
                SelectObject(hdc, old_font);
            }

            int h = render_inline_spans(hdc, content, content_len,
                                        x + MD_LIST_INDENT, cur_y,
                                        max_width - MD_LIST_INDENT,
                                        hFont, hMonoFont, hBoldFont,
                                        theme, paint);
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
                SetTextColor(hdc, RGB_FROM_THEME(theme->text_main));
                RECT nrc;
                nrc.left   = x;
                nrc.top    = cur_y;
                nrc.right  = x + MD_LIST_INDENT;
                nrc.bottom = cur_y + 1000;
                DrawTextW(hdc, nb.ptr, nb.len, &nrc, DT_LEFT | DT_TOP);
                SelectObject(hdc, old_font);
                mdbuf_free(&nb);
            }

            int h = render_inline_spans(hdc, content, content_len,
                                        x + MD_LIST_INDENT, cur_y,
                                        max_width - MD_LIST_INDENT,
                                        hFont, hMonoFont, hBoldFont,
                                        theme, paint);
            cur_y += h + MD_LINE_SPACING;
            break;
        }

        case MD_LINE_BLOCKQUOTE: {
            const char *content = line_buf + info.content_offset;
            int content_len = line_len - info.content_offset;

            int h = render_inline_spans(hdc, content, content_len,
                                        x + MD_BLOCKQUOTE_IND, cur_y,
                                        max_width - MD_BLOCKQUOTE_IND,
                                        hFont, hMonoFont, hBoldFont,
                                        theme, paint);
            if (paint) {
                /* Draw left bar */
                RECT bar_rc;
                bar_rc.left   = x;
                bar_rc.top    = cur_y;
                bar_rc.right  = x + MD_BQ_BAR_WIDTH;
                bar_rc.bottom = cur_y + h;
                HBRUSH bar_br = CreateSolidBrush(
                    RGB_FROM_THEME(theme->text_dim));
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

        case MD_LINE_TABLE: {
            /* Render table lines as monospace text */
            if (md_is_table_separator(line_buf)) {
                /* Skip separator lines — they're just formatting */
                break;
            }
            MdWBuf wb;
            mdbuf_init(&wb, line_buf, line_len);
            HFONT old_font = (HFONT)SelectObject(hdc, hMonoFont);

            RECT rc_m;
            rc_m.left   = x;
            rc_m.top    = cur_y;
            rc_m.right  = x + max_width;
            rc_m.bottom = cur_y + 1000;
            int h = DrawTextW(hdc, wb.ptr, wb.len, &rc_m,
                              DT_LEFT | DT_TOP | DT_CALCRECT);

            if (paint) {
                SetTextColor(hdc, RGB_FROM_THEME(theme->text_main));
                RECT rc_d;
                rc_d.left   = x;
                rc_d.top    = cur_y;
                rc_d.right  = x + max_width;
                rc_d.bottom = cur_y + h;
                DrawTextW(hdc, wb.ptr, wb.len, &rc_d, DT_LEFT | DT_TOP);
            }

            cur_y += h + MD_LINE_SPACING;
            SelectObject(hdc, old_font);
            mdbuf_free(&wb);
            break;
        }

        case MD_LINE_EMPTY:
            /* Reset ordered list counter on blank line */
            olist_num = 0;
            cur_y += MD_PARA_SPACING;
            break;

        case MD_LINE_PARAGRAPH: {
            /* Reset ordered list counter */
            olist_num = 0;
            int h = render_inline_spans(hdc, line_buf, line_len,
                                        x, cur_y, max_width,
                                        hFont, hMonoFont, hBoldFont,
                                        theme, paint);
            cur_y += h + MD_LINE_SPACING;
            break;
        }
        }

        free(line_buf);

        /* Advance past the newline */
        if (*eol == '\n')
            p = eol + 1;
        else
            p = eol;   /* end of string */
    }

    return cur_y - y;
}

/* ── Public API ──────────────────────────────────────────────────────── */

int md_render_text(HDC hdc, const char *text, int x, int y, int max_width,
                   HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                   const ThemeColors *theme)
{
    if (!hdc || !text || !theme)
        return 0;

    int old_bk = SetBkMode(hdc, TRANSPARENT);
    int result = md_render_core(hdc, text, x, y, max_width,
                                hFont, hMonoFont, hBoldFont,
                                theme, 1);
    SetBkMode(hdc, old_bk);
    return result;
}

int md_measure_text(HDC hdc, const char *text, int max_width,
                    HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                    const ThemeColors *theme)
{
    if (!hdc || !text || !theme)
        return 0;

    return md_render_core(hdc, text, 0, 0, max_width,
                          hFont, hMonoFont, hBoldFont,
                          theme, 0);
}

#endif /* _WIN32 */
