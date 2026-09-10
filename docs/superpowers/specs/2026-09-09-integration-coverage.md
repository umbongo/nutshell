# Integration coverage: what the suites prove today and the sweep that would be complete

**Date:** 2026-09-09 · **Version assessed:** v1.1.10 · **Status:** assessment, no changes made

Terms used here: *unit* = the 1,828 native tests in `tests/*.c`; *E2E* = the
17 end-to-end UI cases in `tests/integration/Run-Integration.ps1` (real exe,
keystrokes, window messages, screen captures, session log as the oracle);
*The gate* = a build verification sweep of the user journeys, which is what a
release should pass before anyone tests by hand. There are no Playwright tests
and there cannot be: Nutshell is a native Win32 application, not a web app.
The April spec `2026-04-12-firstPersona-bvt-agent-design.md` planned a
computer-use agent with ten cases (launch, resize, minimise, connect dialog,
menus, settings, theme, close); it was never implemented, and its ten cases
are all below.

## Verdict

The unit layer is strong on logic. The E2E layer proves the redesign work and
the AI execution path well, and thinly or not at all everything else a user
does in a day: managing profiles, authenticating with a password, host-key
dialogs, tabs, settings, disconnecting, closing. Of the 60 user journeys
below, 12 are covered end to end, 11 partially, 35 not at all, and 2 are out
of reach for automation on one machine. A comprehensive sweep needs 49
more cases; about half of them need one harness addition, driving dialogs
through UI Automation instead of keystrokes.

Legend: **E2E** covered by an existing case · **Part** partially (a helper
touches it but nothing asserts it) · **—** no end-to-end coverage. The unit
column names the file when core logic behind the journey is unit-tested.

## 1. Launch, window, shutdown

| # | Journey | Unit | E2E | Case to add |
|---|---|---|---|---|
| 1 | Launch, main window appears, no dialog | — | Part (every case) | LAUNCH-1: assert window class, title, no `#32770` dialog, exit code 0 on close |
| 2 | Launch with no config file (first run) | config | — | LAUNCH-2: delete config, launch, Session Manager or empty state shows, config is written on Save |
| 3 | Launch with a corrupt config | config, json | — | LAUNCH-3: truncated JSON, app starts with defaults and a message, does not crash |
| 4 | Resize through small/large sizes, gutters painted | — | Part (`pty_resizes_with_window`) | RESIZE-1: 640×400 to 2560×1440, capture non-blank at each, no stray GDI artefacts |
| 5 | Minimise and restore | — | — | WINDOW-1: minimise, restore, capture equals pre-minimise capture |
| 6 | Fullscreen toggle (View › Fullscreen) | — | — | WINDOW-2: toggle on and off, window rect and PTY size follow |
| 7 | DPI change / move between monitors | ns_scale, zoom | — | out of reach on one monitor; keep as manual |
| 8 | Close with a live session (prompt or clean exit) | — | Part (Stop-Nutshell) | CLOSE-1: close with one connected tab, process exits within 5 s, no orphan |
| 9 | Menu bar: every top-level menu opens, every item enabled state correct | menubar_line | — | MENU-1: open File/Edit/View/Help by Alt key, capture each, assert item count via UIA |

## 2. Sessions, profiles, authentication

| # | Journey | Unit | E2E | Case to add |
|---|---|---|---|---|
| 10 | Session Manager opens (File › New Session, startup setting) | session_manager | — | SM-1: open, list shows generated profile, phantom-row bug regression (empty list paints nothing) |
| 11 | Create a profile (name, host, port, user, auth type, key path, notes), Save | session_manager, config | — | SM-2: drive fields by UIA, Save, reopen, values persisted, config on disk matches |
| 12 | Edit a profile | session_manager | — | SM-3 |
| 13 | Delete a profile with confirmation | session_manager | — | SM-4 |
| 14 | Connect from the list (double-click / Connect) | — | — | SM-5: select and connect, prompt reached |
| 15 | Key auth, passphrase-free | key_auth, ssh | **E2E** `connect_shows_prompt` | — |
| 16 | Key auth with passphrase: prompt, wrong passphrase retry, correct | key_auth | — | AUTH-1: needs a passphrased key on tompi; drive the prompt by UIA |
| 17 | Password auth, saved password | crypto, config | — | AUTH-2: needs a password-auth user on tompi (create `pwuser`) |
| 18 | Wrong password: error shown, retry offered, no crash | — | — | AUTH-3 |
| 19 | Host unreachable / DNS failure: tab goes red, error text | ssh_timeout | — | CONN-1: connect to `10.255.255.1` or `nonexistent.invalid`, assert status dot and message within the timeout |
| 20 | First connect host-key dialog (TOFU): Yes stores key, No aborts | knownhosts | — | HK-1: empty known_hosts, dialog appears, Yes then reconnect shows no dialog; No leaves no key |
| 21 | Host-key mismatch warning | knownhosts | — | HK-2: poison known_hosts, dialog appears, default button is No (after audit fix H4), No aborts |
| 22 | Disconnect (File › Disconnect) and reconnect | — | — | CONN-2: disconnect, dot grey, reconnect, prompt reached |
| 23 | Connection lost (server drops): red dot, message, no crash | ssh_timeout | — | CONN-3: `kill -9 $$` in the shell, tab shows lost within keepalive window |
| 24 | Idle timeout disconnects after N minutes | ssh_timeout | — | CONN-4: set 1 minute, wait, assert disconnect (slow; nightly only) |
| 25 | Auto-connect at startup (Connection › Startup) | cli_args, config | — | START-1: `auto_connect` set, launch, prompt reached without any command |

