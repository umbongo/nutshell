#ifndef NUTSHELL_CORE_ZOOM_H
#define NUTSHELL_CORE_ZOOM_H

/*
 * zoom_font_fits — check whether a font's cell metrics divide the
 * available client area evenly (no gutter).
 *
 *   client_w   client-area width  (pixels)
 *   term_h     terminal-area height (client_h - tab_height) (pixels)
 *   char_w     character cell width  (pixels); 0 returns 0
 *   char_h     character cell height (pixels); 0 returns 0
 *
 * Returns 1 if both client_w % char_w == 0 and term_h % char_h == 0,
 * meaning the font fills the window exactly with no residual strip.
 */
int zoom_font_fits(int client_w, int term_h, int char_w, int char_h);

#endif
