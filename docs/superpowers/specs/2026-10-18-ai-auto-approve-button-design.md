# Auto-approve button on the AI panel's policy row

**Date**: 2026-10-18
**Thomas**: *"add an auto approve button in the ai assist panel. when used in conjunction
with the read/unknown/write/critical button it should auto approve the commands for the
chosen level. this is so that the ai assist panel can be processing unattended"* ...
*"dont use a thin rail under the cell to denote auto-approve, include a separate button
called 'auto-approve' in the same row as the severity pills but on the right hand side.
when hovering over the severity pills and the auto-approve button, a pop up should appear
with a short description on what the button does and how to use it."* ... *"make sure you
have a shortened set of icons like 'R,U,W,C' for (read, unknown, write, critical) and
something short for auto-approve for when the window has been resized small enough so as
to not fit the full strings."*
**Replaces**: the lower *rail* band of the policy control from
`2026-09-11-status-policy-control-design.md` (the two-marker model stays in the code; the
UI exposes it as one level plus a switch).
**Builds on**: `2026-09-11-status-policy-control-design.md` (`CmdPolicy`, the four stops,
the `ai_policy_default` token), `2026-09-09-pending-command-batches.md` (per-batch
queues), `2026-09-07-design-system-foundation-design.md` (tokens, `ns_hover`, `ns_layout`).

---

## 1. The change in one paragraph

The unattended marker exists today but is hidden: a thin rail under each cell of the
`Read · Unknown · Write · Critical` pill, set by clicking the rail, with no label and no
keyboard path. It reads as missing, and three dispatcher behaviours stop it from doing its
job for a session left alone (raising the marker does not release rows already pending,
the dispatcher is cancelled on a session switch, and a mixed batch waits whole). This
change gives the policy row an explicit **Auto-approve** button, drops the rail, adds
hover tooltips to every cell and to the button, shrinks the labels to `R · U · W · C` and
`Auto` when the row is too narrow for the full words, and makes turning the button on
release the rows it would have approved on arrival. Running a background session's
batches while another tab is displayed is a per-session dispatcher, a separate spec
(section 9); in this change the session being processed must stay the displayed one.

---

## 2. Semantics

`CmdPolicy {allowed, unattended}` (`src/core/cmd_policy.h`) is unchanged, as are the
blocked / pending / approved outcomes in `chat_approval_add_session()`
(`src/core/chat_approval.c`). What changes is which states the UI can produce:

| Button | `unattended` |
|---|---|
| off | `POLICY_NONE` |
| on | `== allowed` |

- The invariant *`unattended` is `POLICY_NONE` or equal to `allowed`* is enforced in one
  place: `cmd_policy_set_allowed()` (`src/core/cmd_policy.c`) sets `unattended = k`
  whenever it was not `POLICY_NONE`, and a new `cmd_policy_set_auto(p, on)` sets it to
  `allowed` or `POLICY_NONE`. `cmd_policy_set_unattended()` is removed so no caller can
  produce a split; the demo seed in `src/ui/window.c` (today `write/read`, with
  `policy_seeded = 1`) and the Settings save path go through the two setters.
