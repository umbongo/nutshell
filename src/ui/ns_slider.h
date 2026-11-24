/* src/ui/ns_slider.h -- a themed, custom-drawn horizontal slider.
 *
 * Custom-drawn rather than TRACKBAR_CLASS because the stock trackbar ignores
 * the theme tokens (its track and thumb follow the visual style, not
 * ns_tokens()). Class "Nutshell_Slider": a raised.base pill track, the part
 * up to the thumb in accent.base, a round thumb (raised.hover when hovered,
 * accent outline), tokens->focus ring when focused, .disabled colours when
 * disabled. A tab stop; Left/Right/Up/Down, PageUp/PageDown, Home and End
 * move it; the mouse captures while dragging. Every change (drag, click, key)
 * sends the parent WM_COMMAND(MAKEWPARAM(id, NSS_CHANGED), hwnd); ns_slider_set
 * is silent. */
#ifndef NUTSHELL_NS_SLIDER_H
#define NUTSHELL_NS_SLIDER_H

#ifdef _WIN32
#include <windows.h>

#define NSS_CHANGED 1

/* Creates the slider (registering the class on first use) with the given
 * range and value (clamped); WS_CHILD | WS_VISIBLE | WS_TABSTOP. The caller
 * positions it with MoveWindow/SetWindowPos. NULL on failure. */
HWND ns_slider_create(HWND parent, int id, int min, int max, int value);

/* Sets the value (clamped) without notifying the parent. */
void ns_slider_set(HWND slider, int value);

/* The current value; 0 for a NULL or foreign window. */
int ns_slider_get(HWND slider);

#endif
#endif
