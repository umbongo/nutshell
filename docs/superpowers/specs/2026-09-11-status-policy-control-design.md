# One status-line policy control, and SAFE → READ

**Date**: 2026-09-11
**Thomas**: *"no need for a mock-up, use the cleaner-still version, also change the wording
throughout from 'safe' to 'read'. create a new branch for this."*
**Replaces**: the `Read-only` / `Read + write` segmented switch **and** the separate
`Auto approve: <mode>` cycle, both from
`2026-09-07-command-dispatch-and-auto-approve-levels.md` / `2026-09-11-unknown-safety-category-design.md`.
**Builds on**: `2026-09-11-unknown-safety-category-design.md` (the four categories, the
segment mask), `2026-09-09-pending-command-batches.md` (per-batch queues).

---

## 1. The change in one paragraph

The AI panel's status line loses its two controls and gains one: a four-stop scale
**`Read · Unknown · Write · Critical`** carrying **two markers**. The upper marker,
*allowed*, is a ceiling — a command whose category sits above it is BLOCKED and shows as
held. The lower marker, *unattended*, is how far the session will run without asking —
anything at or below it runs immediately; anything between the two markers asks first. The
two markers are the two axes the code already has (`permit_write` = "may it run at all",
auto-approve = "may it run without asking"); they stay distinct because collapsing them
loses the most common posture there is, *allow writes, but always show me first*. The
category `SAFE` is renamed `READ` throughout.

---

## 2. The model

```c
/* src/core/cmd_policy.h */
#define POLICY_NONE (-1)          /* the unattended marker's "nothing" position */

typedef struct {
    int allowed;      /* CmdSafetyLevel 0..3 -- the ceiling */
    int unattended;   /* CmdSafetyLevel 0..3, or POLICY_NONE */
} CmdPolicy;
```

Stops are exactly `CmdSafetyLevel`: `CMD_READ = 0`, `CMD_UNKNOWN = 1`, `CMD_WRITE = 2`,
`CMD_CRITICAL = 3`. The unattended marker has one extra position *below* Read, `POLICY_NONE`,
because "auto-approve off" has to remain expressible — that is today's default and the
default a fresh install keeps.

**The invariant is `unattended <= allowed`**, enforced in the setters rather than checked at
the gate:

- `cmd_policy_set_allowed(p, stop)` clamps `stop` to `0..3` and then **drags `unattended`
  down** with it if it would otherwise be left above the ceiling. Lowering the ceiling is a
  tightening gesture; it must never leave a higher unattended marker behind.
- `cmd_policy_set_unattended(p, stop)` clamps `stop` to `POLICY_NONE..p->allowed`. Raising
  the unattended marker never raises the ceiling: you cannot ask for something to run
  unattended that you have not first allowed.
- `cmd_policy_clamp(p)` normalises anything that arrives from disk or from a caller.

The decision, replacing both of the old ones:

```c
int cmd_policy_blocks(CmdPolicy p, CmdSafetyLevel worst);          /* worst > p.allowed */
int cmd_policy_runs_unattended(CmdPolicy p, unsigned safety_mask); /* mask ⊆ prefix(unattended) */
unsigned cmd_policy_unattended_mask(CmdPolicy p);                  /* bits 0..unattended */
```

`chat_approval_add()` becomes, in order:

```
worst > policy.allowed                       -> APPROVE_BLOCKED
runs_unattended(policy, entry->safety_mask)  -> APPROVE_APPROVED
otherwise                                    -> APPROVE_PENDING
```

---

## 3. The design decision: what the scale can and cannot say

Today's auto-approve is a **set** of permitted categories, and the five sets are deliberately
not nested (`2026-09-11-unknown-safety-category-design.md` §5):

| old mode | set | reachable on the scale? |
|---|---|---|
| `off` | {} | yes — `unattended = POLICY_NONE` |
| `safe` | {READ} | yes — `unattended = READ` |
| `safe+unknown` | {READ, UNKNOWN} | yes — `unattended = UNKNOWN` |
| `safe+write` | {READ, WRITE} | **no** |
| `safe+unknown+write` | {READ, UNKNOWN, WRITE} | yes — `unattended = WRITE` |
| `all` | {READ, UNKNOWN, WRITE, CRITICAL} | yes — `unattended = CRITICAL` |

