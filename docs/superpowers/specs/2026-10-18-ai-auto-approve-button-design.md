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
keyboard path. It reads as missing. This change gives the policy row an explicit
**Auto-approve** button, drops the rail, adds hover tooltips to every cell and to the
button, shrinks the labels to `R · U · W · C` and `Auto` when the row is too narrow for
the full words, asks for confirmation before everything runs unattended, and stops a
batch proposed while the tab was hidden from running without a look. Running a background
session's batches while another tab is displayed is a per-session dispatcher, a separate
spec (section 9); in this change the session being processed must stay the displayed one.

---

## 2. Semantics

`CmdPolicy {allowed, unattended}` (`src/core/cmd_policy.h`) is unchanged, as are the
blocked / pending / approved outcomes in `chat_approval_add_session()`
(`src/core/chat_approval.c`). What changes is which states the UI can produce:

| Button | `unattended` |
|---|---|
| off | `POLICY_NONE` |
| on | `== allowed` |

- **The invariant** *`unattended` is `POLICY_NONE` or equal to `allowed`* is enforced at
  the two places a policy can come from outside: `cmd_policy_clamp()` rounds a split
  marker down to `POLICY_NONE` (so `cmd_policy_from_token()`, the Settings default, the
  demo seed in `src/ui/window.c` and the copy of `src->policy` taken for a background
  reply all pass through it), and the UI changes a policy only through
  `cmd_policy_set_allowed()`, which moves `unattended` with it when it is not
  `POLICY_NONE`, and a new `cmd_policy_set_auto(p, on)`. `cmd_policy_set_unattended()`
  stays for the tests that exercise the set gate under split policies
  (`test_chat_approval.c`, `test_cmd_batch.c`, `test_cmd_policy.c`); no UI code calls
  it. A test asserts that no sequence of `set_allowed` and `set_auto` produces a split
  and that `clamp` removes one.
