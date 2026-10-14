/* src/ui/md_render.h */
#ifndef NUTSHELL_MD_RENDER_H
#define NUTSHELL_MD_RENDER_H

#include <windows.h>
#include "ui_theme.h"
#include "ns_layout.h"   /* NsRect */

/* Render markdown text into a GDI device context.
 * x, y: top-left position. max_width: available width for wrapping.
 * Returns the total height consumed (in pixels). */
int md_render_text(HDC hdc, const char *text, int x, int y, int max_width,
                   HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                   const ThemeColors *theme);

/* Where the fenced code blocks of a rendered text ended up, for a caller that
 * draws something on them (the copy glyph). The caller fills `rects`,
 * `max_rects` and `reserve_w`; the renderer sets `count`.
 *
 * One entry per opening fence, in the order md_code_blocks() reports them
 * (both walk the same md_classify_line classification), in the same
 * coordinates as the x/y passed to the renderer: the union of the block's
 * code-line backgrounds. A block with no body lines keeps its slot as an
 * all-zero rect. `count` is the number of fenced blocks met and may exceed
 * `max_rects` (only the first `max_rects` are written).
 *
 * `reserve_w` (px, >= 0) is kept free of text on the right of every code line
 * so the glyph never covers code; code lines never wrap, so it changes no
 * height. */
typedef struct {
    NsRect *rects;
    int     max_rects;
    int     reserve_w;
    int     count;      /* out */
} MdBlockOut;

/* md_render_text() that also reports the fenced code blocks (see above).
 * `blocks` may be NULL, which is exactly md_render_text(). */
int md_render_text_blocks(HDC hdc, const char *text, int x, int y,
                          int max_width,
                          HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                          const ThemeColors *theme, MdBlockOut *blocks);

/* Measure the height that markdown text would consume without painting.
 * Same parameters as md_render_text. */
int md_measure_text(HDC hdc, const char *text, int max_width,
                    HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                    const ThemeColors *theme);

#endif /* NUTSHELL_MD_RENDER_H */
