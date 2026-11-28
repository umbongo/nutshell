#ifndef NUTSHELL_MAIN_CHORD_WIN_H
#define NUTSHELL_MAIN_CHORD_WIN_H

/* The main-window chord (src/core/main_chords.h) that the key-down `vk` is
 * under the keyboard state right now; MCHORD_NONE when it is none. For the
 * AI panel's windows, which exist only while the assistant is on. */

#include <windows.h>
#include "main_chords.h"

static inline MainChord main_chord_now(WPARAM vk)
{
    return main_chord_classify((unsigned)vk,
                               (GetKeyState(VK_SHIFT)   & 0x8000) != 0,
                               (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                               (GetKeyState(VK_MENU)    & 0x8000) != 0, 1);
}

#endif /* NUTSHELL_MAIN_CHORD_WIN_H */
