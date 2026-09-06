# Design-System Foundation — Design

**Date**: 2026-09-07
**Branch**: `ui-polish`
**Status**: In progress — sections are appended as Thomas approves them
(see `2026-09-06-ui-redesign-notes.md` for the roadmap this is step 1 of).

Rendering stays GDI/GDI+. Everything below that can be reasoned about without a
window handle lives in `src/core` and is covered by the native test suite.

## 1. Semantic colour tokens — APPROVED 2026-09-07

### Palette (unchanged, hand-picked per theme)

The four themes keep their eight base colours exactly as they are:
`bg_primary`, `bg_secondary`, `accent`, `text_main`, `text_dim`, `border`,
`terminal_fg`, `terminal_bg`. One change: **each light theme gets its own accent**
rather than sharing the dark theme's. Onyx Light moves from `0x007AFF` (shared with
Onyx Synapse) to a deeper blue that holds 4.5:1 on its light surfaces, proposed
`0x0A5FD6`; Moss & Mist already has its own (`0x84A98C`). Final values are tuned
against the contrast tests below.

### Intent colours (new, hand-picked per theme)

Five per theme: `success`, `warning`, `danger`, `info`, `link`. The existing
activity-dot greens/yellows/reds seed the first three. Every hardcoded colour in
the approval card maps onto them:

| Today (hardcoded in `chat_listview.c`) | Token |
|---|---|
| SAFE tag grey | `text_dim` |
| WRITE tag orange | `warning` |
| CRITICAL tag red | `danger` |
| Allow / checkbox fill green | `success` |
| Deny / Cancel red | `danger` |
| Allow All pastel orange | `warning` |
| Retry link, auto-approve link | `link` |
| `[EXEC]` tag purple | `info` |
| Blocked command text | `text_dim` |

The tab strip's and AI panel's remaining literals map onto surface/border/accent
tokens the same way.

### Derived interaction states (computed, per-theme override allowed)

For every surface token (`bg_primary`, `bg_secondary`, `accent`, and each intent
colour) core derives:

| State | Rule |
|---|---|
| `hover` | shift luminance one step toward the opposite of the theme's polarity: lighten on dark themes, darken on light ones |
| `pressed` | two steps |
| `disabled` | blend 50 % toward `bg_primary`; text tokens blend toward `text_dim` |
| `focus` | the theme's `accent` at full strength, drawn as a 2 px ring |
| `raised` | `bg_secondary` shifted one step (cards, popups) |

"One step" is a fixed relative luminance delta chosen so the change is visible on
every theme (validated by test). A theme may override any derived value by
supplying it explicitly; unspecified values are derived.

### Resolve step

`ui_theme_resolve(const ThemeColors *base, ThemeTokens *out)` in
`src/core/ui_theme.c` fills a complete token struct once, when the theme is
chosen or changed. UI code reads `ThemeTokens` only and never computes a colour.
`ThemeColors` stays as the authoring format so the existing theme table is not
rewritten.

### Tests (native suite)

- Contrast: every text-on-surface pairing the UI uses (`text_main`/`text_dim` on
  `bg_primary`/`bg_secondary`/`raised`; button label on each intent colour and on
  `accent`) is ≥ 4.5:1 in all four themes.
- Derivation: each `hover`/`pressed` differs from its base by at least the minimum
  step; `disabled` is closer to `bg_primary` than its base; derived values never
  clip to pure black/white.
- Coverage: a test scans `src/ui/*.c` and fails if a new `RGB(` literal appears
  outside the shared drawing module (`ui_draw.c`, and a short allow-list for the
  few that are genuinely non-thematic, e.g. the acorn watermark).

## 2. Spacing and type scale — APPROVED 2026-09-07

### One scale helper

`int ns_scale(int px, int dpi)` in `src/core/ns_scale.{c,h}`: integer,
round-half-up (`(px * dpi + 48) / 96`), never rounds a positive value to zero.
Replaces the three existing helpers (`S()` macros in `ai_chat.c`/`help_guide.c`,
`CLV_SCALE` in `chat_listview.c`, `settings_scale` in `settings_layout.c`; the
last becomes a thin alias during migration, then goes).

### Spacing grid (96-DPI base values)

| Token | px | Use |
|---|---|---|
| `SP_XS` | 4 | inset inside chips/tags |
| `SP_SM` | 8 | gap between related controls, panel side padding |
| `SP_MD` | 12 | message gap, card padding |
| `SP_LG` | 16 | section padding |
| `SP_XL` | 24 | between sections |
| `SP_XXL` | 32 | tab strip height, header bands |

Component sizes on the same grid: control height 28, tab height 32, icon 20,
tag height 20, avatar 20, button min width 76 → 80. Corner radii: `R_CTRL` 4,
`R_CARD` 8, `R_PILL` = height/2. Strokes are their own tokens and the only
non-multiples of 4: `STROKE_HAIRLINE` 1, `STROKE_RULE` 2, `STROKE_BAR` 3.
The 22 `BASE_*` constants in `chat_listview.c` collapse onto these.

### Type ramp

