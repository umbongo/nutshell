/* src/ui/ns_tip.h -- the stock comctl32 tooltip, classic-rendered in the
 * theme's colours and font. One helper for every window that wants help
 * tips; the Settings dialog is the first. See
 * docs/superpowers/specs/2026-11-05-settings-help-design.md, section 1. */
#ifndef NUTSHELL_NS_TIP_H
#define NUTSHELL_NS_TIP_H

#ifdef _WIN32
#include <windows.h>
#include "ui_theme.h"

/* Create the tooltip window owned by `owner`: a plain (not balloon) tip with
 * TTS_NOPREFIX | TTS_ALWAYSTIP, WS_EX_TOPMOST, rendered classic so it honours
 * the colours: raised.base background and text_main text from `tokens`, the
 * font `font` (borrowed, never deleted), a ns_scale(360, dpi) maximum width,
 * ns_scale(SP_XS, dpi) margins, and delays of 500 / 100 / 32767 ms.
 * A NULL `tokens` leaves the system colours; a NULL `font` the default font.
 * Returns NULL on failure. */
HWND ns_tip_create(HWND owner, int dpi, HFONT font, const ThemeTokens *tokens);

/* Add `tool` (a window) as a tool: TTF_IDISHWND | TTF_SUBCLASS, hwnd is the
 * tool's parent and uId the tool. Changes no window style. Non-zero on
 * success. */
int ns_tip_add(HWND tip, HWND tool, const char *text);

/* Add the edit inside an editable combo as a tool (the combo's own window
 * only covers the button). Returns 0 when `combo` has no item edit. */
int ns_tip_add_combo(HWND tip, HWND combo, const char *text);

/* Add a rectangle of `host` (client coordinates; NULL = empty) as a tool,
 * identified by `id`: TTF_SUBCLASS without TTF_IDISHWND. Non-zero on
 * success. */
int ns_tip_add_rect(HWND tip, HWND host, UINT_PTR id, const RECT *rc,
                    const char *text);

/* Move a rect tool added with ns_tip_add_rect (NULL = {0,0,0,0}). */
void ns_tip_move_rect(HWND tip, HWND host, UINT_PTR id, const RECT *rc);

#endif /* _WIN32 */
#endif /* NUTSHELL_NS_TIP_H */
