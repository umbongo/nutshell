#ifndef RENDERER_H
#define RENDERER_H

#ifdef _WIN32
#include <windows.h>
#include "term.h"
#include "selection.h"
#include "display_buffer.h"

typedef struct Renderer {
    HFONT hFont;
    HFONT hBoldFont;
    int charWidth;
    int charHeight;
    COLORREF defaultFg;
    COLORREF defaultBg;
    DisplayBuffer dispbuf;
} Renderer;

void renderer_init(Renderer *r, const char *fontName, int fontSize, int dpi);
void renderer_free(Renderer *r);
void renderer_draw(Renderer *r, HDC hdc, Terminal *term, int x, int y, const RECT *paintRect, const Selection *sel);
void renderer_apply_theme(HWND hwnd, COLORREF bg_colorref);

#endif
#endif /* RENDERER_H */