#include "renderer.h"
#include <stdio.h>

#ifdef _WIN32

static COLORREF to_colorref(uint32_t rgb) {
    /* TermAttr stores 0xRRGGBB, Windows uses 0x00BBGGRR */
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

void renderer_init(Renderer *r, const char *fontName, int fontSize) {
    if (!r) return;

    HDC hdc = GetDC(NULL);
    int logPixelsY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);

    int height = -MulDiv(fontSize, logPixelsY, 72);

    r->hFont = CreateFont(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          FIXED_PITCH | FF_MODERN, fontName);

    /* Calculate char dimensions */
    hdc = GetDC(NULL);
    HFONT oldFont = SelectObject(hdc, r->hFont);
    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    r->charWidth = tm.tmAveCharWidth;
    r->charHeight = tm.tmHeight;
    SelectObject(hdc, oldFont);
    ReleaseDC(NULL, hdc);

    r->defaultFg = RGB(204, 204, 204); /* #CCCCCC */
    r->defaultBg = RGB(12, 12, 12);    /* #0C0C0C */
}

void renderer_free(Renderer *r) {
    if (r && r->hFont) {
        DeleteObject(r->hFont);
        r->hFont = NULL;
    }
}

/* Helper to find the TermRow for a given screen row index */
static TermRow *get_visible_row(Terminal *term, int screen_row) {
    int total = term->lines_count;
    int view_height = term->rows;
    int offset = term->scrollback_offset;
    
    /* Calculate the logical index of the top visible line */
    int effective_top = total - view_height - offset;
    if (effective_top < 0) effective_top = 0;
    
    int logical_idx = effective_top + screen_row;
    if (logical_idx >= total) return NULL;
    
    int physical_idx = (term->lines_start + logical_idx) % term->lines_capacity;
    return term->lines[physical_idx];
}

void renderer_draw(Renderer *r, HDC hdc, Terminal *term, int x, int y, RECT *paintRect) {
    if (!r || !term) return;

    HFONT oldFont = SelectObject(hdc, r->hFont);
    SetBkMode(hdc, OPAQUE);

    for (int row_idx = 0; row_idx < term->rows; row_idx++) {
        int py = y + row_idx * r->charHeight;
        
        /* Skip rows outside paint rect */
        if (py + r->charHeight < paintRect->top || py > paintRect->bottom) continue;

        TermRow *row = get_visible_row(term, row_idx);
        if (!row) continue;

        for (int col_idx = 0; col_idx < term->cols; col_idx++) {
            int px = x + col_idx * r->charWidth;
            
            /* Skip cols outside paint rect */
            if (px + r->charWidth < paintRect->left || px > paintRect->right) continue;

            TermCell *cell = &row->cells[col_idx];
            uint32_t cp = cell->codepoint;
            WCHAR wch = (cp == 0) ? L' ' : (WCHAR)cp;

            /* Resolve colors: COLOR_DEFAULT means use the terminal's configured default. */
            COLORREF fg = (cell->attr.fg_mode == COLOR_DEFAULT) ? r->defaultFg : to_colorref(cell->attr.fg);
            COLORREF bg = (cell->attr.bg_mode == COLOR_DEFAULT) ? r->defaultBg : to_colorref(cell->attr.bg);
            
            /* Handle reverse video and cursor */
            bool reverse = (cell->attr.flags & TERM_ATTR_REVERSE) ||
                           (term->cursor.visible && term->cursor.row == row_idx && term->cursor.col == col_idx);

            if (reverse) {
                COLORREF tmp = fg; fg = bg; bg = tmp;
            }

            SetTextColor(hdc, fg);
            SetBkColor(hdc, bg);

            RECT cellRect = {px, py, px + r->charWidth, py + r->charHeight};
            ExtTextOutW(hdc, px, py, ETO_OPAQUE | ETO_CLIPPED, &cellRect, &wch, 1, NULL);
        }
    }

    SelectObject(hdc, oldFont);
}

#endif