/* src/ui/pw_reveal.h -- hold-to-reveal eye button for ES_PASSWORD edits.
 * The press/release logic is the pure state machine in
 * src/core/pw_reveal_state.h; this file is its Win32 skin. */
#ifndef NUTSHELL_PW_REVEAL_H
#define NUTSHELL_PW_REVEAL_H

#include <windows.h>

/* Attach an eye button inside the right edge of `edit` (an ES_PASSWORD
 * EDIT). Idempotent: a second call returns the existing eye. Returns the
 * eye's HWND, or NULL on failure (the field still works, unrevealable).
 * Everything is released when the edit is destroyed. */
HWND pw_reveal_attach(HWND edit);

/* Mask every revealed field among root's descendants (hosts call it on
 * WM_ACTIVATE with WA_INACTIVE). NULL is a no-op. */
void pw_reveal_mask_all(HWND root);

/* 1 while `edit`'s text is shown in plain text, else 0 (tests, review). */
int pw_reveal_is_revealed(HWND edit);

#endif /* NUTSHELL_PW_REVEAL_H */
