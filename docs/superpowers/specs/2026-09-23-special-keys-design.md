# Special keys: getting Alt, function keys and modifiers to the shell

**Date**: 2026-09-23
**Status**: DRAFT, research and options, revised after one Opus critique (see the end).
No code in this change.
**Branch**: `special-keys-spec` (docs only)
**Prompted by**: the maintainer, 2026-09-23, running Microsoft Edit (`edit.exe`, the console
editor in Windows 11 25H2) in a local shell (v1.2.2, busybox) and finding that Alt+F does not
open its menu bar.
**Builds on**: `2026-09-22-local-shell-design.md` (ConPTY transport),
`2026-09-07-design-system-foundation-design.md` (anything on screen).

---

## Problem

The terminal's key encoder was written for a shell prompt over SSH and covers little beyond
it. What reaches the shell today (`src/ui/window.c:3327-3467`), and what xterm sends:

| Key | Today | xterm |
|---|---|---|
| Printable, Enter, Escape, Ctrl+letter | the byte | same |
| Backspace | `0x08` | `0x7F` (DEL) |
| Tab / Shift+Tab | `0x09` / `0x09` | `0x09` / `CSI Z` |
| Arrows | `CSI A-D`, `SS3 A-D` in application mode | same |
| Arrows with Shift, Ctrl or Alt | unmodified sequence | `CSI 1;<m> A-D` |
| Home / End | `CSI H` / `CSI 4~` | `CSI H` / `CSI F`, `SS3 H` / `SS3 F` in application mode |
| Insert / Delete | `CSI 2~` / `CSI 3~` | same; `CSI 2;<m>~` with modifiers |
| PgUp / PgDn | never sent: always local scrollback (`:3435-3449`) | `CSI 5~` / `CSI 6~` |
| F1 to F9, F12 | nothing (`WM_KEYDOWN` has no case; no `WM_CHAR` follows) | `SS3 P-S`, `CSI 15~ .. 24~` |
| F10 | `WM_SYSKEYDOWN` unhandled: enters Nutshell's menu mode | `CSI 21~` |
| F11 | fullscreen toggle (`:3374`) | `CSI 23~` |
| Ctrl+Space | AI panel toggle (`:3385`) | `0x00` |
| Alt+anything | `WM_SYSKEYDOWN` unhandled, `DefWindowProc` enters menu mode (`:3699`) | `ESC` then the key |
| Ctrl+Shift+C/V, Ctrl+V, Shift+Insert, Ctrl+= / Ctrl+-, Ctrl+W, Ctrl+T | app shortcuts (`:3331-3422`) | shells use Ctrl+W (kill word), Ctrl+T (transpose), editors use Ctrl+V |

There is no accelerator table and no mnemonic in the menu (`create_app_menu`,
`window.c:1944-1982`), so Alt+F is not even opening Nutshell's File menu: `DefWindowProc`
enters menu mode, the next keystroke goes to the menu, and both are lost. The emulator
tracks only DEC private modes 1, 25, 1049 and 2004 (`src/term/parser.c:276-288`); it
ignores mouse modes and ConPTY's `CSI ?9001 h` request, which is right (section 6). No mouse
event is forwarded (section 5). Both transports are raw byte pass-throughs, so every fix is
in the encoder and the window, and applies to SSH and local sessions alike.

**A Win32 fact the whole design must respect.** The message loop (`window.c:3772-3774`)
calls `TranslateMessage` before `DispatchMessage`, so by the time `WndProc` sees a
`WM_KEYDOWN` or `WM_SYSKEYDOWN`, the matching `WM_CHAR` or `WM_SYSCHAR` is already queued.
Returning 0 from the key-down does not stop the character. The code already knows this:
`g_ctrlc_swallow_char` (`window.c:306-310`) exists to eat the `0x03` that follows a Ctrl+C
used for copy. Ctrl+T today (`:3380`) very likely leaks `0x14` into the active session for
the same reason. Every rule below therefore says which message handles each key.

