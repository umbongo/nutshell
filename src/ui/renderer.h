#ifndef NUTSHELL_RENDERER_H
#define NUTSHELL_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "../term/term.h"
#include "../core/selection.h"
#include "../core/display_buffer.h"
#include "../core/render_stuck.h"

typedef struct {
#ifdef _WIN32
    HFONT hFont;
    HFONT hBoldFont;
    HFONT hFallbackFont;      /* unicode fallback for glyphs missing from primary font */
    HFONT hBoldFallbackFont;
    COLORREF defaultFg;
    COLORREF defaultBg;
    DisplayBuffer dispbuf;
    int prev_cursor_row;
    int prev_cursor_col;
#endif
    int charWidth;
    int charHeight;

    /* Debug log (spec 2026-10-17-debug-logging-design.md section 2). The
     * caller sets log_sid to the session being painted before renderer_draw();
     * the counters below describe the last paint, for the render.stats event
     * window.c writes (it adds the cooldown skips). */
    unsigned    log_sid;
    RenderStuck stuck;            /* render.stuck_dirty: rows left dirty paint after paint */
    int         stat_rows_painted;   /* rows the last paint redrew                         */
    int         stat_rows_skipped;   /* rows in the paint rect it found clean and skipped   */
} Renderer;

#ifdef _WIN32
void renderer_init(Renderer *r, const char *fontName, int fontSize, int dpi);
void renderer_free(Renderer *r);
void renderer_draw(Renderer *r, HDC hdc, Terminal *term, int x, int y, const RECT *paintRect, const Selection *sel);
/* Apply dark/light title bar to hwnd based on the background COLORREF.
 * Uses DwmSetWindowAttribute with DWMWA_USE_IMMERSIVE_DARK_MODE; no-op on
 * systems that do not support it (pre-Windows 10 1903). */
void renderer_apply_theme(HWND hwnd, COLORREF bg_colorref);
#endif

#endif
