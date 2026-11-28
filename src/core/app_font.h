#ifndef NUTSHELL_APP_FONT_H
#define NUTSHELL_APP_FONT_H

/* Canonical list of allowed discrete font sizes.  Defined once here;
 * every other module (loader.c, settings.c, window.c) must use this
 * table instead of maintaining its own copy. */

#define APP_FONT_NUM_SIZES 8

extern const int k_app_font_sizes[APP_FONT_NUM_SIZES];

/* Default font family and sizes. */
#define APP_FONT_DEFAULT      "Consolas"
#define APP_FONT_DEFAULT_SIZE 10
#define APP_FONT_UI_SIZE       10
#define APP_FONT_UI_FACE      "Inter"
#define APP_FONT_AI_DEFAULT   "Consolas"

/* Load/free the bundled Inter UI font from exe resources.
 * Call app_font_load_ui() once at startup before creating UI.
 * Call app_font_free_ui() at shutdown. */
#ifdef _WIN32
void app_font_load_ui(void);
void app_font_free_ui(void);
#endif

/* Snap a raw font_size to the nearest allowed discrete size. */
int app_font_snap_size(int raw_size);

/* Given current size, return the next size in direction delta (+1 or -1).
 * Returns current size if already at boundary. */
int app_font_zoom(int current_size, int delta);

/* The CreateFont character height for `pt` points at `dpi`: -MulDiv(pt, dpi,
 * 72), rounded to the nearest pixel, negative (a character height, not a cell
 * height). 0 for pt <= 0; dpi <= 0 is read as 96. The AI panel's zoomable
 * fonts are rebuilt from the user's point size with this when the panel moves
 * to a monitor with another scale. */
int app_font_px_height(int pt, int dpi);

#endif
