# App and Documentation Review — 2026-09-07

Reviewed at v1.0.77 on `ui-polish`, running the native Windows build on the dev box
(200 % DPI). Method: drove the app with WM_COMMAND and captured every screen; two
read-only code reviews (docs-vs-code audit, UI-layer defect review); spot-verified
the audit's headline claims by reading the code.

Existing findings in `2026-09-06-ui-redesign-notes.md` (approval card, hover states,
toggle visibility, MessageBox dead ends, empty states, plain chrome, colour debt) are
not repeated here.

## 1. What the app looks like today

Screens captured: main window (dark + light), Session Manager (dark + light),
Settings, User Guide, About, the "no active SSH session" MessageBox.

Working well: the acorn watermark and version stamp in the empty terminal; the
paged Settings window is clean, well-labelled and DPI-correct; dark title bars on the
main, Settings and Session Manager windows; the light theme is genuinely usable;
Settings changes apply live without a restart.

Visual/UX defects found by running it:

1. **Blank icon-only row in an empty Session Manager list.** An empty owner-drawn
   listbox with focus receives WM_DRAWITEM with `itemID == -1`; `paint_session_row`
   (`session_manager.c:40-80`) skips the text but still paints the server icon and
   focus rect, so a fresh install shows a phantom session. Fix: return early when
   `(INT)dis->itemID < 0`, and give the empty state a real message ("No sessions yet
   — click New").
2. **About box is half-themed.** Dark body, but a light system title bar and a
   light system OK button (`renderer_apply_theme` is not applied to it, and the
   button is not a themed_button). Also the "no active SSH session" MessageBox is
   fully light-system-styled. Both jar against the dark app.
3. **Settings and User Guide windows show the default class icon** in the title bar
   instead of the acorn. Register the classes with the app icon.
4. **AI Assist Font combo looks different from every other combo** on the Appearance
   page (classic white drop arrow vs the themed one). Same control should share the
   themed style.
5. **Session Manager form has a dead gap** between Password and AI Notes where the
   key-file row lives when Auth = Key. Collapse the gap when hidden, or reserve it
   visibly.
6. **Password icon is blank** (from `make wintest`); the thinking-cloud dots and
   server LED are invisible for the same reason. Already logged in the checkpoint.
7. **User Guide is a wall of monospace-style ASCII headings rendered in a
   proportional font** ("====" underlines don't line up). Either render it as
   markdown via `md_render` or structure it with real headings.
8. ~~Window keeps re-sizing itself.~~ **Withdrawn.** Re-tested from a DPI-aware
   process: the rectangle stays exactly as set. The apparent jumps were the capture
   harness mixing virtual and physical coordinates under 200 % DPI virtualisation.
9. **Session Manager does not open on first launch**, contrary to README §Getting
   Started step 2. Nothing in `WM_CREATE`/`WM_STARTUP_CONNECT` opens it.
10. **Tab-strip AI icon reads as a CPU chip** at 200 % rather than the intended chat
    bubble with sparkle; worth a look when the icon set is revisited.

## 2. Code review — UI layer correctness (agent review, ranked)

Verified clean: GDI handle pairing in all paint paths, WM_SIZE/DPI arithmetic,
control-ID ranges, atomic config save, CLI arg lifetime, stream-thread stack use,
startup-connect ordering.

High:

- **AI stream thread outlives the objects it borrows.** `ai_chat.c` WM_DESTROY
  (`3545-3576`) deletes the critical section and frees `AiChatData` while the worker
  still holds pointers to them (`975-1009`, used at `460, 617, 701, 723, 747`).
  Reproduce: start a stream, View → Undock AI Assist, or exit the app.
- **Closing a tab mid-stream frees the `Session` the thread targets.** `on_tab_close`
  (`window.c:244-285`) only refuses while connecting; `ai_chat_notify_session_closed`
  (`ai_chat.c:4023-4042`) never sets an abort flag before `free_session`.
- **Retry / Stop-then-Send can run two stream threads into one state.** Retry
  (`ai_chat.c:2612-2643`) sets `busy=0` and relaunches without aborting; the single
  shared `abort_stream` flag is reset at `983` before the old thread sees it.
- **Paste timer writes to a freed channel after Ctrl+W.** Disconnect paths call
  `paste_cancel()`, `free_session` does not (`window.c:201-218, 1077-1093`).

Medium: aborted streams strand non-active sessions as `busy` (`572-576, 1149`);
Settings saves to a relative `CONFIG_FILENAME` after a file dialog changed CWD
(`settings.c:1647`; neither file dialog passes `OFN_NOCHANGEDIR`); unlocked conv
`memcpy` and duplicated assistant turn on session switch (`ai_chat.c:4004-4009,
2935-2941`); `ai_conv_move` leaks heap members incl. base64 images (`77-82`);
connection worker runs modal UI on the worker thread (`window.c:333-393, 576`);
plaintext passwords/API keys not wiped on several free paths.

Low: 64-message conversation cap silently drops turns (`ai_prompt.c:132`);
unchecked `GlobalLock`; settings page host reads freed `d` during teardown; header
overflow silently dropped in `ai_http_win.c`; settings control IDs overlap
`resource.h` ranges.

One design change covers the four high items: a thread-owned argument holding its
own abort flag and critical section, a per-session stream generation number checked
in `WM_AI_STREAM`/`WM_AI_RESPONSE`, and busy/close guards that join the thread.

## 3. Documentation — accuracy audit

Confirmed accurate: version, Settings page tree and every field label, all
limits/defaults, font list, theme names, provider list, terminal feature claims,
crypto parameters, known_hosts path, test count, all of CLAUDE.md.

Wrong or stale (spot-verified items marked ✓):

| # | Where | Says | Code |
|---|---|---|---|
| 1 | README:115, help_guide.c:90 | Paste dialog only above 64 chars | Always shown; `PASTE_CONFIRM_THRESHOLD` is defined and never used ✓ (`window.c:1073`) |
| 2 | README:40,160,233 | Log names use the configurable strftime format | Menu/`[L]` path calls `log_format_filename()` which hardcodes `%Y%m%d_%H%M%S`; the strftime path is gated on `logging_enabled`, which has no UI |
| 3 | README:145-146 | Onyx Synapse "green accents" | Accent is blue `0x007AFF`; help_guide is right |
| 4 | help_guide.c:178 | Permit Write off shows red | Grey (`ai_chat.c:1670`); safe commands still run |
| 5 | README:67 | Separate Passphrase field | Password box doubles as passphrase |
| 6 | README:108,254 | PgUp/PgDn scroll a page | One line ✓ (`window.c:2685-2697`) |
| 7 | help_guide.c:87-88, menu labels | Ctrl+C copies, Ctrl+A selects all | No accelerator table; WM_CHAR only intercepts Ctrl+W/Ctrl+V, so Ctrl+C reaches the shell as SIGINT ✓ (`window.c:2602-2609`) |
| 8 | README:201 | Auto Approve approves *safe* commands | Approves everything not blocked by Permit Write ✓ (`chat_approval.c:35-38`); also needs a double-click within 3 s, undocumented |
| 9 | README:231 vs 163 | Logging "enabled in Settings" | No such toggle; File menu / `[L]` badge only |
| 10 | README:311-312, 322, 330 | window.c 108 KB, ai_chat.c 140 KB, `icon_font.h`, 66 test files | 124 KB, 169 KB, file is `icons.c/.h`, 67 files (65 compiled) |
| 11 | README:216 vs help_guide.c:183 | Inline vs "approval dialog" | Inline; help guide stale |
| 12 | README:227 | "System Notes" | Label is "System Instructions" |
| 13 | ai_tooling.md header | Draft, branch `ai_tooling` | Implemented (`ai_tools.c`, `ai_agentic.c`, web search/fetch + tests); `[EXEC]` still coexists |
| 14 | README targets table | — | `redraw-debug` undocumented; `debug` is `.PHONY` with no rule |

Undocumented: all CLI flags except `-nc` (`-sn`, `-h`, `-l`, `-v`, `-?`); F11 and
Ctrl+Space in README's table; right-click paste; bracketed paste mode (explains why
paste delay does nothing in vim/zsh); paste delay default 350 ms; idle timeout max 7
days; `log_format` whitelist with silent fallback; hidden settings with no UI
(`logging_enabled`, `host_key_verification`, colours, `ai_custom_model`); hard limits
(64 messages, 16 commands per batch, 8 MB context, ~400-word notes); Cisco/Aruba/PAN-OS
classifiers exist but `ai_chat.c:3039` hardcodes Linux.

Gaps a new user hits: no CLI reference; logging behaviour contradicts itself; first
AI setup (empty Model, press refresh) is only in the in-app guide; Ctrl+C interrupts
the remote process while the help says it copies; Auto Approve's double-click is
undiscoverable; README directory tree is missing `ai_agentic`, `ai_tools`, the web
tools, `html_util`, `json_validate`, `ssh_timeout`, `icons`, `ui_draw`, `docs/`.

## 4. Questions for Thomas

1. Ctrl+C: is it meant to be "copy when there is a selection, else SIGINT", or is the
   Edit-menu label simply wrong?
2. Paste confirmation on every paste: intentional, or should the 64-char threshold be
   wired up?
3. PgUp/PgDn one line at a time: intentional?
4. The window re-sizing itself (item 1.8): is the WM_SIZING snap meant to grow the
   window to a row multiple even with no terminal?
5. Session Manager on first launch: was auto-open removed on purpose?
6. Log Name Format ignored on the File-menu logging path: intentional?
7. Auto Approve approving WRITE/CRITICAL commands when Permit Write is on:
   intentional?

## 5. Suggested order

1. Fix the four high-severity thread-lifetime bugs (one design change) and the
   relative-path config save — these lose data or crash.
2. The two-line Session Manager phantom row and `OP_DOT` icon fix.
3. Docs pass on README/help_guide for the table above; add a CLI section.
4. The rest folds into the redesign roadmap already in the checkpoint.
