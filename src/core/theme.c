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

/* Keep fg's own hue: walk its lightness away from bg (lighter on a dark
 * ground, darker on a light one) in 2-L* steps until it reads at
 * min_ratio. theme_shift_lightness scales the channels together, so the
 * hue survives; only its lightness moves. Each step starts from the
 * previous step's (rounded, clamped) result, as theme_role_color() always
 * has, so its outputs are unchanged. Returns 1 with *out set on success,
 * 0 if 50 steps never clear min_ratio. */
static int walk_to_ratio(unsigned int fg, unsigned int bg, double min_ratio,
                         unsigned int *out)
{
    double dir = (theme_lstar(bg) < 50.0) ? 2.0 : -2.0;
    unsigned int c = fg;
    for (int i = 0; i < 50; i++) {
        c = theme_shift_lightness(c, dir);
        if (theme_contrast(c, bg) >= min_ratio) { *out = c; return 1; }
    }
    return 0;
}

unsigned int theme_role_color(unsigned int role, unsigned int bg,
                              unsigned int fallback)
{
    if (theme_contrast(role, bg) >= 4.5) return role;
    /* The fallback is reached only if even the lightest/darkest reachable
     * shade cannot clear 4.5:1 (never the case for a saturated hue against
     * a mid-grey, but guaranteed legible regardless). */
    unsigned int c = 0;
    return walk_to_ratio(role, bg, 4.5, &c) ? c : fallback;
}

unsigned int theme_min_contrast_color(unsigned int fg, unsigned int bg,
                                      double min_ratio)
{
    if (!(min_ratio > 1.0)) return fg;          /* off, and NaN */
    if (theme_contrast(fg, bg) >= min_ratio) return fg;
    unsigned int c = 0;
    if (walk_to_ratio(fg, bg, min_ratio, &c)) return c;
    /* No shade of the hue clears it: the better of the walk's two
     * extremes (theme_shift_lightness never goes past them). */
    return (theme_contrast(0x080808u, bg) >= theme_contrast(0xF7F7F7u, bg))
        ? 0x080808u : 0xF7F7F7u;
}
