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

/* Does `row` look like a PowerShell or cmd.exe prompt? Used by
 * dispatch_line_clear_mode() (src/core/dispatch_line_clear.h) to force
 * DISPATCH_LINE_CLEAR_NONE for a live prompt shaped like this regardless
 * of session kind or shell name -- an SSH session to a Windows host, or a
 * pwsh/cmd started as a nested shell inside a local Git bash/MSYS2
 * session, would otherwise still get the readline clear-line prefix and
 * hit the "^E^U" bug this whole feature exists to prevent.
 *
 * `row` is the terminal's cursor row, taken up to the cursor column, same
 * as shell_prompt_line(). Matches two anchored shapes, both requiring the
 * trimmed line to end in '>' (trailing spaces/tabs ignored, same trim as
 * shell_prompt_line()):
 *   - PowerShell: the literal token "PS ", then a path -- a Windows one
 *     starting "<letter>:\" or, for pwsh running on a POSIX host, a Unix
 *     one starting "/". An optional PowerShell Remoting prefix,
 *     "[host]: " (the shape Enter-PSSession prepends), may precede the
 *     "PS " token.
 *   - cmd.exe: a bare "<letter>:\" path with no "PS " token.
 * Neither shape constrains what comes between the path's start and the
 * final '>' -- this only decides which prefix to send, never whether the
 * line is safe to send a command to (term_at_unambiguous_prompt() gates
 * that separately), so a command paused mid-line under either prompt
 * shape still reads as "a Windows prompt" here.
 *
 * Returns 0 for anything else, including: a bash prompt ("user@host:~$ ",
 * "user@host ~ $ "), zsh's ("host% "), a network-device CLI ("Router>",
 * "<Comware>", "[admin@MikroTik] >", "(ArubaOS) #" -- none start with "PS
 * " or "<letter>:\"), a bare POSIX path with no "PS " token (a Git-bash
 * style "/c/Users/x>" prompt), PowerShell's own continuation prompt
 * (">> " -- it has neither token either), and a NULL or empty row. */
int shell_prompt_is_windows(const char *row);

#endif /* NUTSHELL_CORE_SHELL_PROMPT_H */
