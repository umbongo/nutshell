/* src/core/dispatch_line_clear.h */
#ifndef NUTSHELL_DISPATCH_LINE_CLEAR_H
#define NUTSHELL_DISPATCH_LINE_CLEAR_H

#include "session_io.h"    /* SessionKind */

/* How execute_command() (src/ui/ai_chat.c) should clear whatever is
 * already on the shell's input line before writing an AI-dispatched
 * command to it.
 *
 * DISPATCH_LINE_CLEAR_NONE: write nothing before the command. Required
 * for any shell that does not treat 0x05/0x15 (Ctrl+E Ctrl+U, the GNU
 * readline "end of line, kill to start" idiom) as a line-editing command:
 * PSReadLine (PowerShell) and cmd.exe both take the two bytes as literal
 * input, which is what produced the "^E^U$ErrorView" bug this type exists
 * to prevent.
 * DISPATCH_LINE_CLEAR_READLINE: write "\x05\x15" first, as before, for a
 * shell known to run on GNU readline or a readline-alike that honours it.
 *
 * Neither mode is, or may become, a lone ESC byte: under ConPTY, the VT
 * input parser can fold ESC plus whatever byte follows it into an Alt
 * chord, so a caller must never introduce one here. */
typedef enum {
    DISPATCH_LINE_CLEAR_NONE = 0,
    DISPATCH_LINE_CLEAR_READLINE = 1
} DispatchLineClearMode;

/* Decide the mode for the currently active session.
 *
 * kind: SESSION_LOCAL or SESSION_SSH (session_io.h).
 *
 * local_shell_name: for SESSION_LOCAL, the string local_shell_spec_name()
 * (src/core/local_shell.h) returns for the resolved local shell --
 * "PowerShell", "Git bash", "MSYS2", "cmd", "custom" -- or NULL/empty when
 * it has not resolved yet. Ignored for SESSION_SSH.
 *   - "Git bash" and "MSYS2" run on a real readline (bash) or a
 *     readline-alike that treats Ctrl+E/Ctrl+U the same way -> READLINE.
 *   - "PowerShell" and "cmd" are the shells that motivated this function:
 *     both echo the two control bytes back as literal characters -> NONE.
 *   - "custom" (a user-supplied executable) and an unresolved shell
 *     (NULL or "") are unknown quantities that must not be guessed at ->
 *     NONE, the same safe default as PowerShell/cmd.
 *
 * SESSION_SSH always gets READLINE, regardless of the session's
 * CmdPlatform (src/core/cmd_classify.h) -- this is today's behaviour,
 * kept rather than keyed off platform:
 *   - CMD_PLATFORM_UNKNOWN covers plenty of ordinary, undetected Linux
 *     hosts. Gating on platform would key NONE off "not yet classified",
 *     and an unrelated prompt shape (Gentoo's "user@host ~ $", zsh's
 *     "host ~ %" -- a space before the terminator) would then also fail
 *     the stricter no-prefix check in dispatch_tick(), cancelling the
 *     batch outright on what is otherwise the main use case.
 *   - The network-device CLIs (Cisco IOS/NX-OS/ASA, Junos, EOS, ...) this
 *     app classifies also support Ctrl+E/Ctrl+U as a line-kill idiom, so
 *     READLINE is correct for them too, not just for Linux.
 *   - SSH has no CmdPlatform for "Windows" -- cmd_classify.h's platforms
 *     are Linux plus named network-device families -- so there is nothing
 *     to key a PowerShell/cmd exception off even if one were wanted here.
 *     A remote Windows host reached over OpenSSH and running PowerShell
 *     or cmd as the login shell therefore still gets the Ctrl+E Ctrl+U
 *     prefix and can still hit this function's original bug; recognising
 *     that case is an open item pending Windows/PowerShell platform
 *     detection over SSH, not something this function can safely guess
 *     at today. */
DispatchLineClearMode dispatch_line_clear_mode(SessionKind kind,
                                                const char *local_shell_name);

#endif /* NUTSHELL_DISPATCH_LINE_CLEAR_H */