For the local shell: ConPTY's input parser turns xterm sequences back into console key
events for the child. It maps an `ESC`-prefixed character to the key with Alt held,
`CSI 1;5A` to Ctrl+Up, `SS3 P` to F1, and (to be confirmed, section 9) `0x7F` to Backspace
and `0x08` to Ctrl+H. Edit, vim, nano and bash under ConPTY therefore need what an SSH host
needs: the xterm encoding, with one byte transport-specific.

---

## 1. The encoder: what to send

**Option A, the xterm encoding, complete.** A pure function in `src/core/key_encode.c`,
`key_encode(vk, mods, flags, out, size)` where `flags` carries application cursor mode and
the transport kind, tested natively for every row:

- modifier parameter `m` = 1 + (Shift 1) + (Alt 2) + (Ctrl 4), omitted when 1;
- arrows `CSI 1;m A-D`; unmodified in application mode `SS3 A-D`;
- Home/End `CSI 1;m H/F`; unmodified in application mode `SS3 H/F` (ncurses sends `smkx`
  and expects `khome=\EOH`, `kend=\EOF`; today's `CSI 4~` for End is wrong for it);
- Insert/Delete/PgUp/PgDn `CSI 2/3/5/6;m ~`;
- F1-F4 `SS3 P/Q/R/S`, modified `CSI 1;m P-S`; F5-F12 `CSI 15/17/18/19/20/21/23/24;m ~`;
- Shift+Tab `CSI Z`; Ctrl+Space `0x00`;
- Backspace: `0x7F` for a local session, `0x08` for SSH (section 7);
- Alt with a printable key: `ESC` followed by the character (xterm's `metaSendsEscape`);
  Alt with a special key: the `;m` form. Ctrl+Alt+letter: `ESC` then the control byte.

**Option B, win32-input-mode for the local shell.** Answer ConPTY's `CSI ?9001 h` by
sending every key as `CSI Vk;Sc;Uc;Kd;Cs;Rc _` (what Windows Terminal does). Full
fidelity for console programs (key-up, dead keys, every modifier), but it helps only the
local transport, it is a second encoder to maintain, and nothing the maintainer runs needs
it. Kitty's keyboard protocol is the SSH-side equivalent and is likewise unnecessary here.

**Option C, the minimum.** F1-F12 and the `ESC` prefix for Alt, nothing else. Fixes Edit's
menu and little more; Ctrl+arrows in vim and readline, Shift+Tab in every TUI, Home/End in
ncurses programs stay broken.

**Recommendation: A**, with B recorded for a later real need. A is what every terminal the
shell will meet sends, it is one table, and it is testable without Win32.

**Which message does what.** Keys that never produce a character (arrows, Home, End,
Insert, Delete, PgUp, PgDn, F1-F12) are encoded from `WM_KEYDOWN` / `WM_SYSKEYDOWN` and
the handler returns 0. Keys that do produce one are encoded from `WM_CHAR` / `WM_SYSCHAR`,
where Windows has already applied the layout, AltGr, dead keys and the IME: an Alt+letter
arrives as `WM_SYSCHAR` with the character, Ctrl+letter as `WM_CHAR` with the control byte,
Ctrl+Space as `WM_CHAR 0x20` with Ctrl down (sent as `0x00`). A key-down that must not be
followed by its character (Ctrl+Shift+W closing a tab) sets a swallow flag the way
`g_ctrlc_swallow_char` does today. Ctrl+Alt+letter versus AltGr: `WM_KEYDOWN` with both
down asks `ToUnicodeEx` with the no-side-effect flag whether the layout yields a
character; if it does, the `WM_CHAR` that follows sends it (AltGr), otherwise the key-down
sends `ESC` plus the control byte.

**Known limitations, stated.** The window is ANSI (`CreateWindowExA`, `window.c:543`) and
`WM_CHAR` is truncated to one byte, so Alt+ä sends `ESC` plus the code-page byte; that is
today's behaviour for plain ä as well and is not made worse here. Alt+[ and Alt+O produce
`ESC [` and `ESC O`, the CSI and SS3 introducers; xterm has the same ambiguity.

---

## 2. Alt and the menu bar

**Option A, Alt goes to the shell; a lone Alt tap still opens the menu.** `WndProc` handles
`WM_SYSKEYDOWN` and `WM_SYSCHAR` when the terminal has focus:

- passed through to `DefWindowProc` untouched: the modifier keys themselves (`VK_MENU`,
  `VK_SHIFT`, `VK_CONTROL`, including auto-repeat), Alt+F4 (close), Alt+Space (the system
  menu, which `DefWindowProc` opens from the `WM_SYSCHAR` for the space, so that handler
  must pass it too), Alt+Esc (window cycling), Alt+Enter, and Alt with a numpad key or a
  non-extended navigation key (lParam bit 24 clear), so Alt+0233 still composes é;
- F10 and Shift+F10 (delivered as `WM_SYSKEYDOWN` without the Alt bit) are encoded as
  `CSI 21~` / `CSI 21;2~`, so Midnight Commander's quit key works and Shift+F10 no longer
  becomes `WM_CONTEXTMENU`;
- everything else with Alt is encoded (section 1) and the handler returns 0.

Pressing and releasing Alt alone is not intercepted, so `DefWindowProc` enters menu mode
as today; the menu bar stays reachable from the keyboard and by click. Two honest notes:
whether the menu still opens on the release of an Alt whose press was consumed depends on
the system's own input state, not on `DefWindowProc`, and is settled by the manual
checklist (section 8); and a stray Alt tap puts the window in menu mode and swallows the
next keystroke, which is why PuTTY and mintty make "Alt alone opens the menu" an option
that is off by default. Windows Terminal has no menu bar, so it is not a precedent either
way.

**Option B, a setting.** "Alt alone opens the menu" on the Terminal settings page
(`SETTINGS_PAGE_TERMINAL`, `src/core/settings_layout.h:22-33`), default off, PuTTY's
behaviour. Off: `WM_SYSKEYUP` for a lone Alt is consumed too, the menu is reached by click.
Costs a config key, a settings row and a test.

**Option C, mnemonics for Nutshell's menu, Alt stays with the app.** Makes Edit, Midnight
Commander, Emacs and every Alt-using program unusable in the terminal. Rejected.

**Recommendation: A, then B if the manual checklist shows the lone-Alt menu mode biting.**
Nobody relies on Alt+letter to reach Nutshell's four-item menu today.

---

## 3. The keys the app keeps for itself

Each of today's shortcuts, and the proposal. Every Ctrl check gains the same guard: Ctrl
down **and Alt not down**, because during an AltGr character `GetKeyState(VK_CONTROL)` is
also true (on a Polish layout AltGr+C, ć, would today copy the selection at `:3393`).

| Shortcut | Today | Proposal |
|---|---|---|
| Ctrl+Shift+C / Ctrl+Shift+V | copy / paste | keep: the terminal convention; a shell cannot see Shift on a control character anyway |
| Ctrl+C with a selection | copy | keep; without a selection it is SIGINT, as today |
| Ctrl+V, Shift+Insert | paste with the confirmation dialog | keep, as the user's habit, knowing raw Ctrl+V is vim's block-visual, nano's page down and readline's quoted-insert; the Send key menu (section 4) offers the raw key |
| Ctrl+= / Ctrl+- | zoom | keep, with the AltGr guard |
| Ctrl+W, Ctrl+T | close tab, new tab (from `WM_CHAR`, so the byte never reaches the shell) | **move to Ctrl+Shift+W / Ctrl+Shift+T**, decided in `WM_KEYDOWN` with a swallow flag; plain Ctrl+W (readline kill-word, vim's window prefix) and Ctrl+T (transpose) go to the shell |
| Ctrl+Space | AI panel toggle | **move to Ctrl+Shift+Space**; Ctrl+Space sends `0x00` (Emacs set-mark, a common tmux prefix) |
| F11 | fullscreen | keep; Shift+F11 goes to the shell as `CSI 23;2~` |
| PgUp / PgDn | scrollback, always | **to the program when the alternate screen is active** (less, vim, man, Edit page; the alternate screen has no scrollback: `buffer.c:434-442`), **scrollback otherwise; Shift+PgUp / Shift+PgDn always scroll back.** At a bash prompt nothing changes. The one loss is a pager run with `-X` (git's default `LESS=FRX`), which stays on the primary screen and so cannot be paged with PgUp; Shift+PgUp still scrolls, and Space and arrows still page |
| Mouse wheel | scrollback (Ctrl: zoom) | keep; mouse reporting is section 5 |

The alternative for PgUp, xterm's rule of always sending it and scrolling only with Shift,
was rejected: it turns PgUp at a prompt into `CSI 5~`, which bash echoes as `~` or maps to
a history search depending on the host's inputrc, for no gain over the rule above.

Every moved shortcut is a documented change: the README (lines 54, 59, 85-86, 113, 201,
317-327), `src/ui/help_guide.c` (lines 42, 70-71, 91, 191, 324-337) and the menu labels
(`window.c:1954`, `:1974`).

---

## 4. Keys the OS or the app must keep: an on-screen way to send them

The list is built from what cannot be typed into the shell after sections 1 to 3: F11,
Ctrl+V and Shift+Insert (paste), Ctrl+= and Ctrl+-, Ctrl+Shift chords, Alt+Space, Alt+Esc,
Alt+F4, PgUp/PgDn on the primary screen, and, on a laptop without a function row, F1-F12.

**Option A, a "Send key" submenu** under Edit: F1-F12, PgUp, PgDn, Ctrl+V, Shift+Insert,
Ctrl+=, Ctrl+-, and two one-shot items, **"Send next key raw"** (the next keystroke goes to
the shell bypassing every app shortcut) and **"Send next key with Alt"** (the next character
gets the `ESC` prefix). Each is a `WM_COMMAND` id, so the harness drives it by posting the
id as it does the File menu; no dialog, no drawn surface. The critique's point stands that a
posted `WM_COMMAND` is dispatched even while a modal dialog has the window disabled, so the
harness case for it must run with no dialog up.

**Option B, a special-keys strip** above the terminal, hidden by default, toggled from View:
Esc, Tab, sticky Ctrl and Alt, arrows, F1-F12, PgUp, PgDn, as mobile terminals have. Built
token-only (`ns_tokens`, `ns_font`, `ns_scale`, `ns_type.h`, `ns_hover`), with gallery
states and a harness helper that posts clicks to it. Best for touch and a compact keyboard;
a new drawn control with hit testing and DPI work.

**Option C, a leader chord** (Ctrl+Alt+K, then a key). Cheap, invisible, undiscoverable;
cannot send the OS-reserved chords.

**Recommendation: A in the same change as sections 1 to 3; B as a follow-up if the
maintainer wants touch or a compact keyboard.**

---

## 5. Mouse

Edit's menu is clickable, and vim, less, tmux and Midnight Commander use the mouse when the
terminal reports it. That is a separate change: track modes 1000, 1002, 1003 and 1006 (SGR)
in the parser, send `CSI < b;x;y M/m` from the mouse messages when a mode is on, keep local
selection when Shift is held (xterm's rule) or the mode is off. ConPTY translates SGR mouse
into console mouse records, so Edit's menu becomes clickable in the local shell too. Own
spec: it touches selection, links and the wheel, each with tests and gallery states.

---

## 6. ConPTY specifics

`CSI ?9001 h` on open is ConPTY asking for win32-input-mode. Not answering keeps ConPTY
parsing xterm sequences, which section 1 relies on. Leave it ignored; do not add the mode
to the parser's tracked set. Whether ConPTY forwards a console program's `?1h` (application
cursor keys) and `?1049h` (alternate screen) to Nutshell decides whether the
application-mode and PgUp rules ever trigger for local sessions; it is on the checklist
(section 8) and the wintest recording in `tests/test_local_pty.c` is where to see it.

---

## 7. Deliberately not decided here

- **Backspace on SSH.** xterm sends DEL; Nutshell sends BS, and every SSH host the
  maintainer uses has adapted (readline accepts both). For the local shell section 1 sends
  `0x7F`, because a console child under ConPTY reads `0x7F` as Backspace and (to confirm)
  `0x08` as Ctrl+H, which in Edit would be a binding rather than a delete. Making SSH match
  is one byte and a possible surprise on a host with `stty erase ^?`; a later change, or a
  per-profile setting.
- **Terminal type.** The SSH pty is requested as `"xterm"` (`window.c:980`), the local
  shell gets `TERM=xterm-256color`. The two terminfo entries describe the same keys, so
  this is not part of the key work. `xterm` is the safer request for hosts without the
  256-colour entry (busybox routers, old Unix); a per-profile "terminal type" setting is
  the right shape, later.
- **Keypad application mode (DECKPAM)** and the numpad: nothing tracks it; no user has asked.
- **Kitty keyboard protocol**: section 1B's SSH-side twin.
- **Focus.** With the AI panel's input focused, key messages go to that control and none of
  this applies: Ctrl+Shift+W/T/Space do nothing there and Alt+letter still enters Nutshell's
  menu mode. Only a click returns focus to the terminal (`window.c:3539`). An Escape-to-
  terminal binding is the AI panel's concern and a separate small change.

---

## 8. The change this spec proposes, and its tests

One change, one version:

1. `src/core/key_encode.c/.h` (option 1A), pure, with `tests/test_key_encode.c` covering
   every row of section 1: each key, each modifier combination, application cursor mode on
   and off (arrows and Home/End), both Backspace bytes, Shift+Tab, Ctrl+Space, Alt with a
   printable, Ctrl+Alt+letter.
2. `window.c`: the `WM_SYSKEYDOWN` / `WM_SYSCHAR` handling of section 2A with its
   pass-through list; the `WM_KEYDOWN` switch replaced by the encoder; the `WM_CHAR` path
   for Alt and Ctrl+Space; the shortcut moves of section 3 with swallow flags; the AltGr
   guard on every Ctrl check; PgUp/PgDn by screen.
3. The "Send key" submenu with its two one-shot items (section 4A).
4. README, help guide and menu labels (section 3).
5. Integration harness: `Send-NutshellChord` gains `-Alt`, which sets `VK_MENU` in the
   attached key state and posts only `WM_SYSKEYDOWN` / `WM_SYSKEYUP` with the context bit
   (lParam bit 29), letting the app's own `TranslateMessage` synthesise the `WM_SYSCHAR`
   exactly as for a real key, so a double send cannot hide. The three PgUp sites
   (`10-terminal.ps1:119`, `:155-156`, `50-window.ps1:300`) change to
   `Send-NutshellChord -Key PgUp -Shift`. New gate cases, each reading `cat -v` output from
   the session log: `f_keys_reach_shell` (F1-F12; F10 must be posted as `WM_SYSKEYDOWN` to
   match Windows), `alt_letter_reaches_shell` (expect exactly one `^[f`),
   `shift_tab_and_ctrl_arrows`, `ctrl_space_sends_nul_only` (expect `^@` and no space),
   `pgup_pages_on_alt_screen_and_scrolls_on_primary` (both halves),
   `send_key_menu_posts_f1`, `ctrl_w_reaches_shell_and_shift_closes_tab`.
6. **A manual checklist**, because posted messages skip what the OS does to real
   keystrokes (menu-state tracking, Alt+numpad composition, AltGr's synthetic Ctrl, IME):
   lone Alt tap opens and Escape closes the menu; Alt+F then release does not leave menu
   mode armed; F10 and Shift+F10 in Midnight Commander or `cat -v`; Alt+0233 composes é;
   AltGr on a German and a Polish layout types the character and copies nothing;
   Alt+Esc cycles windows; Ctrl+Space in `cat -v` shows `^@` only; Backspace in Edit
   deletes one character; PgUp at a bash prompt scrolls back, PgUp in `less` pages; in
   Edit, Alt+F opens the File menu, arrows move between menus, Escape closes it.

Out of scope: mouse reporting (5), the strip (4B), win32-input-mode (1B), Backspace on
SSH, terminal type, keypad, kitty, the AI panel's focus binding.

---

## 9. Not verified during research

To settle in the implementation's wintest recording or the checklist, not assumed:
ConPTY's mapping of `0x08`, `0x7F`, `0x00`, `ESC`+char, `CSI 1;mP` and `CSI Z`, and its
lone-`ESC` timeout (microsoft/terminal's `InputStateMachineEngine.cpp` is the reference);
whether ConPTY forwards `?1h` and `?1049h`; the `WM_CHAR` produced for Ctrl+Space and
Ctrl+Shift+Space on US and UK layouts; whether the menu opens on Alt release after a
consumed Alt+F; Edit's own key map (F10, Ctrl+H); whether busybox-w32's `cat` supports `-v`
(else `od -c`).

---

## Critique

One round, Opus (`claude-opus-5`), 2026-09-23, briefed per the `critique` skill: thirteen
findings. Dispositions:

1. Returning 0 from a key-down does not stop the queued character. **Accepted**: "which
   message does what" in section 1; swallow flags; the harness posts only key-downs.
2. F10 arrives as `WM_SYSKEYDOWN` without Alt. **Accepted**: section 2A encodes it.
3. PgUp to the shell on the primary screen is a regression; alt-screen rule is cheaper.
   **Accepted**: section 3 uses the alt-screen rule, names the `less -X` loss, and lists
   the three harness sites.
4. Backspace `0x08` under ConPTY is probably Ctrl+H. **Accepted**: `0x7F` for local
   sessions in section 1; SSH deferred in section 7; on the checklist.
5. The pass-through list was wrong (modifier keys, Alt+Space via `WM_SYSCHAR`, Alt+Esc,
   Alt+numpad; Windows Terminal is not a precedent). **Accepted**: section 2A rewritten;
   option B reframed as PuTTY's default.
6. Ctrl+Alt versus AltGr undecided; the guard is needed on every Ctrl check; the harness
   must set `VK_MENU`. **Accepted**: sections 1, 3 and 8.5.
7. The Ctrl+V justification was false. **Accepted**: corrected; raw Ctrl+V is on the Send
   key menu.
8. The Send key list answered the wrong question; the Alt-letter dialog is a new surface.
   **Accepted**: list rebuilt from what cannot be typed; dialog replaced by two one-shot
   items; the modal-dialog caveat noted.
9. Home/End in application mode missing; End is wrong today. **Accepted**: section 1.
10. `xterm-256color` rests on a false premise and risks old hosts. **Accepted**: removed
    from the change, recorded in section 7 as a future per-profile setting.
11. Focus, non-ASCII and the `ESC [` / `ESC O` ambiguity unaddressed. **Accepted**: stated
    as limitations in sections 1 and 7.
12. The tests would not catch the OS-level behaviour. **Accepted**: the manual checklist
    in section 8.6 and the added gate cases.
13. Documentation spans prose, the help guide and menu labels. **Accepted**: listed in
    section 3.

Not verified by the critic: carried into section 9 for the implementation to settle.
