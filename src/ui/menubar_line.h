#ifndef NUTSHELL_MENUBAR_LINE_H
#define NUTSHELL_MENUBAR_LINE_H

#include "ui_theme.h"

/*
 * Pure-logic helper: compute the color to paint over the Windows-drawn
 * bright line at the bottom of the menu bar.
 *
 * Uses the theme's border color so the line is a subtle but visible
 * separator between the menu bar and the tab bar.  Without this, dark
 * themes show a jarring bright 1px line.
 *
 * Returns 0xRRGGBB color.  fallback_rgb is used when theme is NULL.
 */
static inline unsigned int menubar_line_color(const ThemeColors *theme,
                                               unsigned int fallback_rgb)
{
    return theme ? theme->border : fallback_rgb;
}

/* Compute the RECT for the menu bar bottom border line in screen coordinates.
 * win_x, win_y: window position (screen coords from GetWindowRect)
 * client_y: client area top in screen coords (from ClientToScreen of 0,0)
 * win_width: window width
 * Returns: y position (screen coords) and 1px height. */
static inline void menubar_line_rect(int win_x, int win_y,
                                      int client_y, int win_width,
                                      int *out_x, int *out_y,
                                      int *out_w, int *out_h)
{
    (void)win_y;
    /* The bright line is the 1px row immediately above the client area */
    if (out_x) *out_x = win_x;
    if (out_y) *out_y = client_y - 1;
    if (out_w) *out_w = win_width;
    if (out_h) *out_h = 1;
}

#endif /* NUTSHELL_MENUBAR_LINE_H */
