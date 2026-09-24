/* src/core/shell_prompt.h */
#ifndef NUTSHELL_CORE_SHELL_PROMPT_H
#define NUTSHELL_CORE_SHELL_PROMPT_H

/* Does `text` look like a shell waiting at a prompt for input?
 *
 * `text` is expected to be the terminal's cursor row, taken up to the
 * cursor column, with trailing spaces/tabs trimmed by the caller -- or
 * left untrimmed, since this function trims them itself.
 *
 * Returns 1 when the last non-blank character is one of $ # % > (common
 * primary-prompt terminators across bash/zsh/PowerShell/etc.) and the line
 * is not a continuation prompt per shell_prompt_is_continuation() -- a bare
 * '>' (nothing else on the line -- e.g. a PS2 continuation prompt) or one
 * of zsh's secondary-prompt words followed by '>' (e.g. "dquote> ") returns
 * 0, as does an empty or NULL line. */
int shell_prompt_line(const char *text);

/* Does `text` look like a shell continuation prompt -- one waiting for the
 * rest of a multi-line command, not ready for a new one?
 *
 * `text` is trimmed of leading and trailing spaces/tabs by this function.
 * Returns 1 when the trimmed text is a bare ">" (a generic PS2-style
 * continuation prompt), a bare ">>" (PowerShell's continuation prompt --
 * not "PS C:\>>", a nested primary prompt, nor ">>>", Python's), or one of
 * zsh's secondary-prompt words immediately
 * followed by ">" with no space in between -- dquote, quote, bquote,
 * cmdsubst, heredoc, pipe, cmdand, cmdor, braceparam, math, cursh, then,
 * do, done, for, foreach, while, until, repeat, if, elif, else, fi, case,
 * esac, select, function, array, end (zsh's PS2 vocabulary). The match on
 * the word is exact and case-sensitive against the whole of the trimmed
 * text before the ">" -- "dquote>" matches, "mydquote>" and "dquote2>" do
 * not. Returns 0 for a NULL or empty (or all-whitespace) line, and for
 * anything else, including an ordinary prompt like "foo>" or "router>". */
int shell_prompt_is_continuation(const char *text);

/* A stricter test than shell_prompt_line(): does `text` look like a bare
 * prompt with nothing else typed on the line, safe to append a command to
 * without clearing it first?
 *
 * Used only on the dispatch path for a shell that gets no clear-line
 * prefix (see dispatch_line_clear.h): for those shells, execute_command()
 * writes the AI's command straight onto whatever is on the input line, so
 * "ends in a prompt character" is not enough -- "PS C:\Users\thoma> ls -la
 * >" also ends in '>' (the redirection operator, mid-command) and would
 * have the next command appended to a line the user is still typing.
 *
 * Returns 0 unless shell_prompt_line(text) already returns 1, and 0 again
 * when the character immediately before the trimmed line's final prompt
 * character is a space or tab -- the one case this function adds over
 * shell_prompt_line(): a prompt terminator preceded by whitespace reads as
 * typed content paused before it, not the shell's own prompt (which never
 * puts a space right before its own $/#/%/> in any shell this app
 * supports). A single bare terminator with nothing before it (e.g. a
 * one-character "$" prompt) passes, since there is no preceding character
 * to be whitespace. */
int shell_prompt_line_unambiguous(const char *text);

#endif /* NUTSHELL_CORE_SHELL_PROMPT_H */
