# UI Redesign — Working Notes (checkpoint)

**Status:** sub-projects 1 (design-system foundation) and 2 (AI Assist panel)
landed at v1.0.92/v1.0.96 (see "Todo" below for what's
shipped since — approval-card fixes, prompt-gated dispatch, four-level auto
approve, markdown tables, role colour, smart scrolling, and pending command
batches). Native suite: **4,673 tests**, 0 failures, 3 skipped (hosted `Native tests`, real
libssh2, PR #122's head, 2026-11-28; 4,632 with the libssh2 stub on `automaton`;
`wintest` 257 run, 0 failed, 6 skipped under Wine). Integration suite (`tests/integration/Run-Integration.ps1`): twelve case files
(`cases\10-terminal` … `97-voice`, `85-telnet` new in PR #81), 94 registered cases
(the README's case tables and the telnet file), manual only since 2026-09-15, DPI-aware since
PR #71; `ui_gallery` captures 22 `--ui-demo` states x 4 themes = 88 images. Next action is sub-project 3 (main window
chrome: single toolbar replacing the menu bar, tab strip, status). Resume
from "Todo" below.

Branch: `main` — the redesign branch `ui-polish` was renamed to `main` on 2026-09-07 (the pre-redesign main was kept as branch `v1.0.76`). Now at v1.3.2 (2026-11-28).

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
- Extended thinking mode, v1.2.86 (PR #101, 986a7b7; spec
  `2026-11-21-thinking-extended-mode-design.md`, amending the modes spec): a fourth mode that behaves as compact but
  shows the last 25 lines (pref `extended`, Settings "Always extended", gallery state
  `thinking-extended`). Automatic now streams in Extended and collapses when complete;
  the click cycle is collapsed, compact, extended, open. The review fixes: when a layout
  changes a row's mode (Automatic collapsing at completion) and the view is not pinned to
  the bottom, `recalc_layout` holds the first visible item, which had moved by about 24
  lines; the click-cycle wintest uses 40 lines, because at 14 the extended and open heights
  were equal and it could not fail; a static assert ties the Settings combo to
  `THINK_PREF_MAX`. Its Windows check is in Open.
- Native test build fails on warnings, v1.2.84 (PR #98, e46b98c): `-Werror` added to
  `TEST_CFLAGS` (fuzz builds inherit it); no code needed fixing. The MinGW build's stricter
  `-Wpedantic -Wshadow -Wformat=2 -Wconversion` would add five warnings to the native build
  (three sign conversions, one non-literal format, one empty translation unit) if wanted later.
  Windows check owed: `mingw32-make test` under MSYS2's newer gcc with `-Werror`. Run on
  2026-11-13: it failed (next entry); with the v1.2.85 fixes it passes (below).
- web_search URL builders, v1.2.85: the first Windows `mingw32-make test` under `-Werror`
  (MSYS2 gcc, 2026-11-13) failed on four `-Wformat-truncation=` warnings in
  `src/core/ai_tool_web_search.c` (lines 375, 378, 381 and 509, the `snprintf` calls that
  build the search URL from a query of up to 1,023 bytes). They sit inside `#ifdef _WIN32`,
  so the Linux native build never compiles them and the MinGW cross-compile (gcc 13) does
  not warn. Fixed by checking each `snprintf` result and returning "Search query is too long
  for a search URL" instead of sending a cut URL. The branch compiled clean under MSYS2
  gcc 15.2 on Windows (2026-11-13), then failed to link: `tests/test_settings_label_win.c`
  (PR #93) was in `wintest` but missing from `WIN_ONLY_TESTS`, so a Windows-host
  `make test` built it with `_WIN32` defined and it needs `ns_font` from `src/ui`. On Linux
  it is an empty translation unit, so nothing noticed; Windows-host `make test` had not
  linked since PR #93. Added to `WIN_ONLY_TESTS`. Run on Windows on the branch
  (2026-11-13, MSYS2 gcc 15.2, real libssh2): `mingw32-make test`, 4,131 tests, 0 failed.
- **CodeQL alert #30 dismissed as a false positive** (2026-11-21, by the maintainer's
  decision; no code change). `cpp/cleartext-storage-file` at `src/config/loader.c:127`, opened
  on `main` by the push run after PR #101. Its flow starts at `password_enc_preserved`, which
  only ever holds an already-encrypted blob the config could not decrypt here (filled only by
  `preserve_blob_or_drop()`, written back verbatim by `secret_prepare()`); plaintext secrets
  always go through `crypto_encrypt_dpapi()` and `config_save()` aborts if that fails. It
  appeared because `codeql/cpp-queries` 1.10.1 (CLI 2.27.5) began tracking flow through
  character types (`fprint_json_str`'s `unsigned char c`); main's last clean run used 1.10.0.
  PR #101's run carried the `pr-diff-range` extension and so reported only its changed lines.
- v1.2.87 (PR #103) two queued `src/` fixes. The Search Engine help tip no longer says
  Gemini and custom providers get no tools: they get them with Commands as tool calls On,
  and `test_settings_help` asserts it. `src/core/chat_thinking.[ch]` and
  `tests/test_chat_thinking.c` are deleted (the only reference left in `src/` was an unused
  `#include` in `ai_chat_internal.h`, dropped too).
- AI assistant switch, notice dialog and cancellation (branch feat/ai-switch; spec
  `2026-11-23-ai-assistant-enable-switch-design.md`): `ai_enabled` in the config (off for a
  new install, on for a config that already holds a key), a runtime gate so that nothing of
  the assistant loads while it is off (menu items, tab-strip button, shortcut, help section,
  WinHTTP and the DLL loads), Stop and turning the assistant off close the request handle at
  once, and one themed `Nutshell_Notice` dialog for the upgrade notice, the first-run notice
  and the configuration warnings (missing settings are now written at launch). Its Windows
  checks are in Open.
- Voice input (branch feat/voice-input; spec `2026-11-23-voice-input-output-design.md`,
  speech-to-text only, no spoken replies): press Right Shift (the default key and Toggle the
  default mode since the maintainer's decision of 2026-11-23, was Right Ctrl and Hold to talk;
  Right Ctrl, F8 and F9 and Hold to talk remain as choices; a 1.2.92 config whose voice
  settings were never touched, voice off with Right Ctrl and Hold, migrates once to Right Shift
  and Toggle, and one with voice on or another key or mode keeps its own) with the AI panel focused, speak, press again; the words are transcribed by OpenAI, Groq, Mistral,
  Gemini or a custom endpoint (or the AI provider's own) and go through the chat's normal
  send path with every approval unchanged. Off by default, needs the assistant; while off
  nothing of it loads (no audio thread, no COM, no hook, no mic button). Settings has a Voice
  page; the shortcuts sheet and README gain two rows shown only with voice on
  (`shortcuts_filtered_ex`, `SC_VOICE`); the empty state gains a voice hint line; the demo
  gains `voice-recording`, `voice-transcribing` and `settings-voice`. Its Windows checks are
  in Open.
- Voice review by default (be421b1, kept on branch feat/voice-improvements, which began as feat/voice-review-default; maintainer, 2026-11-23: "for the
  default, i just want you to write down what im saying for me to review before i send it"): a
  transcript is written into the AI input (appended after a space to a draft), caret at the end
  and focus in the box, and never sent; the new tick box "Send automatically when I finish
  speaking" (`voice_auto_send`, default off) restores the old sending. `voice_review` is no
  longer read, so every existing config gets auto-send off, and the upgrade notice names it.
  Voice stays unavailable while the assistant is busy in both modes. Its Windows checks are in
  Open (voice item, (ae) to (ai)).
- Voice improvements (branch feat/voice-improvements; maintainer, 2026-11-24; spec sections
  tagged so): review by default stays (be421b1). A software microphone gain
  (`voice_mic_gain_db`, -12 to +30 dB) is applied on the audio thread before the silence gate,
  so the WAV, the gate and the meter all see it; it is not the Windows endpoint volume. A
  zoned dBFS level meter with a label: silent below -45, quiet below -18, good -18 to -6, hot
  -6 to -3, loud from -3; release 20 dB/s after a 1 s hold. Test is a toggle: no time limit,
  keeps no samples, never uploads, and stops on the Stop test button, a page change, Save,
  Cancel, unticking Enable voice or the AI assistant, or the mic going away. The panel status
  moved to the header after the title, with a tooltip for the full text; the model chip is
  gone and the strip keeps the context meter. A Language dropdown (Auto-detect, English (UK)
  by default, English (US), common languages, and Other... taking an ISO 639-1 code or an
  English name, with a format cue, hint and tooltips): gpt-transcribe gets `languages[]`,
  OpenAI, Groq and custom get a British-spelling `prompt`, Gemini an instruction, Mistral
  neither. Migration marker `voice_defaults_rev` 3 turns an empty language into en-GB once.
  Its Windows checks are in Open (voice item, (aj) onwards).
- Merge record of the branches above (2026-11-28): themed checkboxes v1.2.89 (PR #104,
  4c1a4e7); AI assistant switch and notice dialog v1.2.90 (PR #106, 9467728); the 19
  OK-only boxes moved onto the notice dialog v1.2.91 (PR #107, 74f2871); voice input
  v1.2.92 (PR #108, 4b3ebe3); Right Shift and Toggle v1.2.93 (PR #109, 3bcc8fd); voice
  improvements with review by default v1.2.94 (PR #110, 90fbdbe). Design docs: PR #105
  (ef74f19). Todo notes: PRs #111, #113, #115, #116.
- Live dictation, v1.2.95 (PR #112, 36251ea; PR 1 of live voice): words appear in the AI
  box while speaking, OpenAI `gpt-live-transcribe` and Mistral `voxtral-mini-transcribe-realtime-3`
  over a delay-loaded WinHTTP WebSocket, with a batch fallback; typing is paused while
  recording. Its Windows checks are in Open.
- Conversation mode, v1.2.99 (PR #117, 58dbd00; PR 2 of live voice): a spoken conversation
  with Windows SAPI speech, echo cancellation, ducking, cut-in and local "carry on". Its
  Windows checks are in Open.
- AI speech, v1.3.1 (PR #120, 6b1991d; PR 3 of live voice): spoken replies from Mistral and
  OpenAI-compatible servers, with the Windows speech fallback; the Mistral capture was
  waived (2026-11-26). Closes in code the PR 2 follow-ups marked "fixed in code in PR 3".
  Its Windows checks are in Open.
- Core quick wins, v1.3.2 (PR #122, 4897425): an empty later tool id or name no longer
  overwrites the stored one; Anthropic `type: "error"` stream events are shown; an
  intraword underscore is not emphasis; the telnet auto-login deadline is wrap-safe; the
  Custom provider models URL for a Base URL with no path (`ai_models_url_from_base`).
- Process: the main session runs a plan, token use measured at each retrospective (PR #119,
  1f999b3); one final review per branch, by the model that did not review the tasks
  (PR #121, c03f4e9); Dependabot's actions group (PR #118, f881e01).
- 2026-11-21 retrospective (PR #102, 5b33449;
  `docs/retrospectives/2026-11-21-seven-merges-retrospective.md`).
- 2026-11-28 retrospective (twenty merges, PRs #102 to #122, since marker `be6599f`):
  this compaction moved no Open item whole to Done (the history proves only the parts PR #122
  already struck), updated the counts (4,673 hosted tests, `wintest` 257, `ui_gallery`
  22 x 4, v1.3.2, 93 CRLF files), and added Open "Live dictation follow-ups" from PR #112's
  and #108's bodies. The voice items stay Open: nothing of live voice has run on Windows.
- Housekeeping closed on the process branch (2026-11-28): `vcpkg.json` manifest with the
  `builtin-baseline` of the `~/vcpkg` checkout (e2747d2; the Makefile still reads the
  classic `installed/` directory); `Install-IntegrationRunner.ps1` deleted (9d0cdd1);
  a stream posting during the AI panel's destruction no longer leaks its payload (f2cb01d:
  a failed post frees its payload, and `WM_DESTROY` drains the queued stream messages);
  the integration suite's case count corrected from 84 to 94 (counted from the cases).

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
      an error ending, `cls`/`Clear-Host` capture, `--ui-demo=tool`. #96 (run by the maintainer on Windows on 2026-11-13 and reported fine, *reported*, not
      itemised): the three modes on the four themes (`--ui-demo=thinking`,
      `thinking-compact`, `thinking-open`), a live reply under Automatic (compact then, since 2026-11-21, extended, then
      collapsed), the click cycle and the clicked row keeping its place, the choice
      surviving a restart and an undock and redock, Settings > Automatic resetting it. All:
      `--ui-demo=all` on the four themes and a `ui_gallery` run (20 states x 4 themes).
- [ ] **AI switch / notice dialog / cancellation: nothing run on Windows** (feat/ai-switch).
      (a) Integration harness cases never run (no pwsh on automaton): `notice_dialog_gallery`,
      the rewritten `LAUNCH-2` and `LAUNCH-3`, `launch_upgrade_adds_fields`,
      `menus_open_and_list_items` and its `_ai_on` twin, `settings_help_tool_count` (now
      "1, 84"), `launch_ai_off_no_ai_items`, `self_report_ai_off_clean`, and the AI cases now
      passing `ai_enabled = $true`; run `-Tier gate` and `-Tier ai`. (b) `mingw32-make
      wintest` on Windows: `test_ai_nc_fire_wakes_blocked_winhttp_call` SKIPs under Wine, so a
      close from the UI thread waking a blocked synchronous WinHTTP call is unproven until it
      runs on Windows. (c) Hand checks: upgrading the maintainer's real config (with a key)
      keeps the assistant on and the notice reads well; turning the assistant off mid-reply
      stops at once and closes the conversations; Stop is immediate; exit while a reply
      streams; Refresh Models then close Settings; the notice on Windows 11 (dark title bar,
      per-monitor DPI when dragged between monitors, Narrator reading it, Ctrl+C copies it);
      the tab strip with the AI button hidden; the Help sheet without the AI section;
      Settings' AI controls disabled while the box is unticked.
- [ ] **Voice input: nothing run on Windows** (feat/voice-input; the whole path was built and
      tested here under Wine and the native suite with no microphone). (a) A real microphone
      in Hold and Toggle at Mic start delays 0, 150 and 400. (b) The privacy-blocked path
      (Settings > Privacy & security > Microphone off gives the privacy message). (c)
      Unplugging a USB or Bluetooth mic mid-recording. (d) A Bluetooth headset not dropping to
      hands-free on a Right Shift chord (and a Right Ctrl one, still an option). (e) Right Shift
      chords (capitals, Shift+click, Shift+Insert, Ctrl+Shift+C/V, Shift+arrows) and Right Ctrl chords (Ctrl+C/V, Ctrl+=, Ctrl+wheel,
      Ctrl+letter to the shell) in the terminal and the AI input, docked and undocked. (f)
      AltGr on a European layout. (g) F8 and F9. (h) Alt+Tab and a modal opening
      mid-recording. (i) A real STT round trip on OpenAI, Groq, Mistral, Gemini and a Custom
      base URL. (j) "Use current AI assistant" with each chat provider. (k) The Settings
      device list with two microphones, hot-plug while the page is open, a saved device
      unplugged; Test and the meter. (l) The mic button's pulse and cue on the four themes
      and at 125 % and 200 %. (m) `mingw32-make wintest`, including
      `test_voice_stt_cancel_blocked`'s Windows-only "Cancelled" and timing assertions. (n)
      Harness cases never run (no pwsh): `settings_voice_gallery`,
      `self_report_voice_off_clean`, `voice_off_right_shift_reaches_input`, the updated
      `settings_help_tool_count` (now "1, 1, 110" since PR #112, the arithmetic unverified) and
      `ui_gallery` with its two new states (22 x 4 = 88 captures). (o) A microphone with a
      non-Latin friendly name in the Settings device list: the combo is ANSI, so the name may
      come out as question marks. (p) The mic button's busy look when a card finishes. (q)
      The cheat sheet at 1920x1040 and 96 DPI with voice on: its two extra rows push the page
      past one screen (1013 px against 1001), so it scrolls a little; with voice off (the
      default) it still fits, and `test_cheatsheet_layout_*` measures it that way. Shortening the voice
      row's condition did not change the estimate (still 1013 px), so the real sheet needs a look.
      (r) The upgrade notice on the maintainer's real config shows one "Voice: off until you
      choose to turn it on" line, not the 12 voice sub-settings. (s) Enable voice, Save, reopen
      Settings: the microphones are listed with no restart; turning voice (or the assistant)
      off while recording and while transcribing stops at once, and no transcript arrives
      later. (t) Changing the STT provider on the Voice page moves a default model to the new
      provider's default and keeps a typed one.
      (u) CLOSED in code (feat/voice-conversation, PR 2; hand check pending, see the
      Conversation mode item): Win+L during a Toggle recording now ends it, through
      WTS_SESSION_LOCK on a delay-loaded wtsapi32 (`voice_lock_win.c`, registered only
      while a recording or conversation runs). It used to run to the 60 s cap, because
      locking may not send WM_ACTIVATEAPP. (v) With the microphone blocked in Windows privacy
      settings, an open that returns E_ACCESSDENIED (Activate or Initialize) shows "Windows
      may be blocking the microphone: Settings > Privacy & security > Microphone" (panel and
      Settings > Voice Test); never seen, Wine does not return it.
      The Right Shift default (feat/voice-rshift-toggle) adds: (w) Right Shift on a real
      keyboard opens and stops a recording in Toggle (the new default) and in Hold to talk.
      (x) Capitals typed with Right Shift (terminal and AI input), Shift+click, Shift+Insert,
      Ctrl+Shift+C/V and Shift+arrows never start a recording, and Left Shift never does.
      (y) Ctrl+Shift and Win+Shift+S with the other modifier held first never start one: the
      `GetKeyState` chord path is untested off Windows. (z) Shift+numpad arrows with NumLock
      on (the driver's fake shifts) neither start nor stop a recording. (aa) A 1.2.92
      config with untouched voice settings opens as Right Shift and Toggle with no upgrade
      notice, and one with voice enabled keeps Right Ctrl and Hold to talk.
      (ab) The Settings Voice page on a fresh
      config lists Right Shift and Toggle first and selects them, and the Mic start delay
      field is greyed under Toggle and enabled under Hold to talk. (ac) The renamed harness
      case `voice_off_right_shift_reaches_input` has not been run. (ad) Both Shift keys held
      then released in either order, then a clean Right Shift press works; a capital typed
      with Right Shift during a Toggle recording keeps it recording. Item (u), Win+L during a
      Toggle recording, mattered more once Toggle was the default; it is now fixed in code
      (with auto-send off, the default since feat/voice-review-default, such a
      recording now only fills the box).
      Review by default (feat/voice-review-default) adds: (ae) with "Send automatically when I
      finish speaking" unticked (the default), speak into an empty box: the words appear in
      it, nothing is sent, the caret is at the end and focus is in the box; the cue reads
      "press Right Shift again to finish" and the empty-state hint ends "Your words appear in
      the box for you to review." (af) The same with a typed draft in the box: the words are
      appended after one space, nothing is sent. (ag) With the box ticked: an empty box sends
      as before (approvals unchanged), a draft is appended and not sent, and the cue says "to
      send". (ah) The maintainer's 1.2.93 config (with `voice_review`) opens at 1.2.94 with an
      upgrade notice listing "Voice: send automatically: off, so your words wait in the box
      for you to review", "Voice: microphone gain: 0 dB", and, if its language was empty,
      "Voice: language: English (UK) (choose Auto-detect in Settings > Voice if you speak
      another language)"; the next save drops `voice_review`. (ai) The tick box label fits on the
      Voice page at 100 %, 125 % and 200 %.
      Voice improvements (feat/voice-improvements) adds: (aj) the Microphone gain slider and
      meter with the real mic (HD Pro Webcam C920): the Good zone is reachable by gain alone,
      clipping shows "Too loud — lower the gain", silence shows "Speak to see the level".
      (ak) Test stays on until stopped, and stops on each trigger: the Stop test button, a
      page change, Save, Cancel, unticking Enable voice, unticking the AI assistant, and
      unplugging the mic; a device change restarts it. (al) The slider by keyboard (arrows,
      PgUp/PgDn, Home/End) and mouse, and the gain is live while testing. (am) The header
      status docked and undocked with a long note (for example "Still transcribing the last
      recording"), and its tooltip shows the full text. (an) The model chip is gone and the
      session name is vertically centred (SS_CENTERIMAGE) on Windows 11. (ao) English (UK) on
      OpenAI gpt-transcribe spells "colour" and "organise" and the request is accepted
      (`languages[]` is unverified on the wire). (ap) The Language tooltip shows on hover over
      the dropdown and over the Other box. (aq) The Other flow: pick Other..., the cue
      "2-letter code or name, e.g. cy or Welsh" shows in the empty focused box; type "Welsh"
      to get "Welsh (cy)", Save, reopen shows Other... with cy; type "xx" and the
      not-recognised hint shows and the old language is kept. (ar) The Voice page at 125 %
      and 200 %: every row reachable by scrolling, nothing overlapping. (as) The harness
      `ui_gallery` case (30-ui-demo.ps1, with the new header-status state) has not been run.
- [ ] Voice follow-up (final review, 2026-11-24, M3): after a device-change restart of
      Test, a stale VAE_DEVICE_LOST or level from the old capture can stop or blip the new
      one; fix: a capture generation in VaCmd echoed in VAM_LEVEL and VAM_ERROR.
- [ ] Voice follow-up (final review, 2026-11-24, M5): a 1.2.93 exe reading a 1.2.94 config
      sends en-GB raw as `language`, which OpenAI-format providers reject; an accepted
      older-exe case, noted so it is not rediscovered.
- [ ] Voice, maintainer's test of 1.2.94 (2026-11-24, *reported*, not itemised): review by
      default works (the words wait in the box, but only once the mic is switched off, as
      designed: transcription runs on the whole recording, and live transcription is a
      non-goal of the spec); the gain slider and meter look good (aj); the header status
      works (am); the Language dropdown looks good. The rest of the voice checks above stay
      open.
- [ ] Review the AI panel's header status (feat/voice-improvements, v1.2.94) at a later date:
      it works, but the maintainer does not like it (2026-11-24). Design to be revisited; no
      direction given yet.
- [ ] Settings > Voice: the Push-to-talk key dropdown needs an "Other..." choice in the same
      style as the Language dropdown (maintainer, 2026-11-24): pick Other..., a box with a cue
      for the expected format, a hover tooltip saying how to use it, and a not-recognised hint
      that keeps the old key.
- [ ] Windows hand check: Extended thinking mode (25 lines) streams, follows the tail,
      cycles collapsed→compact→extended→open, and Settings 'Always extended' persists
      (PR #101; also: a reply completing under Automatic while scrolled up keeps the text
      being read in place, and `--ui-demo=thinking-extended` on the four themes).
- [ ] Windows hand check: themed checkboxes (feat/checkbox-style, v1.2.89; seen only under
      Wine at 96 DPI). Settings (every page with a checkbox) and the password prompt on the
      four themes; at 125 % and 200 % and dragged to a monitor of a different scale; no
      frame of the native box flashing on click, focus change or Space; the keyboard focus
      ring on a checked box; no label ellipsised (Debugging's "Keep recent detail for issue
      marks, at" row was widened to 300 px on an estimate).
- [ ] **Live dictation (PR 1 of live voice, feat/voice-live-dictation): nothing run on
      Windows.** Built and tested here under Wine and the native suite with no microphone and
      no live provider. (a) Live text with OpenAI `gpt-live-transcribe` (the maintainer's
      provider): words appear while speaking, provisional words settle when each segment
      completes, English (UK) spelling ("colour", "organise") with the British prompt, and
      the 24 kHz audio sounds right to the model (no garbled words). (b) The same with Mistral
      `voxtral-mini-transcribe-realtime-3`. (c) Typing paused while recording and until the
      last words arrive (typed keys, paste, cut, Backspace, Delete, undo, an IME); Ctrl+C
      still copies; clicks in the box do not move where words go. (d) Send or Enter while
      recording: the mic stops, the final words arrive, then it sends; a slow final gives
      "The last words did not arrive..." and nothing is sent. (e) Escape with the AI panel
      focused removes the recording's words and keeps the draft; Escape in the terminal goes
      to the terminal (vi, `less`) and the recording carries on. (f) Alt+Tab and a modal
      mid-recording keep the words and send nothing, even with auto-send on. (g) A tab
      change and New chat mid-recording: words kept, nothing sent. (h) Saving Settings with a
      voice setting changed ends the recording at once. (i) The connection pulled
      mid-recording (Wi-Fi off): "Live text stopped...", then the batch fallback replaces the
      live text when the mic stops; the fallback failing too keeps the live text with its
      chat note. (j) A live key rejected (HTTP 401/403) and a wrong model (Mistral 403): the
      chat note, and the batch fallback at stop. (k) A 5-minute recording with long pauses:
      the session survives the idle gaps (neither provider documents an idle limit). (l) A
      10-minute recording reaching the cap, with the fallback forced: OpenAI's 25 MB limit is
      documented, Mistral's batch size limit is not. (m) `mingw32-make wintest` on Windows:
      `test_voice_ws_shutdown_wakes_receiver`, `test_voice_ws_cancel_during_handshake` and
      `test_voice_live_close_mid_handshake` take Wine skips, so a WebSocket close waking a
      blocked receive and a handle close aborting a blocked handshake are unproven until they
      run there. (n) `mingw32-make test` and `mingw32-make clean && mingw32-make release` on
      the Windows box (newer gcc and MSYS2 headers, live spec §7.5). (o) The Live model row
      and the "Live text as you speak needs OpenAI or Mistral" note on the Voice page for each
      provider and with "Use current AI assistant". (p) The upgrade notice line "Voice: live
      speech-to-text model". (q) The integration harness had no live-dictation case: the
      WAV-injection seam came in PR #117 (103e18d) and the case `voice_live_dictation_fixture`
      is in the Conversation mode item's (n), never run.
- [ ] **Conversation mode (PR 2 of live voice, feat/voice-conversation): nothing run on
      Windows.** Built and tested here under Wine and the native suite (4519 tests, 170
      `wintest`) with no microphone, no working SAPI and no real audio device; live spec
      §16 "Found at build time (PR 2)" says what each fact rests on. Each line is its own
      check.
      (a) Echo cancellation: what the effects manager reports on the maintainer's devices
      (the two interface IIDs come from the windows-rs 0.58.0 metadata mirror, a wrong one
      fails safe to half-duplex), the render-endpoint reference being set, and a reply on
      speakers not transcribed back.
      (b) Ducking: other apps' audio lowered during a conversation, with the Communications
      setting at its default and at "Do nothing", and with a capture device that is not the
      default communications device; whether Nutshell's own speech is lowered (the opt-out
      `SetDuckingPreference` is built, never run).
      (c) Cutting in on speakers and on a headset: a cough does not stop a reply; the key
      stops speech and keeps the conversation; "carry on" and "say that again" are handled
      locally; the detector numbers (12 dB margin, 15 dB impulse decay, 300 ms un-duck)
      against real speech and real slams.
      (d) Voices: the default on a PC with and without the English (UK) pack; which en-GB
      voices are listed; the notice dialog; a OneCore voice speaking (the dedupe keeps the
      SAPI token over a OneCore one of the same name, so this is only reached for a
      OneCore-only voice).
      (e) A Bluetooth headset in a conversation (the communications category may move it
      to hands-free, with worse sound).
      (f) A full conversation with the free-space question and its approval card: the mic
      indicator off while the card waits; the round cap needing an on-screen approval after
      voice turns; Alt+Tab and Settings pausing; Win+L; 3 minutes without a turn; the
      self-report and the SAPI control.
      (g) The SAPI speak path: word offsets, end of reply, purge latency, volume (duck)
      mid-reply, SetVoice with a OneCore token, the notify event's reset mode.
      (h) Task 13's Win32 routing, which no `wintest` links: the key and the mic button
      starting and ending a conversation, Escape, the Send/Stop caption, the pause while a
      dialog is open, lock, panel hide docked and floating, the mic pulse following the
      level, `WM_ACTIVATEAPP` timing (the hint is passed from the message), the Settings
      Test meter working while a conversation is paused by the dialog, and the draft cue
      while typing in Listening.
      (i) The Settings > Voice layout (`--ui-demo=settings-voice`: Mode, Pause, Windows
      voice, wrapped notes, the dim "seconds" hint) at 100 %, 125 % and 200 %, and Play
      sample (also: it does nothing after Enable voice and Conversation are ticked but not
      yet saved).
      (j) The push-to-talk key state after a lock during a batch dictation recording: the
      next press starts a recording (the fix is `ptt_end_capture` in `post_step`).
      (k) Half-duplex priming: user speech in the first 500 ms of a reply lifts the echo
      estimate; how long a cut-in is blocked, and a reply jumping 10 dB louder cutting
      with no user.
      (l) `mingw32-make test` and `mingw32-make clean && mingw32-make release` on the
      Windows box (newer gcc and MSYS2 headers; the copied echo cancellation declarations
      are guarded by `#ifndef`).
      (m) The SAPI self-report control (`--self-report-sapi`) reporting `sapi-control:
      held` and both `sapi.dll` lines; under Wine it passes on the DLL lines with
      `sapi-control: unavailable` (SetInterest and SetNotifyWin32Event return E_NOTIMPL).
      (n) The two harness cases written and never parsed or run, `voice_live_dictation_fixture`
      and `voice_conversation_card_fixture` (`97-voice.ps1`, ai tier, need
      `nutshell-it.exe` from `make harness-exe` and `NUTSHELL_IT_STT_KEY` besides the model
      key); likely first fixes: `GetNewClosure()` with the `$Exe` override, the
      `SpeechAudioFormatInfo` overload, reading the Send button text, the mic click as
      `WM_COMMAND`. The conversation case does not assert the command ran.
      (o) Follow-ups noted in the build, not built: Play sample has no help entry [fixed in code in PR 3; hand check pending]; a
      notice raised for a launch is lost if the conversation ends before the handler runs [fixed in code in PR 3; hand check pending];
      the echo estimate's and detector's deferred minors are in the live spec §16.
      (p) From the final whole-branch review (2026-11-25), not built, no code: keeping the
      British-voice notice open for more than 30 s loses the held reply (the first reply of a
      first conversation); the cheat sheet's width check skips the `SC_VOICE` rows and the
      voice row is now much longer, so eyeball `--ui-demo=shortcuts`; `test_vspeak_long_reply`
      checks chunk sizes only and cannot detect non-linear time; `test_vconv_win_off_mid_reply`
      uses the fake ops table, so the real speech thread's join on voice off is a hand check.
      The final review's fixes (a cut-in's turn, the watchdogs, an RDP or console disconnect
      counting as a lock, the posted resume after a dialog, the key with focus outside the
      panel, a changed chat, full volume after a cut-in) each want a Windows hand check too.
      (q) Follow-ups from the re-review of those fixes, not built: a spoken reply longer than
      10 minutes is cut silently by the Speaking watchdog ("continue" resumes it; refreshing
      the clock on each spoken word would avoid it) [fixed in code in PR 3; hand check pending]; a Hearing or Ending watchdog expiry
      restarts the 3-minute silence clock, against the rule that a failed turn must not, so
      with uploads hanging a noisy room can keep a conversation alive [fixed in code in PR 3; hand check pending]; a list item or
      indented line that starts with a one-line triple-backtick span opens a fence and the
      rest of the reply is not read (fails closed); after a pause cuts an approval
      announcement the header returns to "Working..." although a card waits, and the
      announcement is not repeated; Settings is destroyed before its owner is re-enabled, so
      Windows may activate another application for a moment and the conversation would show
      "Paused ... in the background" until the user clicks back (unverified, hand check); the
      note shown when an RDP or console disconnect ends recording still reads "Windows was
      locked".
      (r) From the two Opus reviews of the whole branch (2026-11-25), not built: Retry (and any
      other send that does not go through `voice_send_pressed`) leaves a running conversation
      Listening with the microphone open while the chat works, and its reply is not spoken
      (spec 5.2 closes the capture while Working): it needs an "external send" event for the
      controller (Listening to Working, capture closed, the reply baseline taken), not a
      patch [fixed in code in PR 3; hand check pending]; Enter while a reply is being spoken stops the speech instead of sending the typed
      text (Send shows Stop), against spec 5.1 "sent with Enter as normal": the maintainer
      decides; `voice_audio_conv_end` and `voice_audio_conv_mode` go through `va_push_raw`,
      which drops the command if `realloc` fails, so under memory exhaustion the glue believes
      the capture closed while the device stays open (a close that cannot fail: a token field
      under the queue lock) [fixed in code in PR 3; hand check pending]; the glue's event queue drops events past 24 silently, a `VCE_END`
      included (not reachable today, at most about 3 per step) [fixed in code in PR 3; hand check pending]; hand checks: the ducking
      opt-out is set on the default render session after the capture client is initialised
      (Microsoft's guidance applies it to the communications stream's session before it
      opens), so do other apps and the reply duck?; the speech thread's CPU during an idle
      conversation (the SAPI notify event's reset mode); whether trimming the chat's message
      list between a send and its reply can make the conversation skip the reply; the failed
      post of a conversation turn (`va_post` now frees it) has no test, the seam cannot make
      `PostMessage` fail.
      (s) From the Opus re-review of those fixes (2026-11-25), not built: the "I couldn't
      send that" phrase after a refused turn still restarts the 3-minute silence clock
      (`on_sent` reaches Speaking through `enter_speaking`, which clears `local_speech`), so
      with the chat full a noisy room keeps the microphone open; set `local_speech = 1` on
      the refusal path [fixed in code in PR 3; hand check pending]. A local repeat that is paused and resumed also restarts it [fixed in code in PR 3; hand check pending].
      `voice_echo.c:204` and `:208` compare tick counts without a wrap-safe subtraction (a
      conversation spanning the 49.7-day wrap only) [fixed in code in PR 3; hand check pending].
- [ ] **AI speech (PR 3 of live voice): nothing run on Windows.** Built and tested here under
      Wine and the native suite with no real audio device and no live provider; the Mistral
      capture (plan Task 0) was waived by the maintainer on 2026-11-26, so the parser, built
      against the SDK shape with provisional fixtures, is first proven by these hand checks.
      Each line is its own check.
      (a) Mistral `voice_1787` and the other English (UK) voices: accent, time to the first
      word, no gap or click between chunks, the duck, stop latency.
      (b) Kokoro-FastAPI with `bf_emma`, and with no key.
      (c) Shared, separate and rebound keys; the DPAPI blob and its binding.
      (d) The passing fallback: Wi-Fi pulled mid-reply.
      (e) The once-per-conversation note: a wrong key, a wrong model, a 22,050 Hz server.
      (f) "Carry on" after a cut-in (R9).
      (g) Headphones unplugged, and a default-device change.
      (h) Echo cancellation with our render client, and whether our render session is ducked.
      (i) The cost on Mistral's usage page.
      (j) Play sample with unsaved values.
      (k) The page at 100 %, 125 % and 200 %, with the privacy line and the reseeding.
      (l) `mingw32-make test` and `mingw32-make clean && mingw32-make release` on the Windows
      box.
      (m) Retry during a conversation.
      (n) A hung upload ending the conversation at its deadline.
      (o) The real WASAPI render path (Initialize, GetBuffer, padding, PLAY_DONE) has run
      nowhere: under Wine `test_voice_render_real_wine` gets VAE_NO_DEVICE.
      (p) On Windows, `test_ai_stream_ex_cancel_hang` and `test_voice_ais_quit_while_server_hangs`
      must pass (they skip under Wine); for the latter only `VRES_SPEECH_REQ == 0`
      discriminates.
      (q) Inspection item: R36's order in `src/ui/voice_win.c`, `voice_ais_quit_all(3000)`
      before `voice_audio_stop` at both call sites (the only two); `voice_win.c` is not in the
      wintest build.
      (r) A conversation started with a held-only legacy command card on screen (every entry
      APPROVE_BLOCKED; `cmd_batch_finished` counts BLOCKED as finished,
      `cmd_batch.c:133-139`, so `busy_of` is 0 and the start is allowed): raise the ceiling
      (`apply_policy_change` -> `chat_approval_unblock_all`, `ai_chat_status.c:56-59`), click
      Run (`ai_chat.c:1002-1030`), and `ai_chat_send_continue_message`
      (`ai_chat_send.c:739-771`) launches a turn the conversation never sees; it stays in
      Listening and the reply is not spoken. Should: Working on the launch, or a held-only
      card blocks the start. Follow-up.
      (s) A failed Play sample with a wrong key shows `Could not play the sample: the speech
      provider refused the key (HTTP 401)` in the note under the meter (v1.3.1).
      (t) The speech-to-text upload now refuses redirects ("Redirect refused (HTTP n)"): check
      each built-in provider live.
      (u) A short one-sentence reply right after the PC wakes or the output device changes: the
      reply must be heard before the header returns to Listening, and Stop must still stop it.
      The done guard (`src/ui/voice_aispeech_win.c:797-826`) is armed at the worker's exit
      whether or not playback has begun, and does not flush; a render endpoint that takes more
      than 2 s to open would post `VTT_DONE` first. Cannot happen under Wine. Follow-up if
      seen: re-arm while audio is queued, flush when the guard ends a reply.
      (v) Save Settings with a Custom speech URL near 248 bytes and a long key: the save must
      succeed. The bound-key blob is about 1,019 of `CFG_BLOB_MAX` 1,024 at maximum lengths
      (`src/config/loader.c:347-365`); a real DPAPI blob one block larger would abort the whole
      save.
      (w) If Mistral answers `response_format: "wav"` with raw PCM in its first delta, the first
      reply gives "AI speech is off for this conversation: unsupported audio format" (before
      v1.3.1 every reply failed in passing). That is the capture's question (R25) answered by
      ear: report it and the fix is the `pcm` path.
- [ ] Follow-ups from the final review of AI speech, not built:
      a valid WAV `data` chunk size is ignored (R25), so a finite WAV with a trailing LIST or
      id3 chunk has the trailer played as PCM and an odd trailer length fails the reply after it
      was heard (`src/core/voice_wavstream.c:88-97`; spec section 8.4 says only 0 and
      0xFFFFFFFF mean "until the end").
      The typed-key decision in `src/ui/settings.c` (`voice_tts_key_touched`,
      `voice_tts_key_bound`, the reset in `voice_page_read`) has no test; move it to a pure
      function in `src/core/voice_aispeech.c`.
      `The speech Base URL is too long` also refuses a keyless Custom server, or one sharing the
      speech-to-text key, where the binding is never read (`src/core/voice_aispeech.c:155-161`):
      refuse only when the speech key is its own and one is typed.
      `settings_voice_sample_failed` (`src/ui/settings.c:1539`) drops the failure when another
      Settings page is showing; drop the `cur_page` condition.
      The redirect reason says "check the Base URL" for Mistral too, which has no Base URL row
      (`src/core/voice_aispeech.c:321-322`).
      `http://host?x=a@b` passes the authority check but `parse_url`
      (`src/ui/ai_http_win.c:36-46`) ends the host only at `:` or `/`, so the connect fails;
      refuse `?` and `#` in a base, and correct the comment at `src/core/voice_aispeech.c:71-73`.
      `test_voice_ais_sse_error_event`'s first half would pass with the error event ignored;
      split or rename it. Only the native parser tests cover the error event.
      The 5-minute cap counts decoded frames, so a Mistral stream that never ends and carries no
      audio stays open until the user stops it or the 10-minute Speaking watchdog fires.
- [ ] Live dictation follow-ups (PR #112, parked minors; nothing run on Windows): an error
      followed by a ready timeout gives two chat lines; a READY and the 10 s timeout landing
      at the same moment shows "stopped" instead of "unavailable"; teardown can stall the UI
      for up to 1 s if slow; the box-changed check is skipped for the fallback and Escape
      writes (typing is locked then); Shift+Insert is caught as "box changed" (a hand
      check); messages queued when the window is destroyed are not wiped; the glue has no
      automated test, so only the hand checks cover it. From PR #108, not built: the live
      level bar under the input; Gemini's default model `gemini-2.5-flash` is unconfirmed
      on Google's page.
- [ ] **The notice move (feat/notice-msgboxes, 2026-11-23), harness cases changed and
      unrun** (no `pwsh` on `automaton`): `ai_panel_docks_with_key` and
      `ai_panel_opens_without_key` (20-ai.ps1) now look for a `#32770` or a
      `Nutshell_Notice` when asserting no dialog appeared; the telnet cases
      `telnet_port_refused_reports_in_tab` and `ssh_password_box_cancel_reports_in_tab`
      (85-telnet.ps1) only had wording changed, their
      title match still holds; `NutshellIT.psm1`'s class comment and README's
      `Wait-NutshellDialog` row mention `Nutshell_Notice`. Run the `gate` tier on Windows.
- [ ] **Windows hand check of the 19 notices** (the notice move, feat/notice-msgboxes): Close
      Tab, Tab Limit, Reconnect, Debug Log Folder (two), Session Not Found, Connection
      Error, Edit, Delete, Save, Connect (Session Manager), Open Log Folder, Refresh Models
      (four), the two open-folder refusals and Save Error (AI panel). For each: the right
      owner (centred on it, the owner disabled while it is up), the tone (icon and accent),
      the text unchanged and not clipped on four themes, Enter and Escape both close it,
      focus returns to the owner. The docked AI panel's Save Error notice must disable the
      main window, not only the panel.
- [ ] **Notice alert sound and activation, on Windows** (the notice move, PR #107; Wine
      shows neither): each notice plays the system sound for its tone (information or
      warning, as `MessageBox` did) once, launch notices included; and closing a notice
      leaves Nutshell in front, not another application, both for a single notice and for
      two nested ones (e.g. two tabs failing at once). Also: a notice raised while the main
      window is minimized appears centred on its restored position, with a taskbar on the
      top or left edge.
- [ ] OpenAI's batch speech-to-text models `gpt-4o-mini-transcribe` (the default,
      `src/core/voice_stt.c:14`), `gpt-4o-transcribe`, `whisper-1` and `-diarize` shut down on
      2027-02-26, replaced by `gpt-transcribe` or `gpt-live-transcribe`; the
      `-2025-03-20` snapshot shuts down on 2027-01-20. Source:
      `developers.openai.com/api/docs/deprecations.md`, 2026-11-25. Change them in their own
      pull request. Also later: Groq Orpheus once it leaves Preview; resampling for Custom
      servers.
- [ ] SSH: a resize during the connect (maintainer's issue mark, 2026-11-25) is fixed in
      v1.3.4: the current size is sent when the channel is adopted, and the poll keeps the
      remote pty at the terminal's size, so a request that got EAGAIN is finished and never
      lost. Left: the harness case that resizes mid-connect and reads `stty size` is not
      written (no existing case gives the pattern); the hand check is in
      `docs/test-log/v1.3.4-session-io.md`.
- [ ] `ssh_channel_write` on a stalled peer: the 5 s `SO_SNDTIMEO` it sets limits nothing,
      because libssh2 makes the socket non-blocking at the handshake, so a blocking write
      waits in `poll` without a limit and the UI thread with it (reasoned from the libssh2
      1.11.1 source in the review of PR #126's branch, not reproduced). The earlier note
      "up to 5 s per write" is wrong.
- [ ] The AI assistant cannot run queued commands while the session is inside tmux; they
      run only after tmux is closed (maintainer, 2026-11-25, *reported*, not investigated).
- [ ] AI panel: find a way to stop the conversation becoming full (maintainer, 2026-11-25,
      with a screenshot: "[conversation full: 1 tool call(s) not run; start a New Chat]" at
      23.8k of 1000k tokens, mid-task, the model's last tool call dropped). The limit is the
      message count, not the context: `AiConversation.messages` is a fixed array of
      `AI_MAX_MESSAGES` (64, `src/core/ai_prompt.h`), and every guard in
      `ai_conv_guard.h` and `ai_agentic.h` counts slots against it. Not designed yet;
      candidates are a growable message list, or compacting or dropping the oldest turns
      when the slots run low. `thinking_history` (`ai_chat_internal.h`) is sized by the
      same constant.
- [ ] Thinking view: each mode (collapsed, compact, extended, open) needs its own colour as
      it cycles; two of them are both green today (maintainer, 2026-11-25).
- [ ] Thinking view: when a reasoning stream has ended and a new one begins, minimise the
      earlier one so it stops taking screen space; streaming the reasoning live stays as it
      is. The user can reopen an earlier stream to read it, and a thinking box opened by
      hand stays open until it is closed by hand (maintainer, 2026-11-25).
- [ ] Thinking view: while the reasoning is streaming, scrolling with the mouse wheel, and
      the equivalent keystrokes, should trigger smart-scroll (maintainer, 2026-11-25,
      *reported*, not investigated). Read here as: a scroll up stops the view following the
      tail and holds the reader's place, and returning to the bottom resumes following, the
      rule `src/core/stick_scroll.c` already gives the chat list and the terminal; confirm
      that reading with the maintainer, and which of the box and the chat list it covers,
      before building.
- [ ] Thinking row follow-ups (PR #96; the spec's "Flagged to the maintainer", to confirm):
      under Automatic a click on a streaming row goes extended to open (the next mode in
      the cycle; compact to open before 2026-11-21), so hiding the text takes two clicks; Save under Automatic never includes
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
- [ ] Tool-gate follow-ups (PR #91): a pending call in a reply cut off at the output limit
      is not run; DeepSeek's reasoner may need the reasoning pass-back on tool loops; Kimi's
      reasoning on plain replies is not kept; history carries no tool turns (spec non-goal);
      the four v1.2.74 review leftovers (a font left selected in one header paint path, the
      status dot's size written twice, the rounds note's spare row, two "web_fetch is off"
      rows in one round). From PR #88: NX-OS and ASA `do CMD` should use their own tables; a
      sensitive row in `--ui-demo`; blank class reasons (`sh -c: `).
- [ ] Terminal escapes (PR #94's nano work): replay vim, htop, mc and less against tmux as
      nano was. Since v1.3.3 the tab stops (HT, CHT `CSI I`, CBT `CSI Z`, HTS, TBC), auto-wrap
      inside a scroll region and the alternate screen keeping the primary's scroll region are
      done. Left: `ESC c` (RIS) resets only the tab stops; a full reset also clears the
      screen, the attributes, the modes, the scroll region and the cursor.
- [ ] Terminal attributes (found 2026-11-12 while checking this list): underline is drawn
      since v1.3.3 (Windows hand check not run: `printf '\033[4munderlined\033[0m plain\n'`
      at 100 % and 150 % scaling, on a line with a missing glyph too, and a line under
      bold and under reverse video). Blink (`TERM_ATTR_BLINK`) is parsed and not drawn,
      though the in-app guide lists it; the OSC 0/2 window title is parsed into
      `term->title` and nothing reads it, so it is never shown (a tab title or the window
      caption could).
- [ ] Settings follow-ups (PR #93 and its spec's non-goals): keyboard path to the help (Tab
      order, F1); `WM_DPICHANGED` in Settings and `WM_GETMINMAXINFO` on mixed scaling; at
      the minimum width the widest Debugging row overflows; hover help for the profile
      editor; a generated README settings block (*reported*); the automatic log opened at
      connect (`open_session_log`) still fails silently (Start Logging tells the user since
      v1.3.4).
- [ ] Other, from the PR #94 session (*reported*): resize cost with a full history (PR #93
      measured 14 ms at 10,000 lines and 66 ms at 50,000 for 200 columns); a user-supplied
      issue log can hold a typed secret, so `term.rx`/`term.key` payloads are never printed
      and tests use synthetic text.
- [ ] web_search does not URL-encode the query (`src/core/ai_tool_web_search.c`, all four
      URL builders): a query with spaces, `&`, `#` or `+` reaches DuckDuckGo or the custom
      URL mangled; found 2026-11-13.
- [ ] web_fetch: a watchdog that closes the request handle at hop end or on cancel; one
      hop can still exceed its timeout against a trickling server (the final review of
      the guard rails branch; the header wait is bounded, the body loop checks the hop's
      clock only between reads).
- [ ] winhttp_open: fail closed when TLS 1.2 cannot be enforced (today the session keeps
      the OS defaults if the TLS 1.2-only retry is refused).
- [ ] `ssh_channel_write` blocks the UI thread for up to its 5 s send timeout per write
      when the remote window is full (found by PR #79, left alone). The same class of
      symptom as #79's lost input: keys stop while the UI thread waits. Move SSH writes off
      the UI thread or make them non-blocking with a queue. Also from #79 (*reported*): a
      "Mark Issue Ctrl+Shift+M" tooltip drawn over terminal row 2 in the maintainer's
      screenshot, not investigated.
- [ ] Telnet follow-ups (PR #81): `tests/integration/cases/85-telnet.ps1` (five cases) has
      never been parsed or run -- no `pwsh` on `automaton`, so the Windows box is the first
      place it can run (`-Only telnet_autologin_never_logs_password,...`); a stray `IAC SB`
      with no `IAC SE` swallows later output (PuTTY does the same); the minor test gaps in
      #81's final review. From PR #80: no Wine test separates "mask after every edit
      message" from a message whitelist.
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
      shipped in PR #93): Alt+drag block selection (mouse reporting to applications: see
      "Local shell follow-ups").
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
      `thinking-open` (PR #96), `thinking-extended` (PR #101). The list-view `wintest` cases
      added since PR #93 (the `test_chat_select_win_thinking_*` cases, six of them after
      PR #101's `extended_box` and `mode_change_holds_reader_place`, and
      `test_settings_label_win_labels_fit_column`) have no recorded run on Windows, only
      under Wine.
- [ ] No release since v1.1.21 (2026-09-14); `main` is at v1.3.2, which adds telnet, the
      password eye, keyboard-interactive auth, the tool gate, `run_command`, the Thinking
      row modes (four since PR #101), the AI switch, the notice dialog and voice (dictation,
      conversation mode, AI speech) since then.
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
      `src/ui` is never scanned (since PR #81 that includes the telnet socket backend
      `src/ui/telnet_io.c`) -- consider a Windows CodeQL run; `.gitattributes` exists
      but holds only `tests/fixtures/*.bin binary`, so add eol rules and renormalise the
      93 CRLF-indexed files (2026-11-28) in a lone PR when nothing else is in flight;
      mark `tests/fuzz/corpus/**` binary first, since eight seeds there carry CR bytes as
      test input. (Untracking `build/win/nutshell.exe` is off the list: the gate
      now relies on the committed exe being built from `APP_VERSION`.)
- [ ] Harness batches B (sessions, auth, host key, connection), C (terminal, clipboard,
      settings), D (AI extras) — 31 cases planned; the suite has since grown to 94 cases, so reconcile the plan with what exists first; `bvtuser` exists on tompi for auth cases.
      Manual runs only now; still worth having for release checks.
- [ ] Security audit follow-ups (`2026-09-09-security-audit.md`, kept local; C2 and H2
      landed in PR #24, H3/H4 in PR #54, H5/H6 in v1.2.14 -- DPAPI with the config
      kept beside the exe, absolute save path): H7/H8 window-message hardening;
      release and supply chain (exe tied to source, tag ruleset, immutable releases,
      provenance, secret scanning -- the settings changes need the maintainer's
      say-so; PR #87 added the release checksum); then Medium and Low. The web_fetch
      destination filter shipped in PR #85.
- [ ] Older review findings: Session Manager phantom row; a stopped AI reply leaves
      two user turns in a row in the conversation.
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
      path). Fixed in v1.3.4: a background session's dirty row no longer makes the timer
      invalidate the window at 60 fps, and the cooldown tick is per session. One weakness
      the REDRAW_DEBUG log exposed remains: (b) the poll path calls
      `dispbuf_invalidate()` without `force_all_rows`, so a shadow-invalidating repaint
      cannot rescue a row already marked clean — consider making `dispbuf_invalidate`
      also mark all rows dirty, or routing it through `full_redraw_needed`. Also noted by
      the review: the poll
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
