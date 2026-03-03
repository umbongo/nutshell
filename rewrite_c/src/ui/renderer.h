#ifndef CONGA_RENDERER_H
#define CONGA_RENDERER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "../term/term.h"

typedef struct {
#ifdef _WIN32
    HFONT hFont;
    COLORREF defaultFg;
    COLORREF defaultBg;
#endif
    int charWidth;
    int charHeight;
} Renderer;

#ifdef _WIN32
void renderer_init(Renderer *r, const char *fontName, int fontSize);
void renderer_free(Renderer *r);
void renderer_draw(Renderer *r, HDC hdc, Terminal *term, int x, int y, const RECT *paintRect);
/* Apply dark/light title bar to hwnd based on the background COLORREF.
 * Uses DwmSetWindowAttribute with DWMWA_USE_IMMERSIVE_DARK_MODE; no-op on
 * systems that do not support it (pre-Windows 10 1903). */
void renderer_apply_theme(HWND hwnd, COLORREF bg_colorref);
#endif

#endif
