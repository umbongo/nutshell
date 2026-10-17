#include "renderer.h"
#include "display_buffer.h"
#include "paint_cover.h"
#include "selection.h"
#include "theme.h"
#include "dlog.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include "dwm_util.h" /* also defines DWMWA_USE_IMMERSIVE_DARK_MODE */

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

    /* Fallback font for characters missing from the primary font (emoji, rare
     * Unicode symbols, etc.).  "Segoe UI Symbol" is present on all Windows 10+
     * systems and covers a broad range of Unicode including box-drawing, arrows,
     * mathematical operators, and many symbol blocks. */
    r->hFallbackFont = CreateFont(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_TT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, "Segoe UI Symbol");
    r->hBoldFallbackFont = CreateFont(height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_TT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, "Segoe UI Symbol");

    if (!r->hFont || !r->hBoldFont) {
        if (r->hFont)     { DeleteObject(r->hFont);     r->hFont = NULL; }
        if (r->hBoldFont) { DeleteObject(r->hBoldFont); r->hBoldFont = NULL; }
        r->charWidth = 8;
        r->charHeight = 16;
        return;
    }

    /* Calculate char dimensions */
    HDC hdc = GetDC(NULL);
    if (!hdc) {
        r->charWidth = 8;
        r->charHeight = 16;
        return;
    }
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
    if (r->hFont)             { DeleteObject(r->hFont);             r->hFont = NULL; }
    if (r->hBoldFont)         { DeleteObject(r->hBoldFont);         r->hBoldFont = NULL; }
    if (r->hFallbackFont)     { DeleteObject(r->hFallbackFont);     r->hFallbackFont = NULL; }
    if (r->hBoldFallbackFont) { DeleteObject(r->hBoldFallbackFont); r->hBoldFallbackFont = NULL; }
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
                   (term->cursor.visible && term->scrollback_offset == 0 &&
                    term->cursor.row == row_idx && term->cursor.col == col_idx);
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
        if (!r->dispbuf.cells) return;
    }

    LARGE_INTEGER t_start;
    QueryPerformanceCounter(&t_start);
    int rows_in_rect = 0, rows_covered = 0, rows_painted = 0, rows_skipped = 0;
    HFONT oldFont = SelectObject(hdc, r->hFont);
    SetBkMode(hdc, OPAQUE);
    bool cur_bold = false;  /* track which font is selected */

    /* Hide cursor when scrolled back — it's below the visible area */
    int cursor_row = (term->cursor.visible && term->scrollback_offset == 0)
                     ? term->cursor.row : -1;
    int cursor_col = (term->cursor.visible && term->scrollback_offset == 0)
                     ? term->cursor.col : -1;
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

    /* If the terminal buffer was swapped (e.g. alt-screen exit), the display
     * buffer shadow is stale — invalidate it to force a full repaint.
     * Also bypass the row-level dirty skip so every row reaches the
     * cell-level shadow check (defense-in-depth). */
    bool force_all_rows = false;
    if (term->full_redraw_needed) {
        dispbuf_invalidate(&r->dispbuf);
        term->full_redraw_needed = false;
        force_all_rows = true;
        DLOG(DLOG_RENDER, DLOG_TRACE, r->log_sid, "dispbuf.invalidate",
             "why=full_redraw_needed force_all_rows=1");
    }

    /* A row's dirty flag may be cleared only when this paint contained the
     * row's whole rectangle. A partial update region (the 14px gutter
     * exposed when the scrollbar hides for the alternate screen,
     * 2026-10-14) paints only the cells inside it; marking the row clean
     * then strands the rest of the row until some later full-width paint.
     * Left dirty, the row is finished by the next full-window invalidate.
     * A row the paint only partly contained also gets its shadow row
     * poisoned, because GDI clips a cell that straddles the paint edge
     * while the bookkeeping below would record it as painted
     * (src/core/paint_cover.h, display_buffer.h). All range tests are
     * half-open: RECT right and bottom are exclusive. */
    const long content_left  = (long)x;
    const long content_right = (long)x + (long)term->cols * (long)r->charWidth;

    for (int row_idx = 0; row_idx < term->rows; row_idx++) {
        LONG py = (LONG)y + (LONG)row_idx * r->charHeight;

        /* Skip rows outside paint rect */
        if (py + r->charHeight <= paintRect->top || py >= paintRect->bottom) continue;
        rows_in_rect++;

        bool covered = paint_covers_rect(
            (long)paintRect->left, (long)paintRect->top,
            (long)paintRect->right, (long)paintRect->bottom,
            content_left, (long)py,
            content_right, (long)py + (long)r->charHeight) != 0;
        if (covered) rows_covered++;

        TermRow *row = get_visible_row(term, row_idx);
        rows_painted++;   /* undone below if the row turns out clean and is skipped */
        if (!row) {
            /* No terminal content for this row (lines_count < rows after resize).
             * Paint background if the display buffer says these cells are dirty. */
            if (!dispbuf_cell_clean(&r->dispbuf, row_idx, 0,
                                     0, (uint32_t)r->defaultBg, (uint32_t)r->defaultBg, 0)) {
                RECT emptyRect = {x, py, (LONG)x + (LONG)term->cols * r->charWidth, py + r->charHeight};
                SetBkColor(hdc, r->defaultBg);
                ExtTextOutW(hdc, x, (int)py, ETO_OPAQUE, &emptyRect, L"", 0, NULL);
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
        if (!force_all_rows &&
            term->scrollback_offset == 0 &&
            !row->dirty && row_idx != cursor_row && row_idx != prev_row &&
            !has_sel) {
            rows_painted--;
            rows_skipped++;
            continue;
        }

        for (int col_idx = 0; col_idx < term->cols; ) {
            LONG px_start = (LONG)x + (LONG)col_idx * r->charWidth;

            /* Skip cols before paint rect */
            if (px_start + r->charWidth <= paintRect->left) { col_idx++; continue; }
            /* Stop if we're past the paint rect */
            if (px_start >= paintRect->right) break;

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
            WCHAR run_buf[1024]; /* extra room for surrogate pairs */
            INT run_dx[1024];    /* explicit spacing per WCHAR */
            int run_start = col_idx;
            int run_len = 0;
            if (cp == 0) {
                run_buf[run_len] = L' ';
                run_dx[run_len] = r->charWidth;
                run_len++;
            } else if (cp > 0xFFFF) {
                run_buf[run_len] = (WCHAR)(0xD800 + ((cp - 0x10000) >> 10));
                run_dx[run_len] = r->charWidth;
                run_len++;
                run_buf[run_len] = (WCHAR)(0xDC00 + ((cp - 0x10000) & 0x3FF));
                run_dx[run_len] = 0; /* second surrogate half takes 0 cells */
                run_len++;
            } else {
                run_buf[run_len] = (WCHAR)cp;
                run_dx[run_len] = r->charWidth;
                run_len++;
            }
            dispbuf_cell_update(&r->dispbuf, row_idx, col_idx,
                                cp, (uint32_t)fg, (uint32_t)bg, aflags);
            col_idx++;

            /* Accumulate consecutive cells with matching fg/bg/bold */
            while (col_idx < term->cols && run_len < 1020) {
                cell = &row->cells[col_idx];
                resolve_cell(r, cell, term, row_idx, col_idx, sel, &fg, &bg, &bold);
                if (fg != run_fg || bg != run_bg || bold != run_bold) break;
                cp = cell->codepoint;
                aflags = cell->attr.flags;
                if (cp == 0) {
                    run_buf[run_len] = L' ';
                    run_dx[run_len] = r->charWidth;
                    run_len++;
                } else if (cp > 0xFFFF) {
                    run_buf[run_len] = (WCHAR)(0xD800 + ((cp - 0x10000) >> 10));
                    run_dx[run_len] = r->charWidth;
                    run_len++;
                    run_buf[run_len] = (WCHAR)(0xDC00 + ((cp - 0x10000) & 0x3FF));
                    run_dx[run_len] = 0;
                    run_len++;
                } else {
                    run_buf[run_len] = (WCHAR)cp;
                    run_dx[run_len] = r->charWidth;
                    run_len++;
                }
                dispbuf_cell_update(&r->dispbuf, row_idx, col_idx,
                                    cp, (uint32_t)fg, (uint32_t)bg, aflags);
                col_idx++;
            }

            /* Select bold/normal font for this run */
            if (run_bold != cur_bold) {
                SelectObject(hdc, run_bold ? r->hBoldFont : r->hFont);
                cur_bold = run_bold;
            }

            SetTextColor(hdc, run_fg);
            SetBkColor(hdc, run_bg);
            LONG px = (LONG)x + (LONG)run_start * r->charWidth;
            int run_cols = col_idx - run_start;
            RECT runRect = {px, py, px + (LONG)run_cols * r->charWidth, py + r->charHeight};

            /* Check for missing glyphs in non-ASCII runs.  Skip the API call for
             * pure ASCII (common case) to avoid unnecessary Win32 overhead.
             * Surrogate pairs (SMP characters like emoji) always return 0xFFFF from
             * GetGlyphIndicesW since it is a BMP-only API — they automatically fall
             * through to the fallback path where Uniscribe handles them correctly. */
            bool need_fallback_check = false;
            for (int gi = 0; gi < run_len && !need_fallback_check; gi++) {
                if (run_buf[gi] > 0x7Fu) need_fallback_check = true;
            }
            WORD glyph_indices[1024];
            bool has_missing = false;
            if (need_fallback_check) {
                GetGlyphIndicesW(hdc, run_buf, run_len, glyph_indices,
                                 GGI_MARK_NONEXISTING_GLYPHS);
                for (int gi = 0; gi < run_len && !has_missing; gi++) {
                    if (run_dx[gi] > 0 && glyph_indices[gi] == 0xFFFF) has_missing = true;
                }
            }

            /* Draw the entire run with the primary font */
            ExtTextOutW(hdc, (int)px, (int)py, ETO_OPAQUE | ETO_CLIPPED, &runRect,
                        run_buf, (UINT)run_len, run_dx);

            /* Overdraw missing-glyph cells using the Unicode fallback font.
             * Each missing character is redrawn individually, clipped to its cell,
             * so the proportional fallback font stays within the terminal grid. */
            if (has_missing) {
                HFONT fb = run_bold ? r->hBoldFallbackFont : r->hFallbackFont;
                if (fb) {
                    SelectObject(hdc, fb);
                    int cwrun = 0; /* column index within this run */
                    for (int wi = 0; wi < run_len; wi++) {
                        if (run_dx[wi] > 0) {
                            if (glyph_indices[wi] == 0xFFFF) {
                                LONG px_c = (LONG)x + (LONG)(run_start + cwrun) * r->charWidth;
                                UINT nw = (wi + 1 < run_len && run_dx[wi + 1] == 0) ? 2u : 1u;
                                RECT cellRect = {px_c, py, px_c + r->charWidth, py + r->charHeight};
                                INT cell_dx[2] = {r->charWidth, 0};
                                ExtTextOutW(hdc, (int)px_c, (int)py, ETO_OPAQUE | ETO_CLIPPED,
                                            &cellRect, run_buf + wi, nw, cell_dx);
                            }
                            cwrun++;
                        }
                    }
                    /* Restore primary font — cur_bold remains valid */
                    SelectObject(hdc, run_bold ? r->hBoldFont : r->hFont);
                }
            }
        }

        if (covered)
            row->dirty = false;
        else
            dispbuf_invalidate_row(&r->dispbuf, row_idx);
    }

    /* Restore normal font */
    SelectObject(hdc, oldFont);

    /* What this paint left behind, and the events that describe it. The
     * rows still dirty are the ones a partial update region could not
     * finish (see the `covered` note above); a row that stays dirty paint
     * after paint is the 2026-10-14 symptom, so it is counted here and
     * RENDER_STUCK_DIRTY_PAINTS of them in a row raise render.stuck_dirty. */
    int rows_left_dirty = 0, first_dirty = -1;
    for (int ri = 0; ri < term->rows; ri++) {
        TermRow *vr = get_visible_row(term, ri);
        if (vr && vr->dirty) {
            if (first_dirty < 0) first_dirty = ri;
            rows_left_dirty++;
        }
    }
    r->stat_rows_painted = rows_painted;
    r->stat_rows_skipped = rows_skipped;

    int stuck_paints = 0;
    if (render_stuck_update(&r->stuck, r->log_sid, rows_left_dirty, &stuck_paints))
        DLOG(DLOG_RENDER, DLOG_WARN, r->log_sid, "render.stuck_dirty",
             "rows=%d paints=%d first_row=%d", rows_left_dirty, stuck_paints, first_dirty);

    LARGE_INTEGER t_end, freq;
    QueryPerformanceCounter(&t_end);
    QueryPerformanceFrequency(&freq);
    long long us = (freq.QuadPart > 0)
        ? (long long)((t_end.QuadPart - t_start.QuadPart) * 1000000LL / freq.QuadPart) : 0LL;
    DLOG(DLOG_RENDER, DLOG_TRACE, r->log_sid, "render.draw",
         "rows=%d rows_in_rect=%d rows_covered=%d rows_painted=%d rows_left_dirty=%d "
         "char_h=%d us=%lld",
         term->rows, rows_in_rect, rows_covered, rows_painted, rows_left_dirty,
         r->charHeight, us);
}

void renderer_apply_theme(HWND hwnd, COLORREF bg_colorref)
{
    /* Convert COLORREF (0x00BBGGRR) to 0x00RRGGBB for theme_is_dark */
    unsigned int r = (unsigned int)( bg_colorref        & 0xFF);
    unsigned int g = (unsigned int)((bg_colorref >>  8) & 0xFF);
    unsigned int b = (unsigned int)((bg_colorref >> 16) & 0xFF);
    unsigned int bg_rgb = (r << 16) | (g << 8) | b;
    BOOL dark = theme_is_dark(bg_rgb) ? TRUE : FALSE;
    /* Silently a no-op on older Windows versions and if dwmapi.dll can't be
     * loaded at all -- that is fine (ns_dwm_set_dark_mode, dwm_util.h). */
    ns_dwm_set_dark_mode(hwnd, dark);
}

#endif