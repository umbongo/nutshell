/* src/core/dispatch_line_clear.c */
#include "dispatch_line_clear.h"
#include <string.h>

DispatchLineClearMode dispatch_line_clear_mode(SessionKind kind,
                                                const char *local_shell_name,
                                                int cursor_row_is_windows_prompt)
{
    /* The live prompt overrides everything below it -- see the header:
     * whatever kind/local_shell_name says about the session's own
     * transport and configured shell, the thing actually reading the
     * bytes right now is PSReadLine or cmd.exe once the cursor row looks
     * like one. */
    if (cursor_row_is_windows_prompt)
        return DISPATCH_LINE_CLEAR_NONE;

    if (kind == SESSION_LOCAL) {
        if (!local_shell_name || !local_shell_name[0])
            return DISPATCH_LINE_CLEAR_NONE;
        if (strcmp(local_shell_name, "Git bash") == 0)
            return DISPATCH_LINE_CLEAR_READLINE;
        if (strcmp(local_shell_name, "MSYS2") == 0)
            return DISPATCH_LINE_CLEAR_READLINE;
        /* "PowerShell", "cmd", "custom", and anything this function does
         * not recognise all get NONE -- see the header. */
        return DISPATCH_LINE_CLEAR_NONE;
    }

    /* SESSION_SSH (and any future SessionKind this switch does not know
     * about yet) always gets READLINE -- see the header for why this is
     * not keyed off CmdPlatform. */
    return DISPATCH_LINE_CLEAR_READLINE;
}

int dispatch_keystroke_too_recent(unsigned long elapsed_ms)
{
    return elapsed_ms < DISPATCH_KEYSTROKE_GUARD_MS;
}

int dispatch_ambiguous_prompt_timed_out(unsigned long elapsed_ms)
{
    return elapsed_ms >= DISPATCH_AMBIGUOUS_PROMPT_TIMEOUT_MS;
}
