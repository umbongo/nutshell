#ifndef NUTSHELL_THEME_H
#define NUTSHELL_THEME_H

/* Pure, portable theme helpers (no Win32 dependency).
 *
 * Colours are passed as packed 0x00RRGGBB values (matching Win32 COLORREF
 * with the high byte stripped, and matching uint32_t used elsewhere in the
 * terminal emulator).
 */

/* Compute relative luminance of an sRGB colour per ITU-R BT.709.
 * Returns a value in [0.0, 1.0].  0.0 = black, 1.0 = white. */
double theme_luminance(unsigned int color_rgb);

/* Returns 1 if the colour is perceptually "dark" (luminance < 0.5),
 * 0 if "light".  Useful for choosing title-bar and UI chrome colour. */
int theme_is_dark(unsigned int color_rgb);

/* WCAG 2.x contrast ratio between two sRGB colours: (L1+0.05)/(L2+0.05)
 * with L1 the greater of the two relative luminances.  Range [1.0, 21.0]. */
double theme_contrast(unsigned int a, unsigned int b);

/* CIE L* (perceptual lightness), computed from theme_luminance() as the
 * relative luminance (Y, D65 white Y_n = 1.0):
 *   L* = 116 * f(Y) - 16
 *   f(t) = cbrt(t)                     for t > 216/24389
 *   f(t) = (24389/27 * t + 16) / 116   otherwise
 * Range [0.0, 100.0]. 0 = black, 100 = white. */
double theme_lstar(unsigned int rgb);

/* Move a colour's perceptual lightness (CIE L*) by dl units (positive =
 * lighter, negative = darker): the target L* is converted back to a
 * target relative luminance, and the linear-light channels are rescaled
 * proportionally toward white (dl > 0) or black (dl < 0) to hit it, which
 * preserves hue.  Each output channel is clamped to [0x08, 0xF7] so the
 * result never reaches pure black or white (whole-colour bounds: at least
 * 0x080808, at most 0xF7F7F7). */
unsigned int theme_shift_lightness(unsigned int rgb, double dl);

/* Per-channel linear interpolation between a (t=0) and b (t=1).
 * t is clamped to [0.0, 1.0]. */
unsigned int theme_blend(unsigned int a, unsigned int b, double t);

#endif