A marker on an ordered scale can only express a **downward-closed (prefix) set**. Four of the
five old modes are prefixes of `Read < Unknown < Write < Critical` and survive exactly.
`safe+write` is not: it says *run writes unattended but hold unrecognised commands back*, and
that skips a stop.

**The loss is accepted.** Three reasons, in the order that matters:

1. **It cannot be made safe to keep.** Reaching {READ, WRITE} means running the category the
   classifier *understands* without looking, while holding the category it *does not
   understand*. On the scale the user is shown, UNKNOWN sits below WRITE — so the old mode
   asks the UI to auto-run a stop it is simultaneously showing as held back below it. There
   is no honest way to paint that on one scale, and a control that cannot show its own state
   is worse than a missing mode.
2. **Its useful half survives via the other marker.** The posture `safe+write` really serves —
   "writes are fine here, unrecognised things are not" — is now `allowed = Write,
   unattended = Read`: writes are permitted and shown to you, unknowns are permitted and shown
   to you, nothing surprising runs on its own. What is lost is only the *unattended* running of
   writes while unknowns are held, which is a narrower claim than the mode's name suggested.
3. **Nothing gets more permissive.** Every position the new control can reach is a set the old
   model could also reach; migration (§6) rounds `safe+write` **down** to `{READ}`, never up.

**What the scale gains in exchange** is on the other marker, and it is not small: `allowed`
now has four positions where `permit_write` had two. `allowed = Unknown` (accept commands the
classifier cannot vouch for, refuse real writes) and `allowed = Write` (writes yes, outage-class
commands never, not even by clicking Approve) were simply not expressible before. The ceiling
axis goes from 2 states to 4; the unattended axis goes from 6 to 5. Net: 24 reachable policies
against the old model's 12 (2 ceilings × 6 modes), and every one of them is paintable.

**A corollary worth stating**, because it looks like the mask machinery from
`2026-09-11-unknown-safety-category-design.md` §4 just became redundant: for a *downward-closed*
permitted set, testing the whole segment mask and testing only the worst segment are
equivalent — `mask ⊆ prefix(k)  ⟺  max(mask) ≤ k`. The mask stays anyway. `ApprovalEntry.safety_mask`
is already computed, already tested, and costs nothing; writing the gate as a set test keeps it
correct if a future stop set is ever not a prefix again, and the equivalence is pinned by an
exhaustive test (§8) rather than assumed by the code.

---

## 4. The control

```
 ┌───────┬─────────┬───────┬──────────┐
 │ Read  │ Unknown │ Write │ Critical │      ← label band  (sets `allowed`)
 ├───────┴─────────┼───────┴──────────┤
 │▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀│                  │      ← rail band   (sets `unattended`)
 └─────────────────┴──────────────────┘
   allowed = Write, unattended = Unknown
```

One pill, four cells, two horizontal bands.

**Paint.** All colour comes from `ns_tokens()`; no new literals. The control's *intent* colour
is chosen by the `allowed` stop, so a permissive session stays visible at a glance the way the
warning-tinted `Read + write` segment does today:

| `allowed` | intent surface |
|---|---|
| Read | `success` |
| Unknown | `info` |
| Write | `warning` |
| Critical | `danger` |

Per cell, by the cell's stop `k`:

| condition | cell fill | label colour |
|---|---|---|
| `k <= unattended` | `intent.base` | `intent.label` |
| `unattended < k <= allowed` | `rgb_alpha(intent.base, bg_secondary.base, 0.18)` | `text_main` |
| `k > allowed` | `bg_secondary.base` | `text_disabled` |

Hover steps the cell under the cursor one state brighter (`intent.hover`, or a 0.30 blend) via
`ns_hover`; the two elements that changed are the only ones invalidated. The pill has a
`border` hairline and radius `R_CTRL`. The rail band is `STROKE_BAR` tall: `intent.base` from
the control's left edge to the right edge of cell `unattended`, `border` for the remainder —
so the filled length *is* the unattended marker, and an empty rail *is* "nothing runs
unattended".

