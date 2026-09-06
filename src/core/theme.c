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

/* Inverse of linearise(): convert a linear-light channel value in [0,1]
 * back to an 8-bit sRGB channel value in [0,1] (caller scales by 255). */
static double delinearise(double c)
{
    if (c <= 0.0031308)
        return c * 12.92;
    return 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

double theme_contrast(unsigned int a, unsigned int b)
{
    double la = theme_luminance(a);
    double lb = theme_luminance(b);
    double hi = (la > lb) ? la : lb;
    double lo = (la > lb) ? lb : la;
    return (hi + 0.05) / (lo + 0.05);
}

/* CIE standard constants for the L* <-> Y piecewise mapping:
 * epsilon = (6/29)^3, kappa = (29/3)^3 = 24389/27. */
#define LSTAR_EPS   (216.0 / 24389.0)
#define LSTAR_KAPPA (24389.0 / 27.0)

double theme_lstar(unsigned int rgb)
{
    double y = theme_luminance(rgb);
    double fy = (y > LSTAR_EPS) ? cbrt(y) : ((LSTAR_KAPPA * y + 16.0) / 116.0);
    return 116.0 * fy - 16.0;
}

/* Inverse of theme_lstar(): convert a target CIE L* back to a target
 * relative luminance Y in [0,1]. */
static double lstar_to_luminance(double lstar)
{
    double fy = (lstar + 16.0) / 116.0;
    if (lstar > LSTAR_KAPPA * LSTAR_EPS) /* > 8.0 */
        return fy * fy * fy;
    return lstar / LSTAR_KAPPA;
}

/* Clamp a channel to [8, 247] so no derived colour ever reaches a pure
 * 0x00 or 0xFF channel (whole-colour bounds: 0x080808 .. 0xF7F7F7). */
static int clamp_channel(double v)
{
    int c = (int)(v + 0.5); /* round to nearest */
    if (c < 8) return 8;
    if (c > 247) return 247;
    return c;
}

unsigned int theme_shift_lightness(unsigned int rgb, double dl)
{
    unsigned int r = (rgb >> 16) & 0xFFu;
    unsigned int g = (rgb >>  8) & 0xFFu;
    unsigned int b =  rgb        & 0xFFu;
    double lr = linearise(r), lg = linearise(g), lb = linearise(b);
    double L = 0.2126 * lr + 0.7152 * lg + 0.0722 * lb;

    double target_lstar = theme_lstar(rgb) + dl;
    if (target_lstar < 0.0) target_lstar = 0.0;
    if (target_lstar > 100.0) target_lstar = 100.0;
    double target = lstar_to_luminance(target_lstar);
    if (target < 0.0) target = 0.0;
    if (target > 1.0) target = 1.0;

    double nr = lr, ng = lg, nb = lb;
    if (target > L) {
        /* Interpolate toward white so the resulting luminance is exactly
         * (approximately, before channel rounding/clamping) target. */
        double t = (L < 1.0) ? (target - L) / (1.0 - L) : 0.0;
        nr = lr + t * (1.0 - lr);
        ng = lg + t * (1.0 - lg);
        nb = lb + t * (1.0 - lb);
    } else if (target < L) {
        /* Interpolate toward black. */
        double t = (L > 0.0) ? (L - target) / L : 0.0;
        nr = lr * (1.0 - t);
        ng = lg * (1.0 - t);
        nb = lb * (1.0 - t);
    }

    int r2 = clamp_channel(delinearise(nr) * 255.0);
    int g2 = clamp_channel(delinearise(ng) * 255.0);
    int b2 = clamp_channel(delinearise(nb) * 255.0);
    return ((unsigned int)r2 << 16) | ((unsigned int)g2 << 8) | (unsigned int)b2;
}

unsigned int theme_blend(unsigned int a, unsigned int b, double t)
{
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    double ar = (double)((a >> 16) & 0xFFu);
    double ag = (double)((a >>  8) & 0xFFu);
    double ab = (double)( a        & 0xFFu);
    double br = (double)((b >> 16) & 0xFFu);
    double bg = (double)((b >>  8) & 0xFFu);
    double bb = (double)( b        & 0xFFu);
    int r = (int)(ar + t * (br - ar) + 0.5);
    int g = (int)(ag + t * (bg - ag) + 0.5);
    int bl = (int)(ab + t * (bb - ab) + 0.5);
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (bl < 0) bl = 0;
    if (bl > 255) bl = 255;
    return ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)bl;
}