| Role | Size | Weight | Line height | Face |
|---|---|---|---|---|
| `FONT_CAPTION` | 9 pt | regular | 1.3 | Inter |
| `FONT_BODY` | **10 pt** (was 9) | regular | 1.4 | Inter |
| `FONT_TITLE` | 11 pt | semibold | 1.3 | Inter |
| `FONT_HEADING` | 14 pt | semibold | 1.25 | Inter |
| `FONT_MONO` | terminal size | regular | 1.0 | terminal font |

Sizes are points at 96 DPI and go through `ns_scale`. Body moves to 10 pt
across the UI (menus, tabs, dialogs, chat prose).

### Font cache

`HFONT ns_font(NsFontRole role, int dpi)` in `src/ui/ns_font.{c,h}` returns a
cached handle per (role, dpi, face) and owns its lifetime; `ns_font_flush()` is
the whole of what `WM_DPICHANGED` and a font-setting change need to do. Replaces
the 32 `CreateFont` call sites. The key/lookup logic is a pure table in
`src/core` so it is unit-tested; only the `CreateFont` call is Win32.

### Tests (native suite)

- `ns_scale`: identity at 96; monotonic across 96/120/144/192; `ns_scale(1, dpi)`
  ≥ 1; matches the current `settings_scale` results so the Settings window does
  not move.
- Grid: every `SP_*` and component size is a multiple of 4; strokes are 1/2/3.
- Ramp: sizes strictly increase caption < body < title < heading at every DPI;
  line-height boxes never overlap the grid step below them.
- Font cache key: same (role, dpi, face) → same slot; flush empties all slots.

## 3. Shared drawing primitives — APPROVED 2026-09-07

### One drawing module — `src/ui/ns_draw.{c,h}` (grown from `ui_draw`)

The only chrome code that calls GDI+: `ns_draw_round_fill`, `ns_draw_round_stroke`
(alpha, radius from `R_*`), `ns_draw_card`, `ns_draw_chip`, `ns_draw_button`
(rest / hover / pressed / disabled / focus, optional icon + label),
`ns_draw_icon_label`, `ns_draw_separator`, `ns_draw_focus_ring`. All colours come
from `ThemeTokens`, all sizes from the spacing grid. `themed_button.h` keeps its
window plumbing and paints through `ns_draw_button`; the eight ad-hoc
`RoundRect`/`fill_rounded_rect` sites in `ai_chat.c` and `chat_listview.c` go.

**Buttons:** standalone buttons (Send, Save, Connect, Settings, dialog buttons)
stay real child windows so keyboard focus and accessibility keep working;
elements inside lists and cards (approval card, links, tags) are painted.
Both go through `ns_draw_button`, so they look identical.

### Geometry in core — `src/core/ns_layout.{c,h}`

Pure functions from (rect, dpi, content) to sub-rects, shared by paint and
hit-test:

- `ns_button_layout(rect, has_icon, dpi) → { icon, label }`
- `ns_card_layout(rect, dpi) → { inset, header, body }`
- `approval_card_layout(rect, n_cmds, cmd_widths[], dpi) → per row { tag, text,
  checkbox, allow, deny }, plus { allow_all, cancel }`, with an explicit rule:
  a command wider than its text box is ellipsised (no overlap into the next row),
  and more than `APPROVAL_VISIBLE_MAX` rows scroll inside the card's max height.

`paint_cmd_card` draws what the layout returns; `on_lbuttondown` asks the same
layout `approval_card_hit(layout, x, y) → element id` and shrinks to a dispatch
switch.

### Hover tracker — `src/core/ns_hover.{c,h}`

`NsHover { int hot_id; }` with `ns_hover_move(&h, hit_id) → { changed, old_id,
new_id }` and `ns_hover_leave(&h)`. On every `WM_MOUSEMOVE` the owner runs its
hit-test, feeds the id in, and invalidates only the two elements that changed
state; `TrackMouseEvent` drives `ns_hover_leave`. Links and buttons set the hand
cursor while hot. Used by the chat list, the tab strip and the AI toolbar.

### Icons

`icons.c` gains `OP_DOT` (a filled circle via `GdipAddPathEllipse`); the
password / thinking / server glyphs use it. Icon draw calls take token colours.

### Tests

- Native: every `*_layout` at 96 and 192 DPI — sub-rects do not overlap, all lie
  inside the parent, a 16-command card respects the max height and reports
  scrolling, `approval_card_hit` round-trips every painted rect to its id,
  ellipsis rule triggers exactly when text width exceeds the box.
- Native: hover transitions — enter, move within (no change), move between
  (both ids reported), leave.
- `make wintest` green (all 32 glyphs render non-empty at 16/32/48 px).

## 4. Motion tokens — APPROVED 2026-09-07

### Tokens — `src/core/ns_motion.{c,h}`

| Token | ms | Use |
|---|---|---|
| `MOTION_FAST` | 120 | hover / pressed colour transitions |
| `MOTION_BASE` | 200 | panel slides (AI dock), layout shifts, tab status pulse |
| `MOTION_SLOW` | 320 | long travel (toasts, future) |

One easing, ease-out cubic: `ns_ease(t) = 1 - (1 - t)^3`.

