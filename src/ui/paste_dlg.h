#ifndef NUTSHELL_PASTE_DLG_H
#define NUTSHELL_PASTE_DLG_H

#ifdef _WIN32
#include <windows.h>

/* Show a scrollable paste-preview dialog styled with the current colour
 * scheme.  Returns 1 if the user confirmed ("Paste"), 0 if cancelled.
 * colour_scheme: theme name for custom scrollbar (may be NULL for default). */
int paste_preview_show(HWND parent, const char *raw_text,
                       const char *fg_hex, const char *bg_hex,
                       const char *font_name, int font_size,
                       const char *colour_scheme);
#endif

#endif
