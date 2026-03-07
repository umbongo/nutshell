#ifndef NUTSHELL_PASTE_PREVIEW_H
#define NUTSHELL_PASTE_PREVIEW_H

#include <stddef.h>

/* Split raw clipboard text into an array of lines.
 * Strips \r characters.  A trailing newline does NOT produce an empty
 * final line.  Returns a malloc'd array of malloc'd strings and sets
 * *out_count.  Returns NULL (and *out_count = 0) on empty or NULL input. */
char **paste_format_lines(const char *raw, int *out_count);

/* Free array returned by paste_format_lines. */
void paste_line_free(char **lines, int count);

/* Format a summary string: "Paste N line(s) (M chars)?" */
void paste_build_summary(int line_count, size_t char_count,
                         char *buf, size_t buf_sz);

#endif
