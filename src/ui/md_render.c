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

static void paint_word(HDC hdc, const char *text, int byte_off, int byte_len,
                       const MdSpan *span, int px, int py,
                       HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                       const ThemeColors *theme)
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
    GetTextExtentPoint32W(hdc, wb.ptr, wb.len, &sz);

    /* Code background */
    if (span->type == MD_SPAN_CODE) {
        RECT bg = { px - 1, py, px + sz.cx + 1, py + sz.cy };
        HBRUSH br = CreateSolidBrush(RGB_FROM_THEME(theme->chat.cmd_bg));
        FillRect(hdc, &bg, br);
        DeleteObject(br);
    }

    COLORREF clr = (span->type == MD_SPAN_CODE)
        ? RGB_FROM_THEME(theme->chat.cmd_text)
        : RGB_FROM_THEME(theme->text_main);
    SetTextColor(hdc, clr);

    TextOutW(hdc, px, py, wb.ptr, wb.len);

    if (span->type == MD_SPAN_STRIKETHROUGH) {
        TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
        int mid_y = py + tm.tmHeight / 2;
        HPEN pen = CreatePen(PS_SOLID, 1, RGB_FROM_THEME(theme->text_main));
        HPEN old_pen = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, px, mid_y, NULL);
        LineTo(hdc, px + sz.cx, mid_y);
        SelectObject(hdc, old_pen);
        DeleteObject(pen);
    }

    SelectObject(hdc, old_font);
    if (created) DeleteObject(created);
    mdbuf_free(&wb);
}

/* ── Word-by-word inline layouter ────────────────────────────────────── */

/* Lays out parsed spans on one or more visual lines starting at (x, y),
 * wrapping to column `x` when a token won't fit at `cur_x`. Returns the
 * total height consumed (including the last line's height). */
static int render_inline_spans(HDC hdc, const char *line, int line_len,
                               int x, int y, int max_width,
                               HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                               const ThemeColors *theme, int paint)
{
    /* Default line height (used for empty lines and as min line height). */
    int default_lh;
    {
        HFONT old = (HFONT)SelectObject(hdc, hFont);
        TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
        default_lh = tm.tmHeight;
        SelectObject(hdc, old);
    }

    if (line_len <= 0) return default_lh;

    MdSpan spans[MD_MAX_SPANS];
    int span_count = md_parse_inline(line, line_len, spans);
    if (span_count == 0) return default_lh;

    int max_right = x + max_width;
    int cur_x = x;
    int cur_y = y;
    int line_h = default_lh;
    int at_line_start = 1;   /* true if cur_x == x (drop leading whitespace) */

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
            if (is_ws && at_line_start) {
                off = wend;
                continue;
            }

            /* If this is whitespace that would push past max_right, just
             * end the line here (don't emit trailing whitespace before wrap). */
            if (is_ws && cur_x + word_w > max_right) {
                cur_y += line_h;
                cur_x = x;
                line_h = default_lh;
                at_line_start = 1;
                off = wend;   /* consume the whitespace */
                continue;
            }

            /* Word doesn't fit on current line and we're not at line start —
             * wrap before placing it. */
            if (!is_ws && cur_x + word_w > max_right && !at_line_start) {
                cur_y += line_h;
                cur_x = x;
                line_h = default_lh;
                at_line_start = 1;
            }

            /* Place the token. */
            if (paint) {
                paint_word(hdc, line, wstart, w_byte_len, span,
                           cur_x, cur_y,
                           hFont, hMonoFont, hBoldFont, theme);
            }
            cur_x += word_w;
            if (word_h > line_h) line_h = word_h;
            at_line_start = 0;
            off = wend;
        }
    }

    /* Account for the final line. */
    return (cur_y - y) + line_h;
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
                                        theme, paint);
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

int md_render_text(HDC hdc, const char *text, int x, int y, int max_width,
                   HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                   const ThemeColors *theme)
{
    if (!hdc || !text || !theme)
        return 0;

    int old_bk = SetBkMode(hdc, TRANSPARENT);
    COLORREF old_color = GetTextColor(hdc);
    int result = md_render_core(hdc, text, x, y, max_width,
                                hFont, hMonoFont, hBoldFont,
                                theme, 1);
    SetTextColor(hdc, old_color);
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
