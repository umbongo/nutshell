/* src/ui/ns_draw.h — the one drawing module for Nutshell UI chrome.
 *
 * The only chrome code that calls GDI+ directly (rounded-rect fill/stroke
 * need anti-aliasing that plain GDI's RoundRect() cannot give us); plain
 * GDI is used where no anti-aliasing is needed (chips, pulses, separators).
 * All colours come from ThemeTokens/ThemeSurface, all sizes from the
 * spacing grid in ns_type.h — see
 * docs/superpowers/specs/2026-09-07-design-system-foundation-design.md
 * section 3.
 *
 * Grown from ui_draw.{c,h} (Design-System Foundation, Task 4).
 */
#ifndef NUTSHELL_NS_DRAW_H
#define NUTSHELL_NS_DRAW_H

#ifdef _WIN32
#include <windows.h>
#include "ui_theme.h"
#include "icons.h"

/* Blend `fg` toward `bg` by `alpha` (0.0 = bg, 1.0 = fg).
 * Clamps to [0, 1].  Used to tint chip backgrounds against the
 * containing surface, and to wash out disabled button labels. */
COLORREF rgb_alpha(COLORREF fg, COLORREF bg, float alpha);

/* Draw a rounded-rect chip with centred text.
 *  rc   — outer bounds.
 *  bg   — fill colour (use rgb_alpha against the surface bg).
 *  fg   — text colour.
 *  font — font to select for text (may be NULL to keep current).
 *  text — UTF-8 label.
 */
void ns_draw_chip(HDC hdc, const RECT *rc, COLORREF bg, COLORREF fg,
                   HFONT font, const char *text);

/* Draw a pulsing outer ring around a status dot.
 *  rc     — bounding rect of the inner dot (ring is drawn around it).
 *  colour — ring colour; alpha modulated by `phase` via sin(phase).
 *  phase  — [0, 2*PI); advance by ~0.1 per frame from a timer.
 */
void ns_draw_pulse(HDC hdc, const RECT *rc, COLORREF colour, float phase);

/* Anti-aliased rounded-rect fill/stroke — the primitives every other
 * ns_draw_* helper (and every remaining ad-hoc RoundRect() call site in
 * src/ui) builds on. `radius_px` is a true corner radius, already scaled
 * by the caller via ns_scale(R_CTRL|R_CARD, dpi). */
void ns_draw_round_fill(HDC hdc, const RECT *rc, int radius_px,
                         COLORREF fill, BYTE alpha);
void ns_draw_round_stroke(HDC hdc, const RECT *rc, int radius_px,
                           COLORREF stroke, int width_px);

/* A raised card: raised.base fill, border stroke at STROKE_HAIRLINE,
 * corner radius ns_scale(R_CARD, dpi). */
void ns_draw_card(HDC hdc, const RECT *rc, const ThemeTokens *tokens,
                   int dpi);

typedef enum {
    NS_BTN_REST,
    NS_BTN_HOVER,
    NS_BTN_PRESSED,
    NS_BTN_DISABLED
} NsBtnState;

/* A themed button: fill from surface->base/hover/pressed/disabled per
 * `state`, label in surface->label (blended toward the fill when
 * disabled), corner radius ns_scale(R_CTRL, dpi), and a focus ring
 * (ns_draw_focus_ring) when `focused`. `label` is UTF-8, may be NULL. */
void ns_draw_button(HDC hdc, const RECT *rc, const ThemeSurface *surface,
                     NsBtnState state, int focused, const char *label,
                     HFONT font, int dpi);

/* An icon followed by a label: icon at SZ_ICON on the left, SP_SM gap,
 * label vertically centred in the rest of `rc`. `label` may be NULL to
 * draw the icon alone. */
void ns_draw_icon_label(HDC hdc, const RECT *rc, NsIconId icon,
                         const char *label, COLORREF colour, HFONT font,
                         int dpi);

/* A STROKE_HAIRLINE horizontal rule from x1 to x2 at y. */
void ns_draw_separator(HDC hdc, int x1, int x2, int y, COLORREF colour);

/* A keyboard-focus ring: STROKE_RULE wide, radius ns_scale(R_CTRL, dpi),
 * inset 1 px inside `rc`. */
void ns_draw_focus_ring(HDC hdc, const RECT *rc, COLORREF colour, int dpi);

#endif /* _WIN32 */
#endif /* NUTSHELL_NS_DRAW_H */
