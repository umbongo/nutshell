# Design-System Foundation — Implementation Plan

> **For agentic workers:** implement task by task, in order. Each task is TDD:
> write the failing tests, make them pass, run the full suite, then the Windows
> build. Steps use checkbox (`- [ ]`) syntax for tracking. Do not start a task
> until the previous one's verification commands are green.

**Goal:** Land the six foundation modules from the spec so that every colour,
size, font, animation and hover state in the UI layer comes from one tested
source, the approval card shares geometry between paint and hit-test, and every
panel state can be screenshotted without a live session.

**Spec:** [2026-09-07-design-system-foundation-design.md](../specs/2026-09-07-design-system-foundation-design.md)

**Architecture (from the spec):**
- Core, native-testable: `ui_theme` resolve (`ThemeTokens`), `ns_scale`,
  `ns_type` (grid, ramp, font-slot key), `ns_layout`, `ns_hover`, `ns_motion`,
  `ui_demo`.
- Win32, thin: `ns_draw` (grown from `ui_draw`), `ns_font` (cache), demo-mode
  entry in `window.c`, one animation timer per window.
- Two gates in the native suite (colour, scale) implemented as a **ratchet**: the
  test holds a baseline count that each task lowers; the final task sets it to
  the allow-list only.

**Tech stack:** C11, MinGW-w64 via MSYS2 on Windows (`mingw32-make`), custom test
framework (`tests/test_framework.h`), PowerShell integration harness
(`tests/integration`).

## Global constraints

- **Version bump before every Windows build**: `src/ui/resource.h`
  (`APP_VERSION` + `APP_VERSION_BINARY`) and `README.md`. Current at plan time:
  `1.0.80`. One bump per task that produces a binary.
- Build: `export PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH"` then
  `mingw32-make test` (native), `mingw32-make clean && mingw32-make release`
  (Windows), `mingw32-make wintest` (icon harness). Never plain `make`.
- `-Werror -Wpedantic -Wshadow -Wconversion -Wformat=2`: see CLAUDE.md pitfalls
  (no local named `msg` in a WndProc, explicit `size_t` casts, `<stdio.h>`
  where `snprintf` is used, string literals under 4095 chars).
- Testable logic goes in `src/core`; `src/ui` is excluded from the native suite.
- Line endings: after editing, `git ls-files --eol <file>`; if the index says
  `i/crlf`, run `unix2dos -q <file>`.
- Commits as if Thomas authored them; no AI attribution.
- Integration suite needs tompi reachable and the keyboard free; run it at the
  end of tasks 3, 5, 6, 9 and 10 (`tests\integration\Run-Integration.ps1`).

## Task overview

| # | Task | New files | Touches |
|---|---|---|---|
| 1 | `ns_scale` + `ns_type` | `src/core/ns_scale.{c,h}`, `src/core/ns_type.{c,h}`, `tests/test_ns_scale.c` | `settings_layout.c` (alias), `runner.c` |
| 2 | `ThemeTokens` resolve + gates (ratchet) | `tests/test_ui_tokens.c` | `ui_theme.{c,h}`, `theme.{c,h}`, `runner.c` |
| 3 | `ns_font` cache, body 10 pt | `src/ui/ns_font.{c,h}` | all 32 `CreateFont` sites, `WM_DPICHANGED`, `app_font.h` |
| 4 | `ns_draw` | `src/ui/ns_draw.{c,h}` (from `ui_draw`) | `themed_button.h`, `ai_chat.c`, `chat_listview.c` |
| 5 | `ns_layout` + approval card | `src/core/ns_layout.{c,h}`, `tests/test_ns_layout.c` | `chat_listview.c` (paint + hit-test) |
| 6 | `ns_hover` + wiring | `src/core/ns_hover.{c,h}`, `tests/test_ns_hover.c` | `chat_listview.c`, `tabs.c`, `ai_chat.c` |
| 7 | Icons `OP_DOT` | — | `icons.c`, `wintest` |
| 8 | `ns_motion` + one timer | `src/core/ns_motion.{c,h}`, `tests/test_ns_motion.c` | `window.c` (dock slide), `tabs.c`, `chat_listview.c` |
| 9 | `ui_demo` + `--ui-demo` + gallery | `src/core/ui_demo.{c,h}`, `tests/test_ui_demo.c` | `cli_args.{c,h}`, `main.c`, `window.c`, `Run-Integration.ps1` |
| 10 | Migrate the rest, gates to zero, docs | — | `tabs.c`, `ai_chat.c`, `window.c`, `paste_dlg.c`, `md_render.c`, `README.md`, `CLAUDE.md` |