## 3. Terminal

| # | Journey | Unit | E2E | Case to add |
|---|---|---|---|---|
| 26 | Shell output reaches the screen and log | term, term_extract | **E2E** `connect_shows_prompt` | — |
| 27 | Colour, bold, underline, reverse rendering | vt_sequences, color, display_buffer | — | TERM-1: print a 16/256/truecolor test pattern, capture, compare pixels at known cells |
| 28 | Alternate screen (vim/less/htop) enter and leave, restore primary | term, scroll_region | — | TERM-2: `less` then `q`, screen restored, scrollback intact |
| 29 | Scroll regions (ncurses apps) | scroll_region, vt_sequences | — | covered by unit; TERM-2 exercises it live |
| 30 | Ctrl+C interrupts | — | **E2E** `ctrl_c_without_selection_interrupts` | — |
| 31 | Page Up / Page Down / wheel / scrollbar drag | edit_scroll, scrollbar | Part (`page_up_scrolls_history`, captures only) | TERM-3: assert the captured top line changes; add wheel and scrollbar-drag variants |
| 32 | Smart scrolling holds position under output; keypress returns | term (smart scroll) | **E2E** `terminal_holds_position_while_output_arrives` | — |
| 33 | PTY resize follows the window; background tab gets it | — | **E2E** `pty_resizes_with_window`, `resize_applies_to_inactive_tab` | — |
| 34 | Zoom Ctrl+= / Ctrl+- / Ctrl+0, font size persists | zoom | — | TERM-4: zoom in twice, `tput cols` drops, zoom reset restores |
| 35 | OSC title sets the tab title | vt_sequences | — | TERM-5: `printf '\e]0;OSCTEST\a'`, tab caption reads OSCTEST |
| 36 | Selection, Ctrl+C copies, Ctrl+Shift+C/V, Shift+Insert, select-all | selection | — | CLIP-1: drag-select by client coordinates, Ctrl+C, clipboard text equals selection; Ctrl+Shift+V pastes |
| 37 | Paste with and without confirmation, paste delay | paste_preview | **E2E** two cases | add CLIP-2: multi-line paste with a 200 ms delay arrives in order |

## 4. Tabs

| # | Journey | Unit | E2E | Case to add |
|---|---|---|---|---|
| 38 | Open second tab, switch by click, Ctrl+Tab, close with × and Ctrl+W | tabs | Part (helpers only) | TABS-1: open two, switch both ways, close the active one, the other becomes active, close-last behaviour |
| 39 | Status dots grey/yellow/green/red and log indicator | tabs | — | TABS-2: capture the tab strip at each state and assert the dot colour pixel |

## 5. Logging

| # | Journey | Unit | E2E | Case to add |
|---|---|---|---|---|
| 40 | Start/stop session logging; file named per format; ANSI stripped | log_format, logger | **E2E** `log_filename_follows_log_format` (+ every case uses the log) | add LOG-1: stop logging, further output is not written; restart appends a new file |
| 41 | Debug terminal log setting | logger | — | LOG-2: enable, connect, file exists and escapes are written as `ESC`/`\xHH` |

## 6. Settings

| # | Journey | Unit | E2E | Case to add |
|---|---|---|---|---|
| 42 | Settings dialog opens, every page lays out, Save persists, Cancel discards | settings, settings_layout | — | SET-1: open, walk all nine pages by UIA, capture each; change one value per page, Save, config on disk changed; Cancel leaves it |
| 43 | Theme switch applies to terminal, tabs, panel, dialogs | ui_theme, ui_tokens, color_consistency | Part (`ui_gallery` covers the panel per theme) | SET-2: switch theme, main window capture's background pixel equals the theme's bg |
| 44 | Terminal font and size; AI font | app_font | — | SET-3: change font, capture differs; size change alters `tput cols` |
| 45 | AI provider page: provider/model combo, model list fetch, key stored encrypted | crypto, config | Part (config written by harness) | SET-4: enter a key through the dialog, Save, config holds an encrypted blob, panel opens with key |
| 46 | Relative config save after a file dialog (open audit bug H6) | — | — | SET-5: browse for a key file in another directory, Save settings, config written to the real path (fails until H6 is fixed) |

