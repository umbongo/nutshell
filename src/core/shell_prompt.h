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
 * primary-prompt terminators across bash/zsh/PowerShell/etc.). A bare '>'
 * (nothing else on the line -- e.g. a PS2 continuation prompt) returns 0,
 * as does an empty or NULL line. */
int shell_prompt_line(const char *text);

#endif /* NUTSHELL_CORE_SHELL_PROMPT_H */