Dependencies: 1 → 2 → 3 → 4 → 5 → 6; 7 anytime after 4; 8 after 4; 9 after 5
and 8; 10 last.

---

### Task 1: `ns_scale` + `ns_type`

**Interfaces**
```c
/* src/core/ns_scale.h */
int ns_scale(int px, int dpi);              /* round-half-up, >= 1 for px >= 1 */

/* src/core/ns_type.h */
enum { SP_XS = 4, SP_SM = 8, SP_MD = 12, SP_LG = 16, SP_XL = 24, SP_XXL = 32 };
enum { SZ_CTRL_H = 28, SZ_TAB_H = 32, SZ_ICON = 20, SZ_TAG_H = 20, SZ_AVATAR = 20, SZ_BTN_MIN_W = 80 };
enum { R_CTRL = 4, R_CARD = 8 };            /* R_PILL = h/2 via ns_type_pill(h) */
enum { STROKE_HAIRLINE = 1, STROKE_RULE = 2, STROKE_BAR = 3 };
typedef enum { FONT_CAPTION, FONT_BODY, FONT_TITLE, FONT_HEADING, FONT_MONO, FONT_ROLE_COUNT } NsFontRole;
typedef struct { int size_pt; int weight; double line_height; int is_mono; } NsFontSpec;
const NsFontSpec *ns_type_font(NsFontRole role);
int  ns_type_font_px(NsFontRole role, int dpi, int mono_size_pt);  /* pixel height for CreateFont */
int  ns_type_font_slot(NsFontRole role, int dpi, int face_id);     /* stable cache slot index */
int  ns_type_pill(int height);
```

- [ ] Write `tests/test_ns_scale.c`: identity at 96; monotonic across
      96/120/144/192; `ns_scale(1, dpi) >= 1`; matches `settings_scale` for
      1..64 px at 96/120/144/192; every `SP_*`/`SZ_*` multiple of 4; strokes
      1/2/3; ramp strictly increasing at each DPI; `ns_type_font_slot` unique
      per (role, dpi, face) over the DPIs above and stable across calls.
- [ ] Declare + call in `tests/runner.c` (section "Core").
- [ ] Implement both modules. `settings_scale()` becomes `return ns_scale(px, dpi);`.
- [ ] `mingw32-make test` green; compile-check `mingw32-make src/core/ns_scale.o src/core/ns_type.o`.

### Task 2: `ThemeTokens` resolve + gates

**Interfaces**
```c
/* src/core/ui_theme.h additions */
typedef struct { unsigned int base, hover, pressed, disabled; } ThemeSurface;
typedef struct {
    ThemeSurface bg_primary, bg_secondary, raised, accent;
    ThemeSurface success, warning, danger, info, link;
    unsigned int text_main, text_dim, text_disabled, border, focus;
    unsigned int terminal_fg, terminal_bg;
    ThemeChatColors chat;                    /* unchanged, for the migration window */
    int is_dark;
} ThemeTokens;
void ui_theme_resolve(const ThemeColors *base, ThemeTokens *out);
/* ThemeColors gains: intent colours + optional overrides (0 = derive) */
```
- [ ] `theme.c`: add `theme_shift_luminance(rgb, delta)` and `theme_blend(a, b, t)`
      (pure, tests in `test_theme.c`).