**Geometry and hit-test are pure**, in `src/core/ns_layout.c`:

```c
#define NS_POLICY_STOPS 4

typedef struct {
    NsRect cell[NS_POLICY_STOPS];       /* the painted cells                     */
    NsRect label[NS_POLICY_STOPS];      /* painted upper band (the stop's text)  */
    NsRect rail[NS_POLICY_STOPS];       /* painted lower band (the rail)         */
    NsRect hit_label[NS_POLICY_STOPS];  /* click target for `allowed`            */
    NsRect hit_rail[NS_POLICY_STOPS];   /* click target for `unattended`         */
    NsRect rail_fill;                   /* the filled part of the rail           */
    int total_w;
} NsPolicyLayout;

int  ns_policy_width(const int stop_text_w[NS_POLICY_STOPS], int dpi);
void ns_policy_layout(NsRect r, const int stop_text_w[NS_POLICY_STOPS],
                      int unattended, int hit_h, int dpi, NsPolicyLayout *out);
int  ns_policy_hit(const NsPolicyLayout *l, int x, int y, int *stop_out);
    /* -> HIT_POLICY_ALLOWED | HIT_POLICY_UNATTENDED | HIT_NONE */
```

Cells touch (one control, not four). Each cell is `max(1, stop_text_w[k] + 2*SP_SM)` wide, and
`ns_policy_width()` gives their sum so the caller can size `r` before laying it out. The bands
split the cell at ⅔ of its height for *painting*; for *hitting*, the two bands tile a box of
height `max(hit_h, r.h)` centred on `r`, split at the same y — the panel passes the full
status-line height, so the painted rail is 3 px but the band that accepts a click on it is
most of the row. That split is the reason the geometry is pure and tested: a 3 px click target
would be a bug, and it is invisible in a screenshot.

**Click.** Label band on cell `k` → `cmd_policy_set_allowed(k)`. Rail band on cell `k` →
`cmd_policy_set_unattended(k)`, except when `unattended == k` already, which sets
`POLICY_NONE` — the one toggle, and the only way back to "nothing unattended".

**Command ids** (so every gesture has a non-mouse path, and so the integration harness can
post an *absolute* position rather than counting cycles):

| id | meaning |
|---|---|
| `IDC_CHAT_POLICY_ALLOW_BASE + k` (4030..4033) | set `allowed` = k |
| `IDC_CHAT_POLICY_AUTO_BASE + (k+1)` (4040..4044) | set `unattended` = k, index 0 = `POLICY_NONE` |
| `IDC_CHAT_PERMIT` (4005, kept) | cycle `allowed`: Read → Unknown → Write → Critical → Read |
| `IDC_CHAT_AUTOAPPROVE` (4015, kept) | cycle `unattended`: none → Read → … → `allowed` → none |

**Keyboard** is unchanged, i.e. still none: the status line is painted on the panel's client
area, not built from child windows, and neither of the two controls being replaced had a
focus ring or a key path either. Giving painted status-line elements real keyboard focus is a
design-system job for the whole line (meter included), not something to bolt onto one widget;
the four command ids above are what a menu item or accelerator would post when it is done.
This is a deliberate non-goal of this package, and not a regression.

**Tooltip** text is built by a pure, tested function so the wording is testable without a
window: `cmd_policy_tip(policy, band, stop, buf, cap)`, e.g. *"Allow Write commands — they
still ask first."*, *"Block anything above Unknown."*, *"Run Read and below without asking."*,
*"Stop running anything unattended."*. `cmd_policy_caption(policy, buf, cap)` gives the
one-line summary — *"allowed to Write, runs Read unattended"* — used by the panel's tooltip
fallback and by `--ui-demo`.

**Raising and lowering have side effects, unchanged from today's two controls.** Raising
`allowed` unblocks the BLOCKED entries in every pending batch that the new ceiling allows
(→ PENDING) and injects the corrective note into the conversation so the AI stops citing a
policy block; lowering it re-blocks pending entries above the new ceiling. Both already exist
behind `IDC_CHAT_PERMIT`; they now key off the ceiling's new value rather than a boolean, and
`chat_approval_unblock_all()` becomes the exact inverse of
`chat_approval_block_disallowed()` — it releases only what the policy allows, so raising the
ceiling from Read to Write does not also release a CRITICAL row (the old unconditional
version would have).