- Rounding down is the conservative direction: nothing runs without a click that did
  not before. The decision to give up the split posture ("allow writes, auto-run only
  reads") is the maintainer's (2026-10-18, "this is fine"); the 2026-09-11 spec kept it
  on purpose, and the trade is recorded here. A config saved by an older exe with a split
  token loads as "button off" and is rewritten that way on the next save; this is lossy
  and one-way, and accepted.
- Clicking a cell calls `set_allowed(k)`; the button follows if it is on. Clicking the
  button calls `set_auto(!on)`.
- **Critical needs a confirmation.** Turning the button on while `allowed` is Critical,
  or clicking Critical while the button is on, shows a Yes/No message box ("Run every
  command the assistant proposes, including ones that can destroy data, without
  asking?"); No leaves the policy as it was. The same box appears on Save in Settings
  when Allowed is Critical and the tickbox is set. Re-entrancy: the handler records the
  active session's id and hides the tooltip before the box, sets a guard that makes a
  second toggle (click or posted id) a no-op while the box is up, and after Yes applies
  the change only if the same session is still active; otherwise it does nothing.
  Dispatch ticks and replies arriving during the box are unaffected by it.
- The config token keeps its `"<allowed>/<unattended>"` form. Settings > AI Assistant >
  Behaviour replaces the "Runs unattended" combo with an **Auto-approve by default**
  tickbox; the "Allowed" combo stays. Fresh-install default stays `read/none`.
- **Command ids**: `4030`..`4033` (set allowed) stay; `4005` (cycle allowed) stays;
  `4015` toggles the button. `4040`..`4044` (unattended none / k) are **retired**: they
  return without effect, and the harness helper `Set-NutshellAiPolicy`
  (`tests/integration/NutshellIT.psm1`) is rewritten in the same pull request to
  `-Allowed <level> [-Auto]`, posting `4030+k` then, if `-Auto`, `4015` when the button
  is off (it reads the state first through the existing status query). A case that needs
  Critical with the button on answers the confirmation with `Wait-NutshellDialog`. No
  stale harness can exist because the helper and the ids change together.

## 3. The control

The row is the AI panel's status line, laid out by `ai_status_layout()` in
`src/core/ai_panel_layout.c` (core, natively tested in `tests/test_ai_panel.c`): the
policy pill on the left, the context meter (bar and text) right-aligned with the bar
dropped before the text, and the busy label drawn right-aligned into the meter's space.
The button joins that layout; `ns_policy_layout()`, `ns_policy_width()` and
`ns_policy_hit()` in `src/core/ns_layout.c` keep slicing the pill.

- **Order, left to right**: pill, free space, meter (or the busy label), **Auto-approve
  button at the right edge**. The meter and the busy label end at the button's left
  edge minus `SP_SM`; the layout returns that label rect and `ai_chat.c`'s
  `context_label` drawing uses it instead of `status_rc.right`, so the busy label can
  never paint over the button.
- **Shrink order** as the row narrows, decided in `ai_status_layout()` from the
  caller's measured label widths (full and compact sets) and the meter widths:
  1. full labels, meter bar and text;
  2. full labels, meter text only;
  3. full labels, no meter;
  4. compact labels `R · U · W · C` and `Auto`, no meter;
  5. narrower still: the button keeps its rect at the right edge and the pill is clipped
     at the button's left edge minus `SP_XS`. `ns_policy_layout()` gains a `clip_right`
     argument; cells that are not wholly inside it are marked unhittable, and
     `ns_draw_policy` paints inside `IntersectClipRect` on the same bound so a partly
     clipped cell neither paints under the button nor takes a click.
  The layout returns the button rect, the pill rect and clip, the meter and label rects,
  the `compact` flag and which label set to draw. `ai_chat.c` and `ns_draw.c` consume it
  and compute no rect of their own.
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
  through `ns_hover`. No pressed state: the status line acts on button-down and has no
  press tracking today. The hit enum keeps `HIT_POLICY_UNATTENDED` at its value (a test
  pins it and `HIT_COPY` after it) and appends `HIT_POLICY_AUTO` at the end; the rail's
  id is simply never produced. In `ai_chat.c` the packed `STATUS_HIT_ID(band, stop)`
  loses the rail band and gains one id for the button (`STATUS_HIT_COUNT` becomes
  `NS_POLICY_STOPS + 1`); the button is hit by a plain rect test on the layout's rect.
- **Tests** for the layout, in `tests/test_ai_panel.c` and `tests/test_ns_layout.c`:
  each of the five tiers at a width inside it; the two widths either side of each
  threshold; the button, the label rect, the meter and the pill never overlap at any
  tested width; the hit-test maps each cell and the button and refuses a clipped cell.

## 4. Tooltips

- The status line owns a native comctl32 `TOOLTIPS_CLASS` window today, registered as
  one `TTF_IDISHWND` callback tool over the whole panel (`ai_chat.c`, around lines
  3431-3456), so moving from cell to cell never re-triggers it. It is re-registered as
  **one rect tool per element**: four cells and the button, with `TTM_NEWTOOLRECT` after
  every relayout (resize, DPI change, tier change) and `TTM_GETTEXT` callbacks so the
  text is fetched fresh each time. Windows then shows and hides the tip per element on
  its own. The tooltip is a system control: no token, radius or font claim is made.
- The text lives in `src/core/cmd_policy.c`, unit-tested: `cmd_policy_cell_tip(level,
  compact, auto_on)` and `cmd_policy_auto_tip(on, allowed)`; `cmd_policy_tip` goes.
  - Read: *"Read: commands that only inspect, such as ls, cat or git status. Click to make
    this the highest level the assistant may run; anything above it is blocked."*
  - Unknown: *"Unknown: commands the classifier cannot place, including wrappers such
    as sh -c or xargs that hide what they run. Click to allow up to this level; Write
    and Critical stay blocked."*
  - Write: *"Write: commands that change files or state, such as rm, mv or apt install.
    Click to allow up to this level; Critical stays blocked."*
  - Critical: *"Critical: commands that can destroy data or the system, such as rm -rf /
    or mkfs. Click to allow everything. Use with care."*
  - With `auto_on`, each cell appends *" With Auto-approve on, commands up to this level
    run without asking."* Compact variants prefix the letter: *"R = Read: ..."*.
  - Auto-approve, off: *"Auto-approve: off. Click to run commands up to the <Level> level
    without asking. Anything above <Level> stays blocked."*
  - Auto-approve, on: *"Auto-approve: on. Commands up to <Level> run without asking;
    anything above is blocked. Keep this tab shown: commands proposed while it is hidden
    wait for your approval. Click to turn off."*
- The tooltip is hidden (`TTM_POP`) before the confirmation box and on any policy
  change; the next hover fetches the new text.

## 5. Keyboard

No keyboard path is added. The panel has no context menu with level entries (its menu
is Copy / Select all / Paste), a Ctrl+Shift chord would need handling in both the list
view and the input subclass plus removal of the queued `WM_CHAR`, and a silent chord that
enables unattended execution is the wrong first keyboard gesture for this control. The
status-line keyboard path stays the Open item it already is in the notes.

## 6. Dispatcher

Two small changes, both in the existing paths, nothing new in the dispatcher's
structure:

- **No release on a policy change.** With the button on, `unattended == allowed`, so
  every row that is not blocked is approved on arrival and nothing new can wait. The
  only case a release would serve is a card that was already waiting when the button is
  turned on, and that card takes one click on its own "Run N selected" button. The
  alternative (approving waiting rows when the button turns on) needs a snapshot of
  their status before `apply_policy_change()`'s unblock pass overwrites it, has no
  meaningful "ticked by the user" test because new and unblocked rows are ticked
  automatically (`chat_msg.c`, `ai_chat.c` ~1010), and could be made to run a formerly
  blocked destructive command by toggling the button off and on. One click is cheaper
  than that rule.
- **A batch proposed while the tab was hidden waits.** The background reply path
  (`ai_chat.c` around line 4356) builds its batch under a copy of the session's policy
  with `unattended` forced to `POLICY_NONE`, so its rows are PENDING or BLOCKED, never
  approved on arrival; and `do_session_switch` calls `maybe_start_next_batch` for the
  newly displayed session so the card is shown at once rather than after the user's
  next prompt. Today a background batch can be built all-approved and then runs,
  stale, after an unrelated later prompt; that is closed here because the button makes
  all-approved batches common. The displayed session keeps running batches only while
  it stays displayed (`do_session_switch` cancels the dispatcher, unchanged); the
  tooltip says so.

No round or turn limit is added; the ceiling and the blocked rows are the safety limit,
as the 2026-09-11 spec decided.

## 7. Tests

Native (`make test`):
- `tests/test_cmd_policy.c`: `set_auto` on/off; `set_allowed` with the button on moves
  both markers and with it off moves one; no sequence of the two produces a split;
  `clamp` rounds a split down and `from_token` therefore does too; tooltip strings for
  all four levels in both label sets with the button on and off, and for the button in
  both states with the level name.
- `tests/test_ai_panel.c`, `tests/test_ns_layout.c`: section 3's tiers, thresholds,
  no-overlap (button, label, meter, pill) and clipped-hit cases; the pinned hit enum
  test extended with `HIT_POLICY_AUTO`.
- `tests/test_chat_approval.c`: a batch added under a policy with `unattended` forced
  to none has no approved row even when the session policy would approve all.
- Loader (`tests/test_config*.c`, wherever the token round-trip tests live): an old
  `write/read` token loads as `write/none`; `write/write` loads as on; `read/none`
  unchanged; save after load writes the normalised token.

What the native suite cannot reach, because `ai_chat.c` is `NON_TEST_SRCS`: the
confirmation box and its guard, the tooltip tools, the busy-label rect, the background
policy copy and the switch-back start. Covered on Windows: two new `--ui-demo` states,
`policy-auto` (button on at Write, a running card) and `policy-compact` (the panel at a
width below the compact threshold, set through the panel's persisted width before the
window is laid out), added to `src/core/ui_demo.c`'s state list and its test; gallery
screenshots of both in all four themes; the harness gate tier with the rewritten
`Set-NutshellAiPolicy`; `ai_write_command_held_then_runs_after_allow`
(`tests/integration/cases/20-ai.ps1`) unchanged in meaning, run with `-Allowed Write`
and no `-Auto` so its "Run selected" step still does the running; a new gate-tier case
that turns the button on at Critical and answers the box with No, then checks the policy
did not change; and the two AI-tier cases that have never been run are run once with the
key before the PR leaves draft.

## 8. Out of scope

Saving a session's live policy back to config (the Settings default is the only
persisted value, as now); a turn cap for unattended runs; any keyboard path; per-profile
policy; partial runs of a mixed batch; releasing already-waiting rows when the button
turns on.

## 9. Follow-up spec: a per-session dispatcher

Unattended processing of a session while another tab is displayed needs the dispatcher
to own its terminal, conversation and card list per session instead of reading the
panel's active ones, and the session switch to stop cancelling. That is a restructuring
of `ai_chat.c`'s dispatch path and gets its own spec; it goes on the notes' Open list
with this one's merge.

## Critique (2026-10-18, two rounds, Opus)

**Round 1**, ten findings:

1. Background dispatch is a per-panel dispatcher re-architecture and a session switch
   cancels the running batch. **Accepted**: dropped, section 9 opened.
2. The approved-prefix rule conflicts with `next_approved` picking any row and the batch
   ending under pending rows. **Accepted**: partial runs removed; mixed batches wait.
3. Release on policy change would run rows the same change unblocked. **Accepted** in
   round 1 by narrowing; superseded by round 2, item 1.
4. The context meter is already right-aligned; the layout is in `ai_panel_layout.c`;
   nothing clips today. **Accepted**: section 3 rewritten.
5. The invariant was enforced nowhere central; the demo seed bypasses it. **Accepted**:
   see round 2, item 9 for the final form.
6. Tooltips are native comctl32; the "Details link" is a click popup. **Accepted**.
7. No context menu with level entries; Ctrl+Shift+A would hit Ctrl+A; focus ring
   contradicts no-traversal. **Accepted**: keyboard section dropped.
8. Auto-approve at Unknown runs wrappers; one click reaches everything-unattended.
   **Accepted in part**: honest tooltip, Critical confirmation; the one-level model is
   the maintainer's decision.
9. Migration of a split token is lossy and one-way. **Rejected as a change**, recorded in
   section 2.
10. Wrong names, `STATUS_HIT_COUNT`, silent retired ids, nondeterministic AI-tier case.
    **Accepted**.

**Round 2**, fourteen findings on the revised spec:

1. The release helper cannot see ticks or the pre-change status, and "ticked" means
   nothing because rows are auto-ticked; off-on-off could run a formerly blocked
   command. **Accepted**: the release rule is dropped (section 6); one click instead.
2. Switching tabs postpones unattended processing: a background reply builds an
   all-approved batch that runs after an unrelated later prompt. **Accepted**: background
   batches are built with `unattended` forced to none and shown on switch-back.
3. "Head batch" undefined. **Accepted** by dropping the rule.
4. Mapping `4041`..`4044` through `set_allowed` lowers the ceiling and the helper's
   contract breaks silently. **Accepted**: ids retired, helper rewritten in the same PR.
5. Settings can reach Critical plus auto with no prompt. **Accepted**: the box on Save.
6. The message box has no re-entrancy handling. **Accepted**: session captured, guard,
   tooltip hidden, re-check after Yes.
7. The busy label paints over the button. **Accepted**: label rect from the layout.
8. One callback tool over the panel never re-triggers; the cell tip cannot warn about
   auto. **Accepted**: one rect tool per element; `auto_on` parameter.
9. Removing `set_unattended` breaks about fifteen tests and leaves `clamp`/`from_token`
   producing splits. **Accepted**: setter kept for tests, `clamp` normalises.
10. The hit enum is pinned by a test; `ns_policy_hit` takes a layout. **Accepted**:
    append `HIT_POLICY_AUTO`, `clip_right` on the layout.
11. The tiers miss the text-only meter state; clipped cells paint under the button.
    **Accepted**: five tiers, paint clip, clipped cells unhittable.
12. The demo and gallery cannot produce the described states. **Accepted**: two named
    states, width set through the persisted panel width.
13. A simpler design: the release only matters on off-to-on; drop it or snapshot.
    **Accepted**: dropped.
14. No pressed state; `4045` duplicates `4015`. **Accepted**: both removed.

Not verified by the critics and settled here: themes share one UI face (`ns_font`'s
single `g_ui_face`), so one compact threshold per DPI; no translated strings exist;
the loader tests' home is found by the implementer and named in the PR.
