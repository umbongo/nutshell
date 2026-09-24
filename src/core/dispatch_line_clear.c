/* src/core/dispatch_line_clear.c */
#include "dispatch_line_clear.h"
#include <string.h>

DispatchLineClearMode dispatch_line_clear_mode(SessionKind kind,
                                                const char *local_shell_name)
{
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
