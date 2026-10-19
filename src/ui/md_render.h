/* src/ui/md_render.h */
#ifndef NUTSHELL_MD_RENDER_H
#define NUTSHELL_MD_RENDER_H

#include <windows.h>
#include "ui_theme.h"
#include "ns_layout.h"   /* NsRect */
#include "text_runs.h"

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

/* Character selection (spec docs/superpowers/specs/
 * 2026-10-18-ai-panel-text-selection-design.md sections 3 and 6). Passed to
 * md_render_ex(): while painting, every word drawn is appended to `runs` as
 * a TextRun (rect, byte range, cumulative widths from one
 * GetTextExtentExPointW per run), and the part of it inside [sel_start,
 * sel_end) is filled with rgb_alpha(focus, <the colour under it>, 30%)
 * immediately before its text is drawn. Offsets are into the part's source:
 * text[0] is source offset `base`. */
typedef struct {
    TextRuns *runs;        /* append here; NULL records nothing */
    int       base;        /* source offset of text[0] in the part */
    int       part;        /* TR_PART_MAIN or TR_PART_THINKING */
    int       sel_start;   /* selection in the part's source bytes; */
    int       sel_end;     /*   sel_start >= sel_end: none */
    COLORREF  focus;       /* highlight hue: ns_tokens()->focus */
    COLORREF  surface;     /* the colour plain text sits on */
    int       clip_top;    /* record only runs meeting [clip_top, */
    int       clip_bottom; /*   clip_bottom); top >= bottom: no clip */
} MdRunCtx;

/* Layout modes for md_render_ex(). MD_MODE_PLAIN skips the parser: every
 * source line is a hard break, the text wraps word by word in hFont and
 * plain_clr, whitespace at the start of a source line is kept, and a word
 * wider than the line is broken at a character boundary. */
#define MD_MODE_MARKDOWN 0
#define MD_MODE_PLAIN    1

/* The one entry point behind the three above. paint = 0 measures (x and y
 * are ignored, nothing is drawn or recorded); paint = 1 draws, sets
 * TRANSPARENT background mode for the call and restores the text colour.
 * plain_clr is used by MD_MODE_PLAIN only. `blocks` and `rc` may be NULL.
 * Returns the height consumed, identical for paint = 0 and paint = 1 at the
 * same width. */
int md_render_ex(HDC hdc, const char *text, int x, int y, int max_width,
                 HFONT hFont, HFONT hMonoFont, HFONT hBoldFont,
                 const ThemeColors *theme, int mode, int paint,
                 COLORREF plain_clr, MdBlockOut *blocks, MdRunCtx *rc);

#endif /* NUTSHELL_MD_RENDER_H */