`NsAnim { start_tick; duration; }` with
`ns_anim_progress(&a, now_tick, reduced_motion) → { double t_eased; int done; }`:
clamps past the end, reports `done` exactly once, and returns `t = 1` on the
first tick when `reduced_motion` is set.

### One animation timer per window

Each window that animates keeps a small list of `NsAnim` and one 16 ms
`WM_TIMER`; it steps every active animation, invalidates what changed, and kills
the timer when the list is empty. The AI dock slide (currently linear on its own
timer), the tab status pulse and the chat activity dot move onto it. Paste delay,
heartbeat and keep-alive timers are not animations and stay separate.

### Reduced motion

`SystemParametersInfo(SPI_GETCLIENTAREAANIMATION)` is read at startup and on
`WM_SETTINGCHANGE`; when off, every animation snaps to its end state.

### Tests (native)

- `ns_ease`: monotonic on [0,1], exactly 0 at 0 and 1 at 1.
- `ns_anim_progress`: t increases with time, clamps at 1, `done` fires once, and
  with `reduced_motion` the first call returns t = 1 and done.

## 5. Verification harness — APPROVED 2026-09-07

### `--ui-demo[=<state>]` (hidden CLI flag)

`cli_args.c` gains `CLI_UI_DEMO` with an optional state name and an optional
`--theme <name>`. Neither appears in `-?` output or the user guide; the flag is
compiled into the normal binary so screenshots always come from the shipped exe.
No Help-menu entry.

In demo mode the app opens a fake session tab with canned terminal output (no
SSH, no network, no key) and the AI panel docked, populated from a fixed script.

### States (canned data in `src/core/ui_demo.{c,h}`, a table over the real
message and approval structures so it cannot drift)

| State | Contents |
|---|---|
| `chat` | user message; AI reply with thinking block, markdown (headings, list, table, code block); status line |
| `approval` | one card with safe / write / critical commands in pending, approved, denied and blocked states; Allow All visible |
| `executing` | a command in EXECUTING with the activity dot; a completed one above it |
| `tool` | a `[Tool: web_search]` call and a truncated `[Result]` with the 1 MB warning |
| `error` | an HTTP error with Retry; a cancelled stream |
| `empty` | fresh panel, no messages (the empty state to be designed in sub-project 2) |
| `all` | every state above stacked in one conversation |

### Harness integration

Integration case `ui_gallery` launches `nutshell.exe --ui-demo=<state> --theme
<theme>` for every state × theme (7 × 4) and saves `artifacts\gallery\<theme>-
<state>.png` — a contact sheet for reviewing the redesign without a live session
or credits. Runs in well under a minute; no assertions beyond "window appeared and
painted" (a non-blank capture).

### Tests (native)

- `cli_parse` recognises `--ui-demo`, `--ui-demo=approval`, `--theme "Onyx
  Light"`, and rejects an unknown state with a CLI error.
- `ui_demo_build(state, &conv, &approval)` produces the documented message and
  entry counts for every state, and `all` equals the union.

## 6. Tests — APPROVED 2026-09-07

Three layers. Sections 1–5 placed every computable piece in `src/core` so the
first layer covers almost everything; the Win32 side only paints what core
computed.

### Native suite (`make test`)

Six new test files, one per module, holding the tests listed in each section:

| Module | Test file |
|---|---|
| `ui_theme` resolve + derivation | `tests/test_ui_tokens.c` |
| `ns_scale`, grid, ramp, font-cache key | `tests/test_ns_scale.c` |
| `ns_layout` (button, card, approval card, hit-test) | `tests/test_ns_layout.c` |
| `ns_hover` | `tests/test_ns_hover.c` |
| `ns_motion` | `tests/test_ns_motion.c` |
| `ui_demo` + CLI flag | `tests/test_ui_demo.c` (+ cases in `test_cli_args.c`) |

Expected: 90–120 new tests on top of the current 1,539.

### Two gates (fail the native suite)

- **Colour gate** — `test_ui_tokens.c` scans `src/ui/*.c` at test time and fails
  on any `RGB(` outside `ns_draw.c`, except an explicit allow-list kept in the
  test (acorn watermark colours, the four terminal ANSI fallbacks in
  `renderer.c`). Growing the allow-list is a visible review decision.
- **Scale gate** — the same scan fails on any new `#define S(` / `*_SCALE(`
  macro or a bare `MulDiv(..., 96)` in `src/ui`, so no second rounding rule
  reappears.

### Windows layers

- `make wintest` green: all 32 glyphs non-empty at 16/32/48 px (needs the
  section-3 `OP_DOT`).
- Integration suite (`tests/integration`): the existing 11 cases plus
  `ui_gallery` from section 5. A full run leaves `artifacts\gallery\` as the
  contact sheet.

### Definition of done for the foundation

1. All six modules landed with their tests; native suite green.
2. Both gates passing; hardcoded colours in `src/ui` outside the allow-list: 0
   (from 77).
3. `make wintest` green.
4. Integration suite green including `ui_gallery`.
5. A before/after gallery diff reviewed by Thomas: existing screens unchanged
   except body text at 10 pt and the light themes' new accent.
