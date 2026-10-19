#ifndef NUTSHELL_TERM_COLORS_H
#define NUTSHELL_TERM_COLORS_H

/* The colours a terminal cell is painted with (spec
 * docs/superpowers/specs/2026-10-19-light-theme-ansi-contrast-design.md,
 * sections 4 and 5.4). Pure C, no Win32: src/ui/renderer.c only calls
 * term_colors_resolve() and converts the result to COLORREF.
 *
 * Every theme's resolved tokens carry one TermPalette (ThemeTokens.term,
 * built by ui_theme_resolve()). On a theme whose floor is non-zero (the
 * light themes) the 256 indexed foregrounds are floored to that contrast
 * ratio against the theme's terminal background, and only cells drawn on
 * that background use the floored table; cells with an explicit background
 * keep the raw palette on both sides. Truecolour is never floored. */

#include <stdint.h>
#include "term.h"

/* A dim (SGR 2) cell's foreground is blended this far toward its
 * background, after the floor, on a theme whose floor is non-zero. */
#define TERM_DIM_TOWARD_BG 0.40

typedef struct {
    uint32_t default_fg, default_bg;  /* the theme's terminal_fg / terminal_bg */
    double   min_contrast;            /* 0 = off: raw table, dim ignored */
    uint32_t fg_on_default[256];      /* color256_to_rgb(i), floored against default_bg
                                         when min_contrast > 0, else a raw copy */
} TermPalette;

/* Fill *p for one theme. min_contrast <= 0 (or NaN) stores 0: the table is
 * a raw copy of color256_to_rgb() and dim is ignored. NULL p is a no-op. */
void term_palette_build(TermPalette *p, uint32_t default_fg, uint32_t default_bg,
                        double min_contrast);

#define TERM_DRAW_CURSOR   1u
#define TERM_DRAW_SELECTED 2u

/* The colours `cell` is painted with, as 0xRRGGBB, in this order:
 *  1. background: default -> default_bg; indexed -> color256_to_rgb(bg_index);
 *     truecolour -> attr.bg;
 *  2. foreground on the default background: default -> default_fg; indexed ->
 *     fg_on_default[fg_index]; truecolour -> attr.fg (never floored);
 *  3. foreground on an explicit background: default -> default_fg; indexed ->
 *     color256_to_rgb(fg_index); truecolour -> attr.fg (all raw);
 *  4. swap once for TERM_DRAW_CURSOR or TERM_ATTR_REVERSE (either, not both);
 *  5. dim: TERM_ATTR_DIM, min_contrast > 0, a non-blank codepoint (not 0 or
 *     ' ') and not the cursor cell -> fg = theme_blend(fg, bg, TERM_DIM_TOWARD_BG);
 *  6. swap again for TERM_DRAW_SELECTED.
 * Any NULL argument is a no-op. */
void term_colors_resolve(const TermPalette *p, const TermCell *cell, unsigned draw_flags,
                         uint32_t *out_fg, uint32_t *out_bg);

#endif /* NUTSHELL_TERM_COLORS_H */
