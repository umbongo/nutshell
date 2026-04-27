/* src/ui/ui_draw.h — small GDI helpers shared across Nutshell UI surfaces.
 *
 * Helpers for severity chips (approval list), status pulses (tab strip),
 * and alpha-blended fills.  All functions assume a caller-provided HDC
 * and do not own any GDI objects.
 */
#ifndef NUTSHELL_UI_DRAW_H
#define NUTSHELL_UI_DRAW_H

#ifdef _WIN32
#include <windows.h>

/* Blend `fg` toward `bg` by `alpha` (0.0 = bg, 1.0 = fg).
 * Clamps to [0, 1].  Used to tint chip backgrounds against the
 * containing surface. */
COLORREF rgb_alpha(COLORREF fg, COLORREF bg, float alpha);

/* Draw a rounded-rect chip with centred text.
 *  rc   — outer bounds.
 *  bg   — fill colour (use rgb_alpha against the surface bg).
 *  fg   — text colour.
 *  font — font to select for text (may be NULL to keep current).
 *  text — UTF-8 label.
 */
void draw_chip(HDC hdc, const RECT *rc, COLORREF bg, COLORREF fg,
               HFONT font, const char *text);

/* Draw a pulsing outer ring around a status dot.
 *  rc     — bounding rect of the inner dot (ring is drawn around it).
 *  colour — ring colour; alpha modulated by `phase` via sin(phase).
 *  phase  — [0, 2*PI); advance by ~0.1 per frame from a timer.
 */
void draw_status_pulse(HDC hdc, const RECT *rc, COLORREF colour, float phase);

#endif /* _WIN32 */
#endif /* NUTSHELL_UI_DRAW_H */
