#include "ns_scale.h"

/* MulDiv-equivalent: round(px * dpi / 96), without pulling in windows.h.
 * A 64-bit intermediate avoids overflow for any pixel/DPI value this module
 * is realistically called with. Must match settings_scale() in
 * settings_layout.c exactly (that function is now a one-line alias onto
 * this one). */
int ns_scale(int px, int dpi)
{
    if (dpi <= 0) dpi = 96;

    long long scaled = ((long long)px * dpi + 48) / 96; /* round to nearest */

    if (px >= 1 && scaled < 1) scaled = 1;

    return (int)scaled;
}
