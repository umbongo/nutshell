#include "renderer.h"
#include "display_buffer.h"
#include "selection.h"
#include "theme.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <dwmapi.h>

/* DWMWA_USE_IMMERSIVE_DARK_MODE is attribute 20 (Windows 10 2004+).
 * Define it here so we can compile against older SDK headers. */
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

static COLORREF to_colorref(uint32_t rgb) {
    /* TermAttr stores 0xRRGGBB, Windows uses 0x00BBGGRR */
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

void renderer_init(Renderer *r, const char *fontName, int fontSize, int dpi) {
    if (!r) return;

    int height = -MulDiv(fontSize, dpi, 72);

    r->hFont = CreateFont(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_TT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          FIXED_PITCH | FF_MODERN, fontName);

    r->hBoldFont = CreateFont(height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_TT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              FIXED_PITCH | FF_MODERN, fontName);

    /* Calculate char dimensions */
    HDC hdc = GetDC(NULL);
    HFONT oldFont = SelectObject(hdc, r->hFont);
    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    r->charWidth = tm.tmAveCharWidth;
    r->charHeight = tm.tmHeight;
    SelectObject(hdc, oldFont);
    ReleaseDC(NULL, hdc);

    r->defaultFg = RGB(0,   0,   0);   /* #000000 — overridden by apply_config_colors() */
    r->defaultBg = RGB(255, 255, 255); /* #FFFFFF — overridden by apply_config_colors() */

    /* Display buffer starts empty — allocated on first draw */
    memset(&r->dispbuf, 0, sizeof(r->dispbuf));
    r->prev_cursor_row = -1;
    r->prev_cursor_col = -1;
}

void renderer_free(Renderer *r) {
    if (!r) return;
    if (r->hFont)     { DeleteObject(r->hFont);     r->hFont = NULL; }
    if (r->hBoldFont) { DeleteObject(r->hBoldFont); r->hBoldFont = NULL; }
    dispbuf_free(&r->dispbuf);
}

/* Helper to find the TermRow for a given screen row index.
 * Must match parser.c get_screen_row() logic so the renderer sees the
 * same rows the parser writes to.  After resize with sparse content
 * lines_count can be < rows; the rows beyond lines_count are still
 * pre-allocated and the parser writes to them, so we must access them. */
static TermRow *get_visible_row(Terminal *term, int screen_row) {
    if (screen_row < 0 || screen_row >= term->rows) return NULL;

    int offset = term->scrollback_offset;
    int top_logical;
    if (offset == 0) {
        /* Normal view — same as parser's get_screen_row */
        top_logical = (term->lines_count >= term->rows)
                    ? (term->lines_count - term->rows) : 0;
    } else {
        /* Scrolled back — shift view up */
        top_logical = (term->lines_count >= term->rows)
                    ? (term->lines_count - term->rows - offset) : -offset;
        if (top_logical < 0) top_logical = 0;
    }

    int logical_idx = top_logical + screen_row;
    /* When lines_count < rows the logical index can exceed lines_count.
     * The row is still allocated (pre-allocated by init/resize) so return it. */
    if (logical_idx >= term->lines_capacity) return NULL;

    int physical_idx = (term->lines_start + logical_idx) % term->lines_capacity;
    return term->lines[physical_idx];
}

/* Check if cell (row, col) falls within the selection range. */
static bool cell_in_selection(const Selection *sel, int row, int col)
{
    if (!sel || !sel->valid) return false;
    int r0, c0, r1, c1;
    selection_normalise(sel, &r0, &c0, &r1, &c1);
    if (row < r0 || row > r1) return false;
    if (row == r0 && row == r1) return col >= c0 && col <= c1;
    if (row == r0) return col >= c0;
    if (row == r1) return col <= c1;
    return true; /* middle row — fully selected */
}

/* Resolve a cell's fg, bg, and bold flag, accounting for reverse + selection. */
static void resolve_cell(const Renderer *r, const TermCell *cell,
                          const Terminal *term, int row_idx, int col_idx,
                          const Selection *sel,
                          COLORREF *out_fg, COLORREF *out_bg,
                          bool *out_bold)
{
    COLORREF fg = (cell->attr.fg_mode == COLOR_DEFAULT) ? r->defaultFg : to_colorref(cell->attr.fg);
    COLORREF bg = (cell->attr.bg_mode == COLOR_DEFAULT) ? r->defaultBg : to_colorref(cell->attr.bg);
    bool reverse = (cell->attr.flags & TERM_ATTR_REVERSE) ||
                   (term->cursor.visible && term->cursor.row == row_idx && term->cursor.col == col_idx);
    if (reverse) { COLORREF tmp = fg; fg = bg; bg = tmp; }
    if (cell_in_selection(sel, row_idx, col_idx)) {
        COLORREF tmp = fg; fg = bg; bg = tmp;
    }
    *out_fg = fg;
    *out_bg = bg;
    *out_bold = (cell->attr.flags & TERM_ATTR_BOLD) != 0;
}

void renderer_draw(Renderer *r, HDC hdc, Terminal *term, int x, int y, const RECT *paintRect, const Selection *sel) {
    if (!r || !term) return;

    /* Ensure display buffer matches terminal dimensions */
    if (r->dispbuf.rows != term->rows || r->dispbuf.cols != term->cols) {
        dispbuf_resize(&r->dispbuf, term->rows, term->cols);
    }

    HFONT oldFont = SelectObject(hdc, r->hFont);
    SetBkMode(hdc, OPAQUE);
    bool cur_bold = false;  /* track which font is selected */

    int cursor_row = term->cursor.visible ? term->cursor.row : -1;
    int cursor_col = term->cursor.visible ? term->cursor.col : -1;
    bool has_sel = sel && sel->valid;

    /* If cursor moved, invalidate the old cursor cell in the display buffer
     * so the ghost cursor gets erased on repaint. */
    int prev_row = r->prev_cursor_row;
    int prev_col = r->prev_cursor_col;
    if (prev_row != cursor_row || prev_col != cursor_col) {
        if (prev_row >= 0 && prev_row < r->dispbuf.rows &&
            prev_col >= 0 && prev_col < r->dispbuf.cols) {
            dispbuf_cell_update(&r->dispbuf, prev_row, prev_col,
                                0xFFFFFFFF, 0, 0, 0xFF);  /* force mismatch */
        }
        r->prev_cursor_row = cursor_row;
        r->prev_cursor_col = cursor_col;
    }

    for (int row_idx = 0; row_idx < term->rows; row_idx++) {
        int py = y + row_idx * r->charHeight;

        /* Skip rows outside paint rect */
        if (py + r->charHeight < paintRect->top || py > paintRect->bottom) continue;

        TermRow *row = get_visible_row(term, row_idx);
        if (!row) {
            /* No terminal content for this row (lines_count < rows after resize).
             * Paint background if the display buffer says these cells are dirty. */
            if (!dispbuf_cell_clean(&r->dispbuf, row_idx, 0,
                                     0, (uint32_t)r->defaultBg, (uint32_t)r->defaultBg, 0)) {
                RECT emptyRect = {x, py, x + term->cols * r->charWidth, py + r->charHeight};
                SetBkColor(hdc, r->defaultBg);
                ExtTextOutW(hdc, x, py, ETO_OPAQUE, &emptyRect, L"", 0, NULL);
                /* Mark all cells in this row as painted with background */
                for (int c = 0; c < term->cols; c++) {
                    dispbuf_cell_update(&r->dispbuf, row_idx, c,
                                        0, (uint32_t)r->defaultBg, (uint32_t)r->defaultBg, 0);
                }
            }
            continue;
        }

        /* Skip clean rows that don't contain the cursor and aren't selected.
         * When scrolled back, visible rows differ from what was last painted,
         * so we must repaint all of them regardless of dirty flags.
         * Also repaint the previous cursor row to erase the ghost cursor.
         * After a full invalidation (font/zoom/resize) repaint everything. */
        if (term->scrollback_offset == 0 &&
            !row->dirty && row_idx != cursor_row && row_idx != prev_row &&
            !has_sel) continue;

        for (int col_idx = 0; col_idx < term->cols; ) {
            int px_start = x + col_idx * r->charWidth;

            /* Skip cols before paint rect */
            if (px_start + r->charWidth < paintRect->left) { col_idx++; continue; }
            /* Stop if we're past the paint rect */
            if (px_start > paintRect->right) break;

            /* Resolve first cell */
            TermCell *cell = &row->cells[col_idx];
            COLORREF fg, bg;
            bool bold;
            resolve_cell(r, cell, term, row_idx, col_idx, sel, &fg, &bg, &bold);
            uint32_t cp = cell->codepoint;
            uint8_t aflags = cell->attr.flags;

            /* Cell-level dirty check: skip if the display buffer already
             * has this exact content painted. */
            if (dispbuf_cell_clean(&r->dispbuf, row_idx, col_idx,
                                   cp, (uint32_t)fg, (uint32_t)bg, aflags)) {
                col_idx++;
                continue;
            }

            COLORREF run_fg = fg, run_bg = bg;
            bool run_bold = bold;
            WCHAR run_buf[512];
            int run_start = col_idx;
            int run_len = 0;
            run_buf[run_len++] = (cp == 0) ? L' ' : (WCHAR)cp;
            dispbuf_cell_update(&r->dispbuf, row_idx, col_idx,
                                cp, (uint32_t)fg, (uint32_t)bg, aflags);
            col_idx++;

            /* Accumulate consecutive cells with matching fg/bg/bold */
            while (col_idx < term->cols && run_len < 512) {
                cell = &row->cells[col_idx];
                resolve_cell(r, cell, term, row_idx, col_idx, sel, &fg, &bg, &bold);
                if (fg != run_fg || bg != run_bg || bold != run_bold) break;
                cp = cell->codepoint;
                aflags = cell->attr.flags;
                run_buf[run_len++] = (cp == 0) ? L' ' : (WCHAR)cp;
                dispbuf_cell_update(&r->dispbuf, row_idx, col_idx,
                                    cp, (uint32_t)fg, (uint32_t)bg, aflags);
                col_idx++;
            }

            /* Select bold/normal font for this run */
            if (run_bold != cur_bold) {
                SelectObject(hdc, run_bold ? r->hBoldFont : r->hFont);
                cur_bold = run_bold;
            }

            /* Draw the entire run in one call */
            SetTextColor(hdc, run_fg);
            SetBkColor(hdc, run_bg);
            int px = x + run_start * r->charWidth;
            RECT runRect = {px, py, px + run_len * r->charWidth, py + r->charHeight};
            ExtTextOutW(hdc, px, py, ETO_OPAQUE | ETO_CLIPPED, &runRect,
                        run_buf, (UINT)run_len, NULL);
        }

        row->dirty = false;
    }

    /* Restore normal font */
    SelectObject(hdc, oldFont);
}

void renderer_apply_theme(HWND hwnd, COLORREF bg_colorref)
{
    /* Convert COLORREF (0x00BBGGRR) to 0x00RRGGBB for theme_is_dark */
    unsigned int r = (unsigned int)( bg_colorref        & 0xFF);
    unsigned int g = (unsigned int)((bg_colorref >>  8) & 0xFF);
    unsigned int b = (unsigned int)((bg_colorref >> 16) & 0xFF);
    unsigned int bg_rgb = (r << 16) | (g << 8) | b;
    BOOL dark = theme_is_dark(bg_rgb) ? TRUE : FALSE;
    /* DwmSetWindowAttribute silently fails on older Windows versions — that is fine. */
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

#endif