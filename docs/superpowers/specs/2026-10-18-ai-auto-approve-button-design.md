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
job (a mixed batch holds its auto-approved rows, raising the marker does not release rows
already pending, and a background session never dispatches). This change gives the policy
row an explicit **Auto-approve** button at its right end, drops the rail, adds hover
tooltips to every cell and to the button, shrinks the labels to `R · U · W · C` and `Auto`
when the row is too narrow for the full words, and fixes the three dispatcher gaps so a
session left alone keeps processing.

---

## 2. Semantics

`CmdPolicy {allowed, unattended}` (`src/core/cmd_policy.h`) is unchanged, as are the
blocked / pending / approved outcomes in `chat_approval_add_session()`
(`src/core/chat_approval.c`). What changes is which states the UI can produce:

| Button | `unattended` |
|---|---|
| off | `POLICY_NONE` |
| on | `== allowed` |

- Clicking a cell sets `allowed = k`. If the button is on, `unattended` follows to `k`.
- Clicking the button toggles it: on sets `unattended = allowed`; off sets `POLICY_NONE`.
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
- Command ids: `4030`..`4033` (set allowed) stay. `4040`..`4044` (set unattended to
  none / k) collapse to one id, `4040`, **toggle Auto-approve**; `4041`..`4044` are
  retired, not reused. `4005` (cycle allowed) stays; `4015` (cycle unattended) becomes an
  alias of `4040`. The integration harness's `Set-NutshellAiPolicy` is updated to the
  new ids.

## 3. The control

The row is the AI panel's own status line (`src/ui/ai_panel_layout.c`), left-aligned
pill, nothing else on the row today.

- **Pill**: four touching cells, one band each (the rail is gone). Colour follows
  `allowed` as now: `success` for Read, `info` for Unknown, `warning` for Write, `danger`
  for Critical. Cells at or below `allowed` are tinted with the intent colour, cells above
  are `text_disabled` on the surface base. When the button is on, the cells at or below
  `allowed` are **solid** (intent `.base` with `.label` text) instead of tinted, so the
  pill itself says "these will run on their own". Radii from `ns_type_pill()`, stroke
  `STROKE_HAIRLINE`, font `FONT_CAPTION`, geometry through `ns_scale`.
- **Auto-approve button**: a pill of the same height at the **right edge** of the row,
  separated from the four cells by the row's free width. Off: outlined in `border`, label
  in `text_dim`. On: filled with the current level's intent `.base`, label in the
  intent's `.label`. Hover uses the intent's `.hover` when on and the surface `.hover`
  when off, through `ns_hover` with a new hit id `HIT_POLICY_AUTO`; pressed uses
  `.pressed`. Focus ring in `focus` when the control has keyboard focus (section 5).
- **Compact mode**. `ns_layout` computes the row at two label sets: full (`Read`,
  `Unknown`, `Write`, `Critical`, `Auto-approve`) and compact (`R`, `U`, `W`, `C`,
  `Auto`). If the full set plus the minimum gap (`SP_3`) does not fit the row width at
  the current font, the compact set is used. The decision is pure geometry in
  `ns_layout.c` (`ns_layout_policy_row()`, extended to return the button rect and a
  `compact` flag) and is tested natively with a fake text measurer. There is no third
  tier: below the compact width the pill clips, as it does today.
- Layout returns, for hit-testing and painting: four cell rects, the button rect, the
  `compact` flag, and the label strings to draw. Painting in `ns_draw.c` consumes that
  struct; `ai_chat.c` never computes a rect of its own.

## 4. Tooltips

- Hovering a cell or the button for the system hover delay shows a tooltip. The text
  lives in `src/core/cmd_policy.c` as `cmd_policy_cell_tip(level, compact)` and
  `cmd_policy_auto_tip(on, allowed)` so it is unit-tested and the compact variant can
  spell out the letter. `cmd_policy_tip` (the existing whole-control tip) is removed.