- [ ] Write `tests/test_ui_tokens.c`: contrast ≥ 4.5:1 for every pairing the spec
      lists, all four themes; hover/pressed differ from base by ≥ the step;
      disabled closer to `bg_primary`; nothing clips to 0x000000/0xFFFFFF;
      override honoured when a theme supplies one.
- [ ] Colour gate + scale gate in the same file, as a ratchet: read
      `src/ui/*.c` relative to the repo root (test runner runs from the root),
      count `RGB(` outside `ns_draw.c` and count local scale macros; assert
      `count <= BASELINE` with `BASELINE` set to today's numbers (77 / 3).
- [ ] Implement resolve; add the five intent colours per theme (seed from the
      activity-dot colours); set Onyx Light accent `0x0A5FD6` and tune until
      contrast tests pass.
- [ ] Windows: `window.c` resolves once into a global `g_tokens` when the theme
      is chosen/changed and passes `&g_tokens` where `g_theme` went; no visual
      change yet.
- [ ] `mingw32-make test` green; bump version; `mingw32-make clean && mingw32-make release`.

### Task 3: `ns_font` cache, body 10 pt

- [ ] `src/ui/ns_font.{c,h}`: `HFONT ns_font(NsFontRole, int dpi)`,
      `void ns_font_flush(void)`, backed by `ns_type_font_slot`; face for
      `FONT_MONO` comes from settings.
- [ ] Replace all 32 `CreateFont` sites; `WM_DPICHANGED` and the font-setting
      change path call `ns_font_flush()` once. `APP_FONT_UI_SIZE` becomes 10.
- [ ] Bump version, release build, integration suite (Settings and Session
      Manager screenshots: text larger, nothing clipped — widen any control that
      now truncates).

### Task 4: `ns_draw`

- [ ] Rename `ui_draw.{c,h}` → `ns_draw.{c,h}` (git mv); keep `rgb_alpha`,
      `draw_chip` → `ns_draw_chip`, `draw_status_pulse` → `ns_draw_pulse`.
- [ ] Add `ns_draw_round_fill/stroke`, `ns_draw_card`, `ns_draw_button`
      (state enum `NS_REST/HOVER/PRESSED/DISABLED` + `focused` flag),
      `ns_draw_icon_label`, `ns_draw_separator`, `ns_draw_focus_ring`. All take
      `const ThemeTokens *`.
- [ ] `themed_button.h` paints via `ns_draw_button`; replace the six `RoundRect`
      sites in `ai_chat.c` and `fill_rounded_rect`/stroke in `chat_listview.c`.
- [ ] Lower the colour-gate baseline to the new count. Bump, build.

### Task 5: `ns_layout` + approval card

**Interfaces**
```c
typedef struct { int x, y, w, h; } NsRect;
typedef struct { NsRect icon, label; } NsButtonLayout;
typedef struct { NsRect inset, header, body; } NsCardLayout;
#define APPROVAL_VISIBLE_MAX 8
typedef struct { NsRect tag, text, checkbox, allow, deny; int ellipsis; } ApprovalRowLayout;
typedef struct { ApprovalRowLayout rows[APPROVAL_MAX_CMDS]; int n_rows, first_visible, scrollable;
                 NsRect allow_all, cancel, viewport; } ApprovalCardLayout;
enum { HIT_NONE, HIT_TAG, HIT_TEXT, HIT_CHECKBOX, HIT_ALLOW, HIT_DENY, HIT_ALLOW_ALL, HIT_CANCEL };
void ns_button_layout(NsRect r, int has_icon, int dpi, NsButtonLayout *out);
void ns_card_layout(NsRect r, int dpi, NsCardLayout *out);
void approval_card_layout(NsRect r, int n, const int *cmd_text_w, int text_h, int dpi, ApprovalCardLayout *out);
int  approval_card_hit(const ApprovalCardLayout *l, int x, int y, int *row_out);
```
- [ ] Write `tests/test_ns_layout.c` per the spec's list (96 and 192 DPI;
      no overlap; inside parent; 16 commands → `scrollable`; hit round-trip for
      every rect; ellipsis exactly when text width > box).
