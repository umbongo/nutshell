#include "term_colors.h"
#include "theme.h"

void term_palette_build(TermPalette *p, uint32_t default_fg, uint32_t default_bg,
                        double min_contrast)
{
    if (!p) return;
    p->default_fg = default_fg;
    p->default_bg = default_bg;
    p->min_contrast = (min_contrast > 0.0) ? min_contrast : 0.0;
    for (int i = 0; i < 256; i++) {
        uint32_t raw = color256_to_rgb((uint8_t)i);
        p->fg_on_default[i] = (p->min_contrast > 0.0)
            ? theme_min_contrast_color(raw, default_bg, p->min_contrast)
            : raw;
    }
}

static int is_indexed(uint8_t mode)
{
    return mode == COLOR_ANSI16 || mode == COLOR_256;
}

void term_colors_resolve(const TermPalette *p, const TermCell *cell, unsigned draw_flags,
                         uint32_t *out_fg, uint32_t *out_bg)
{
    if (!p || !cell || !out_fg || !out_bg) return;
    const TermAttr *a = &cell->attr;
    const int on_default_bg = (a->bg_mode == COLOR_DEFAULT);
    uint32_t fg, bg, t;

    if (on_default_bg)               bg = p->default_bg;
    else if (is_indexed(a->bg_mode)) bg = color256_to_rgb(a->bg_index);
    else                             bg = a->bg;

    if (is_indexed(a->fg_mode))
        fg = on_default_bg ? p->fg_on_default[a->fg_index] : color256_to_rgb(a->fg_index);
    else if (a->fg_mode == COLOR_RGB)
        fg = a->fg;
    else
        fg = p->default_fg;

    const int cursor = (draw_flags & TERM_DRAW_CURSOR) != 0u;
    if (cursor || (a->flags & TERM_ATTR_REVERSE)) { t = fg; fg = bg; bg = t; }

    if ((a->flags & TERM_ATTR_DIM) && p->min_contrast > 0.0 && !cursor &&
        cell->codepoint != 0u && cell->codepoint != (uint32_t)' ')
        fg = theme_blend(fg, bg, TERM_DIM_TOWARD_BG);

    if (draw_flags & TERM_DRAW_SELECTED) { t = fg; fg = bg; bg = t; }

    *out_fg = fg;
    *out_bg = bg;
}
