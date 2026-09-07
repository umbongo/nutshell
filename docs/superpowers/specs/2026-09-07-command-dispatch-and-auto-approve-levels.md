# Prompt-gated command dispatch and auto-approve levels

**Status:** approved by Thomas 2026-09-07 ("when deploying commands to a
terminal, check for a prompt first. also, add an option to auto-approve write
commands as well as off and safe only. and another setting to auto-approve
'all' commands, which includes critical."). Implemented in v1.1.1 (A) and
v1.1.2 (B).

## A. Commands go to the terminal one at a time, only at a shell prompt

### Problem

Every execution path in `src/ui/ai_chat.c` (auto-approve, Run N selected,
approve one) wrote all approved commands to the SSH channel in one burst. The
tty echoed them while the first was still running, so `sudo apt update`,
`sudo apt full-upgrade -y`, `sudo apt autoremove -y` and `do-release-upgrade`
were typed into whatever the previous command left waiting — a package
prompt, a pager, a password prompt. The follow-up message to the AI ("the
commands above have been executed") was sent after a fixed 2 s delay, so the
AI usually saw partial output.

### Behaviour

- One dispatcher per panel sends approved commands **in order, one at a
  time**. The next command is sent only when the active terminal is *at a
  prompt*: the cursor row, taken up to the cursor column, ends in a shell
  prompt character, nothing follows the cursor on that row, the primary
  screen is active, and the terminal has been quiet (no bytes processed) for
  `PROMPT_QUIET_MS` = 400 ms. After a command is sent the dispatcher also
  waits for the terminal to change at all (the echo) before it will accept a
  prompt again, so the old prompt line can't be mistaken for a new one.
- While waiting the activity line reads `running 2/4 · waiting for prompt`;
  it stays there for as long as the command runs (an `apt full-upgrade` may
  take minutes). There is no timeout: a command that stops at a password or
  `[Y/n]` prompt simply holds the queue until the user answers in the
  terminal, which is the safe outcome.
- The Send button shows Stop while the dispatcher is active; Stop cancels
  the remaining commands (denied in the queue, settled in the list) and
  appends `[command queue stopped]`.
- The continue message to the AI is sent only after the **last** command's
  prompt has returned (same test), replacing the fixed `CONTINUE_DELAY_MS`
  wait. Switching session or closing the panel cancels the dispatcher.
- Denied, blocked and already-executed entries are never sent.

### Prompt detection (pure, tested)

`src/core/shell_prompt.{c,h}`: `int shell_prompt_line(const char *text)` —
`text` is the cursor row up to the cursor, trailing blanks trimmed. Returns
1 when the last character is one of `$ # % >`; a bare `>` (continuation
prompt) returns 0; empty returns 0. Positives: `thomas@tompi:~$`,
`root@web-01:/var/log#`, `PS C:\Users\thoma>`, `[user@host dir]$`, `zsh%`.
Negatives: `[sudo] password for thomas:`, `Do you want to continue? [Y/n]`,
`Reading package lists...`, `1100`, `>`, `--More--`.

`src/term/term.c` (or `buffer.c`): `int term_at_prompt(const Terminal *t)`
builds that text from the cursor row's cells (ASCII codepoints as-is, others
as `?`), requires `!alt_screen_active`, cursor inside the screen, and no
non-blank cell after the cursor on that row. `Terminal.write_seq` (unsigned)
increments in `term_process` whenever `len > 0`; the dispatcher uses it for
"quiet" and "changed since send".

## B. Auto approve has four levels

- Per session, the status-line control cycles **off → safe only → safe +
  write → all → off**. Label text: `Auto approve: off | safe only | safe +
  write | all`.
- `ApprovalQueue.auto_approve_all` becomes `auto_approve_level` with
  `AUTO_APPROVE_SAFE = 0`, `AUTO_APPROVE_WRITE = 1`, `AUTO_APPROVE_ALL = 2`.
  Decision in `chat_approval_add`, in this order: write/critical with Permit
  write off → BLOCKED (unchanged); else auto-approved when `auto_approve` is
  on and (SAFE, or WRITE with level ≥ WRITE, or level == ALL); else PENDING.
  `chat_approval_reset` keeps `auto_approve` and the level.
- Settings › AI behaviour: the checkbox "Auto Approve also covers
  write/critical commands" becomes a drop-down **Auto approve for new
  sessions**: Off / Safe only / Safe + write / All. Config key
  `ai_auto_approve_default` (0..3, default 0). The old key
  `ai_auto_approve_all` is dropped and ignored when read. A new session's
  auto-approve state starts from the default; the status-line cycle then
  changes it for that session only (`AiSessionState` carries both fields).
- Tests: `chat_approval` decision matrix (level × safety × permit), config
  load/save/default/clamp, `ai_modes_label`, plus the settings page still
  laying out.
