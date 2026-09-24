# AI Assist Panel — Design

**Date**: 2026-09-07
**Branch**: `ui-polish`
**Status**: Approved — Thomas took the recommendations on the mockup page
(card 1, "safe only" / "all" wording, empty state with three suggestions), then chose **frame B** over A.
**Depends on**: the design-system foundation (`2026-09-07-design-system-foundation-design.md`, done in v1.0.92).
**Mockups**: AI Assist Panel Options artifact (frame B / card 1 are the chosen ones).
**Plan**: `../plans/2026-09-07-ai-assist-panel.md`.

## Problem

The panel is the product's signature feature and the review's first three
weaknesses live in it: the approval card is loud and repeats "Blocked" per row,
the two safety toggles show their state only through a letter icon's colour,
and the tool buttons have no labels or tooltips. A fresh panel is one grey
line, and two MessageBoxes ("No active SSH session", "No AI API key") block the
panel from opening at all instead of telling the user what to do next.

## Structure (frame B)

Top to bottom, all sizes from `ns_type`, all colours from `ns_tokens()`:

1. **Header** (`SP_XXL` tall): session name (`FONT_TITLE`), model chip
   (`ns_draw_chip`, `raised`/`text_dim`), spacer, then three icon buttons with
   tooltips — New chat, Save chat, Undock/Dock.
2. **Thread** — the existing `chat_listview`, taking all the height between
   header and status line; unchanged except the approval card and the empty
   state below.
3. **Status line** (`SZ_CTRL_H` tall, `bg_secondary`, `border` rule above),
   directly above the composer, painted (not child windows) with `ns_hover`,
   hand cursor and tooltips:
   - **Mode segmented control** — `Read-only` | `Read + write`, drawn by a new
     `ns_draw_segmented` primitive (selected segment `raised` fill + `text_main`,
     the other `text_dim`; `Read + write` selected tints the segment `warning`
     with its `label` colour so a permissive session is visible at a glance).
     Maps onto the existing `IDC_CHAT_PERMIT` handler.
   - **Auto approve** — a text control "Auto approve: off / safe only / all";
     click cycles session auto-approve on/off (existing `IDC_CHAT_AUTOAPPROVE`
     id, so the integration harness keeps working). "all" appears only when
     the Settings option "Auto Approve also covers write/critical commands"
     is on.
   - spacer, then the **context meter**: a 60 px bar filled `accent` for
     used/limit plus `used / limit` in `FONT_CAPTION` with tabular numerals;
     replaces the boxed "Context: N / 200k (0%)" label.
4. **Composer** — unchanged position and behaviour (input, Send; Stop while
   streaming). The input's placeholder becomes "Message the assistant…".

The four themes render through tokens; nothing in the panel may use a literal
colour (the gate enforces it).

## Approval card (card 1)

- **Header row**: `N commands · M held` in `FONT_BODY` bold, and when M > 0 the
  reason in `text_dim`: "Permit write is off". This replaces the per-row
  "Blocked" label and the lock icon.
- **Rows**: checkbox · command (`FONT_MONO`, ellipsised) · risk chip
  (safe/write/critical on `text_dim`/`warning`/`danger` with the surface's
  `label` colour). Held rows: checkbox disabled, command in `text_dim`.
  Rows are always single-line: with per-row Allow/Deny gone the text keeps its
  width at any panel size, so the two-line rule from v1.0.91 is removed.
- **Actions row**: `Deny all` (ghost button: `border` stroke, `text_main`) and
  `Run N selected` (primary, `success`; disabled when nothing is selected).
  Pending safe rows start checked. With session auto-approve on, safe rows are
  approved on arrival exactly as today and the card shows them as running.
- **Executing / completed** rows keep the activity dot / tick as today.
- `ns_layout`: `approval_card_layout` v2 returns per row `{ checkbox, text,
  tag }`, plus `{ header, deny_all, run_selected }`; `HIT_ALLOW`/`HIT_DENY` go,
  `HIT_RUN_SELECTED` replaces `HIT_ALLOW_ALL`. `approval_row_height` stays but
  no longer doubles.

## Thought process (Thomas: "make sure there's a way to view the AI thought process")

Every AI reply that carried reasoning keeps it, and it is always reachable:

- A **Thinking** disclosure sits above the reply text: a row with a chevron,
  the word "Thinking", and a `text_dim` summary — `· 240 words` when collapsed,
  `· streaming…` while it is still arriving. Clicking anywhere on the row
  toggles it; `ns_hover` gives it the hand cursor.
- **Expanded** shows the full reasoning in `text_dim` on `bg_secondary` with a
  `STROKE_BAR` `accent` bar down the left, wrapped, in `FONT_BODY`; long
  reasoning scrolls inside a box capped at `THINKING_MAX_LINES` (50) lines of
  its own text (`ai_thinking_max_body_h()`, `ai_panel_layout.h`) rather than
  pushing the reply off screen, with a thin scrollbar once it overflows.
  Amended 2026-09-24 (maintainer request): originally capped at half the
  thread height; a fixed line count reads more predictably regardless of
  panel size.
