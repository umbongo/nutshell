#ifndef CONGA_THEME_H
#define CONGA_THEME_H

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

#endif
