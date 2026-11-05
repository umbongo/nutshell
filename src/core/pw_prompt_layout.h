/* pw_prompt_layout.h -- geometry of the modal secret prompt (src/ui/pw_prompt.c).
 *
 * Portable (ns_scale.h and ns_type.h only) so the native tests can check the
 * rows. The Win32 dialog builds its controls from the same struct. */
#ifndef PW_PROMPT_LAYOUT_H
#define PW_PROMPT_LAYOUT_H

#include <stddef.h>

/* One layout for WM_CREATE and the frame size. */
typedef struct {
    int pad, gap, ctlh, fw, bw, texth;
    int y_text, y_label, y_detail, y_edit, y_save, y_btn;
    int client_h;
} PwLayout;

/* Rows top to bottom: [text area], label, [detail], field, [save], buttons.
 * Each row but the text area is ctlh high; rows are gap or pad apart.
 * Without a text area texth is 0 and y_text is pad (the label's y). Without a
 * detail row y_detail equals y_edit; without a save row y_save is the line
 * below the field and unused. */
void pw_layout(int dpi, int has_detail, int has_text, int has_save, PwLayout *L);

/* LF -> CRLF for the multi-line EDIT (an existing CRLF is not doubled). Returns
 * the length written; stops before a pair that would not fit; NUL-terminated
 * when cap > 0. NULL in gives "". */
size_t pw_text_crlf(const char *in, char *out, size_t cap);

#endif
