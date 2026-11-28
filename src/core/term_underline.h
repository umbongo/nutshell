#ifndef NUTSHELL_TERM_UNDERLINE_H
#define NUTSHELL_TERM_UNDERLINE_H

/*
 * Where the terminal draws SGR 4 underline inside a cell. Pure arithmetic,
 * Win32-free, so it is tested natively; src/ui/renderer.c only fills the
 * rectangle this describes.
 *
 * The line is STROKE_HAIRLINE scaled to dpi (ns_scale: 1 px at 96 dpi, never
 * under 1), placed below the baseline, a third of the way into the descent.
 * It always lies inside [0, cell_h): top >= 0 and top + thickness <= cell_h,
 * whatever the font size, with thickness >= 1 for any cell_h >= 1.
 * `ascent` is the font's tmAscent (the baseline's distance from the cell
 * top), clamped into the cell. cell_h < 1 gives top 0, thickness 0.
 */
void term_underline_geom(int cell_h, int ascent, int dpi,
                         int *top, int *thickness);

#endif
