#ifndef NUTSHELL_MAIN_CHORD_WIN_H
#define NUTSHELL_MAIN_CHORD_WIN_H

/* The main-window chord (src/core/main_chords.h) that the key-down `vk` is
 * under the keyboard state right now; MCHORD_NONE when it is none. For the
 * AI panel's windows, which exist only while the assistant is on. */

#include <windows.h>
#include "main_chords.h"

/* The private message the AI panel sends the main window for a chord it
 * forwards (WM_APP + 0x40..0x51 are taken by voice and the chat list).
 * wParam = MainChord, lParam = 1 for an auto-repeat key-down, else 0. The
 * main window runs the chord's action and nothing else: it is not a key, so
 * the Send Next Key one-shot, the terminal-input mark and the key encoder
 * are out of its reach. */
#define WM_APP_MAIN_CHORD (WM_APP + 0x52)

/* The panel's side: send chord `mc` (key-down lParam `keydown_lparam`) from
 * window `from` to the main window that owns the panel (GA_ROOTOWNER: its
 * parent when docked, its owner when floating), when
 * main_chord_should_forward() allows it. */
static inline void main_chord_forward(HWND from, MainChord mc, LPARAM keydown_lparam)
{
    HWND main_wnd = GetAncestor(from, GA_ROOTOWNER);
    if (main_wnd && main_wnd != from &&
        main_chord_should_forward(mc, IsWindowEnabled(main_wnd) != 0))
        SendMessage(main_wnd, WM_APP_MAIN_CHORD, (WPARAM)mc,
                    (keydown_lparam & (1L << 30)) ? 1 : 0);
}

static inline MainChord main_chord_now(WPARAM vk)
{
    return main_chord_classify((unsigned)vk,
                               (GetKeyState(VK_SHIFT)   & 0x8000) != 0,
                               (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                               (GetKeyState(VK_MENU)    & 0x8000) != 0, 1);
}

#endif /* NUTSHELL_MAIN_CHORD_WIN_H */