## 7. AI assist

| # | Journey | Unit | E2E | Case to add |
|---|---|---|---|---|
| 47 | Panel opens, docks, no-key and no-session states | ai_panel, ai_dock, ui_demo | **E2E** `ai_panel_docks_with_key`, `ai_panel_opens_without_key`, `ui_gallery` | — |
| 48 | Undock to a floating window and redock | ai_dock | — | AI-1: View › Undock, top-level window of class `Nutshell_AIChat` appears, redock returns it |
| 49 | Send a prompt, streaming reply, Thinking disclosure opens/collapses/stays | chat_thinking, ai_prompt | Part (streaming seen in captures, no assertion) | AI-2: prompt, Thinking header appears, collapse it by click, it stays collapsed after completion |
| 50 | Commands proposed, card, Run selected, Deny all, single approve | chat_approval, cmd_batch, ns_layout | **E2E** four cases | add AI-3: Deny all settles the card with "denied" rows and no execution |
| 51 | Auto approve levels off/safe/write/all via status line and Settings default | chat_approval, config | Part (safe only) | AI-4: cycle the status line four times, label text follows; `ai_auto_approve_default` seeds a new session |
| 52 | Permit write toggle, blocked NOTE to the model | chat_approval | **E2E** `ai_write_command_held_then_runs_after_permit` | — |
| 53 | One-at-a-time dispatch, prompt gating, Stop | shell_prompt, term | **E2E** `ai_commands_run_one_at_a_time` | add AI-5: Stop during a `sleep 30` batch, queue stops, status line says so |
| 54 | Pending cards survive further prompts | cmd_batch | **E2E** `ai_prompt_while_approval_pending` | — |
| 55 | Retry after an API error | chat_activity | — | AI-6: bad key, error with Retry appears, fix key, Retry succeeds (needs the key path in Settings) |
| 56 | Web search and web fetch tools | tool_web_fetch, tool_web_search, html_util | — | AI-7: enable fetch, ask for a known URL's title, reply contains it (network, one call) |
| 57 | Save transcript (File › Save AI) | — | — | AI-8: save, file exists with the prompt text |
| 58 | Markdown rendering: tables, colour roles, code | md_table, markdown_render, theme | Part (`ui_gallery` chat state) | covered visually by the gallery; keep |
| 59 | Session switch keeps per-session chat, pending cards, auto-approve | cmd_batch, ai_chat | — | AI-9: two tabs, prompt in each, switch, transcript and card belong to the right tab |

## 8. CLI

| # | Journey | Unit | E2E | Case to add |
|---|---|---|---|---|
| 60 | `-sn`, `-h`, `-nc`, `-l`, `-v`, `-?`, `--ui-demo`, `--theme` | cli_args | Part (`-sn`, `--ui-demo`, `--theme`) | CLI-1: `-l` prints the profile list to the console; `-v` prints the version; `-h host` connects; `-nc` opens without connecting; `-?` prints usage; an unknown flag errors |

## What the additions need

- **UI Automation helpers in the harness** (SM-1..5, AUTH-1..3, HK-1..2, SET-1..5, AI-6, MENU-1): find a dialog by class `#32770` and title, find controls by automation id or by the resource ids in `src/ui/resource.h`, set text with `WM_SETTEXT`, click with `BM_CLICK`, read combo selections. This is one PowerShell function set, about a day, and removes the keystroke dependency for dialogs entirely, so those cases run even when the desktop is locked.
- **A second identity on tompi**: a password-auth user and a passphrased key for AUTH-1..3. Both are one-time setup.
- **A pixel oracle** for TERM-1, TABS-2, SET-2: sample known cells from a capture and compare with the theme's colour tokens; the gallery's `Test-NutshellCaptureNonBlank` is the seed.
- **Time**: CONN-4 and anything with a real timeout belongs in a nightly tier, not the per-build sweep.

## Proposed tiers

- **Per build (the `gate` tier), about 3 minutes, no AI calls:** LAUNCH-1..3, RESIZE-1, WINDOW-1..2, CLOSE-1, MENU-1, SM-1..5, HK-1..2, CONN-1..2, TERM-1..5, CLIP-1..2, TABS-1..2, LOG-1..2, SET-1..4, CLI-1, plus the existing non-AI cases and `ui_gallery`.
- **Per build with a key, about 12 model calls:** the five existing AI cases plus AI-1..5, AI-8, AI-9.
- **Nightly:** AUTH-1..3, CONN-3..4, AI-6, AI-7, SET-5.
