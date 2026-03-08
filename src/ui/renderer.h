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

typedef struct {
#ifdef _WIN32
    HFONT hFont;
    HFONT hBoldFont;
    COLORREF defaultFg;
    COLORREF defaultBg;
    DisplayBuffer dispbuf;
#endif
    int charWidth;
    int charHeight;
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
