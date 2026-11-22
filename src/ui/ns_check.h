/* src/ui/ns_check.h -- themed painting for a BS_AUTOCHECKBOX button.
 *
 * The control stays a native checkbox (state, BM_GETCHECK, BN_CLICKED, the
 * space bar, tab stops and accessibility are the system's); a window subclass
 * takes over only its painting: a rounded SZ_CHECK square, an accent fill and
 * an anti-aliased check mark when checked, the label in the theme's text
 * colour, hover and a focus ring, all from ns_tokens(). No visual-styles
 * opt-out (ns_control_classic) is needed for these controls. */
#ifndef NUTSHELL_NS_CHECK_H
#define NUTSHELL_NS_CHECK_H

#ifdef _WIN32
#include <windows.h>

/* Subclasses `chk` (a BUTTON with BS_AUTOCHECKBOX). Idempotent; a NULL
 * handle or a failed subclass leaves the native control as it was.
 * Call right after creating the control; give it its font with WM_SETFONT
 * at any time (the label follows WM_GETFONT). */
void ns_check_attach(HWND chk);

/* Nonzero once `chk` carries the themed painter (the wintests use it). */
int ns_check_is_attached(HWND chk);

#endif
#endif
