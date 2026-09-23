# UI Redesign — Working Notes (checkpoint)

**Status:** sub-projects 1 (design-system foundation) and 2 (AI Assist panel)
landed at v1.0.92/v1.0.96 (see "Landed since v1.0.96" below for what's
shipped since — approval-card fixes, prompt-gated dispatch, four-level auto
approve, markdown tables, role colour, smart scrolling, and pending command
batches). Native suite: **1,960 tests**, 0 failures (2026-09-21). Integration suite
(`tests/integration/Run-Integration.ps1`): 30 cases in seven case files
(`cases\10-terminal` … `70-cli`), manual only since 2026-09-15; `ui_gallery`
captures 10 `--ui-demo` states x 4 themes = 40 images. Next action is sub-project 3 (main window
chrome: single toolbar replacing the menu bar, tab strip, status). Resume
from "Todo" below.

Branch: `main` — the redesign branch `ui-polish` was renamed to `main` on 2026-09-07 (the pre-redesign main was kept as branch `v1.0.76`). Now at v1.1.23.

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

Open, in priority order:
- [ ] Test by hand, against the 1.2.5 exe: (a) the dispatch fix -- an unclosed-quote
      command followed by two more; expect running / queued / queued, the stall line
      once, then Stop giving not run on all three and a prompt back; (b) the special-keys
      checklist in `2026-09-23-special-keys-design.md` section 8.6 (lone Alt tap, Alt+F
      in Edit, F10, Alt+0233, AltGr, Backspace in Edit, PgUp at a prompt and in less).
- [ ] Make the merge gate compile something: only `Version bump` is required and it
      checks versions and the committed exe but builds nothing; PR #44 was merged with
      CodeQL's `analyze` red (four unstaged files) and #45 with it pending, and `main`
      was unbuildable for four minutes. Options, for the maintainer: require `analyze`
      in the ruleset (`Protect-Main.ps1`), or add a hosted `make test` job to
      `checks.yml` and require it. Also `codeql.yml`'s concurrency group cancels the
      `main` push run when merges come fast (bc78b5c was never analysed on `main`).
      Until then: `gh pr checks N --watch` before every merge (CLAUDE.md).
- [ ] Local shell follow-ups: test PowerShell as the local shell (a profile whose shell
      command is `powershell.exe` or `pwsh.exe`; custom kind, platform auto -- prompt
      detection on `PS C:\...>`, paste line ends, Ctrl+C, the AI ruleset); run the
      `local_shell` case with the busybox sidecar; decide on embedding busybox (GPLv2
      source with each release, spec 4.4); move the dispatcher out of `src/ui` so it can
      be tested natively; a timeout for commands that never return; WSL; tab title from
      the shell's directory (OSC 7); mouse reporting (special-keys spec section 5, own
      spec); Backspace on SSH and a per-profile terminal type (special-keys spec 7).
- [ ] CI review follow-ups (2026-09-10 review, minus the items the desktop gate's
      retirement made moot): CodeQL installs no libssh2, so the SSH/known-hosts files
      and all of `src/ui` go unscanned — install `libssh2-1-dev`, fail if the Makefile
      probe still says no, and consider a Windows CodeQL run for `src/ui`;
      `.gitattributes` and renormalise (90 CRLF-indexed files) in a lone PR when nothing
      else is in flight. (Untracking `build/win/nutshell.exe` is off the list: the gate
      now relies on the committed exe being built from `APP_VERSION`.)
- [ ] Harness batches B (sessions, auth, host key, connection), C (terminal, clipboard,
      settings), D (AI extras) — 31 cases; `bvtuser` exists on tompi for auth cases.
      Manual runs only now; still worth having for release checks.
- [ ] Security audit follow-ups (`2026-09-09-security-audit.md`, kept local; C2 and H2
      landed in PR #24): H3/H4 host-key fail-closed and default No;
      H5/H6 config in %APPDATA% with DPAPI and absolute save path; H7/H8 window-message
      hardening; then Medium and Low.
- [ ] Older review findings: AI-stream thread lifetime bugs; Session Manager phantom row.
- [ ] Policy control follow-ups from the PR #27 review: the AI-tier harness cases it
      rewrote (`ai_runs_read_command_unattended`, `ai_write_command_held_then_runs_after_allow`)
      have not been run yet — needs the Kimi key in `tests\integration\.ai_key`; and the
      status line still has no keyboard path (a whole-line design-system job, the four
      `WM_COMMAND` ids 4030–4044 are ready for a menu or accelerator).
- [ ] `md_render.c` renders at raw 96-DPI pixels — fold into sub-project 3.
- [ ] Sub-project 3 (main window chrome): spec first, mockups before choosing, as with
      sub-project 2.

Process (CLAUDE.md): hand off wherever possible; Opus orchestrates a multi-step
package and reviews diffs, Sonnet writes code and tests, the main session keeps the
decisions and reads the diff in full; `gh pr merge N --merge --auto` merges at once
when the PR is mergeable and only `Version bump` is required (version, README,
committed exe -- nothing is compiled), so `gh pr checks N --watch` until every check
is green first. Run the harness tier a change touches by hand first. "Create a checkpoint" = compact this
list, compact memory, delete temp files and worktrees, leave the tree ready for a new
session.
