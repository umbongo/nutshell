#ifndef NUTSHELL_CHEATSHEET_DLG_H
#define NUTSHELL_CHEATSHEET_DLG_H

#ifdef _WIN32
#include <windows.h>
#include "ui_theme.h"

/*
 * Help > Keyboard Shortcuts: the modal, custom-painted shortcuts sheet
 * (spec docs/superpowers/specs/2026-10-20-keyboard-cheat-sheet-design.md
 * section 2.3). Draws src/core/shortcuts.c's table in the layout
 * src/core/cheatsheet_layout.c computes.
 *
 *   parent         -- the main window: owner, disabled while the sheet is
 *                     up, re-enabled only if this call disabled it.
 *   floating_panel -- the undocked AI Assist window, or NULL; disabled and
 *                     re-enabled by the same rule.
 *   theme          -- the active ThemeColors (title bar, Close button,
 *                     scrollbar); NULL means the first built-in theme.
 *
 * Returns when the sheet closes (Esc, Enter, Close, the caption's X). A call
 * made while a sheet is already open returns at once and opens nothing. A
 * WM_QUIT that arrives while it is open is re-posted after it closes.
 */
void cheatsheet_show(HWND parent, HWND floating_panel, const ThemeColors *theme);

#endif /* _WIN32 */
#endif /* NUTSHELL_CHEATSHEET_DLG_H */
