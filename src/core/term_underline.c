#include "term_underline.h"
#include "ns_scale.h"
#include "ns_type.h"

void term_underline_geom(int cell_h, int ascent, int dpi,
                         int *top, int *thickness)
{
    int t = 0, h = 0;
    if (cell_h >= 1) {
        h = ns_scale(STROKE_HAIRLINE, dpi);
        if (h < 1) h = 1;
        if (h > cell_h) h = cell_h;
        if (ascent < 0) ascent = 0;
        if (ascent > cell_h) ascent = cell_h;
        t = ascent + (cell_h - ascent) / 3;
        if (t + h > cell_h) t = cell_h - h;
        if (t < 0) t = 0;
    }
    if (top) *top = t;
    if (thickness) *thickness = h;
}