- Cell text, one level per line of the tooltip, two short sentences: what the level
  means, then how the click works. The strings:
  - Read: *"Read: commands that only inspect, such as ls, cat or git status. Click to make
    this the highest level the assistant may run; anything above it is blocked."*
  - Unknown: *"Unknown: commands the classifier cannot place. Click to allow up to this
    level; Write and Critical stay blocked."*
  - Write: *"Write: commands that change files or state, such as rm, mv or apt install.
    Click to allow up to this level; Critical stays blocked."*
  - Critical: *"Critical: commands that can destroy data or the system, such as rm -rf /
    or mkfs. Click to allow everything. Use with care."*
  - Compact variants prefix the letter: *"R = Read: ..."*.
  - Auto-approve, off: *"Auto-approve: off. Click to run commands up to the <Level> level
    without asking. Anything above <Level> stays blocked."*
  - Auto-approve, on: *"Auto-approve: on. Commands up to <Level> run without asking;
    anything above is blocked. Click to turn off and approve each batch yourself."*
- Rendering: the panel's existing tooltip path (the one the command rows' Details link
  uses), tokens only, `FONT_CAPTION`, `R_CTRL` radius, positioned below the hovered rect
  and kept inside the panel. The tooltip hides on mouse leave, on click and on any
  policy change.

## 5. Keyboard path (small, since the ids are already there)

The 2026-09-11 spec left the keyboard out. The button makes the gap more visible, so:
`Ctrl+Shift+A` toggles Auto-approve (`4040`) while the AI panel has focus; it is listed
on the panel's context menu next to the four level entries (`4030`..`4033`), so the
whole control is reachable without the mouse. No focus traversal into the pill is added.

## 6. Dispatcher changes

Three changes in `src/ui/ai_chat.c` and `src/core/chat_approval.c`, each with a native
test on the core side:

1. **Partial batch run.** `chat_approval_needs_user()` stays as the "card must be shown"
   test, but the batch start path no longer waits for the whole batch: rows marked
   approved on arrival are dispatched at once (`dispatch_start()` on the approved
   subset), the card shows them as running / ran while the pending rows keep their
   tickboxes. Order within the batch is preserved: a pending row that precedes an
   approved row holds the approved row, because the model wrote them in order and the
   second may depend on the first. So "runs at once" means the longest approved prefix
   of the batch. A test covers approved-prefix, pending-first, and all-approved.
2. **Release on policy change.** When the button turns on or `allowed` rises while it is
   on, every pending row at or below the new `unattended` in the displayed session's
   queue is marked approved and the batch start path runs again, subject to rule 1.
   Blocked rows are unaffected by this rule; whether a level change re-evaluates them is unchanged.
3. **Background sessions dispatch.** A batch built for a session that is not displayed
   (`ai_chat.c`, the build path around line 4356) runs the same approval and
   `dispatch_start()` decision as a displayed one. `maybe_start_next_batch` takes the
   session as a parameter and is called for the session that owns the finished stream,
   not the displayed one; switching sessions calls it for the newly displayed session
   so a batch that was waiting for a click shows its card. The terminal writes already
   go to the owning session's transport, so nothing else changes.

No round or turn limit is added; the ceiling and the blocked rows are the safety limit,
as the 2026-09-11 spec decided.

## 7. Tests

Native (`make test`):
- `cmd_policy`: toggle on/off, level change with the button on and off, normalise
  rounds a split marker down, tooltip strings for all four levels in both label sets
  and for the button in both states.
- `ns_layout`: the row at a wide width uses full labels and places the button flush
  right; at a narrow width uses compact labels; hit-test maps each cell and the button;
  the pill and button never overlap at the compact threshold.
- `chat_approval`: the three dispatcher cases of section 6.
- Loader: an old `write/read` token loads as `write/none`; `write/write` loads as on.

Windows: `--ui-demo=ai-policy` gains the button in both states and the compact row;
gallery screenshots in all four themes; harness `-Tier gate` cases that set policy
through `Set-NutshellAiPolicy`; the two AI-tier cases that have never run
(`ai_runs_read_command_unattended`, `ai_write_command_held_then_runs_after_allow`) are
run once with the key before the PR leaves draft, and a third AI-tier case covers a
mixed batch.

## 8. Out of scope

Saving a session's live policy back to config (the Settings default is the only
persisted value, as now); a turn cap for unattended runs; focus traversal into the pill
cells; per-profile policy.
