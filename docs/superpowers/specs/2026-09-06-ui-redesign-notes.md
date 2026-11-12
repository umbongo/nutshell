# UI Redesign — Working Notes (checkpoint)

**Status:** sub-projects 1 (design-system foundation) and 2 (AI Assist panel)
landed at v1.0.92/v1.0.96 (see "Todo" below for what's
shipped since — approval-card fixes, prompt-gated dispatch, four-level auto
approve, markdown tables, role colour, smart scrolling, and pending command
batches). Native suite: **4,132 tests**, 0 failures (hosted `Native tests`, real
libssh2, PR #96's head, 2026-11-12; 4,091 with the libssh2 stub on `automaton`;
`wintest` 56). Integration suite (`tests/integration/Run-Integration.ps1`): eleven case files
(`cases\10-terminal` … `95-ai-select`, `85-telnet` new in PR #81), 84 registered cases
(the README's case tables and the telnet file), manual only since 2026-09-15, DPI-aware since
PR #71; `ui_gallery` captures 19 `--ui-demo` states x 4 themes = 76 images. Next action is sub-project 3 (main window
chrome: single toolbar replacing the menu bar, tab strip, status). Resume
from "Todo" below.

Branch: `main` — the redesign branch `ui-polish` was renamed to `main` on 2026-09-07 (the pre-redesign main was kept as branch `v1.0.76`). Now at v1.2.83 (2026-11-12).

## Decisions so far

| Decision | Choice |
|---|---|
| Scope | Full redesign, free rein on visual direction |
| First surface | AI Assist panel |
| Rendering | Stay on GDI/GDI+; build a proper design system on top (tokens, spacing scale, shared primitives, motion tokens). No Direct2D, no WebView2, no new runtime dependencies. |
| Verification | Originally Wine + Xvfb on the Linux box; abandoned because Wine's first-run configuration dialog blocked headless launch. **Now: the session moves to Windows so builds can be run natively and inspected in the user's browser/desktop.** |
| Roadmap (each gets its own spec + plan) | 1. Design-system foundation · 2. AI Assist panel · 3. Main window chrome (single toolbar replacing the menu bar, tab strip, status) · 4. Session Manager, first run, Settings · 5. Toasts + inline validation replacing MessageBox |

## Review findings (from code survey + marketing screenshots)

Strengths worth keeping: vector GDI+ icon set (`src/ui/icons.c`, 32 glyphs),
bundled Inter font, per-monitor-v2 DPI, four themes with contrast tests
(`src/core/ui_theme.*`), 200 ms dock slide, inline AI errors with Retry,
`settings_layout` as the model for a testable layout module.

Weaknesses, in impact order:

1. **Approval card** (`src/ui/chat_listview.c` `paint_cmd_card` / `paint_cmd_container`):
   "Blocked" repeats per row, long commands overlap the next row, WRITE/SAFE chips +
   checkboxes + three loud buttons (green/red/orange) compete. Palette is hardcoded
   (`chat_listview.c:66-87`), so it ignores the two light themes.
2. **No hover state in the chat list** — Allow/Deny/Retry/auto-approve links give no
   hover feedback and no hand cursor (`chat_listview.c` WM_MOUSEMOVE only drags selection).
3. **Toggle state nearly invisible** — Permit Write / Auto Approve signal on/off only via
   a small letter icon changing colour. Undock/Save icons have no labels or tooltips.
4. **Modal dead ends** — 26 `MessageBox` sites, incl. field validation and the "no API key"
   gate (`window.c:907`) which offers no way to open Settings. No toast mechanism exists.
5. **No designed empty states / first run** — one grey line in the chat, empty LISTBOX in
   Session Manager.
6. **Plain chrome** — classic text menu bar above the tab strip; shortcuts (Ctrl+W,
   Ctrl+=/-, PgUp/PgDn, Shift+Insert) never shown in the UI; no accelerator table.
   (Corrected 2026-10-20: Help > Keyboard Shortcuts now shows every chord, from the
   one table in `src/core/shortcuts.c` that also labels the menus; there is still no
   Win32 accelerator table, and plain Ctrl+W goes to the shell.)
7. **Debt that fights a refresh:**
   - Hardcoded colours outside the token system: `chat_listview.c` (27), `tabs.c` (16),
     `ai_chat.c` (12), `window.c` (14).
   - 5–6 copies of the RGB→COLORREF helper; two rounded-rect helpers; two DPI-scale macros
     (`S`, `CLV_SCALE`) plus `settings_scale()`.
   - Approval-card hit-testing (`on_lbuttondown`, ~180 lines) duplicates geometry from the
     paint code instead of sharing it.
   - AI panel `relayout()` uses inline magic numbers; button widths repeated in WM_CREATE.
   - Token struct has only 8 base colours + 14 chat colours; no hover/pressed/disabled/
     focus/elevation/success/warn/danger/link tokens.
   - `src/ui/*.c` is excluded from the Linux test build; only header-only pure logic and
     `src/core` modules are tested.

## Design-system foundation — original proposed shape (superseded 2026-09-07)

Kept for history; the approved version with all decisions is the spec named above.
As first proposed:

1. **Semantic tokens** — keep the 4 themes' 8 base colours; derive hover/pressed/disabled/
   elevation/focus tints in `src/core` via luminance math (testable); add explicit
   success/warning/danger/info/link per theme. Approval-card and tab colours move onto tokens.
2. **Spacing + type scale** — 4 px base grid, named steps (xs 4, sm 8, md 12, lg 16, xl 24);
   type ramp caption/body/title/mono; single DPI scale helper replacing `S`/`CLV_SCALE`.
3. **Shared primitives** — one `ns_draw` module: rounded rect with alpha, card, chip/pill,
   button (rest/hover/pressed/disabled/focus), icon+label; geometry structs in `src/core`
   (like `settings_layout`) so paint and hit-test share one source of truth.
4. **Motion tokens** — durations (fast 120 ms, base 200 ms) and one easing, shared by the
   dock slide, tab pulse and chat activity dot.
5. **Verification harness** — a hidden `--ui-demo` CLI flag that opens the AI panel with
   canned messages (user, AI with thinking, approval card in all states, error with Retry)
   so every screen state can be inspected without a live SSH session or API key.
6. **Tests** — token-coverage test that fails on new `RGB(` in `src/ui` outside `ns_draw`;
   derived-colour contrast tests; geometry tests for the shared primitives.

## Windows build environment (done 2026-09-06, session now on Windows)

The repo builds and tests natively on the Windows dev box (MSYS2 MINGW64 at
`C:\msys64`, gcc 15.2). Changes made, all on `ui-polish`, version bumped to 1.0.77:

- `Makefile` detects `OS=Windows_NT` and switches to pacman libs, `-lz`, `-static`,
  `windres`, a 16 MB test-runner stack, and links WinHTTP/GDI for the `#ifdef _WIN32`
  paths in `src/core`. Linux/vcpkg behaviour is unchanged.
- `src/term/libssh2.h` (test stub) moved to `tests/stubs/`, added to the include path
  only when no real libssh2 is found — removes the include-order pitfall.
- `TEST_TMP_DIR` in `test_framework.h` replaces literal `/tmp/` in file-writing tests.
- Three SSH/known-hosts tests that had rotted (never compiled on the Linux box, which
  lacked libssh2) now compile and run: 1,509 tests, 0 failures (was 1,464).
- Use `mingw32-make`; MSYS `make` breaks gcc's temp-file path. See CLAUDE.md.

**Finding from `make wintest` (first time it has ever run):** `NS_ICON_PASSWORD`
renders zero pixels. Its glyph is three zero-length `OP_MOVE/OP_LINE/OP_CLOSE/OP_FILLSTROKE`
"dots", which GDI+ draws as nothing even with round caps. The same trick is used for the
three dots on `NS_ICON_THINKING` and the LED on `NS_ICON_SERVER`, so those details are
invisible too. Fix belongs in foundation section 3 (shared primitives): add an `OP_DOT`
op backed by `GdipAddPathEllipse` and re-run `wintest` until it is green.

## Todo (compacted at the 2026-09-11 checkpoint)

Done, in order (details in the commit messages and the specs named):
- v1.0.77 native Windows build and test environment · v1.0.78 six review fixes
  (`2026-09-07-review-fixes-design.md`) · v1.0.80 lost lines after resize.
- v1.0.82–v1.0.92 design-system foundation, 10 of 10 tasks
  (`2026-09-07-design-system-foundation-design.md`; gates are exact allow-lists).
- v1.0.93–v1.0.96 AI Assist panel, 5 of 5 tasks (`2026-09-07-ai-assist-panel-design.md`).
- v1.0.97–v1.1.10 AI-panel polish, command dispatch and auto-approve levels
  (`2026-09-07-command-dispatch-and-auto-approve-levels.md`,
  `2026-09-09-pending-command-batches.md`), security fixes C1, C3/L3, C4/H9/H10/L4.
- BVT programme (`2026-09-09-bvt-coverage.md`): stage 1 helpers, runner `k2so-bvt`,
  `bvt.yml` and the ruleset (PR #10) · batch A, 34 bvt cases, all posted input — chords
  via `Send-NutshellChord`, tab clicks posted to `Nutshell_Tabs` (PR #11, c2f55b0) ·
  harness reasons from the achieved window size and the real tab-strip rect, never an
  assumed desktop or DPI (PR #18, ffe9606).
- v1.1.12 minimise/restore keeps the scrolled-back view (PR #12, 373e258); its
  known-bug block in `minimise_restore_repaints` now reports XPASS.
- CI (PR #13, 755f330): actions pinned to SHAs with Dependabot, explicit permissions,
  `Version bump` required check (`.github/scripts/check-version.sh`), `release.yml` on
  `v*` tags, auto-merge and branch auto-delete on, execution-policy-proof PowerShell
  shell for the self-hosted runner (it starts from the Startup shortcut and inherits
  Restricted).

- v1.1.14-v1.1.17 command safety classification (PR #24, 2b06b1e), four commits:
  coverage across ten CLI families incl. HP ProCurve/Comware, Junos, FortiOS, VyOS,
  RouterOS (`2026-09-11-command-classification-coverage.md`) - fixes the `|`
  display-filter and device-filesystem bugs; audit **H2** per-profile Platform with
  banner/prompt auto-detection (`2026-09-11-platform-plumbing-design.md`); audit **C2**
  as an UNKNOWN category plus a five-mode auto-approve over a category *set*
  (`2026-09-11-unknown-safety-category-design.md`); and a Linux-only segfault the CI
  caught (`fdopen` is POSIX, hidden by `-std=c11` under glibc, so the test build now
  passes `-D_POSIX_C_SOURCE=200809L`).

- v1.1.18 `ai_command_is_readonly()` deleted with its tests, the coverage restated
  against `cmd_classify()` (PR #25, a33ea3d).
- v1.1.19–v1.1.21 **status-line policy control and SAFE → READ** (PR #27, 9ac664e;
  `2026-09-11-status-policy-control-design.md`): one four-stop scale
  `Read · Unknown · Write · Critical` with the *allowed* ceiling and the *unattended*
  marker (`src/core/cmd_policy.[ch]`, pure geometry in `ns_layout.c`), replacing the
  `Read-only`/`Read + write` switch and the `Auto approve` cycle; the old `safe+write`
  set is gone by design (not a prefix of the scale; migration rounds down); one
  `ai_policy_default` config token with migration from both old keys; harness drives it
  through absolute command ids (`Set-NutshellAiPolicy`).
- Harness: `cli_no_connect_opens_idle` waits for the window to settle before timing
  it (PR #28, 5cc1dd9) — the empty window paints its version watermark on a second
  paint under load.
- **2026-09-15: the desktop integration gate is decommissioned.** Runner `k2so-bvt`
  died whenever its console was closed from the in-use RDP desktop and the
  screenshot-comparison cases flaked under interactive use, so `integration.yml` is
  gone and the ruleset requires `Version bump` only. That check is now the whole
  release gate: version bumped, README current, and `build/win/nutshell.exe` both
  changed with the build inputs and built from `APP_VERSION` (its FileVersion is read
  out of the UPX-packed exe). `tests/integration/` stays as a manual tool.
- `release.yml` publishes the **committed** exe from a hosted runner: it re-verifies
  that the tag, `resource.h` and the exe's version resource agree
  (`.github/scripts/exe-version.sh`, shared with the gate) and runs `gh release create`.
  Nothing is built at release time — the build is not reproducible, so rebuilding would
  ship a different binary from the one the gate checked. No self-hosted runner is
  referenced anywhere any more.

- v1.1.13 Ctrl+W reattaches the surviving tab (PR #20, 6fd0970); both harness
  known-bug blocks (`tabs_open_switch_close`, `minimise_restore_repaints`) are
  unwrapped.
- Dependabot #14–#17 (per-action major bumps) were closed unmerged, superseded by
  the grouped PR #22 (3c48c09: checkout 7.0.1, upload-artifact 7.0.1, codeql-action
  4.37.9). PR #33 (codeql-action 4.38.0) merged 2026-09-21.
- 2026-09-21 retrospective (`docs/retrospectives/2026-09-21-repository-retrospective.md`):
  project memory and skills now live in `.claude/` (`memory/`, `skills/`), with
  `.claude/CHANGELOG.md` as their record of change and an import from CLAUDE.md.
- Process, 2026-09-21: the model hierarchy replaced by one hand-off rule (PR #35), the
  `critique` and `feature-workflow` skills (PR #35, #36), `steward` folded into
  `feature-workflow` (PR #37), merge counter (PR #38).
- Local shell, 2026-09-22 (`2026-09-22-local-shell-design.md`, 16 critique findings):
  v1.2.0 `SessionIo` seam between the terminal and its transport (PR #39, 35dfd1d; gate
  tier 34/34 by hand); v1.2.1 ConPTY backend, `src/core/local_shell.c` resolver
  (profile command, busybox sidecar, Git bash, MSYS2), `Profile.kind`/`shell`, a saved
  "Local shell" profile, `--local`, Session Manager Type row, `--ui-demo=local`,
  `make wintest` pseudo-console cases, `local_shell` gate case (PR #40, 87cfacb +
  1bfe29e). Embedding busybox in the exe (spec 4.4) waits on the GPLv2 decision.
- v1.2.2 command cards tell the truth (PR #41, `2026-09-23-command-dispatch-states-
  design.md`, 12 critique findings): over-long `[EXEC]` blocks rejected and reported
  instead of cut at 1024 bytes; card labels synced from the approval queue (queued /
  running / ran / not run); a shell on a continuation prompt reported, Stop sends Ctrl+C,
  the model told each command's outcome. Live check still to run (Open).
- Special keys (PR #43 spec, 13 critique findings; PR #44 v1.2.4; PR #45 v1.2.5 adds the
  four files #44 left unstaged): `src/core/key_encode.c` xterm encoder (modifiers, F1-F12,
  Shift+Tab, Ctrl+Space, Alt as ESC prefix, Backspace DEL local / BS SSH), Alt chords to
  the shell with a lone Alt tap keeping the menu, Ctrl+W/T/Space back to the shell with
  the app functions on Ctrl+Shift, PgUp/PgDn by screen, Edit > Send Key, harness `-Alt`
  and `cases/90-keys.ps1`. ConPTY measured: 0x7F Backspace, 0x08 Ctrl+Backspace, ?1049h
  forwarded, ?1h not. Manual checklist (spec 8.6) still to run (Open).
- 2026-09-24 retrospective (`docs/retrospectives/2026-09-24-eleven-merges-retrospective.md`).
- Manual checks passed by the maintainer, 2026-09-23: the dispatch fix (v1.2.2) live
  scenario and the special-keys checklist (spec 2026-09-23 section 8.6), on 1.2.7.
- `codeql.yml` concurrency keyed on the sha for pushes to `main` and cancelling only
  pull-request runs, so fast merges no longer cancel the `main` baseline (bc78b5c).
- The merge gate compiles: `Native tests` job in `checks.yml` (Ubuntu, real libssh2,
  `make test`), required alongside `Version bump` via `Protect-Main.ps1`; CodeQL now
  installs libssh2 too, closing the 2026-09-10 scan gap for the SSH files. Decided by
  the maintainer after the 2026-09-24 retrospective's F1.
- Security review fixes, v1.2.14 (full backup of v1.2.13 on branch `v1.2.13`):
  secrets moved to per-user DPAPI with the config kept beside the exe; absolute
  config save path, OFN_NOCHANGEDIR; busybox removed, installed shells detected
  (PowerShell first) and listed in the profile editor; DLL search hardening
  (SetDefaultDllDirectories, dwmapi at runtime, crypt32/winhttp delay-loaded);
  paste strips control and bidi characters and shows them in the preview, UTF-8
  decoder rejects malformed forms, copy/extract blank control cells; AI stream
  and connection-thread lifetimes refcounted (double free and use-after-free
  fixed); platform detection anchored and only ever tightens after resolution.

- 2026-10-13 retrospective (PR #62, 298fd28;
  `docs/retrospectives/2026-10-13-thirteen-merges-retrospective.md`).
- v1.2.29 no line-clear prefix at a PowerShell or cmd prompt over SSH (PR #60, e5180ab);
  its exe was the first gate-passing build cross-compiled on the Linux box `automaton`.
- CI: the native suite is skipped, still reporting green, on a pull request that changes
  no code (PR #63, cd1322a; first exercised by #67). Dependabot #61 (41b264c) and #67
  (c04945d): codeql-action 4.38.3, upload-artifact 7.0.2.
- v1.2.30 command details popup, copy icons and transcript clipboard (PR #64, 573b60c;
  `2026-10-13-command-detail-and-copy-design.md`).
- v1.2.32 `--help`/`--version`/`--list` write to a redirected stdout (PR #65, c581a8d).
- v1.2.34 a row stays dirty when a paint did not contain it, the stale-band seed (PR #66,
  4f8275e); the two weaknesses it exposed are in Open.
- v1.2.36 in-app debug logging with a Settings section and an AI log tool (PR #68,
  2208cba; `2026-10-17-debug-logging-design.md`).
- v1.2.37 Auto-approve button on the policy row; the rail's ids 4040-4044 retired
  (PR #69, c86dea1; `2026-10-18-ai-auto-approve-button-design.md`).
- Character selection in the AI Assist panel, v1.2.38-v1.2.41 (PR #71, 7960bd2;
  `2026-10-18-ai-panel-text-selection-design.md`, plan
  `docs/superpowers/plans/2026-10-18-ai-panel-text-selection.md`): drag,
  double- and triple-click select characters across messages; Ctrl+C, Edit >
  Copy and the menu copy the visible text; every item is laid out by
  md_render, which records runs and draws the highlight. Two spec rules
  changed on the prototype's evidence: a list marker copies only when the
  range covers its bytes, and the user bubble's highlight uses its text
  colour (the focus tint is invisible on the accent bubble).

- Readable ANSI colours on the light themes (PR #72, spec
  `2026-10-19-light-theme-ansi-contrast-design.md`): PowerShell's yellow, bright and
  grey output was near unreadable on Onyx Light and Moss & Mist. Each theme now
  carries a minimum-contrast floor applied at paint time (`terminal_min_contrast`,
  `src/core/term_colors.c`), the 16-colour and 256-colour tables are built per theme,
  and `--ui-demo=ansi` shows the palette; ConPTY captures of Windows PowerShell 5.1
  pin the painted colours end to end.
- ConPTY resize anchor, v1.2.42 (PR #70, 52b46f3; spec
  `2026-10-18-conpty-resize-anchor-design.md`): slices 1 and 2 of the plan only. The
  console probe (`test_local_pty_resize_parity`, wintest) dumps conhost's own buffer
  around a `ResizePseudoConsole` and records fixtures; `local_pty` gains `pcon_flags`
  and a raw tap; two reflow fixes for every session (a row that fills the width
  exactly gains no blank row; a cursor past the content keeps its distance). The
  top-anchored reflow itself (slice 3) is not built yet.
- ConPTY resize probe run, 2026-10-21 (PR #77; fixtures 89c68d4, report 8dc46fe):
  Windows 11 26H2 build 26300.9711 confirmed spec section 1 as written in all four
  scenarios; `PSEUDOCONSOLE_RESIZE_QUIRK` changed nothing and is not adopted; a
  soft-wrapped row is repainted as one run without `\r\n`. Report filled, the four
  `tests/fixtures/conpty_resize_*.bin` committed.
- PuTTY-style terminal selection, keyboard cheat sheet and Enter-confirms-paste,
  v1.2.44 (PR #74, 6e8d69d; specs `2026-10-20-putty-selection-design.md` and
  `2026-10-20-keyboard-cheat-sheet-design.md`): selection is buffer-absolute per
  session (`SelPoint{line, col}`, `lines_recycled`/`epoch`/64-bit write stamps in
  `Terminal`), survives scrolling and new output, auto-scrolls past the edges, joins
  soft-wrapped rows, copies CRLF on release; double-click word, triple-click line;
  Ctrl+C always interrupts, Ctrl+Shift+C and Edit > Copy copy, Select All copies.
  Confirm Paste opens with Paste focused. Help > Keyboard Shortcuts is a themed sheet
  built from `src/core/shortcuts.c`, which also feeds the menu accelerator text and
  README's generated block (a native test keeps README in step, so README.md now
  counts as code in `checks.yml`). The sixteen harness cases passed on Windows.
  Closes the "copy and paste broken past the viewport" item.
- 2026-10-19 retrospective (PR #73, baac96d;
  `docs/retrospectives/2026-10-19-eleven-merges-retrospective.md`) and the 2026-10-20
  checkpoint (PR #75, f6180ab).
- Selection follow-ups, v1.2.45 (PR #76, 8faf4bf; spec
  `2026-10-20-putty-selection-design.md` amended in section 1): Shift+click and
  middle-click extend the selection by PuTTY's rule, keeping the word or line mode;
  the click count cycles 1, 2, 3, 1; a combining mark joins the cell before it, so a
  decomposed accent no longer splits a word; a narrowing reflow drops the oldest rows,
  not the newest (on the alternate screen the top rows, by decision); the help guide's
  Send Key paragraph no longer lists F1-F12. Four new gate cases; whether they have
  been run on Windows is not recorded.
- Keys and wheel reach the terminal after the AI panel hides, v1.2.46 (PR #79,
  2890e01; from the 2026-10-29 issue mark): focus now leaves the panel on every hide
  path (`ai_panel_release_focus`), sub-notch wheel deltas accumulate per session
  (`term_wheel_lines`, `src/core/edit_scroll.c`), and the issue mark records focus,
  write and receive timing (`src/core/write_stats`). The blocking SSH write it found
  is in Open.
- Hold-to-reveal eye on password fields, v1.2.48 (PR #80, 9552d61; spec
  `2026-11-03-telnet-and-password-reveal-design.md`, plan
  `docs/superpowers/plans/2026-11-03-password-reveal.md`): Session Manager password,
  Settings > AI API Key and the SSH key passphrase prompt; a pure state machine
  (`src/core/pw_reveal_state.c`) under a Win32 skin (`src/ui/pw_reveal.c`); the
  passphrase prompt moved out of `window.c` into a themed `src/ui/pw_prompt.c`;
  copy and cut refused while revealed. Tested on Windows by the maintainer before merge
  (*reported*).
- Telnet sessions, v1.2.49 (PR #81, 94d7ae3; same spec, plan
  `docs/superpowers/plans/2026-11-03-telnet.md`): profile kind `telnet`, passive
  negotiation (`src/core/telnet_proto.c`), auto-login that never sends the password to
  a non-password prompt and never logs it (`telnet_login.c`, `telnet_session.c`), port
  checks with local echo, status lines in the tab, a non-blocking socket backend on
  `SessionIo` (`src/ui/telnet_io.c`), the AI panel's remote-host path, and an SSH
  password box when no password is saved. The final Opus review's Critical and
  Important findings were fixed in e710a16; CodeQL's `cpp/world-writable-file-creation`
  on a test was fixed with `test_fopen_private()` (0499c11). Tested on Windows by the
  maintainer before merge (*reported*); the new harness cases have not been run.
- 2026-11-03 retrospective (PR #82, 582300b;
  `docs/retrospectives/2026-11-03-ten-merges-retrospective.md`); retrospectives on request
  after each merge, the 10-merge cadence dropped (PR #84, 97866f1).
- SGR colour sub-parameters and the six CodeQL alerts, v1.2.50 (PR #83, 723ee51): the
  unpushed `fix/sgr-codeql-tidy` commits rebased and finished. A truncated `38;5` or
  `38;2;r;g` no longer leaks attributes; colon forms parsed; `4:0` clears underline; a
  leading empty parameter and parameter overflow handled; `config_save` writes its temp
  file through `open_private_for_write()`; `md_classify_line` checks the bound first.
  Alerts #11, #13, #14, #15, #22 and #27 closed; none open on 2026-11-12.
- Guard rails from the 2026-11-04 external code review, v1.2.52 (PR #85, 1f85a3a; spec
  `2026-11-04-guard-rails-design.md`): auto-approve pauses after 3 unattended rounds;
  web_fetch resolves every hop and refuses loopback, private and link-local addresses,
  follows at most 5 redirects, never https to http, 30 s deadline; `[EXEC]` markers in
  code are "quoted, not run"; a 15 s SSH setup timeout naming the stage; one WinHTTP
  session helper (automatic proxy, TLS 1.2+); clear-text custom URL warning; JSON depth
  cap. Merged unattended (maintainer's instruction of 2026-11-04); its Windows checks are
  in Open.
- `ai_chat.c` split into seven files plus `ai_chat_internal.h`, no behaviour change,
  v1.2.54 (PR #86, 0750cab; spec `2026-11-05-ai-chat-split-design.md`).
- CI and tooling, v1.2.55 (PR #87, 5808d3d; spec `2026-11-05-ci-tooling-design.md`):
  per-object test build (`make -j test`), `make test SAN=1`, `make lint-gate`, libFuzzer
  targets with committed seeds (`make fuzz-run`, `make fuzz-long`), `quality.yml`
  (`Sanitizers`, `Lint`, `Fuzz`) and `fuzz-weekly.yml`, a `.sha256` beside the released
  exe, dependency versions in About and `-v`, `docs/audit-tags.md`; `tests/fake_dlog.c`
  pads in one allocation, so the ASan run finishes (629681f); the OSC numeric overflow
  the fuzzer found is fixed.
- Trust boundary part A, v1.2.57 (PR #88, 4ba5662; spec `2026-11-05-trust-boundary-design.md`):
  terminal output leaves the system prompt for a delimited data block in the last user
  turn; a `sensitive` bit gives credential reads a card at any policy; classifier fixes
  (`sh -c`/`bash -c`/`eval` bodies, `xargs`/`watch`, `$(...)`, IOS `conf t`/`debug`/`do`);
  `env rm -rf /` and `echo $(rm F)` now classify CRITICAL.
- SSH keyboard-interactive authentication, v1.2.56 (branch `feat/keyboard-interactive-auth`;
  spec `2026-11-05-keyboard-interactive-design.md`): the server is asked what it offers and
  the methods are tried in a fixed plan (`src/core/ssh_auth_plan.c`; a saved password goes
  first when password is on offer, else keyboard-interactive does); each server prompt is
  shown one at a time with its text in a scrolling box (`src/core/ssh_kbd.c`,
  `src/core/pw_prompt_layout.c`, `src/ui/pw_prompt.c`); a saved password answers only a
  plain password prompt, once (and not under an instruction that names a token or code);
  the save tick appears on such a prompt; a refused saved password re-queries the method
  list at once and is then followed by one more question (the final review's ruling of
  2026-11-05, which removed the partial-success Open item); the setup timeout is re-armed
  after each answered round, typed or sent from the saved password, so a push approval
  gets a minute; a failed login names the offered methods.
  `tests/manual/kbdint_server.py` is a paramiko server for hand checks (not in any gate).
  Merged as v1.2.59 after a merge of `main` (PR #89, 62202f8); its hand tests are in Open.
- Startup crash fixed, v1.2.60 (PR #90, 0f5bf9a; snapshot branch `v1.2.60`): every build
  from v1.2.49 to v1.2.59 died at launch on the maintainer's Windows 11 24H2 workstation;
  the manifest now asks for comctl32 v6, whose exports the subclassing calls need. Wine
  never reproduced it.
- Dependabot #78 (7122a29): codeql-action 4.38.3 to 4.39.1, after 15 days open.
- Theme text contrast gate, v1.2.62 (PR #92, 35e09e7): `tests/test_theme_contrast.c`
  checks 72 text and surface pairs on all four themes (4.5 for text, 3.0 for glyphs);
  token changes on all four, Moss & Mist's accent to deep sage `57705C`; seven exemptions.
- Trust boundary part B, the tool gate, v1.2.74 (PR #91, d73eeec): every tool carries a
  safety level, `web_fetch` is UNKNOWN and gets a one-row card unless Auto-approve allows
  it; the stream thread waits on a decision slot, never the UI thread; an approved fetch
  is pinned to its host; Moonshot (Kimi) gets the tools; tool approval timeout and
  automatic tool rounds in Settings; stream parser fixes; `ai_header_layout()` stops the
  header overdraw. The maintainer ran the gate checks on v1.2.67-v1.2.69 with Kimi.
- Settings help on hover, v1.2.75 (PR #93, 6432ca0; spec `2026-11-05-settings-help-design.md`):
  46 tips from `src/core/settings_help.c`, the dialog opens tall enough for the tallest
  page, Web Access reordered, a second request raises the open dialog. **Scrollback Lines
  now takes effect** (default 10,000) with rows allocated as lines are kept, which closes
  the scrollback-size part of the selection follow-ups.
- `run_command` tool and the command list, v1.2.82 (PR #94, d966713; spec
  `2026-11-05-run-command-tool-design.md`): commands as structured tool calls (Automatic
  is On for Anthropic, OpenAI and Moonshot, Off for DeepSeek, Gemini and Custom), command
  chips and cards that fold once final, a conversation-limit guard, server-sent lines up
  to 256 KiB, RFC 8259 numbers in `json_validate`. Outside the panel: CSI Z handled, which
  fixes nano's cursor over SSH (2eaacb1, found by replaying a recorded session against tmux,
  299 chunks); the window title shows the version (4dcd308); the Settings label column
  widened, with a `wintest` that measures every label at four scalings (99bea50). The
  maintainer tested v1.2.77 and v1.2.81 on Windows; v1.2.82 itself was not seen on screen
  before the merge.
- 2026-11-12 retrospective (PR #95, f842775;
  `docs/retrospectives/2026-11-12-fourteen-merges-retrospective.md`).
- Thinking row modes, v1.2.83 (PR #96, 1a03af5; spec
  `2026-11-12-thinking-row-modes-design.md`): a Thinking row is collapsed, compact (the last
  four lines of the text) or open, and a click cycles them. One global choice for every
  row, saved in the config as `ai_thinking_mode` and replacing the session-wide
  `IDC_CHAT_THINKING_OPENED/CLOSED` preference. Automatic, the default, is compact while
  the reply streams and collapsed once it is idle; Settings has a "Thinking display" combo
  (Automatic, Always collapsed, Always compact, Always open). The label colour follows the
  mode (new theme tokens). The review fixes: the click works from an undocked panel, the
  clicked row keeps its place on screen, compact ends at the last non-blank text. Gallery
  states `thinking-compact` and `thinking-open`. Its Windows checks are in the first Open
  item.

Open, in priority order:
- [ ] **Windows hand checks never recorded as run** (2026-11-12 retrospective, F1). Each PR
      body lists them; nothing records a run except where noted. #85: web_fetch refuses
      `127.0.0.1`, `[::1]`, `169.254.169.254` and a redirect to one; SSH to a silent port
      fails after about 15 s; the clear-text URL warning; the "quoted, not run" chip. #86: a
      real session that streams, pastes an image and runs one approved command (likely
      covered by the v1.2.77 Kimi test, not recorded as such). #87: About at 125 % and
      200 % and after a DPI change, a real zlib version, `-v` two lines, `mingw32-make test`
      twice, `cli_version_redirect_file`. #88: the data block seen at a loopback listener,
      Anthropic `cache_control` returning 200, sensitive cards at Read. #89: the spec's hand
      tests against `tests/manual/kbdint_server.py`. #92: the four-theme gallery judgement
      (Moss & Mist's deep-sage accent, the light-theme indicator greens). #93: the spec's
      13-item checklist and `settings_help_tool_count` (83 tools since PR #96's combo). #94: v1.2.82 on screen (title,
      fold anchor, row heights after a scaling change), Retry, New Chat, undock and redock,
      an error ending, `cls`/`Clear-Host` capture, `--ui-demo=tool`. #96: the three modes on the four themes (`--ui-demo=thinking`,
      `thinking-compact`, `thinking-open`), a live reply under Automatic (compact, then
      collapsed), the click cycle and the clicked row keeping its place, the choice
      surviving a restart and an undock and redock, Settings > Automatic resetting it. All:
      `--ui-demo=all` on the four themes and a `ui_gallery` run (19 states x 4 themes).
- [ ] Thinking row follow-ups (PR #96; the spec's "Flagged to the maintainer", to confirm):
      under Automatic a click on a streaming row goes compact to open (the next mode in
      the cycle), so hiding the text takes two clicks; Save under Automatic never includes
      the reasoning, since the idle mode is collapsed (before PR #96, opening any row once
      made Save include it). Both are as the spec decided; the maintainer says whether
      either should change.
- [ ] `run_command` follow-ups (PR #94): live rounds were run on Moonshot (Kimi) only;
      Anthropic and OpenAI (Automatic means On) are untested live, DeepSeek, Gemini and
      Custom stay Off until tried. Duplicate "session ended" rows (*reported*); the `[EXEC]`
      continue path's failed launch leaves a second approved batch waiting, and its Stop
      differs from the tool path's; the run log keeps 16 records; a batch broken by other
      rows never folds (commands plus a web fetch, or prose between commands); the list view
      has no automated paint test (a `wintest` fixture for the list view and header paint);
      opening a card while a reply streams stops the list following the stream (by intent).
- [ ] Tool-gate follow-ups (PR #91): a later chunk with `"id":""` overwrites a stored call
      id; the indexed path has no upper bound on the index; a pending call in a reply cut off
      at the output limit is not run; Anthropic `type:"error"` stream events are not shown;
      DeepSeek's reasoner may need the reasoning pass-back on tool loops; Kimi's reasoning on
      plain replies is not kept; history carries no tool turns (spec non-goal); a tool row in
      `--ui-demo`; the four v1.2.74 review leftovers (a font left selected in one header
      paint path, the status dot's size written twice, the rounds note's spare row, two
      "web_fetch is off" rows in one round). From PR #88: NX-OS and ASA `do CMD` should use
      their own tables; a sensitive row in `--ui-demo`; blank class reasons (`sh -c: `).
- [ ] Terminal escapes (PR #94's nano work): CSI I, HTS and TBC unhandled; `term_put_char`
      auto-wrap inside a scroll region; entering the alternate screen resets the primary's
      scroll region (*reported*); replay vim, htop, mc and less against tmux as nano was.
- [ ] Terminal attributes (found 2026-11-12 while checking this list): the parser keeps SGR
      underline and blink (`TERM_ATTR_UNDERLINE`, `TERM_ATTR_BLINK`) but `renderer.c` draws
      neither, though the in-app guide lists both styles; the OSC 0/2 window title is parsed
      into `term->title` and nothing reads it, so it is never shown (a tab title or the
      window caption could).
- [ ] Dead code: `src/core/chat_thinking.[ch]` (the old Thinking-box timer state) is used
      only by `tests/test_chat_thinking.c`; no file in `src/` includes it. Delete both with
      the test, or use it.
- [ ] Settings help text contradicts itself: the Search Engine tip in
      `src/core/settings_help.c` says "Gemini and custom providers get no tools", while the
      Provider and Commands as tool calls tips say they get them with Commands as tool calls
      On. A `src/` change, so it waits for the next code PR.
- [ ] Settings follow-ups (PR #93 and its spec's non-goals): keyboard path to the help (Tab
      order, F1); `WM_DPICHANGED` in Settings and `WM_GETMINMAXINFO` on mixed scaling; at
      the minimum width the widest Debugging row overflows; hover help for the profile
      editor; Start Logging fails silently; Refresh models turns a Base URL of `https://host`
      into `https://models`; a generated README settings block (*reported*).
- [ ] Other, from the PR #94 session (*reported*): markdown `_` inside a word; resize cost
      with a full history (PR #93 measured 14 ms at 10,000 lines and 66 ms at 50,000 for 200
      columns); a user-supplied issue log can hold a typed secret, so `term.rx`/`term.key`
      payloads are never printed and tests use synthetic text.
- [ ] web_fetch: a watchdog that closes the request handle at hop end or on cancel; one
      hop can still exceed its timeout against a trickling server (the final review of
      the guard rails branch; the header wait is bounded, the body loop checks the hop's
      clock only between reads).
- [ ] winhttp_open: fail closed when TLS 1.2 cannot be enforced (today the session keeps
      the OS defaults if the TLS 1.2-only retry is refused).
- [ ] `vcpkg.json` manifest with a baseline (deferred by PR #87, whose body says it is in
      this list; it was not until 2026-11-12).
- [ ] `ssh_channel_write` blocks the UI thread for up to its 5 s send timeout per write
      when the remote window is full (found by PR #79, left alone). The same class of
      symptom as #79's lost input: keys stop while the UI thread waits. Move SSH writes off
      the UI thread or make them non-blocking with a queue. Also from #79 (*reported*): a
      "Mark Issue Ctrl+Shift+M" tooltip drawn over terminal row 2 in the maintainer's
      screenshot, not investigated.
- [ ] Telnet follow-ups (PR #81): `tests/integration/cases/85-telnet.ps1` (five cases) has
      never been parsed or run -- no `pwsh` on `automaton`, so the Windows box is the
      first place it can run (`-Only telnet_autologin_never_logs_password,...`); a stray
      `IAC SB` with no `IAC SE` swallows later output (PuTTY does the same); the 20 s
      auto-login timer misjudges once per 49.7 days of uptime (fails safe); the minor test
      gaps in #81's final review. From PR #80: no Wine test separates "mask after every
      edit message" from a message whitelist.
- [ ] SSH agent / Pageant authentication: not supported (keyboard-interactive spec,
      non-goal). Low priority; a profile that needs the agent has to use a key file or a
      password today.
- [ ] Designed light-theme ANSI palette (follow-up to the contrast floor, maintainer's
      decision 3 of 2026-10-19): on Onyx Light and Moss & Mist the floor makes normal and
      bright colours converge (90 and 97 both paint `6C6C6C`) and flattens the 232-255 grey
      ramp; a hand-designed 16-colour palette per light theme would restore the distinctions.
      Also from that spec's Out of scope: answer OSC 11 (background colour query) so programs
      can detect a light background; default text on an explicit background stays raw.
- [ ] Terminal selection follow-ups (spec `2026-10-20-putty-selection-design.md` section 4;
      the selection shipped in PR #74, Shift+click and middle-click extend, the click cycle,
      combining marks and the reflow drop order in PR #76; middle-click now extends, so
      middle-button paste is off the list; the scrollback-size setting and lazy allocation
      shipped in PR #93): Alt+drag block selection; the per-byte paste writes over SSH
      (mouse reporting to applications: see "Local shell follow-ups").
- [ ] Top-anchored reflow for ConPTY sessions, slices 3-6 of
      `2026-10-19-conpty-resize-probe-and-reflow.md` (top mode and the reconnect push,
      the 100 ms throttle with its starved-timer check, DPI reorder, logging, the
      integration case). The 2026-10-21 probe confirmed spec section 1 as written and
      ruled out the quirk flag, so this is unblocked; until it ships a grow still
      strands the prompt mid-screen between the resize and conhost's repaint.
- [ ] Harness cases on `main`: `resize_applies_to_inactive_tab` fails on every run
      (since v1.2.13); `ctrl_w_reaches_shell_and_shift_closes_tab` failed the v1.2.36 and
      v1.2.37 gate runs and passed the v1.2.41 one, so it is intermittent -- both are
      tab-switch cases; investigate together (tab B never reports its grid). Not run
      since, as far as the log shows: PR #76's four gate cases, the telnet cases and
      `settings_help_tool_count` (see their own items), `ui_gallery` since the PR #92 token
      changes, and its states added since: `runlist` (PR #94), `thinking-compact` and
      `thinking-open` (PR #96).
- [ ] No release since v1.1.21 (2026-09-14); `main` is at v1.2.83, which adds telnet, the
      password eye, keyboard-interactive auth, the tool gate, `run_command` and the Thinking
      row modes since then.
      The first tag will also be the first run of `release.yml`'s `.sha256` step (PR #87).
      Tagging is the maintainer's call (`feature-workflow` step 10), best after the Windows
      checks in the first item.
- [ ] AI panel: commands of a **finished** batch have no details popup and no copy icon
      after a session switch. `cmd_batch_remove` drops the batch, so `chat_rebuild_display`
      shows them only as inline `[EXEC]` segments of the AI text. Either keep finished
      batches (dispatcher change) or parse the rebuilt text into settled rows. Known limit
      from `2026-10-13-command-detail-and-copy-design.md`, section 1. The same spec lists
      two smaller follow-ups: the classifier's reason string as a "why this level" line in
      the popup, and a Unicode input box.
- [ ] AI panel selection follow-ups (spec `2026-10-18-ai-panel-text-selection-design.md`
      section 8 and the plan's notes): an I-beam cursor over text; Shift+arrow keyboard
      selection; grapheme-safe boundaries (a combining mark or an emoji sequence can
      split); dragging past the Thinking box's edge does not scroll the box; the message
      box is still an ANSI EDIT, so text outside the code page cannot be typed (the
      "Unicode input box" item above); the highlight leaves 2-6 px gaps between lines
      (line spacing), which the four-theme review may want closed.
- [ ] Review and critique the whole repository, security first (maintainer's request,
      2026-09-24). A fresh audit, not a re-read of `2026-09-09-security-audit.md`: the
      local shell (ConPTY spawn, environment, PATH, the shell search order),
      the AI command path (what the model can make the terminal run, the approval
      policy, the 1023-byte limit, auto-approve levels), secrets (API keys, profile
      passwords, `secure_zero`, what reaches logs), config loading, host keys, window
      messages, and CI/workflow permissions. Then the general code-health critique
      (`window.c` size, `src/ui` logic that belongs in `src/core`; `ai_chat.c` was split
      in PR #86). The external code review of 2026-11-04 and PRs #85, #87, #88, #89 and
      #91 covered part of the AI command path, web_fetch and CI; start from what they
      left. Findings ranked by severity, each answered in writing; folds in the open H7/H8
      items below. Known classifier holes to start from (PR #50 review, 2026-09-23):
      `env rm -rf /` and `echo $(rm ...)` are CRITICAL since PR #88; check
      `perl -e "unlink ..."` and backtick substitution; no PowerShell ruleset, so
      destructive cmdlets are UNKNOWN rather than CRITICAL; the `>>` prompt test uses
      synthetic bytes rather than a captured PSReadLine/ConPTY stream.
- [ ] Local shell follow-ups: PowerShell as the local shell now has a harness case
      (`local_shell_powershell`, `cases/80-local-shell.ps1`) and a native resolver test;
      check which of prompt detection on `PS C:\...>`, paste line ends, Ctrl+C and the AI
      ruleset it covers before striking that part; busybox
      is gone as a shell (maintainer, 2026-09-24; v1.2.14), so no embedding decision is
      needed; move the dispatcher out of `src/ui` so it can
      be tested natively; a timeout for commands that never return; WSL; tab title from
      the shell's directory (OSC 7); mouse reporting (special-keys spec section 5, own
      spec); Backspace on SSH and a per-profile terminal type (special-keys spec 7).
- [ ] CI review follow-ups (2026-09-10 review; the libssh2 part was done 2026-09-24):
      `tests/integration/Install-IntegrationRunner.ps1` still exists for the runner
      decommissioned on 2026-09-15 and names the removed `integration.yml`; deleting it is
      the maintainer's call;
      `src/ui` is never scanned (since PR #81 that includes the telnet socket backend
      `src/ui/telnet_io.c`) -- consider a Windows CodeQL run; `.gitattributes` exists
      but holds only `tests/fixtures/*.bin binary`, so add eol rules and renormalise the
      94 CRLF-indexed files (2026-11-12) in a lone PR when nothing else is in flight;
      mark `tests/fuzz/corpus/**` binary first, since eight seeds there carry CR bytes as
      test input. (Untracking `build/win/nutshell.exe` is off the list: the gate
      now relies on the committed exe being built from `APP_VERSION`.)
- [ ] Harness batches B (sessions, auth, host key, connection), C (terminal, clipboard,
      settings), D (AI extras) — 31 cases planned; the suite has since grown to 84 cases, so reconcile the plan with what exists first; `bvtuser` exists on tompi for auth cases.
      Manual runs only now; still worth having for release checks.
- [ ] Security audit follow-ups (`2026-09-09-security-audit.md`, kept local; C2 and H2
      landed in PR #24, H3/H4 in PR #54, H5/H6 in v1.2.14 -- DPAPI with the config
      kept beside the exe, absolute save path): H7/H8 window-message hardening;
      release and supply chain (exe tied to source, tag ruleset, immutable releases,
      provenance, secret scanning -- the settings changes need the maintainer's
      say-so; PR #87 added the release checksum); then Medium and Low. The web_fetch
      destination filter shipped in PR #85.
- [ ] Older review findings: Session Manager phantom row; a stopped AI reply leaves
      two user turns in a row in the conversation; a stream posting during the AI
      panel's destruction leaks its payload (tiny; probably closed by the v1.2.14
      stream-lifetime rework, f2cb01d -- confirm, then strike).
- [ ] v1.2.15 review follow-ups: a local custom command that is an SSH client to a
      device (`ssh.exe`, plink) is locked to the unresolved ruleset, which is looser
      than device rulesets -- keep detecting on strict-locked sessions and add a
      detected device's rules on top (worse of the two), or ask for an explicit
      platform; a session contradicted after a weak prompt match counts only the
      first platform plus unresolved, not every platform seen; no tests yet for save
      write/flush failure, failed backup rename, locked config, forward-slash/UNC
      custom paths. (The `resize_applies_to_inactive_tab` failure is the harness item
      above.)
- [ ] Policy control follow-ups from the PR #27 review: the AI-tier harness cases it
      rewrote (`ai_runs_read_command_unattended`, `ai_write_command_held_then_runs_after_allow`)
      have not been run yet — needs the Kimi key in `tests\integration\.ai_key`; and the
      status line still has no keyboard path (a whole-line design-system job; the
      `WM_COMMAND` ids 4030–4033 and 4015 are ready for a menu or accelerator, and
      4040–4044 were retired with the rail on 2026-10-18).
- [ ] Per-session AI dispatcher (follow-up from `2026-10-18-ai-auto-approve-button-design.md`
      section 9): with Auto-approve on, a session processes unattended only while it is the
      displayed tab — a switch cancels the dispatcher, and a reply for a hidden tab builds its
      batch with Auto-approve forced off. Running a background session's batches needs the
      dispatcher to own its terminal, conversation and card list per session instead of the
      panel's active ones, and the switch to stop cancelling. Own spec first.
- [ ] AI command origin marker (external reviewer, 2026-09-25): a bar in the terminal's
      left margin on rows carrying an AI-dispatched command, stamped at write time —
      spec `2026-09-25-ai-command-origin-marker-design.md` (DRAFT, critique and seven
      open questions before implementation; ConPTY repaint behaviour to measure first).
- [ ] `md_render.c` renders at raw 96-DPI pixels — fold into sub-project 3.
- [ ] Sub-project 3 (main window chrome): spec first, mockups before choosing, as with
      sub-project 2.
- [ ] Paint-path follow-ups from the 2026-10-14 stale-band investigation (v1.2.34 fixed
      the seed: `renderer_draw` cleared `row->dirty` on a paint that did not contain
      the row, and `term_alt_screen_enter()` now requests a full redraw like the exit
      path). Two weaknesses the REDRAW_DEBUG log exposed remain: (a) a disconnected
      background session keeps a row dirty forever, so `term_has_dirty_rows()` makes
      the WM_TIMER poll loop invalidate the window at 60 fps indefinitely — find why
      the row never clears (rows of a non-active session are never painted) and stop
      polling dirty state for sessions that are not on screen; (b) the poll path calls
      `dispbuf_invalidate()` without `force_all_rows`, so a shadow-invalidating repaint
      cannot rescue a row already marked clean — consider making `dispbuf_invalidate`
      also mark all rows dirty, or routing it through `full_redraw_needed`. The 16 ms
      cooldown's `g_last_paint_tick` is shared by all sessions, which is what let the
      background tab's no-op paints starve nano's. Also noted by the review: the poll
      loop only repaints for connected idle sessions, so a disconnected or connecting
      tab that takes a partial paint waits for the next full invalidate from elsewhere
      (as before); and `rcPaint` is the update region's bounding box, which the per-row
      shadow poisoning now makes harmless.
- [ ] The AI panel never refreshes `d->dpi` after `WM_CREATE`: paint, hit and tooltip
      rects stay consistent with each other, but the panel does not rescale on a monitor
      move (found 2026-10-18 during the Auto-approve review). Still true on 2026-11-12:
      only the chat list view remeasures on `WM_DPICHANGED` (07953d2).
- [ ] Scrolled-back repaint storm (2026-10-18 `issue3.log` lines 11761-12797, out of scope of
      `2026-10-18-conpty-resize-anchor-design.md`): with `scrollback_offset` 12..6, 186 paints
      in 4.4 s had `dirty_rows` equal to the offset, driven by `poll.invalidate poll_rc=0`
      every ~16 ms, `rows_left_dirty=0`, and `render.stuck_dirty` never fired. The live rows
      below the scrolled view look never cleared; find why and teach the detector to see it.
- [ ] SSH to a Windows host (Win32-OpenSSH runs the remote shell under a server-side ConPTY)
      shows the same full-viewport repaints after a resize as the local shell. The top-anchored
      reflow of `2026-10-18-conpty-resize-anchor-design.md` is per session: offer it as a
      profile option, or switch to it when platform detection says Windows.
- [ ] Wide characters: the parser gives every codepoint one cell, ConPTY and most terminals
      give East Asian wide characters and most emoji two, so such rows drift from the shell's
      idea of the cursor column. Pre-existing; needs a width table and two-cell rendering.
- [ ] Parser quirk (pre-existing, left alone by the ConPTY resize spec because fixing it changes
      SSH rendering): EL and ED never update `TermRow.len`, so `len` can exceed the written
      width. The `wrapped` half was settled by PR #74: a whole-row erase (EL 2, EL 0 from
      column 0, EL 1 to the last column, ED over a whole row) clears it, and a line feed
      never does, by design (readline moves onto a continuation row with a bare LF).
- [ ] Chords asked about that do not exist: Ctrl+Tab (switch tabs), Ctrl+0 (zoom reset),
      approve/deny keys in the AI panel. The cheat-sheet spec (2026-10-20, "Not changed")
      left them out; decide whether any is wanted, then add it to `src/core/shortcuts.c`.
- [ ] Main-window chords (Ctrl+Shift+T/W/M, F11, Ctrl+Shift+Space) fire only while the main
      window has focus; from the AI panel's input they are dead. The cheat sheet states the
      condition per row ("from the terminal"); forwarding them is a separate change.

Process (CLAUDE.md): hand off wherever possible; Opus orchestrates a multi-step
package and reviews diffs, Sonnet writes code and tests, the main session keeps the
decisions and reads the diff in full; `gh pr merge N --merge --auto` merges at once
when the PR is mergeable and only `Version bump` and `Native tests` are required
(CodeQL, `Sanitizers`, `Lint` and `Fuzz` are not), so `gh pr checks N --watch` until every check is green first. Run the harness tier a change touches by hand first. "Create a checkpoint" = compact this
list, compact memory, delete temp files and worktrees, leave the tree ready for a new
session.
