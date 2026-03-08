#ifndef NUTSHELL_APP_FONT_H
#define NUTSHELL_APP_FONT_H

/* Canonical list of allowed discrete font sizes.  Defined once here;
 * every other module (loader.c, settings.c, window.c) must use this
 * table instead of maintaining its own copy. */

#define APP_FONT_NUM_SIZES 8

extern const int k_app_font_sizes[APP_FONT_NUM_SIZES];

/* Default font family and sizes. */
#define APP_FONT_DEFAULT      "Cascadia Code"
#define APP_FONT_DEFAULT_SIZE 10
#define APP_FONT_UI_SIZE       9

/* Snap a raw font_size to the nearest allowed discrete size. */
int app_font_snap_size(int raw_size);

/* Given current size, return the next size in direction delta (+1 or -1).
 * Returns current size if already at boundary. */
int app_font_zoom(int current_size, int delta);

#endif
