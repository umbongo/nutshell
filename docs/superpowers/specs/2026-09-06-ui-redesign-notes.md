# UI Redesign — Working Notes (checkpoint)

**Status:** sub-projects 1 (design-system foundation) and 2 (AI Assist panel)
landed at v1.0.92/v1.0.96 (see "Landed since v1.0.96" below for what's
shipped since — approval-card fixes, prompt-gated dispatch, four-level auto
approve, markdown tables, role colour, smart scrolling, and pending command
batches). Native suite: **1,804 tests**, 0 failures. Integration suite
(`tests/integration/Run-Integration.ps1`): 17 cases (9 core + 5 AI +
`ui_gallery` + `approval_card_run_selected_settles` +
`ai_panel_opens_without_key`); `ui_gallery` now captures 10 `--ui-demo`
states x 4 themes = 40 images. Next action is sub-project 3 (main window
chrome: single toolbar replacing the menu bar, tab strip, status). Resume
from "Todo" below.

Branch: `main` — the redesign branch `ui-polish` was renamed to `main` on 2026-09-07 (the pre-redesign main was kept as branch `v1.0.76`). Now at v1.1.9.

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

Open, in priority order:
- [ ] **Status-line policy control** (branch `status-policy-control` created, empty).
      Verbatim ask: *"no need for a mock-up, use the cleaner-still version, also change
      the wording throughout from 'safe' to 'read'. create a new branch for this."*

      Replaces the two-segment `Read-only` / `Read + write` switch **and** the separate
      `Auto approve: off` cycle with ONE control: a four-stop scale
      `Read > Unknown > Write > Critical` carrying **two markers** -
      *runs unattended* and *allowed, asks first* - where the unattended marker can
      never pass the allowed one. The two axes are already in the model and must stay
      distinct: `permit_write` decides whether a command may run at all (above the
      ceiling it is BLOCKED, the card says "N held"), `auto_approve`/`auto_approve_level`
      decide whether it runs without asking. Collapsing them loses the common posture
      "allow writes, but always show me first".

      Also rename the category **SAFE -> READ** throughout: the chip in
      `chat_listview.c`, `ai_modes_label()`, the Settings combo, README, the User Guide,
      and the `CMD_SAFE` / `AUTO_APPROVE_SAFE*` identifiers and `safe+...` config tokens
      (keep reading the old tokens so existing configs survive - `ai_auto_approve_mode`
      shipped in v1.1.16). Careful with blanket search-and-replace: `CmdSafetyLevel`,
      `safety_mask` and `safety_tag_*` name the *axis*, not the category, and should keep
      their names.

      Touches: `ns_layout.c` (pure geometry + hit-test, so it is testable),
      `chat_approval.[ch]`, `cmd_classify.h`, `ai_panel_layout.c`, `ai_chat.c`,
      `chat_listview.c`, `settings.c`, `loader.c` + `config.h` (one default policy
      instead of two settings, with migration), `ui_demo.c`, the User Guide and README,
      and `tests/integration/` - `NutshellIT.psm1` and `cases/20-ai.ps1` drive the
      current switch, so the gate will fail until they follow. Design-system rules apply:
      colours from `ns_tokens()`, sizes from `ns_type.h`, hover via `ns_hover`, motion via
      `ns_motion`; `tests/test_ui_tokens.c` enforces this as an exact allow-list.
- [ ] Dependabot PRs #14–#17 bump actions to new majors (Node 24 runner support
      needed): check before merging, or ignore semver-major updates in `dependabot.yml`.
      (Runner `k2so-bvt` was offline from 2026-09-09 until 2026-09-11; started again this
      session and PR #24 got the first green `Integration tests` on a real run. It had no
      Startup shortcut at all, which is why it stayed down — one now exists, wrapping
      `run.cmd` in a titled console so the window is findable rather than a nameless
      `cmd.exe`. Two caveats: the stale pre-rename `Nutshell BVT runner.lnk` is still in
      that Startup folder and `Install-IntegrationRunner.ps1` warns it starts a second
      runner instance at logon, so delete it; and the shortcut went to `C:\Users\thoma`
      while this session ran as `K2SO\thomas` — confirm which profile actually logs in,
      or it will not fire. The runner is interactive by design, so it survives an RDP
      disconnect but not a logout.)
- [ ] Ctrl+W bug — `window.c` on_tab_close leaves `g_active_session` NULL, tab A blank
      and unresponsive. Fix in progress in a separate session as uncommitted edits in
      the main checkout (tab_manager, tabs.c, window.c, tests, `60-tabs-logging.ps1`);
      it must land on a branch rebased onto `main` (#12 and #18 touched the same files),
      unwrapping both known-bug blocks (`tabs_open_switch_close`, `minimise_restore_repaints`).
- [ ] CI review follow-ups (2026-09-10 review): fork-PR approval to all external
      contributors plus a same-repo guard on the BVT job (public repo, self-hosted
      runner on the dev box); CodeQL installs no libssh2, so the SSH/known-hosts files
      and all of `src/ui` go unscanned — install `libssh2-1-dev`, fail if the Makefile
      probe still says no, and consider a Windows CodeQL run for `src/ui`; BVT artifacts
      are public and carry the LAN address, hostname, user and banners — upload on
      failure only or scrub; a nightly scheduled BVT to catch environment drift;
      untrack `build/win/nutshell.exe` (every product PR commits a 5 MB binary);
      `.gitattributes` and renormalise (90 CRLF-indexed files) in a lone PR when nothing
      else is in flight.
- [ ] BVT batches B (sessions, auth, host key, connection), C (terminal, clipboard,
      settings), D (AI extras) — 31 cases; `bvtuser` exists on tompi for auth cases.
- [ ] Security audit follow-ups (`2026-09-09-security-audit.md`, kept local; C2 and H2
      landed in PR #24): H3/H4 host-key fail-closed and default No;
      H5/H6 config in %APPDATA% with DPAPI and absolute save path; H7/H8 window-message
      hardening; then Medium and Low.
- [ ] Older review findings: AI-stream thread lifetime bugs; Session Manager phantom row.
- [ ] `ai_command_is_readonly()` (`ai_prompt.h:226`) has no callers and hardcodes the
      Linux ruleset - the H2 pattern, missed because nothing calls it. Delete it with its
      tests, or give it a platform parameter. **A cloud session was already started on
      this** (spun off 2026-09-11); check it before picking this up.
- [ ] `md_render.c` renders at raw 96-DPI pixels — fold into sub-project 3.
- [ ] Sub-project 3 (main window chrome): spec first, mockups before choosing, as with
      sub-project 2.

Process (CLAUDE.md): Opus orchestrates each package in its own worktree and picks
models; Fable reviews at PR level; `gh pr merge N --merge --auto` queues the merge for
a green `BVT` and `Version bump`. "Create a checkpoint" = compact this list, compact
memory, delete temp files and worktrees, leave the tree ready for a new session.