- **Every disclosure starts collapsed**, including while its reply is still
  streaming — amended 2026-09-24 (maintainer request: "most people don't
  want to see it"). It no longer opens itself just because reasoning is
  arriving. The one carry-over from the original design is a per-session
  preference (`show_thinking`): once the user has opened a disclosure
  themselves this session, later replies open already-expanded too, so
  they aren't re-clicking every turn; a manual click on any one disclosure
  always overrides that for the rest of its own reply
  (`thinking_user_set`).
- **While an expanded box is streaming**, it follows the newest text as long
  as the user is scrolled to its bottom; scrolling up inside it stops the
  follow and holds the position until they scroll back down
  (`stick_scroll_on_layout()`/`stick_scroll_after_user()`,
  `src/core/stick_scroll.c` — the same "stuck vs released" logic the outer
  chat list already used for its own stick-to-bottom behaviour, reused
  rather than reimplemented for the box).
- Providers that send no reasoning show no disclosure at all — no empty row.
- Geometry comes from `ai_panel_layout`'s `thinking_layout(rect, expanded,
  dpi) → { row, chevron, label, summary, body }`. The `chat_thinking` core
  module (`ThinkingState`/`chat_thinking_init()` et al.) this section
  originally pointed to turned out unused in practice — the real
  open/closed/streaming state lives on `ChatMsgItem.u.ai` instead
  (`thinking_collapsed`, `thinking_user_set`, `thinking_autoscroll`,
  `thinking_scroll_y`, `thinking_elapsed`, `thinking_complete` —
  `src/core/chat_msg.h`), owned by `src/ui/ai_chat.c` and
  `src/ui/chat_listview.c`.
- The demo `chat` state carries a thinking block so the gallery shows the
  collapsed form; the `thinking` state (2026-09-24) carries a much longer one
  so it can be expanded to show the box at its 50-line cap with a scrollbar.

## Empty and blocked states (frame C)

Rendered inside the thread area by `chat_listview` when it has no items, from a
small core table `ai_panel_states.{c,h}` so the copy is testable:

| State | Title | Body | Action |
|---|---|---|---|
| `empty` | Ask about this session | The assistant sees the last *N* lines of your terminal (N from `ai_max_context_lines`) and can run commands with your approval. | three suggestion chips: "What's using disk?", "Why did that fail?", "Summarise the log" — clicking one sends it as a prompt |
| `no_key` | Add an API key to use the assistant | Choose a provider and paste a key in Settings. Nothing is sent anywhere until you do. | button **Open Settings** → opens Settings on AI Assistant › Provider |
| `no_session` | Connect to a session first | The assistant works on a live terminal. Open the Session Manager to connect. | button **Open Session Manager** |

The panel therefore opens in every case; the two MessageBoxes in `window.c`
(`on_ai_clicked`) are deleted. The glyph is `NS_ICON_AI` in a `border`-stroked
circle, `accent` foreground. Suggestion chips are a constant table in core;
Thomas can replace the three strings.

## Settings entry point

`settings_dlg_show()` gains an optional initial page id so "Open Settings" from
the no-key state lands on Provider. No other Settings change.

## Demo / gallery

`ui_demo` gains states `nokey` and `nosession` (and `empty` now shows the real
empty state). `ui_gallery` captures them; the existing `approval` state shows
card 1.

## Tests

- Native: `ai_panel_layout(rect, dpi)` → header / thread / status / composer
  rects tile the panel with no overlap at 96 and 192; status-line geometry
  (segmented control with two segments, auto-approve text, meter) inside the
  line with no overlap; approval card v2
  rects (no overlap, inside parent, hit round-trip, `Run N selected` disabled
  when none checked → reported by layout as `run_enabled`); mode label
  function `ai_modes_label(auto_on, all_setting)` → "off"/"safe only"/"all";
  state-table copy contains the live N; suggestions are exactly three non-empty
  strings; `ui_demo` counts for the new states.
- Gates stay at zero (new drawing goes through `ns_draw`).
- Integration: the existing `ai_*` cases pass unchanged (same control ids);
  `ui_gallery` includes the two new states; a new keystroke-free case
  `ai_panel_opens_without_key` asserts the panel opens with an empty key and
  shows the no-key state (capture non-blank, no `#32770` dialog).

## Definition of done

1. Frame B header, status line with segmented mode control; card 1; three states; MessageBoxes
   gone. 2. Gates at zero, native suite green, `wintest` green. 3. Gallery
   reviewed by Thomas in all four themes. 4. The three `ai_*` integration cases
   green against Moonshot (two requests) once the desktop is unlocked.
