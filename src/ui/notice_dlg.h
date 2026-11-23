/* notice_dlg.h -- the themed modal notice dialog (window class
 * Nutshell_Notice). Content and order live in src/core/notice.[ch], geometry
 * in src/core/notice_layout.[ch]; this is the window.
 *
 * Real STATIC and BUTTON children (readable by screen readers and the
 * integration harness), colours from ns_tokens(), fonts from ns_font(),
 * buttons from ns_draw_button(). Not topmost; centred on the owner; takes the
 * owner's DPI and follows WM_DPICHANGED. */
#ifndef NUTSHELL_NOTICE_DLG_H
#define NUTSHELL_NOTICE_DLG_H

#ifdef _WIN32
#include <windows.h>
#include "notice.h"
#include "ui_theme.h"

/* Control ids (GetDlgItem). Labels and values are LABEL0 + i / VALUE0 + i.
 * The action button's id cannot be 1: that is IDOK, which is the OK button. */
#define NOTICE_ID_TITLE    100
#define NOTICE_ID_OPENING  101
#define NOTICE_ID_CLOSING  102
#define NOTICE_ID_LABEL0   110
#define NOTICE_ID_VALUE0   140
#define NOTICE_ID_ACTION   10
#define NOTICE_ID_OK       IDOK

/* Shows `n` (copied at create time) and runs a modal loop; `owner` is
 * disabled for the duration (and re-enabled only if it was enabled), and may
 * be NULL. Returns 1 when the action button was pressed (button or Enter on
 * it), 0 for OK, Enter on OK, Escape, the close box or a WM_QUIT (which is
 * reposted for the main loop).
 *
 * Keys, handled in the loop because owner-drawn buttons defeat default-button
 * handling: Enter presses the focused button, Escape = OK, Tab and Shift+Tab
 * move between the buttons, Ctrl+C copies the notice as plain text (CRLF).
 *
 * When n->action_page is -1 an action press still returns 1: the caller then
 * opens Settings at its default page. The caller opens Settings after this
 * returns, never from inside it. `theme` may be NULL (the first theme). */
int notice_dlg_show(HWND owner, const Notice *n, const ThemeColors *theme);

#endif /* _WIN32 */
#endif /* NUTSHELL_NOTICE_DLG_H */