---

## 5. Where the state lives

`ApprovalQueue.auto_approve` / `.auto_approve_level` / `.auto_approve_confirming` /
`.confirm_start_time` are replaced by a single `CmdPolicy policy;`. `AiChatData.permit_write`
is replaced by the same `CmdPolicy` on the panel, copied into each new batch by
`cmd_batch_add()` exactly as `auto_approve`/`auto_approve_level` were; `AiSessionState` carries
one `CmdPolicy` instead of the three fields. `chat_approval_add()` drops its `permit_write`
parameter — the queue's own policy is the only input.

`chat_approval_auto_approve_click()` / `chat_approval_revoke_auto()` and the double-click
"are you sure?" confirm go with them. They served the approval card's old *Allow all session*
button, which was removed when the card was redesigned: nothing has posted `IDC_AUTO_APPROVE`
(3041) since, so the handler, the two fields and the timeout are already unreachable. Deleting
them here is not extra scope — they are the last state that only the replaced controls named.

---

## 6. Config: one setting, and its migration

One key replaces one key, but it now carries both markers — so a user can, for the first time,
make new sessions start with a ceiling above Read:

```json
"ai_policy_default": "write/read"      /* "<allowed>/<unattended>" */
```

Stop tokens: `none` (unattended only), `read`, `unknown`, `write`, `critical`. `Settings` gets
`CmdPolicy ai_policy_default;` — one field, parsed and printed by
`cmd_policy_from_token()` / `cmd_policy_to_token()` in `src/core/cmd_policy.c` so both are
natively testable. `settings_validate()` calls `cmd_policy_clamp()`. Default for a fresh
install: `read/none` — read-only, nothing unattended, exactly today's default.

**Migration**, in the loader, first match wins:

1. `ai_policy_default` present → parse it (garbage → the default; `unattended > allowed` →
   clamped down).
2. else `ai_auto_approve_mode` present (v1.1.16 configs) → `allowed = read`, and
   `unattended = none` when the mode was `off`, `read` otherwise.
3. else the legacy numeric `ai_auto_approve_default` (pre-v1.1.16) → mapped to a mode by the
   existing `{0,1,3,5}` table, then rule 2.
4. else the default.

Rule 2 looks lossy — every non-`off` mode collapses to `read` — and it is, but only of intent
that never took effect. `permit_write` was per-session and **always started off**, so in every
fresh session a command above READ was BLOCKED before the auto-approve gate ever saw it: the
only category that could actually run unattended in a new session was READ, whatever the mode
said. Mapping the stored mode's top category onto the new `allowed` marker instead would have
started new sessions with a higher ceiling than the same config gives them today. **A migration
must never make a session more permissive than it already is**, so the ceiling migrates to
`read` and the user raises it deliberately.

Save writes `ai_policy_default` only; neither old key is written back.

**Settings › AI Assistant › Behaviour** replaces the single `Auto Approve:` combo with the two
markers, which is what the one setting contains: **Allowed (asks first)** — Read / Unknown /
Write / Critical — and **Runs unattended** — Nothing / Read / Unknown / Write / Critical, the
second clamped to the first on OK the same way the control clamps it (`IDC_AI_POLICY_ALLOWED`
reuses 3073, `IDC_AI_POLICY_UNATTENDED` is 3076).

---

## 7. SAFE → READ

Renamed — the *category*:

- `CMD_SAFE` → `CMD_READ` (`src/core/cmd_classify.h` and every use), and the file-local
  Linux allow-list names that spell the category (`linux_safe_cmds` → `linux_read_cmds`,
  `is_safe_subcommand` → `is_read_subcommand`).
- The approval-card chip text `"SAFE"` → `"READ"` in `chat_listview.c`.
- `AUTO_APPROVE_SAFE*` and the `safe…` config tokens: gone with the mode enum (§5, §6).
- `ai_modes_label()` / `ai_permit_label()`: gone, replaced by `cmd_policy_stop_label()` and
  `cmd_policy_caption()`.
