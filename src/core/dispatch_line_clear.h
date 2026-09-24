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

/* The two timing decisions dispatch_tick() (src/ui/ai_chat.c) makes on the
 * no-prefix path (DISPATCH_LINE_CLEAR_NONE above), where a command is
 * written straight onto the input line rather than after a clear-line
 * prefix. Both take an already-computed elapsed-milliseconds value so this
 * header stays free of GetTickCount()/DWORD -- the caller does the
 * GetTickCount() subtraction (which is safe across a wraparound: unsigned
 * arithmetic) and hands the result in. */

/* Minimum time, in milliseconds, that must have passed since the last
 * keystroke or paste actually written to the session's terminal (not any
 * other UI activity -- see SessionIo.last_input_tick,
 * src/term/session_io.h) before a no-prefix dispatch may write a command
 * onto the input line.
 *
 * ConPTY echoes a keystroke asynchronously. Without this guard, a
 * character the user just typed but that has not reached the terminal
 * buffer yet is invisible to term_at_unambiguous_prompt() -- the
 * terminal's write_seq has not changed yet either, so the dispatcher's own
 * "quiet" tracking (PROMPT_QUIET_MS in ai_chat.c) does not catch it -- and
 * the AI's command gets glued onto the front of whatever the user typed
 * (e.g. "xGet-ChildItem"). Set to the same 400 ms as PROMPT_QUIET_MS: the
 * rest of the dispatcher already treats that as long enough for the
 * terminal to have caught up with anything in flight.
 *
 * dispatch_tick() treats a true result as a silent delay, not an
 * "ambiguous prompt": it must not post the wait status line below, and
 * must not start or advance DISPATCH_AMBIGUOUS_PROMPT_TIMEOUT_MS's clock
 * either, unlike a failed term_at_unambiguous_prompt() check. A recent
 * keystroke resolves itself the moment the echo catches up -- almost
 * always well under a second -- so treating it the same as a genuinely
 * ambiguous prompt would report and, worse, eventually cancel a batch
 * over nothing more than typing having briefly outrun the terminal. */
#define DISPATCH_KEYSTROKE_GUARD_MS 400u

/* True when elapsed_ms (time since the session's last terminal keystroke
 * or paste) is too recent to trust the terminal's current cursor row for
 * a no-prefix dispatch. */
int dispatch_keystroke_too_recent(unsigned long elapsed_ms);

/* How long, in milliseconds, dispatch_tick() may wait on a no-prefix
 * prompt that term_at_prompt() accepts but term_at_unambiguous_prompt()
 * does not, before giving up and cancelling the batch. (A too-recent
 * keystroke, dispatch_keystroke_too_recent() above, is a separate, silent
 * delay that never reaches this clock -- see its own doc comment.)
 *
 * A single failed check must not cancel outright: an ordinary prompt can
 * legitimately have a space before its terminator (cmd's default "$P $G"
 * renders as "C:\x >"; a themed PowerShell/posh-git prompt like
 * "[main] >"), and output going quiet for a moment on a line that happens
 * to end "... 50 %" is not a hang. But nothing here will ever make the
 * line become unambiguous on its own -- unlike a continuation prompt,
 * where the user finishing the command resolves it -- so waiting forever
 * is not an option either; the dispatcher would sit forever on a
 * genuinely custom, always-ambiguous prompt shape. 5000 ms is about 12x
 * PROMPT_QUIET_MS (400 ms) and 20x the dispatcher's own poll interval
 * (CMD_QUEUE_POLL_MS, 250 ms) -- long enough that a momentary read is
 * never mistaken for a real stall, short enough that a genuine stall is
 * still reported and cleared up within a few seconds rather than left
 * hanging indefinitely. dispatch_tick() measures elapsed_ms from when it
 * first saw the condition, not reset by output arriving in between (a
 * command that keeps refreshing a prompt-shaped line, e.g. a progress
 * percentage, must not re-arm this every burst without ever timing out)
 * -- only clearing it when the check passes, or the batch starts,
 * cancels or finishes. */
#define DISPATCH_AMBIGUOUS_PROMPT_TIMEOUT_MS 5000u

/* True when elapsed_ms (time since dispatch_tick() first saw the
 * ambiguous-prompt condition for the command it is trying to send) means
 * the dispatcher should stop waiting and cancel the batch. */
int dispatch_ambiguous_prompt_timed_out(unsigned long elapsed_ms);

#endif /* NUTSHELL_DISPATCH_LINE_CLEAR_H */
