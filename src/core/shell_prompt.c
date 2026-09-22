/* src/core/shell_prompt.c */
#include "shell_prompt.h"
#include <string.h>

/* zsh's secondary-prompt ($PS2) words, each valid immediately before a
 * trailing '>' with no space in between (e.g. "dquote>"). Exact,
 * case-sensitive whole-word match only -- see shell_prompt_is_continuation()
 * in the header. */
static const char *const CONTINUATION_WORDS[] = {
    "dquote", "quote", "bquote", "cmdsubst", "heredoc", "pipe", "cmdand",
    "cmdor", "braceparam", "math", "cursh", "then", "do", "done", "for",
    "foreach", "while", "until", "repeat", "if", "elif", "else", "fi",
    "case", "esac", "select", "function", "array", "end"
};
#define CONTINUATION_WORDS_COUNT (sizeof(CONTINUATION_WORDS) / sizeof(CONTINUATION_WORDS[0]))

int shell_prompt_is_continuation(const char *text)
{
    if (!text || !text[0]) return 0;

    size_t len = strlen(text);
    size_t start = 0;
    while (start < len && (text[start] == ' ' || text[start] == '\t'))
        start++;
    while (len > start && (text[len - 1] == ' ' || text[len - 1] == '\t'))
        len--;
    if (len <= start) return 0; /* empty or all whitespace */

    if (text[len - 1] != '>') return 0;

    size_t word_len = (len - 1) - start;
    if (word_len == 0) return 1; /* bare '>' */

    for (size_t i = 0; i < CONTINUATION_WORDS_COUNT; i++) {
        size_t wl = strlen(CONTINUATION_WORDS[i]);
        if (wl == word_len && strncmp(text + start, CONTINUATION_WORDS[i], wl) == 0)
            return 1;
    }

    return 0;
}

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

    /* A continuation prompt -- a bare '>' or one of zsh's secondary-prompt
     * words followed by '>' (dquote>, cmdsubst>, ...) -- is waiting for
     * more input, not ready for a new command. */
    if (last == '>' && shell_prompt_is_continuation(text))
        return 0;

    return 1;
}