- Clicking a cell calls `set_allowed(k)`; the button follows if it is on.
- Clicking the button calls `set_auto(!on)`.
- **Critical needs a confirmation.** Turning the button on while `allowed` is Critical,
  or clicking Critical while the button is on, shows a Yes/No message box ("Run every
  command the assistant proposes, including ones that can destroy data, without
  asking?"); No leaves the policy as it was. Under the old model that posture took two
  gestures on the rail; one click should not reach it silently.
- A policy whose `unattended` is strictly between `POLICY_NONE` and `allowed` (the old
  split posture, "allow writes but auto-run only reads") can arrive from a saved
  `ai_policy_default` token written by an earlier version. It is **rounded down to off**
  on load (`cmd_policy_normalise()`, new, called by the loader and by
  `ai_chat_seed_policy`). Rounding down is the conservative direction: nothing runs
  without a click that did not before. The decision to give up the split posture is the
  maintainer's (2026-10-18, "this is fine"); the 2026-09-11 spec kept it on purpose, and
  the trade is recorded here so a future reader knows it was not an oversight.
- The config token keeps its `"<allowed>/<unattended>"` form, so an old config loads and a
  new config is readable by an old exe. Settings > AI Assistant > Behaviour replaces the
  "Runs unattended" combo with an **Auto-approve by default** tickbox; the "Allowed"
  combo stays. The tickbox writes `unattended = allowed` or `POLICY_NONE`.
- Fresh-install default stays `read/none`: Read allowed, button off.
- Command ids: `4030`..`4033` (set allowed) stay. `4040` (unattended none) becomes
  `set_auto(0)`; `4041`..`4044` (unattended = k) become `set_allowed(k)` then
  `set_auto(1)`, which is the nearest state the new model has and keeps the harness's
  `Set-NutshellAiPolicy` meaningful rather than a silent no-op. `4005` (cycle allowed)
  stays; `4015` (cycle unattended) toggles the button. A new id `4045` toggles the
  button for the harness and the demo. The Critical confirmation of the previous bullet
  applies to posted ids as well; the harness case that needs Critical unattended, if
  one is ever written, answers the box through `Wait-NutshellDialog`.

## 3. The control

The row is the AI panel's status line, laid out by `ai_status_layout()` in
`src/core/ai_panel_layout.c` (core, natively tested in `tests/test_ai_panel.c`): the
policy pill on the left, the context meter (bar and text) right-aligned, the meter dropped
first when the row is tight. The button joins that layout; `ns_policy_layout()`,
`ns_policy_width()` and `ns_policy_hit()` in `src/core/ns_layout.c` keep slicing the pill.

- **Order, left to right**: pill, free space, meter, **Auto-approve button at the right
  edge**. The maintainer asked for the button on the right-hand side; the meter moves
  inward by the button's width plus `SP_SM`.
- **Drop and shrink order** as the row narrows, decided in `ai_status_layout()` from the
  caller's measured label widths (full and compact sets) and the meter width:
  1. full labels, meter shown;
  2. full labels, meter dropped (as today);
  3. compact labels `R · U · W · C` and `Auto`, meter dropped;
  4. narrower still: the button keeps its rect at the right edge and the pill is clipped
     at the button's left edge minus `SP_XS`; `ns_policy_hit()` is given the clipped
     rect so a cell under the button is not hittable. Nothing overlaps.
  The layout returns the button rect, the pill rect, the `compact` flag and which label
  set to draw; `ai_chat.c` and `ns_draw.c` consume it and compute no rect of their own.
- **Pill**: four touching cells, one band each (the rail is gone). Colour follows
  `allowed` as now: `success` for Read, `info` for Unknown, `warning` for Write, `danger`
  for Critical. Cells at or below `allowed` are tinted with the intent colour, cells above
  are `text_disabled` on the surface base. When the button is on, the cells at or below
  `allowed` are **solid** (intent `.base` with `.label` text) instead of tinted, so the
  pill itself says "these will run on their own". Radii from `ns_type_pill()`, stroke
  `STROKE_HAIRLINE`, font `FONT_CAPTION`, geometry through `ns_scale`.
- **Auto-approve button**: a pill of the same height. Off: outlined in `border`, label in
  `text_dim`. On: filled with the current level's intent `.base`, label in the intent's
  `.label`. Hover uses the intent's `.hover` when on and the surface `.hover` when off,
  through `ns_hover`; pressed uses `.pressed`. The hover ids in `ai_chat.c` are packed
  as `STATUS_HIT_ID(band, stop)` with `STATUS_HIT_COUNT = 2 * NS_POLICY_STOPS`; the rail
  band goes and the button takes one id, so the count becomes `NS_POLICY_STOPS + 1` and
  every range check that reads it changes with it.
- **Tests** for the layout, in `tests/test_ai_panel.c` and `tests/test_ns_layout.c`:
  each of the four tiers at a width inside it; the two widths either side of each
  threshold; no overlap between the button, the meter and the pill at any tested width;
  the hit-test maps each cell and the button and refuses a clipped cell.

## 4. Tooltips

- The status line already owns a native comctl32 `TOOLTIPS_CLASS` window with per-cell
  text from `cmd_policy_tip` (`ai_chat.c`, around lines 3431-3456). It stays: system
  tooltips are themed by Windows, so no token, radius or font claim is made for them.
  What changes is the text and when it is refreshed: `cmd_policy_tip` becomes
  `cmd_policy_cell_tip(level, compact)` and `cmd_policy_auto_tip(on, allowed)` in
  `src/core/cmd_policy.c`, unit-tested, and the tooltip text is updated
  (`TTM_UPDATETIPTEXT`, then `TTM_POP` if the hovered hit id changed) on every
  `ns_hover_move()` transition, so moving from cell to cell never shows the previous
  cell's text.
- Cell text, one level per line of the tooltip, two short sentences: what the level
  means, then how the click works. The strings:
  - Read: *"Read: commands that only inspect, such as ls, cat or git status. Click to make
    this the highest level the assistant may run; anything above it is blocked."*
  - Unknown: *"Unknown: commands the classifier cannot place, including wrappers such
    as sh -c or xargs that hide what they run. Click to allow up to this level; Write
    and Critical stay blocked."*
  - Write: *"Write: commands that change files or state, such as rm, mv or apt install.
    Click to allow up to this level; Critical stays blocked."*
  - Critical: *"Critical: commands that can destroy data or the system, such as rm -rf /
    or mkfs. Click to allow everything. Use with care."*
  - Compact variants prefix the letter: *"R = Read: ..."*.
  - Auto-approve, off: *"Auto-approve: off. Click to run commands up to the <Level> level
    without asking. Anything above <Level> stays blocked."*
  - Auto-approve, on: *"Auto-approve: on. Commands up to <Level> run without asking;
    anything above is blocked. Keep this tab shown; switching tabs stops the batch. Click to turn off and approve each batch yourself."*
- The tooltip hides on mouse leave, on click and on any policy change, and its text is
  re-fetched on the next hover because the button's tip names the current level.

## 5. Keyboard

No keyboard path is added. The panel has no context menu with level entries (its menu
is Copy / Select all / Paste), a Ctrl+Shift chord would need handling in both the list
view and the input subclass plus removal of the queued `WM_CHAR`, and a silent chord that
enables unattended execution is the wrong first keyboard gesture for this control. The
status-line keyboard path stays the Open item it already is in the notes.

## 6. Dispatcher changes

One change, scoped to the displayed session, with its decision logic in
`src/core/chat_approval.c` so it is natively tested:

- **Release on policy change.** `apply_policy_change()` (`ai_chat.c`, around line 999)
  already re-evaluates every pending batch: rows above the new ceiling become BLOCKED and
  rows newly under it become PENDING. After that pass, when the button is on, a new core
  helper `chat_approval_release(q, policy, ticked_only)` marks APPROVED every row that
  (a) was PENDING **before** this policy change, not one the same change just unblocked,
  (b) is still ticked by the user (`selected`), and (c) runs unattended under the new
  policy by `cmd_policy_runs_unattended()`. It is applied to the batch at the head of
  the displayed session's queue only; older held cards are not touched. If the head
  batch then has no PENDING rows, the existing start path runs it. Rule (a) is what stops
  the scenario "Auto-approve on at Read, an old card holds `rm -rf build/`, the user
  clicks Write for something new and the old command runs": that row was BLOCKED, so it
  becomes PENDING and waits for a tick. A row the user unticked stays unticked.
- **Mixed batches keep waiting.** A batch with one PENDING row shows its card whole and
  nothing in it runs until the user decides, exactly as today. The dispatcher picks
  approved rows in any order (`chat_approval_next_approved()`) and ends the batch, with
  the continue message to the model, when no APPROVED row is left, so running an
  approved prefix would end the batch under the pending rows. The rule "the model wrote
  them in order" argues for waiting, not for partial runs.
- **Session switch cancels the running batch** (`do_session_switch`, `ai_chat.c` around
  line 5338: `KillTimer` then `dispatch_cancel`). Unchanged here: `execute_command`,
  the prompt checks and the continue message all read the panel's active terminal and
  conversation, so a batch cannot run for a session that is not displayed. The user
  leaves the tab being processed on screen; the Auto-approve tip says so when on
  ("... in the tab shown; switching tabs stops the batch"). A per-session dispatcher is
  section 9.

No round or turn limit is added; the ceiling and the blocked rows are the safety limit,
as the 2026-09-11 spec decided.

## 7. Tests

Native (`make test`):
- `tests/test_cmd_policy.c`: `set_auto` on/off; `set_allowed` with the button on moves
  both markers and with it off moves one; no sequence of the two setters produces a
  split; `cmd_policy_normalise` rounds a split down; tooltip strings for all four levels
  in both label sets and for the button in both states, including the level name in the
  button's text.
- `tests/test_ai_panel.c`, `tests/test_ns_layout.c`: section 3's tiers, thresholds,
  no-overlap and clipped-hit cases.
- `tests/test_chat_approval.c`: `chat_approval_release` approves a previously pending
  ticked Read row when auto turns on at Read; leaves an unticked row; leaves a row that
  this same change unblocked; leaves everything when the button is off; touches only the
  head batch.
- Loader: an old `write/read` token loads as `write/none`; `write/write` loads as on;
  `read/none` unchanged.

What the native suite cannot reach, because `ai_chat.c` is `NON_TEST_SRCS`: the call
into `chat_approval_release` from `apply_policy_change`, the Critical confirmation box,
the tooltip refresh and the demo seed. These are covered on Windows: the
`--ui-demo=approval` state (the names are in `src/core/ui_demo.c`) gains the button on
and off and a compact-width variant; gallery screenshots in all four themes; the harness
gate tier with `Set-NutshellAiPolicy` on the mapped ids; and `ai_write_command_held_then_
runs_after_allow` (tests/integration/cases/20-ai.ps1) is kept as the guard it is by
setting the policy with the button **off** before the raise, so its "Run selected" step
still does the running. The two AI-tier cases that have never been run are run once with
the key before the PR leaves draft. No new AI-tier case: a mixed batch cannot be made
deterministic.

## 8. Out of scope

Saving a session's live policy back to config (the Settings default is the only
persisted value, as now); a turn cap for unattended runs; any keyboard path; per-profile
policy; partial runs of a mixed batch.

