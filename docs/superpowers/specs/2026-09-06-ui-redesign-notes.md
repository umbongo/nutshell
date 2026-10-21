# UI Redesign — Working Notes (checkpoint)

**Status:** sub-projects 1 (design-system foundation) and 2 (AI Assist panel)
landed at v1.0.92/v1.0.96 (see "Landed since v1.0.96" below for what's
shipped since — approval-card fixes, prompt-gated dispatch, four-level auto
approve, markdown tables, role colour, smart scrolling, and pending command
batches). Native suite: **3,035 tests**, 0 failures (Linux, 2026-10-19). Integration suite
(`tests/integration/Run-Integration.ps1`): 49 case definitions in ten case files
(`cases\10-terminal` … `95-ai-select`), manual only since 2026-09-15, DPI-aware since
PR #71; `ui_gallery` captures 10 `--ui-demo` states x 4 themes = 40 images. Next action is sub-project 3 (main window
chrome: single toolbar replacing the menu bar, tab strip, status). Resume
from "Todo" below.

Branch: `main` — the redesign branch `ui-polish` was renamed to `main` on 2026-09-07 (the pre-redesign main was kept as branch `v1.0.76`). Now at v1.2.44 (2026-10-20).

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
- ConPTY resize probe run, 2026-10-21 (branch `conpty/probe-report`, fixtures 89c68d4):
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

Open, in priority order:
- [ ] Parser: a truncated extended-colour SGR (`38;2;r;g`, `38;5` without its index, or
      one cut by the 16-parameter cap) leaks its trailing parameters as attributes (`2` ->
      dim on the light themes, `5` -> blink, `1;4` -> bold/underline); malformed input
      only; consume the remaining parameters as xterm does.
- [ ] Designed light-theme ANSI palette (follow-up to the contrast floor, maintainer's
      decision 3 of 2026-10-19): on Onyx Light and Moss & Mist the floor makes normal and
      bright colours converge (90 and 97 both paint `6C6C6C`) and flattens the 232-255 grey
      ramp; a hand-designed 16-colour palette per light theme would restore the distinctions.
      Also from that spec's Out of scope: answer OSC 11 (background colour query) so programs
      can detect a light background; default text on an explicit background stays raw.
- [ ] Terminal selection follow-ups (spec `2026-10-20-putty-selection-design.md` section 4;
      the selection itself shipped in PR #74): Shift+click to
      extend; Alt+drag block selection; middle-button paste; mouse reporting to
      applications; the per-byte paste writes over SSH; the scrollback-size setting
      (Settings and the help guide offer 100-50,000 lines but every session gets 3,000;
      `term_init` and `term_reflow_buffer` allocate the whole capacity, so lazy
      allocation in reflow comes first, spec section 6.2); a narrowing reflow of a full
      ring drops the newest rows rather than the oldest (`buffer.c:142, 165`,
      pre-existing). Minor residuals from #74's reviews: a fourth rapid click selects a
      word again where PuTTY cycles; decomposed accents split a word; the help guide's
      Send Key paragraph still lists F1-F12 beside the new "F1-F12 go to the shell" line.
- [ ] Top-anchored reflow for ConPTY sessions, slices 3-6 of
      `2026-10-19-conpty-resize-probe-and-reflow.md` (top mode and the reconnect push,
      the 100 ms throttle with its starved-timer check, DPI reorder, logging, the
      integration case). The 2026-10-21 probe confirmed spec section 1 as written and
      ruled out the quirk flag, so this is unblocked; until it ships a grow still
      strands the prompt mid-screen between the resize and conhost's repaint.
- [ ] Harness cases on `main`: `resize_applies_to_inactive_tab` fails on every run
      (since v1.2.13); `ctrl_w_reaches_shell_and_shift_closes_tab` failed the v1.2.36 and
      v1.2.37 gate runs and passed the v1.2.41 one, so it is intermittent -- both are
      tab-switch cases; investigate together (tab B never reports its grid).
- [ ] Six CodeQL alerts open on `main`, none new code: #27 `cpp/offset-use-before-range-check`
      at `src/core/markdown.h:62` is alert #12 (open since 2026-04-07 at `src/ui/markdown.h`)
      renumbered when PR #71 moved the parser (56f0805) -- `line[level] == '#' && level < 6`
      reads before the bound; swapping the operands closes it. The other five are
      `cpp/world-writable-file-creation` (`loader.c:935`, four in `test_settings.c`).
      Fix or dismiss each with a reason, in the next `src/` pull request.
- [ ] No release since v1.1.21 (2026-09-14); `main` is at v1.2.44. Tagging is the
      maintainer's call (`feature-workflow` step 10).
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
      (`window.c` and `ai_chat.c` size, `src/ui` logic that belongs in `src/core`).
      Findings ranked by severity, each answered in writing; folds in the open H3-H8
      items below. Known classifier holes to start from (PR #50 review, 2026-09-23):
      `env rm -rf /`, `perl -e "unlink ..."`, `echo $(rm -rf /)` and backtick
      substitution classify READ on Linux and unresolved platforms (wrappers and
      command substitution are not looked through); no PowerShell ruleset, so
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
      `src/ui` is never scanned -- consider a Windows CodeQL run; `.gitattributes` exists
      but holds only `tests/fixtures/*.bin binary`, so add eol rules and renormalise the
      85 CRLF-indexed files in a lone PR when nothing else is in flight. (Untracking `build/win/nutshell.exe` is off the list: the gate
      now relies on the committed exe being built from `APP_VERSION`.)
- [ ] Harness batches B (sessions, auth, host key, connection), C (terminal, clipboard,
      settings), D (AI extras) — 31 cases planned; the suite has since grown to 49 case
      definitions, so reconcile the plan with what exists first; `bvtuser` exists on tompi for auth cases.
      Manual runs only now; still worth having for release checks.
- [ ] Security audit follow-ups (`2026-09-09-security-audit.md`, kept local; C2 and H2
      landed in PR #24, H3/H4 in PR #54, H5/H6 in v1.2.14 -- DPAPI with the config
      kept beside the exe, absolute save path): H7/H8 window-message hardening;
      release and supply chain (exe tied to source, tag ruleset, immutable releases,
      provenance, secret scanning -- the settings changes need the maintainer's
      say-so); web_fetch destination filter; then Medium and Low.
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
      move (found 2026-10-18 during the Auto-approve review).
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
(CodeQL is not), so `gh pr checks N --watch` until every check is green first. Run the harness tier a change touches by hand first. "Create a checkpoint" = compact this
list, compact memory, delete temp files and worktrees, leave the tree ready for a new
session.
