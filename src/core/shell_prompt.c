/* src/core/shell_prompt.c */
#include "shell_prompt.h"
#include <ctype.h>
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

    /* PowerShell's continuation prompt, ">> " (PSReadLine's default
     * ContinuationPrompt and the console host's own). Only a ">>" with
     * nothing else on the line: "PS C:\>> " is a nested primary prompt and
     * ">>>" is Python's. */
    if (word_len == 1 && text[start] == '>') return 1;

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

int shell_prompt_line_unambiguous(const char *text)
{
    if (!shell_prompt_line(text)) return 0;

    /* shell_prompt_line() already confirmed text is non-NULL/non-empty and
     * that its right-trimmed form ends in one of $ # % >. Re-trim the same
     * way to look at the character just before that terminator. */
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t'))
        len--;

    if (len >= 2) {
        char prev = text[len - 2];
        if (prev == ' ' || prev == '\t') return 0;
    }

    return 1;
}

/* True when trimming trailing spaces/tabs off `text` (assumed non-NULL)
 * leaves it ending in '>'. Shared by the two shapes in
 * shell_prompt_is_windows() below. */
static int ends_in_gt_trimmed(const char *text)
{
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t'))
        len--;
    return len > 0 && text[len - 1] == '>';
}

int shell_prompt_is_windows(const char *row)
{
    if (!row || !row[0]) return 0;

    const char *p = row;

    /* Skip an optional PowerShell Remoting prefix -- "[host]: " or
     * "[host.domain]: ", the literal shape Enter-PSSession prepends to
     * every prompt line -- before the "PS " token. A '[' that is not this
     * exact shape (no matching "]: " right after it) is left alone and
     * falls through to the checks below, so a device CLI's own bracketed
     * prompt ("[admin@MikroTik] >") is never silently unwrapped into a
     * false match. */
    if (p[0] == '[') {
        const char *close = strchr(p, ']');
        if (close && close[1] == ':' && close[2] == ' ')
            p = close + 3;
    }

    /* PowerShell: the literal "PS " token, then a path ending in '>' --
     * either a Windows path ("C:\...") or, for pwsh running on a POSIX
     * host, a Unix one ("/..."). The path's own contents are not
     * otherwise constrained: this only decides whether a clear-line
     * prefix should be sent, never whether the line is safe to send a
     * command to (term_at_unambiguous_prompt() gates that separately), so
     * a command paused mid-line under a "PS ...>" prompt is fine to also
     * read as "a PowerShell prompt". */
    if (p[0] == 'P' && p[1] == 'S' && p[2] == ' ') {
        const char *path = p + 3;
        int windows_path = isalpha((unsigned char)path[0]) &&
                            path[1] == ':' && path[2] == '\\';
        int posix_path = path[0] == '/';
        return (windows_path || posix_path) && ends_in_gt_trimmed(path);
    }

    /* cmd.exe: a bare drive-letter path ending in '>', with no "PS "
     * token -- "C:\...>". */
    if (isalpha((unsigned char)p[0]) && p[1] == ':' && p[2] == '\\')
        return ends_in_gt_trimmed(p);

    return 0;
}
