# UI Redesign — Working Notes (checkpoint)

**Status:** sub-project 1 (design-system foundation) **and** sub-project 2
(AI Assist panel) are both **implemented and done**:
- Sub-project 1: spec `2026-09-07-design-system-foundation-design.md`, plan
  `../plans/2026-09-07-design-system-foundation.md` (10 of 10 tasks landed).
- Sub-project 2: spec `2026-09-07-ai-assist-panel-design.md`, plan
  `../plans/2026-09-07-ai-assist-panel.md` (5 of 5 tasks landed, v1.0.96).

Native Windows build, unit suite (1,699, up from 1,539 at sub-project 1's
plan time) and the tompi integration suite (`ui_gallery` now 9 states x 4
themes = 36 captures, plus the new keystroke-free `ai_panel_opens_without_key`
case) are all green. Next action is sub-project 3 (main window chrome:
single toolbar replacing the menu bar, tab strip, status). Resume from
"Todo" below.

Branch: `main` — the redesign branch `ui-polish` was renamed to `main` on 2026-09-07 (the pre-redesign main was kept as branch `v1.0.76`). Now at v1.0.96.

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

## Todo

- [x] Native Windows build + test environment (committed 58fc45e, v1.0.77).
- [x] App + docs review (`2026-09-07-app-and-docs-review.md`) and the six behaviour
      fixes Thomas approved from it (`2026-09-07-review-fixes-design.md`, v1.0.78).
      Still open from that review: the AI-stream thread lifetime bugs, the relative
      config-save path, and the Session Manager phantom row — fold into the roadmap.
- [x] Lost lines after app resize (v1.0.80). Root cause: `WM_SIZE` only resized the
      active tab, so a tab in the background during the resize kept its old grid and
      PTY size; its prompt then sat mid-window with the rest blank until enough Enters
      scrolled it into place. `on_tab_select` now calls `sync_session_grid()`.
      Regression case `resize_applies_to_inactive_tab` in the integration suite.
- [x] Design-system foundation: all six sections presented and approved one at a
      time (2026-09-07). Decisions: derived interaction states with per-theme
      override; each light theme gets its own accent; body text 10 pt; standalone
      buttons stay child windows, in-card elements are painted; `--ui-demo` stays a
      hidden flag, no menu entry.
- [x] `docs/superpowers/specs/2026-09-07-design-system-foundation-design.md`
      written, self-reviewed, committed.
- [x] Foundation implementation plan: `docs/superpowers/plans/2026-09-07-design-system-foundation.md`
      (10 tasks, TDD, gates as a ratchet). Next: implement task 1.
- [x] Sub-project 2 (AI Assist panel) designed: mockups artifact, Thomas chose frame B,
      card 1, "safe only"/"all" wording, empty state with three suggestions, and asked
      that the AI thought process stay viewable (thinking disclosure). Spec
      `2026-09-07-ai-assist-panel-design.md`, plan `../plans/2026-09-07-ai-assist-panel.md`
      (5 tasks). Next: implement task 1.
- [x] During foundation section 3 (shared primitives): add `OP_DOT` to `icons.c` so the
      password / thinking / server dots render; `make wintest` must go green.
