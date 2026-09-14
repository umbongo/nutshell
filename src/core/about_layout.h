#ifndef NUTSHELL_ABOUT_LAYOUT_H
#define NUTSHELL_ABOUT_LAYOUT_H

/*
 * about_layout — portable (Win32-free) layout math for the About window.
 * Pure C, no windows.h, no HWND: this module compiles into both the MinGW
 * cross-build and the native Linux test build (tests/test_about_layout.c).
 *
 * The About window is one centred column: acorn icon, title, tagline,
 * copyright, OK button. Every gap comes from the SP_ / SZ_ grid in
 * ns_type.h and is scaled with ns_scale(), so the window sizes itself to
 * its content at any DPI instead of using a fixed pixel height.
 *
 * The Win32 shell (src/ui/window.c) supplies the measured text line
 * heights and does the drawing.
 */

/* 96-DPI base metrics. */
enum {
    ABOUT_CLIENT_W_96 = 360, /* client width (fits the tagline at 96 DPI) */
    ABOUT_ICON_96     = 64   /* acorn icon edge */
};

typedef struct { int x, y, w, h; } AboutRect;

typedef struct {
    AboutRect icon;      /* centred, icon_px square */
    AboutRect title;     /* full-width rows; draw the text DT_CENTER */
    AboutRect tagline;
    AboutRect copyright;
    AboutRect button;    /* SZ_BTN_MIN_W x SZ_CTRL_H, centred */
    int client_w;
    int client_h;
} AboutLayout;

/*
 * Stack the About window's content and report the client size it needs.
 *   dpi         — window DPI; <= 0 is treated as 96.
 *   icon_px     — already-scaled icon edge (e.g. ns_scale(ABOUT_ICON_96, dpi)).
 *   title_h,
 *   tagline_h,
 *   copyright_h — already-scaled line heights of the three text rows.
 * Negative sizes are clamped to 0; `out` may be NULL (no-op).
 */
void about_layout_compute(AboutLayout *out, int dpi, int icon_px,
                          int title_h, int tagline_h, int copyright_h);

#endif /* NUTSHELL_ABOUT_LAYOUT_H */