- [ ] Implement. Then rewrite `paint_cmd_card` to draw from the layout and
      `on_lbuttondown` to call `approval_card_hit` + a switch. Delete the
      duplicated geometry (~180 lines). "Blocked" label once per card.
- [ ] Lower the colour-gate baseline (`chat_listview.c` palette moves onto
      tokens). Bump, build, integration suite.

### Task 6: `ns_hover` + wiring

- [ ] `tests/test_ns_hover.c`: enter, move within, move between, leave.
- [ ] Implement `NsHover`, `ns_hover_move`, `ns_hover_leave`.
- [ ] Wire into `chat_listview.c` (`WM_MOUSEMOVE` → hit → hover → invalidate
      two rects; `TrackMouseEvent`; `IDC_HAND` over links/buttons), `tabs.c`
      (tab and close-glyph hot state), AI toolbar buttons in `ai_chat.c`.
- [ ] Bump, build, integration suite.

### Task 7: Icons `OP_DOT`

- [ ] Add `OP_DOT cx cy r` (`GdipAddPathEllipse`) to the glyph interpreter; use it
      for `NS_ICON_PASSWORD`, the three `NS_ICON_THINKING` dots and the
      `NS_ICON_SERVER` LED. Icon draw takes token colours.
- [ ] `mingw32-make wintest` green (2/2). Bump, build.

### Task 8: `ns_motion` + one timer

- [ ] `tests/test_ns_motion.c`: easing endpoints/monotonic; progress clamps;
      `done` once; reduced-motion snaps.
- [ ] Implement `ns_ease`, `NsAnim`, `ns_anim_progress`; a tiny `NsAnimList`
      (fixed capacity 8) with `add/step/active`.
- [ ] `window.c`: one `ANIM_TIMER` driving the list; dock slide moves to
      `MOTION_BASE` + easing; `tabs.c` status pulse and `chat_listview.c`
      activity dot join the list. Read `SPI_GETCLIENTAREAANIMATION` at startup
      and on `WM_SETTINGCHANGE`.
- [ ] Bump, build; eyeball the dock slide.

### Task 9: `ui_demo` + `--ui-demo` + gallery

- [ ] `test_cli_args.c`: `--ui-demo`, `--ui-demo=approval`, `--theme "Onyx
      Light"` with `--ui-demo`, `--theme` alone is `CLI_ERROR`, unknown state is
      `CLI_ERROR`. Add `CLI_UI_DEMO`, `demo_state[32]`, `theme[64]` to `CliOptions`.
- [ ] `tests/test_ui_demo.c`: each state's message/entry counts; `all` = union.
- [ ] Implement `ui_demo_build(const char *state, AiConversation *, ApprovalQueue *,
      char *term_text, size_t)` in core.
- [ ] `window.c`: on `CLI_UI_DEMO`, create a session with no channel, feed
      `term_text` through `term_process`, open the AI panel docked, load the
      conversation and approval queue, apply `--theme` if given. `-?` text
      unchanged.
- [ ] Integration: add `ui_gallery` case (7 states × 4 themes → `artifacts\gallery\`),
      assert each capture is non-blank. Bump, build, run it.

### Task 10: Migrate the rest, gates to zero, docs

- [ ] Move every remaining `RGB(` in `tabs.c`, `ai_chat.c`, `window.c`,
      `paste_dlg.c`, `md_render.c` onto tokens; keep only the allow-list
      (acorn watermark, `renderer.c` ANSI fallbacks). Remove the `S()` macros and
      `CLV_SCALE`; delete the `settings_scale` alias. Set both gate baselines to
      the allow-list.
- [ ] Docs: README directory tree (new modules, `ui_draw` → `ns_draw`), CLAUDE.md
      (tokens/scale/fonts rules: "never `RGB(`, never `MulDiv(...,96)`, never
      `CreateFont` in `src/ui`"), checkpoint todo.
- [ ] Bump, `mingw32-make clean && mingw32-make release`, `mingw32-make test`,
      `mingw32-make wintest`, full integration suite. Produce the before/after
      set for Thomas (definition of done, item 5).
