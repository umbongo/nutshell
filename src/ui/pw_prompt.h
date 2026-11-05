/* src/ui/pw_prompt.h -- modal themed prompt for one secret (the SSH key
 * passphrase today). Extended by the telnet password prompt. */
#ifndef NUTSHELL_PW_PROMPT_H
#define NUTSHELL_PW_PROMPT_H

#include <windows.h>
#include "ui_theme.h"

typedef struct {
    const char        *title;  /* window caption, e.g. "SSH Key Passphrase" */
    const char        *label;  /* text above the field, e.g. "Enter key passphrase:" */
    const ThemeColors *theme;  /* for the title bar; NULL leaves it default */
    const char        *detail;      /* line under the label: profile name and host; NULL = none */
    int                offer_save;  /* show the "Save password to this profile" checkbox */
    const char        *text;        /* read-only scrolling area above the label (LF line
                                     * breaks; converted to CRLF here); NULL = none */
    int                echo;        /* 1 = the answer is shown as typed (no mask, no eye) */
} PwPromptOpts;

/* The strings in `opts` (title, label, detail, text) are read only while the
 * window is created, before the modal loop starts, and are not kept: the
 * caller's copies may go away while the dialog is up.
 *
 * Modal prompt owned by `parent`. Returns 1 on OK with the text in `out`
 * (NUL-terminated, at most out_size - 1 bytes), 0 on Cancel, close or
 * WM_QUIT (re-posted). Every internal copy is wiped with secure_zero().
 * *save_out (may be NULL) is 1 when OK was pressed with the save box
 * ticked, else 0. */
int pw_prompt_show(HWND parent, const PwPromptOpts *opts, char *out, int out_size,
                   int *save_out);

#endif /* NUTSHELL_PW_PROMPT_H */