## 9. Follow-up spec: a per-session dispatcher

Unattended processing of a session while another tab is displayed needs the dispatcher
to own its terminal, conversation and card list per session instead of reading the
panel's active ones, and the session switch to stop cancelling. That is a restructuring
of `ai_chat.c`'s dispatch path and gets its own spec; it goes on the notes' Open list
with this one's merge.

## Critique (2026-10-18, one round, Opus)

Ten findings; dispositions:

1. Background dispatch is a per-panel dispatcher re-architecture and a session switch
   cancels the running batch. **Accepted**: dropped from this change, section 9 opened,
   the tip tells the user to keep the tab shown.
2. The approved-prefix rule conflicts with `next_approved` picking any row and the batch
   ending under pending rows. **Accepted**: partial runs removed; mixed batches wait.
3. Release on policy change would run rows the same change unblocked, and unticked rows;
   the harness guard case would pass without exercising its step. **Accepted**: rules
   (a)-(c) and head-batch-only in section 6; the case sets the button off first.
4. The context meter is already right-aligned on the row; the layout lives in
   `ai_panel_layout.c` (core); nothing clips today. **Accepted**: section 3 rewritten
   with the order, the four tiers and the clipped hit-test.
5. The invariant was enforced nowhere central; the demo seed bypasses the normalise.
   **Accepted**: two setters in `cmd_policy.c`, `set_unattended` removed, demo updated.
6. Tooltips are native comctl32 and the "Details link" is a click popup. **Accepted**:
   native tooltip kept, styling claims removed, text refreshed on hover transition.
7. No context menu with level entries; Ctrl+Shift+A would hit Ctrl+A; focus ring
   contradicts no-traversal. **Accepted**: keyboard section dropped.
8. Auto-approve at Unknown runs wrappers (`sh -c`, `xargs`); one click reaches
   everything-unattended. **Accepted in part**: tooltip wording made honest, Critical
   gets a confirmation. The one-level model itself is the maintainer's decision.
9. Migration of a split token is lossy and one-way. **Rejected as a change**, recorded:
   the maintainer chose one level, so a split posture has no representation to keep;
   rounding down is the safe direction and the spec says so.
10. Names that do not exist, `STATUS_HIT_COUNT`, retired ids as silent no-ops, AI-tier
    mixed-batch case nondeterministic, `ai_chat.c` untestable. **Accepted**: names
    corrected, ids mapped, the case dropped, the release logic moved to core.

Not verified by the critic and settled here: themes share one UI face (`ns_font`'s
single `g_ui_face`), so one compact threshold per DPI; no translated strings exist.
