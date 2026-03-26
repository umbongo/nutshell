/* src/ui/md_render.h */
#ifndef NUTSHELL_MD_RENDER_H
#define NUTSHELL_MD_RENDER_H

#include <windows.h>
#include "ui_theme.h"

/* Render markdown text into a GDI device context.
 * x, y: top-left position. max_width: available width for wrapping.
 * Returns the total height consumed (in pixels). */
int md_render_text(HDC hdc, const char *text, int x, int y, int max_width,
                   HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                   const ThemeColors *theme);

/* Measure the height that markdown text would consume without painting.
 * Same parameters as md_render_text. */
int md_measure_text(HDC hdc, const char *text, int max_width,
                    HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                    const ThemeColors *theme);

#endif /* NUTSHELL_MD_RENDER_H */
