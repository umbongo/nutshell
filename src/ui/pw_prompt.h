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
} PwPromptOpts;

/* Modal prompt owned by `parent`. Returns 1 on OK with the text in `out`
 * (NUL-terminated, at most out_size - 1 bytes), 0 on Cancel, close or
 * WM_QUIT (re-posted). Every internal copy is wiped with secure_zero().
 * *save_out (may be NULL) is 1 when OK was pressed with the save box
 * ticked, else 0. */
int pw_prompt_show(HWND parent, const PwPromptOpts *opts, char *out, int out_size,
                   int *save_out);

#endif /* NUTSHELL_PW_PROMPT_H */
