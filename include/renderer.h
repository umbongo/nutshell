#ifndef RENDERER_H
#define RENDERER_H

#ifdef _WIN32
#include <windows.h>
#include "term.h"

typedef struct Renderer {
    HFONT hFont;
    int charWidth;
    int charHeight;
    COLORREF defaultFg;
    COLORREF defaultBg;
} Renderer;

void renderer_init(Renderer *r, const char *fontName, int fontSize);
void renderer_free(Renderer *r);
void renderer_draw(Renderer *r, HDC hdc, Terminal *term, int x, int y, RECT *paintRect);

#endif
#endif /* RENDERER_H */