- [x] **Design-system foundation implementation complete (task 10 of 10, v1.0.92).**
      All six modules landed with their tests; native suite green at **1,665 tests**
      (up from 1,539 at plan time). Both gates are exact-allow-list assertions, not
      ratchets: the colour gate allows exactly 2 literal `RGB(` calls in all of
      `src/ui` (`renderer.c`'s terminal fg/bg fallback), 0 everywhere else including
      `ns_draw.c`'s own callers; the scale gate allows 0 local scale macros /
      `MulDiv(..., 96)` sites (`settings_scale` deleted, its callers moved to
      `ns_scale`). `make wintest` 2/2. `make clean && make release` clean under
      `-Werror -Wpedantic -Wshadow -Wconversion -Wformat=2`. Definition of done
      (spec section 6) met: colours outside the allow-list are 0 (from 77 at plan
      time), both gates passing, wintest green, integration suite green including
      `ui_gallery`.
- [x] **AI Assist panel implementation complete (task 5 of 5, v1.0.96).** All five
      tasks landed: `ai_panel_layout`/`ai_status_layout`/`thinking_layout`/`ai_modes_label`
      in `src/core/ai_panel_layout.*`, `ai_panel_states` table, `approval_card_layout` v2
      in `ns_layout` (checkbox/text/tag rows, `Deny all`/`Run N selected`, no more two-line
      rule); card 1 painted in `chat_listview.c` with the Thinking disclosure; the header
      (session name, model chip, New chat/Save chat/Undock-Dock icon buttons with
      tooltips) and status line (Read-only/Read + write segmented control, Auto approve
      off/safe only/all, context meter) replacing the old owner-drawn tab buttons in
      `ai_chat.c`; the three empty/no-key/no-session states with suggestion chips and
      Open Settings/Open Session Manager buttons, both `MessageBox` dead ends in
      `window.c on_ai_clicked()` gone; `ui_demo` gained `nokey`/`nosession` states
      (`ai_chat_apply_demo_extras()` now forces `AI_STATE_EMPTY`/`AI_STATE_NO_KEY`/
      `AI_STATE_NO_SESSION` for `empty`/`nokey`/`nosession` respectively). Native suite
      green at **1,699 tests** (up from 1,694 at task 5's start). `ui_gallery` now
      captures 9 states x 4 themes = 36 images; the new keystroke-free
      `ai_panel_opens_without_key` case (empty `ai_api_key`, connects to tompi, opens
      the panel via the View menu command, asserts no `#32770` dialog and a non-blank
      capture) passes. `make clean && make release` clean under `-Werror`, `wintest` 2/2.
      README ("AI Chat Assistant" summary + user-guide section, feature bullets, test
      count) and `help_guide.c`'s in-app AI section rewritten for the new panel (each
      `GUIDE_TEXT_*` string literal still under 4095 chars). Gallery reviewed for four
      sample captures (`Onyx-Synapse-nokey`, `Onyx-Light-nosession`, `Sage-and-Sand-all`,
      `Moss-and-Mist-approval`) — all render correctly (empty-state copy/buttons match
      `ai_panel_states.c` verbatim, Thinking disclosure expands with the right word
      count, approval card header/rows/actions match the design spec). One
      non-blocking observation: the demo `approval` state's already-settled
      (approved/denied) commands render as nothing, since `chat_listview.c` hides
      settled command items on the assumption their outcome is narrated as an `[EXEC]`
      block in the AI's own reply text -- the canned `APPROVAL_ASSISTANT_MSG` in
      `ui_demo.c` doesn't include one, so the gallery's `approval` capture only ever
      shows the two still-open (pending/blocked) rows. Cosmetic-only (doesn't affect a
      live conversation, where the AI's real reply does narrate settled commands); left
      as-is rather than reshaping the canned demo script outside task 5's scope.

  **Still open:**
  - The three review findings not yet folded into a task: the AI-stream thread
    lifetime bugs, the relative config-save path, and the Session Manager phantom row
    (all first noted under "Review findings" above, before the design-system work
    started).
  - Done 2026-09-07: the full integration suite ran 13/13 green against tompi and
    Moonshot on v1.0.96 with the desktop unlocked (definition-of-done item 4).
  - Thomas to review the gallery (all 36 captures, not just the four sampled above)
    before sub-project 3 starts.

- [x] **Two approval-card bugs fixed (v1.0.97).** Crash: clicking "Run N selected"
      settled the active commands but only invalidated `chat_listview` *before* the
      settle, not after; `WM_PAINT` doesn't recalc_layout on its own, so it painted
      the command container against a stale `cmd_count` with zero live items behind
      it and dereferenced a NULL `cmd_items[]` slot. Fixed at every layer:
      `build_cmd_card_geometry` now derives its row count from the live walk instead
      of `lv->cmd_count`, the paint/hit-test loops guard a NULL slot regardless, a
      settled item paints nothing even with a stale non-zero `measured_height`, and
      `settle_all_commands` (now taking `AiChatData *`) always re-invalidates after
      settling. Held commands: when Permit write was off, write commands were
      dropped from `d->queued_cmds`/the approval queue entirely (only shown as
      blocked chips), so switching to Read + write and hitting "Run N selected"
      queued nothing — a write-only batch had no path to ever run. Every extracted
      command is now always queued through `chat_approval_add` (blocked commands
      included), keeping item/queue/`queued_cmds` order identical always; added
      `chat_approval_needs_user()` (TDD, `tests/test_chat_approval.c`) to decide
      whether the card must stay up. New integration case
      `approval_card_run_selected_settles` (no SSH/key needed, `--ui-demo=approval`)
      and `ai_write_command_held_then_runs_after_permit` (renamed/extended from
      `ai_write_command_blocked_without_permit_write`) cover the regressions. Native
      suite green at 1,707 tests (up from 1,699).

- [x] **Thinking disclosure overlap fixed (v1.0.98).** Measured height disagreed
      with painted height: `chat_msg_set_thinking()` already marked its item
      dirty, but `ai_chat.c` flipped `thinking_collapsed` directly in two more
      places (the streaming handler's open/collapse-on-reply logic, and
      `ai_chat_apply_demo_extras()`'s gallery "all" state) without setting
      `dirty`, so `recalc_layout()` — which only remeasures dirty items — kept
      the item's shorter pre-expand height and later items (the approval card,
      the "Waiting for output…" indicator) painted over the open block. Both
      sites now set `item->dirty = 1` alongside the flag flip, matching
      `chat_listview.c`'s own click-to-toggle handler. Confirmed
      `measure_item`'s thinking width (`width - ai_indent - 3*side_pad`) and
      `paint_ai_item`'s box width (`rc->right - side_pad` minus
      `rc->left + ai_indent`, with `rc` already inset by `side_pad` in
      `on_paint`) already reduce to the same value — no geometry change
      needed there. TDD: `tests/test_chat_msg.c` gained
      `test_chat_msg_set_thinking_marks_dirty`.
- [x] **Settled commands now render as inline rows (v1.0.98).** A decided
      command used to measure to height 0 and vanish from the transcript on
      the (false, since v0.9.41) assumption that its outcome was still
      narrated in the AI's finalised text as an `[EXEC]` block — closing the
      non-blocking observation noted under task 5 above. Each settled command
      now paints as one compact inline row (mono command text, ellipsised
      when needed, no card/header/checkbox/buttons) with a right-aligned
      outcome chip: **ran** (approved, success intent), **held** (still
      blocked by policy, warning intent), **denied** (explicit denial, dimmed
      chip), or **skipped** (superseded before a decision, same dimmed
      styling). New core geometry `settled_row_layout()` in
      `src/core/ns_layout.*` (TDD, `tests/test_ns_layout.c`) mirrors
      `approval_card_layout`'s row shape without the checkbox; `measure_item`
      and a new `paint_cmd_settled_row()` in `chat_listview.c` both size the
      row from it, so they can't drift apart. Hit-testing already excluded
      settled items from the live container (verified, unchanged). Native
      suite green at **1,715 tests** (up from 1,707).
- [x] **AI Assist chat list now sticks to bottom properly (v1.0.99).**
      Replaced the old per-event `chat_listview_is_near_bottom()` margin
      check (raced against `recalc_layout()` growing `total_height` first,
      so a big content jump — the Thinking block opening, an approval card
      appearing — regularly left the list stranded above the new bottom)
      with classic stick-to-bottom state: `ChatListView.stick_to_bottom`,
      set to 1 only when a user-driven scroll (wheel/scrollbar/keyboard/
      selection-drag) ends at or past max scroll, consulted (never changed)
      by `recalc_layout`/`WM_SIZE` to decide whether to follow new content
      or hold position. New pure core module `src/core/stick_scroll.{c,h}`
      (TDD, `tests/test_stick_scroll.c`) holds the two decision functions.
      `ai_chat.c`'s per-chunk/per-event `chat_listview_scroll_to_bottom()`
      calls (WM_AI_STREAM, WM_AI_TOOL_MSG, WM_AI_RESPONSE) were removed —
      following now happens automatically via `chat_listview_invalidate()`
      when stuck; the deliberate ones (sending a prompt, Retry, session
      switch, cancelling a stream) were kept.

- [x] Smart scrolling for the terminal pane (v1.1.0). `term_scroll` now moves a
      scrolled-back `scrollback_offset` up by one for every line scrolled in
      (clamped to the history held), so the view stays anchored while output
      arrives; at the bottom it follows, and a keypress already resets to the
      live view. Tests in `tests/test_term.c`; integration case
      `terminal_holds_position_while_output_arrives` compares captures.
      Versioning: after 1.0.99 the number rolls to 1.1.0 (Thomas; rule in CLAUDE.md).

- [x] Prompt-gated command dispatch (v1.1.1; feature A of
      `2026-09-07-command-dispatch-and-auto-approve-levels.md`). Every
      command-execution path in `ai_chat.c` (auto-approve, Run N selected,
      approve one) used to burst all approved commands onto the SSH channel
      at once, so the tty echoed the next command into whatever the
      previous one left waiting (a package prompt, a pager, a password
      prompt). Replaced with a single dispatcher (`dispatch_start()` /
      `dispatch_tick()` on `TIMER_CMD_QUEUE`, 250ms) that sends approved
      commands one at a time, only once `term_at_prompt()` says the active
      terminal is back at a shell prompt and has been quiet for
      `PROMPT_QUIET_MS` (400ms); the continue message to the AI now fires
      right after the last command's prompt returns instead of a fixed 2s
      delay. New `src/core/shell_prompt.{c,h}` (`shell_prompt_line()`) and
      `Terminal.write_seq` / `term_at_prompt()` in `src/term`, both unit
      tested. Integration case `ai_commands_run_one_at_a_time` (not run by
      Claude — costs API credits; reviewer runs it once).

- [x] Auto approve has four levels (v1.1.2; feature B of
      `2026-09-07-command-dispatch-and-auto-approve-levels.md`).
      `ApprovalQueue.auto_approve_all` replaced with `auto_approve_level`
      (`AutoApproveLevel`: SAFE/WRITE/ALL) in `src/core/chat_approval.{c,h}`;
      the status-line click on the AI panel now cycles **off → safe only →
      safe + write → all → off**. `ai_modes_label()` grew a third label
      ("safe + write"). Config key `ai_auto_approve_all` (bool) replaced
      with `ai_auto_approve_default` (int 0-3) in `config.h`/`loader.c` —
      the old key is dropped and ignored on read, no migration. Settings ›
      AI behaviour's checkbox became a drop-down, labelled "Auto Approve:"
      (kept short to fit the page's fixed 150px label column — the
      tooltip spells out that it sets the starting level for new
      sessions), with items Off/Safe only/Safe + write/All. Same control id
      (`IDC_AI_AUTO_APPROVE_DEFAULT`, renamed from
      `IDC_AI_AUTO_APPROVE_ALL`). `AiSessionState` carries `auto_approve`
      and `auto_approve_level` plus a new `auto_approve_seeded` flag so a
      fresh session is seeded from the configured default exactly once
      (via `ai_chat_set_auto_approve_default()`, renamed from
      `ai_chat_set_auto_approve_all()`) while a session the user has
      already toggled keeps its own choice across `chat_approval_reset()`
      and session switches. Native suite green at **1,764 tests** (up from
      1,757).

- [x] Thinking-disclosure collapse sticks, and the activity indicator never
      overlaps text (v1.1.3). Two independent fixes:
      (1) A user's manual open/close of a reply's Thinking block now sticks
      for the rest of that stream. New `thinking_user_set` flag on
      `ChatMsgItem.u.ai` (`src/core/chat_msg.h`), zeroed on create; the
      listview's click toggle (`src/ui/chat_listview.c`) sets it on every
      manual toggle and posts a new `IDC_CHAT_THINKING_CLOSED`
      (`src/ui/resource.h`) on close, mirroring the existing
      `IDC_CHAT_THINKING_OPENED` on open. `ai_chat.c`'s `WM_AI_STREAM`
      handler now skips both the auto-open (while thinking streams) and
      the auto-collapse (once content starts) for any item with the flag
      set, and `IDC_CHAT_THINKING_CLOSED` clears `show_thinking` (mirrored
      into `active_state`, same as `IDC_CHAT_NEWCHAT` already did) so a
      later reply still starts open.
      (2) The inline activity indicator ("Processing…"/"Responding…"/etc.,
      `paint_activity_indicator()`) could land on top of the last
      message's text whenever that message painted taller than
      `measure_item()` predicted. `paint_ai_item`, `paint_user_item`,
      `paint_status_item`, `paint_cmd_container` and
      `paint_cmd_settled_row` now return the height they actually painted;
      `on_paint()` tracks `painted_bottom` (max of item-top + painted
      height over all painted items) and places the indicator at
      `max(layout y, painted_bottom + msg_gap)`, caching the result in a
      new `ChatListView.indicator_y` field so `chatlv_items_bottom_y()`
      (used by both hover and the `[Retry]` link's click hit-test) can
      never disagree with what was actually drawn. Audit of measure vs.
      paint for the AI item found one real, structural mismatch: for a
      message containing `[EXEC]...[/EXEC]`, `measure_item()` used to
      strip the tags and measure the whole message as one markdown-
      rendered string, while `paint_ai_item`'s `draw_ai_text_with_exec()`
      actually paints it as separate segments (markdown-rendered prose
      interleaved with mono-font purple command text) — a different
      computation that can legitimately produce a different height. Fixed
      by adding `measure_ai_text_with_exec()` (and its `measure_ai_segment()`
      helper, now also used by `draw_ai_segment()` for its own non-markdown
      height calc), a height-only twin that walks the identical segment
      split as the paint path; `ai_text_for_measure()` is gone. Also
      hardened the EXEC segments' own paint: they used to clip to the
      item's original (possibly stale) `rc->bottom` — now two-pass
      (calc then paint into an exact-fit rect), matching
      `draw_ai_segment`'s existing non-markdown approach, so a late growth
      spurt can never be silently truncated. The thinking block, markdown
      path, and plain-text path were checked and already agreed between
      measure and paint. Native suite green at **1,766 tests** (up from
      1,764).

- [x] Markdown tables now render as real tables instead of raw piped text
      (v1.1.4). New pure/tested `src/core/md_table.{c,h}`:
      `md_table_split_row()` (trims cells, handles missing outer pipes and
      `\|` escapes), `md_table_alignments()` (per-column left/center/right
      from the `|---|:--:|--:|` separator row), and
      `md_table_fit_columns()` (natural widths when they fit; otherwise
      shrinks the widest column(s) first, evenly among ties, floored at
      each column's widest single word or 6 em). `src/ui/md_render.c`
      gathers a run of consecutive `MD_LINE_TABLE` lines as one block
      (`md_render_table_block()`, intercepted before the per-line switch
      in `md_render_core()` so header/body cells share the file's existing
      inline renderer — bold/italic/code spans, header cells forced bold)
      instead of the old per-line monospace dump. Header row gets a
      subtle `text_dim`-over-`bg_primary` fill via `rgb_alpha()` (same
      technique as `chat_listview`'s dim chips); hairline row separators
      and an outer border in the theme's `border` colour; no vertical
      column lines. Like the rest of this file, table constants (cell
      padding, hairline width) are raw 96-DPI pixels, not run through
      `ns_scale()` — `md_render_text`/`md_measure_text` aren't passed a
      dpi, so nothing else in the file is either. 17 new native tests in
      `tests/test_md_table.c`. Native suite green at **1,783 tests** (up
      from 1,766).

- [x] Wheel trap fixed (v1.1.5). An open Thinking box (or approval card) that
      was only partly visible swallowed every wheel-down, so the list could
      never reach the bottom and re-engage stick-to-bottom while a reply
      streamed — the box stayed cut off. Inner boxes now take the wheel only
      when fully inside the viewport; a scrollbar thumb dragged to the end of
      the track snaps to the true maximum. Open: `md_render.c` renders at raw
      96-DPI pixels (its padding is not `ns_scale`d) — fold into sub-project 3.

- [ ] Sub-project 3 (main window chrome) — brainstorm next: single toolbar replacing
      the menu bar, tab strip, status. Mockups of layout options are worth showing
      visually before choosing, same as sub-project 2.
