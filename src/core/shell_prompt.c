/* src/core/shell_prompt.c */
#include "shell_prompt.h"
#include <string.h>

int shell_prompt_line(const char *text)
{
    if (!text || !text[0]) return 0;

    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t'))
        len--;
    if (len == 0) return 0;

    char last = text[len - 1];
    if (last != '$' && last != '#' && last != '%' && last != '>')
        return 0;

    /* A bare '>' -- nothing else on the (trimmed) line -- is a PS2-style
     * continuation prompt, not a primary prompt ready for a new command. */
    if (last == '>' && len == 1) return 0;

    return 1;
}
