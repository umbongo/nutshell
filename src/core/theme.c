#include "theme.h"
#include <math.h>

/* Convert a single 8-bit sRGB channel value to linear light.
 * Implements the IEC 61966-2-1 transfer function. */
static double linearise(unsigned int channel)
{
    double c = (double)channel / 255.0;
    if (c <= 0.04045)
        return c / 12.92;
    return pow((c + 0.055) / 1.055, 2.4);
}

double theme_luminance(unsigned int color_rgb)
{
    unsigned int r = (color_rgb >> 16) & 0xFFu;
    unsigned int g = (color_rgb >>  8) & 0xFFu;
    unsigned int b =  color_rgb        & 0xFFu;
    return 0.2126 * linearise(r)
         + 0.7152 * linearise(g)
         + 0.0722 * linearise(b);
}

int theme_is_dark(unsigned int color_rgb)
{
    return theme_luminance(color_rgb) < 0.5;
}