- Prose: README (feature list, the Settings and status-line sections, the category table),
  the in-app User Guide (`src/ui/help_guide.c`), `--ui-demo` comments.

**Not renamed** — these name the *axis*, not the category, and stay exactly as they are:
`CmdSafetyLevel`, `ApprovalEntry.safety`, `ApprovalEntry.safety_mask`, `safety_tag_colors()`,
`safety_tag_text()`, `cmd_classify*()`. `ai_command_is_readonly()` in `ai_prompt.[ch]` is
deleted by PR #25 and is not touched here.

---

## 8. Test plan

**`tests/test_cmd_policy.c`** (new)
- default is `{read, none}`; `clamp` normalises out-of-range, negative and inverted input.
- `set_allowed` clamps to 0..3 and drags `unattended` down; `set_unattended` clamps to
  `none..allowed` and never raises `allowed`. Invariant holds after any sequence.
- `blocks()` truth table: 4 categories × 4 ceilings.
- `runs_unattended()` over all 16 possible segment masks × all 5 unattended positions.
- **the equivalence**: for every one of those 80 combinations,
  `runs_unattended(p, mask) == (max_category(mask) <= p.unattended)` — pins §3's corollary.
- **the documented loss**: no policy yields an unattended set of `{READ, WRITE}`.
- token round-trip over all 24 legal pairs; `"write/critical"` clamps to `write/write`;
  garbage, empty and NULL → the default.
- `cmd_policy_caption()` / `cmd_policy_tip()` strings, including the `POLICY_NONE` wording and
  the "already at this stop" toggle tip.

**`tests/test_ns_layout.c`** — `ns_policy_width` / `ns_policy_layout` / `ns_policy_hit`: cells
touch and sum to `total_w`; bands split and cover the full row height for hitting; a point in
each of the 8 zones returns the right `(band, stop)`; points outside → `HIT_NONE`; NULL layout
and NULL `stop_out` are safe; 96 and 192 DPI; `rail_fill` is empty at `POLICY_NONE` and reaches
cell 3's right edge at `critical`.

**`tests/test_chat_approval.c`** — the decision matrix rewritten against the policy: blocked
above the ceiling, approved at or below the unattended marker, pending in between, for all 4
categories × the 24 policies; the mixed-pipeline case (`{UNKNOWN, WRITE}`) at
`unattended = unknown` stays PENDING; `unblock_all` / `block_pending_writes` after a ceiling
move; `chat_approval_reset` preserves the policy.

**`tests/test_config.c`** — `ai_policy_default` round-trips for all 24 pairs; a v1.1.16
`ai_auto_approve_mode` config migrates per §6 rule 2 (each of the six tokens, asserting the
ceiling stays `read`); a pre-v1.1.16 numeric config migrates per rule 3; `ai_policy_default`
wins when both keys are present; garbage → default; the saved JSON contains neither old key.

**`tests/test_ai_panel.c`** — `ai_status_layout` with the policy rect in place of the two
segments: nothing overlaps, everything stays inside the line, the meter still degrades
bar-then-text on a narrow panel.

**Integration** (`tests/integration/cases/20-ai.ps1`, `NutshellIT.psm1`) — `Set-NutshellAiAutoApprove`
becomes `Set-NutshellAiPolicy -Allowed <stop> -Unattended <stop>`, posting the absolute ids
from §4 (no cycle counting, no foreground, no screen-size assumption).
`ai_runs_read_command_unattended` and `ai_commands_run_one_at_a_time` set `-Unattended Read`.
`ai_write_command_held_then_runs_after_allow` (renamed from `…_after_permit`) sets
`-Unattended Read`, confirms the WRITE command is held at ceiling Read, then posts
`-Allowed Write` and Run selected. `ai_prompt_while_approval_pending` is unaffected. Every case
stays a hard test — none is skipped and none needs a desktop.

**Eyeball** — `nutshell.exe --ui-demo=all` across the four themes for the control's three cell
states, the rail, and the intent colour changing with the ceiling. The demo session is seeded
with `allowed = Write, unattended = Read` (in `window.c`, where the demo tab is built) rather
than the `{read, none}` default, so the `ui_gallery` integration case captures all three cell
states and a partly-filled rail instead of forty screenshots of the same one.
