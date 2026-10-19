# Light-theme ANSI contrast: a minimum-contrast floor for terminal text

DRAFT

**Date**: 2026-10-19
**Status**: DRAFT, not yet critiqued.
**Branch**: `fix/light-theme-ansi-contrast` (cut from `main` at `c86dea1`, v1.2.37)
**Prompted by**: the maintainer, 2026-10-19: *"Many of the PowerShell results return in yellow,
this makes them hard to see when using a light theme in Nutshell."*
**Builds on**: `2026-09-07-design-system-foundation-design.md` (the four themes, `terminal_fg` /
`terminal_bg`, the L*-step helpers in `src/core/theme.c`), and `theme_role_color()`, which
`src/ui/md_render.c` already uses to keep the AI panel's intent hues at WCAG AA.

---

## 1. Problem

The terminal's 16-colour ANSI palette is one global table tuned for dark backgrounds. On the two
light themes most of it falls below WCAG AA (4.5:1) against the terminal background, and the
colours PowerShell uses most are the worst.

**Which themes are light.** `Onyx Light` (`terminal_bg` `0xF5F5F7`, L* 96.6) and `Moss & Mist`
(`0xF1F3F0`, L* 95.6). `Sage & Sand` is a *dark* theme (`0x2B2D24`, L* 18.0); yellow reads well
on it (6.23:1 and 13.09:1). The report's "light theme" is one of the first two.

**What PowerShell emits.** A local session reaches the parser through ConPTY as SGR 30-37/90-97
(indexed), not RGB. The repository already holds a capture of PSReadLine painting the command
token with `ESC[93m` (`tests/test_term.c`, `test_term_at_prompt_powershell_continuation`).
That is the only captured byte sequence; the rest of this paragraph is PSReadLine 2.x's and
PowerShell 7's documented defaults, to be confirmed by the manual check in section 6.
PSReadLine's other defaults are also 16-colour: default tokens and types `37`, parameters and
operators `90`, numbers and members `97`, strings `36`, variables and keywords `92`. PowerShell 7's
`$PSStyle.Formatting.Warning`/`Verbose`/`Debug` are `33;1` (Nutshell renders bold as a heavier
font only, so this stays palette yellow `0xCCAA33`). `Write-Host -ForegroundColor Yellow` arrives
as `93`. So the problem is not only yellow: on a light theme bright white, bright cyan, bright
green and grey are all close to invisible too.

**Measured** (WCAG 2.x ratio, `theme_contrast()` semantics, computed by compiling
`src/core/theme.c` natively against the real `PALETTE` in `src/term/parser.c` and the real
`terminal_bg` values in `src/core/ui_theme.c`):

| SGR | palette | Onyx Light today | Moss & Mist today | Onyx Synapse | Sage & Sand |
|---|---|---|---|---|---|
| 33 yellow | `CCAA33` | **2.06** | **2.01** | 8.35 | 6.23 |
| 93 bright yellow | `FFFF55` | **1.02** | **1.05** | 17.56 | 13.09 |
| 37 white (grey) | `C0C0C0` | **1.67** | **1.63** | 10.30 | 7.68 |
| 97 bright white | `FFFFFF` | **1.09** | **1.12** | 18.73 | 13.97 |
| 96 bright cyan | `55FFFF` | **1.13** | **1.10** | 15.29 | 11.40 |
| 92 bright green | `55FF55` | **1.22** | **1.19** | 14.11 | 10.52 |
| 36 cyan | `44BBBB` | **2.13** | **2.07** | 8.09 | 6.03 |
| 32 green | `4CB84C` | **2.33** | **2.28** | 7.38 | 5.50 |
| 90 bright black | `808080` | **3.63** | **3.54** | 4.74 | 3.54 |
| 30 black | `000000` | 19.29 | 18.82 | **1.12** | **1.50** |
| 31 red | `CC3333` | 4.72 | 4.60 | **3.65** | **2.72** |

Below 4.5:1 against `terminal_bg`: **14 of 16** entries on Onyx Light (all but black and red),
14 on Moss & Mist, 2 on
Onyx Synapse (black, red), 6 on Sage & Sand (black, red, blue, magenta, bright black, bright red).
The themes' own default text (`terminal_fg` on `terminal_bg`) is 15.46 (Onyx Light) and 7.87
(Moss & Mist), so un-coloured output is fine.

## 2. Current implementation

- **Palette.** One `static const uint32_t PALETTE[16]` in `src/term/parser.c`, global, not per
  theme. `color256_to_rgb()` (same file) returns it for indices 0-15, a fixed 6x6x6 cube for
  16-231 and a grey ramp for 232-255.
- **Per-cell storage.** `handle_sgr()` (`src/term/parser.c`) resolves colour at parse time:
  `TermAttr.fg`/`.bg` hold the resolved `0xRRGGBB`, `fg_mode`/`bg_mode` say how it was given
  (`COLOR_DEFAULT`, `COLOR_ANSI16`, `COLOR_256`, `COLOR_RGB`; `src/term/term.h`). For
  `COLOR_ANSI16` the parser writes `fg_index = 0`, so the palette index is *lost*; only
  `COLOR_256` keeps its index. `COLOR_DEFAULT` cells carry no colour and are resolved at paint.
- **Bold.** `TERM_ATTR_BOLD` selects `hBoldFont` in `renderer_draw()`; it never brightens the
  colour. SGR 2 (dim), 3 (italic) and 8 (conceal) are ignored. OSC 4/10/11 are not handled
  (`process_osc()` handles 0 and 2 only).
- **Paint.** `resolve_cell()` (`src/ui/renderer.c`) picks `r->defaultFg`/`defaultBg` for
  `COLOR_DEFAULT`, else `to_colorref(cell->attr.fg)`, then swaps fg/bg once for SGR 7 *or* the
  cursor cell, and once more if the cell is selected. It runs for every cell `renderer_draw()`
  visits, *before* the display-buffer shadow check (`dispbuf_cell_clean()` compares the resolved
  fg/bg), so clean cells in a repainted row pay for it too. Rows are skipped wholesale when clean,
  except when scrolled back or when a selection exists.
- **Theme at paint time.** Yes, partly: `apply_config_colors()` (`src/ui/window.c`) copies
  `g_theme->terminal_fg`/`terminal_bg` into `g_renderer.defaultFg`/`defaultBg`. It is called after
  every `renderer_init()` (startup, Settings, `--theme`, zoom, DPI change). The renderer knows the
  default bg but not the theme struct.
- **Contrast helpers.** `src/core/theme.c`: `theme_luminance()` (BT.709 relative luminance),
  `theme_contrast()` (WCAG 2.x ratio, symmetric), `theme_lstar()`, `theme_shift_lightness()`
  (hue-preserving L* move with channels clamped to `[0x08, 0xF7]`), and `theme_role_color()`,
  which walks a colour away from a background in 2-L* steps until it clears 4.5:1. Callers:
  `md_render.c` (`md_role_clr()`, per paint), `ui_theme.c` (token derivation). Nothing in the
  terminal path uses them.
- **Scrollback on theme switch.** `COLOR_DEFAULT` cells re-colour (resolved at paint). Indexed
  cells keep the RGB they were parsed with, which is theme-independent today. Separately, on
  reading `on_settings_clicked_page()` (`src/ui/window.c`), a theme change re-inits the renderer
  and invalidates the window but marks no terminal row dirty unless the grid size changed, and
  `renderer_draw()` skips clean rows; whether clean rows keep the old theme's colours until they
  next change is unverified on Windows (section 5.6 covers it).
- **Settings.** No palette or contrast setting exists. `Settings.foreground_colour`/
  `background_colour` (`src/config/config.h`) are loaded but unused since the design-system
  migration; `colour_scheme` picks the theme.
- **Gates.** `tests/test_ui_tokens.c` allows exactly 2 literal `RGB(` calls in `renderer.c`;
  `to_colorref()` unpacks a variable and does not count.

## 3. Options considered

Paint-path cost figures were measured natively (gcc -O2, Linux host): `theme_contrast()` is
0.09 us per call; the `theme_role_color()` walk for the worst palette case (97 on Onyx Light,
27 steps) is 7.4 us. A 200x60 grid is 12,000 cells.

### Option 1: a per-theme 16-colour palette

Light themes ship a darker palette (as Windows Terminal's light schemes do), dark themes keep
today's.

- **Changes.** `ThemeColors` and `ThemeTokens` gain `ansi[16]`; `handle_sgr()` stores the index
  for `COLOR_ANSI16` (`fg_index = p - 30` etc.); `resolve_cell()` looks the index up in the
  current theme's table for `COLOR_ANSI16` and for `COLOR_256` below 16. 32 hand-picked values.
- **Scrollback.** Re-colours on a theme switch, but only because of the cell-format change
  (index stored, resolved at paint).
- **Paint cost.** A table lookup. None to speak of.
- **Borrowing a light scheme does not fix it.** Windows Terminal's light schemes keep the bright
  colours light: One Half Light yellow `C18301` is 2.96:1 on Onyx Light, bright yellow `E4C07A`
  1.59; Campbell's `C19C00`/`F9F1A5` are 2.40/1.06. Nor do the themes' own intent tokens:
  `warning` is 2.99 (Onyx Light) and 4.05 (Moss & Mist) against `terminal_bg`, because they were
  tuned against their label colour, not as flat text. The palette has to be designed to 4.5:1.
- **Does not fix.** 256-colour cube/ramp and truecolour (`38;5;226`, `bat`/`delta` themes built
  for dark terminals), and it *breaks explicit pairs*: an application that writes `97;41` (white
  on red, 5.14:1 today) would get a darkened white on the unchanged red, 1.02:1 if the light
  palette's white is the 4.5:1 grey. Avoiding that needs a second rule (use the light palette only
  on a default background), at which point it is option 4b.
- **Gallery.** No capture changes: no demo terminal text uses SGR colour today.

### Option 2: a minimum-contrast floor at paint time

Keep the palette. When a foreground's contrast against the cell's actual background is below a
threshold, move its lightness (hue kept) until it clears it, as VS Code's
`terminal.integrated.minimumContrastRatio` (default 4.5) and Windows Terminal's
`adjustIndistinguishableColors` do.

- **Measure.** WCAG 2.x ratio, `theme_contrast()`. The walk is `theme_role_color()`'s:
  `theme_shift_lightness()` in 2-L* steps away from the background, which keeps hue.
- **Threshold 4.5:1** (WCAG AA body text): the same bar `theme_role_color()` and the token tuning
  in `test_ui_tokens.c` use, and VS Code's default. At 3.0:1 yellow would only reach 3.20
  (`A28725`) and white 3.17.
- **Applies to** every foreground mode: 16-colour, 256-colour and truecolour, and the default fg
  on an explicit background (`ESC[40m` on a light theme). Not to cells where both fg and bg are
  default (the theme guarantees those).
- **Scrollback.** Re-colours on a theme switch with no cell-format change: the stored RGB is
  theme-independent and the floor is applied against the current background at every paint.
- **Paint cost.** Per non-default cell, a contrast check, plus a walk on a miss. Uncached, a full
  12,000-cell repaint of coloured text is about 1 ms of checks and, if every cell needs the walk,
  89 ms. A small cache keyed on (fg, bg) makes it a hash and two compares; distinct pairs on a
  screen number in the tens.
- **Selection, cursor, SGR 7.** Contrast is symmetric (`test_theme_contrast_symmetric`), so
  applying the floor before the swaps keeps the swapped pair readable too.
- **Gallery.** No capture changes (as option 1). `helpers_theme_pixel_matches_token` samples a
  default-bg pixel, which the floor never touches.
- **Does not fix / costs.** Normal and bright of one hue converge on the same 4.5:1 shade on a
  light background: on Onyx Light 90 and 97 both become `6C6C6C`, 37 becomes `707070`, 32/92
  `307C30`/`2B7F2B`, 36/96 `2E7B7B`/`297B7B`. Syntax colouring keeps its hues but loses the
  normal/bright distinction within a hue. On dark themes (if enabled there) it would lift
  PSReadLine's inline prediction (`38;5;238` = `444444`, 1.92:1 on Onyx Synapse) to roughly the
  grey of real parameter tokens.

### Option 3: both

A designed per-theme palette as the baseline (normal entries darker than bright, both above 4.5,
so the distinction survives) and the floor as the safety net for 256-colour, truecolour and
explicit backgrounds. Fixes everything option 2 fixes and keeps normal/bright apart, at the cost
of both changes: the cell-format change, 32 hand-tuned values per light theme, a palette test
table, and the floor. More than the report needs; it is a follow-up if the convergence above
bothers the maintainer in use (section 8).

### Option 4: cheaper alternatives the code suggests

- **4a. Have PowerShell use other colours** (set `$PSStyle`/`Set-PSReadLineOption -Colors` when
  spawning a local shell). Fixes one shell's defaults on one machine; not `Write-Host`, not
  Windows PowerShell's `$Host.PrivateData`, not remote hosts, and it edits the user's shell
  environment. Rejected.
- **4b. Derive a light palette once, at theme-resolve time**, by running each of the 16 entries
  through `theme_role_color()` against `terminal_bg`, store indices in cells, and use the derived
  table only on a default background. Per-cell cost is a lookup, and for the reported case it
  gives exactly option 2's colours. But it needs the same parser change as option 1, covers only
  16 colours on the default background, and the code is not smaller than option 2's. Rejected in
  favour of option 2.
- **4c. Map yellow to the theme's `warning` token** on light themes. Below AA (2.99, 4.05) and
  only one hue. Rejected.

## 4. Decision

**Option 2: a WCAG minimum-contrast floor of 4.5:1, applied at paint time against the cell's
actual background, to every foreground colour mode, on the light themes only.** The floor is a
per-theme property: `Onyx Light` and `Moss & Mist` set it to 4.5, the two dark themes to 0 (off),
so dark themes paint exactly as today. Whether to turn it on for the dark themes as well is an
open question (section 8).

After the change, on both light themes every one of the 16 palette entries reads at 4.5:1 or
better against `terminal_bg`:

| SGR | Onyx Light: today -> floor | Moss & Mist: today -> floor |
|---|---|---|
| 33 yellow | 2.06 -> 4.56 (`846E1B`) | 2.01 -> 4.80 (`7E6919`) |
| 93 bright yellow | 1.02 -> 4.65 (`727223`) | 1.05 -> 4.54 (`727223`) |
| 37 white | 1.67 -> 4.55 (`707070`) | 1.63 -> 4.78 (`6B6B6B`) |
| 97 bright white | 1.09 -> 4.82 (`6C6C6C`) | 1.12 -> 4.71 (`6C6C6C`) |
| 96 bright cyan | 1.13 -> 4.58 (`297B7B`) | 1.10 -> 4.78 (`277676`) |
| 92 bright green | 1.22 -> 4.62 (`2B7F2B`) | 1.19 -> 4.51 (`2B7F2B`) |
| 90 bright black | 3.63 -> 4.82 (`6C6C6C`) | 3.54 -> 4.71 (`6C6C6C`) |
| 34 blue | 3.53 -> 4.72 (`4C6BB6`) | 3.45 -> 4.61 (`4C6BB6`) |

(The rest: 32 -> 4.76/4.64, 36 -> 4.55/4.75, 35 -> 4.55/4.76, 91 -> 4.70/4.59, 94 -> 4.81/4.69,
95 -> 4.84/4.72; 30 and 31 are already above 4.5 and unchanged.)

Reasons, in order: it fixes the report and the thirteen other unreadable entries, plus 256-colour,
truecolour and explicit-background text, where a palette fixes only 16 colours on the default
background; it reuses the algorithm the design system already ships and tests
(`theme_role_color()`), puts all the logic in `src/core` where native tests reach it, and needs no
cell-format change, so scrollback re-colours on a theme switch for free; and because it is judged
against the real cell background it cannot damage an application's deliberate pairs (`30;47`
stays 11.54, `97;41` stays 5.14).

## 5. Design

### 5.1 The walk, generalised (`src/core/theme.c`)

Add

```c
/* fg unchanged when min_ratio <= 1.0 or theme_contrast(fg, bg) >= min_ratio. Otherwise fg's
 * lightness is walked away from bg (lighter on a bg with L* < 50, darker otherwise) in 2-L*
 * steps with theme_shift_lightness() until it clears min_ratio, at most 50 steps. If no step
 * clears it, returns whichever of 0x080808 / 0xF7F7F7 has the higher contrast with bg. */
unsigned int theme_min_contrast_color(unsigned int fg, unsigned int bg, double min_ratio);
```

`theme_role_color()` keeps its signature and its exact outputs (it keeps its own `fallback`
argument); it and the new function share one static walk. The fallback differs on purpose:
`theme_role_color()` falls back to a theme token, the terminal falls back to the best extreme,
because on an explicit cell background the theme's text colour may itself be unreadable.

### 5.2 Terminal colour resolution and the cache (new `src/core/term_contrast.c`/`.h`)

Pure C, no Win32, natively tested.

```c
#define TERM_CONTRAST_SLOTS 256
typedef struct {
    double   min_ratio;                 /* 0 = off */
    uint32_t key_fg[TERM_CONTRAST_SLOTS], key_bg[TERM_CONTRAST_SLOTS], out[TERM_CONTRAST_SLOTS];
    uint8_t  used[TERM_CONTRAST_SLOTS];
} TermContrast;

void     term_contrast_reset(TermContrast *c, double min_ratio); /* sets ratio, empties cache */
uint32_t term_contrast_fg(TermContrast *c, uint32_t fg, uint32_t bg); /* cached floor */
int      term_contrast_exempt(uint32_t codepoint);
/* Resolve a cell's fg/bg as 0xRRGGBB (default colours from dfg/dbg) and apply the floor.
 * Swaps (SGR 7, cursor, selection) stay with the caller. */
void     term_cell_colors(const TermCell *cell, uint32_t dfg, uint32_t dbg,
                          TermContrast *c, uint32_t *out_fg, uint32_t *out_bg);
```

- `term_cell_colors()` returns early, untouched, when `min_ratio <= 1.0`, when both modes are
  `COLOR_DEFAULT`, or when the codepoint is exempt.
- **Exempt glyphs**: block elements U+2580-U+259F and the Powerline range U+E0A0-U+E0D4. Their
  foreground is an area colour meant to match a neighbouring background (half-block images,
  Powerline separators), not text on a ground; moving it would tear the picture. Box-drawing
  lines (U+2500-U+257F) are not exempt: a yellow frame should be visible.
- **Cache**: direct-mapped, slot from a multiplicative hash of (fg, bg); a hit compares both keys.
  The ratio is not in the key; `term_contrast_reset()` empties the cache whenever it changes.
  3 KB, lives in the `Renderer` (a global), never on the stack.

### 5.3 Per-theme threshold (`src/core/ui_theme.c`/`.h`)

`ThemeColors` gains `double terminal_min_contrast;` (4.5 for Onyx Light and Moss & Mist, 0 for
Onyx Synapse and Sage & Sand) and `ThemeTokens` copies it in `ui_theme_resolve()`. A theme
property rather than an `is_dark` test so a later theme, or the dark-theme decision, is a data
change. `Get-NutshellThemeColor`'s regex reads only `0xRRGGBB` fields and is unaffected.

### 5.4 Renderer (`src/ui/renderer.c`/`.h`, `src/ui/window.c`)

- `Renderer` gains `TermContrast contrast` and packed `default_fg_rgb`/`default_bg_rgb`.
  `renderer_init()` calls `term_contrast_reset(&r->contrast, 0)`.
- `resolve_cell()` calls `term_cell_colors()` and converts with `to_colorref()`; the reverse,
  cursor and selection swaps follow unchanged. No new literal `RGB(` (the gate's renderer.c count
  stays 2).
- `apply_config_colors()` sets the packed defaults and calls
  `term_contrast_reset(&g_renderer.contrast, g_theme->terminal_min_contrast)`. Every path that
  changes theme, font, zoom or DPI already calls it after `renderer_init()`.

### 5.5 Data flow

`handle_sgr()` (unchanged) stores raw palette RGB -> `renderer_draw()` -> `resolve_cell()` ->
`term_cell_colors()` (default substitution, floor via cache) -> swaps -> `dispbuf_cell_clean()`
compares the floored colour -> `ExtTextOutW`. The parser, `TermAttr`, `PALETTE` and
`color256_to_rgb()` do not change; `test_color.c`'s palette assertions stay as they are.

### 5.6 Theme switch

Switching from a dark theme to a light one re-colours everything on screen and in scrollback at
the next paint, because the floor is computed per paint and the shadow buffer compares floored
colours. The plan's first task verifies on Windows that a Settings theme change repaints clean
rows (section 2's reading of `on_settings_clicked_page()` says it may not); if it does not, add a
`force_full_terminal_repaint()` call at the end of `on_settings_clicked_page()`. That fixes the
same staleness for default-coloured text, which exists today regardless of this change.

### 5.7 Settings

None. The floor fires only where the text is below AA on a light background, i.e. hard to read;
no one needs a switch to get unreadable text back. If a need appears (exact colours for
truecolour art), a hidden config key overriding the theme's ratio is a small later addition.

### 5.8 Paint-path cost after the change

Default/default cells (most of any screen): one branch. Coloured cells: a hash and two compares.
Misses: bounded by the distinct (fg, bg) pairs on screen, each at most about 7.4 us (measured on
Linux; expect the same order on Windows), paid once per theme.

## 6. Tests

Native first (`make test`); declare each in `tests/runner.c`.

**`tests/test_theme.c`**
- `theme_min_contrast_color` returns fg unchanged when already above the ratio (`FFFF55` on
  `121212` at 4.5), and for any pair when `min_ratio` is 0 or 1.0.
- `FFFF55` on `F5F5F7` at 4.5: result >= 4.5, < 5.0 (moved just far enough), and still yellow
  (R and G within 0x10 of each other, both above B).
- Table-driven: all 16 `color256_to_rgb(i)` for i < 16, against both light `terminal_bg`s, clear
  4.5; the first step that clears it is taken (the previous step's colour is below 4.5).
- Unreachable target: fg `777777` on bg `767676` (L* 49.6) at 4.5: the walk goes lighter and
  stops short (`F7F7F7` is 4.24), so it returns `080808` (4.41), the better extreme.
- `theme_role_color()` regression: its outputs for each theme's `link` and `info` on `bg_primary`
  are byte-identical to before the refactor (pin the current values).

**`tests/test_term_contrast.c`** (new)
- `term_cell_colors`: default/default untouched at any ratio; `ESC[93m` on Onyx Light's bg
  floored to >= 4.5; the same cell with ratio 0 returns `FFFF55`; default fg on `ESC[40m` lifted;
  `ESC[30;47m` (11.54) and `ESC[97;41m` (5.14) untouched; `ESC[33;47m` (1.23) floored;
  `38;5;226` and `38;2;255;255;0` floored (256 and truecolour).
- Cells are produced by feeding the parser real sequences (`term_process`), not by hand-built
  attrs, so the test also pins that the parser's stored RGB is what the floor sees.
- Exempt boundaries: U+257F not exempt, U+2580 and U+259F exempt, U+25A0 not; U+E09F not,
  U+E0A0 and U+E0D4 exempt, U+E0D5 not. An exempt `ESC[93m` cell keeps `FFFF55`.
- Cache: a hit returns the uncached result; two pairs that hash to one slot each return their own
  correct result; `term_contrast_reset()` with a new ratio changes the answer for a cached pair.
- Symmetry: floored fg vs bg and the swapped pair have the same ratio (the cursor/selection
  argument, pinned).

**`tests/test_ui_theme.c`**
- Each theme's `terminal_min_contrast` is 4.5 when `theme_is_dark(terminal_bg)` is 0 and 0
  otherwise; `ui_theme_resolve()` copies it.
- For every theme with the floor on: all 16 palette entries, floored, clear it against
  `terminal_bg`, and `terminal_fg` on `terminal_bg` clears it unfloored (so the default/default
  fast path is sound).

**`tests/test_ui_demo.c`**
- A new demo state `ansi` is valid; insert it before `settings-debug` so the tail assertions in
  `test_ui_demo_states_list_ends_in_all` hold, with the count raised to 17. Its terminal text
  contains each of `ESC[30m`..`ESC[37m` and `ESC[90m`..`ESC[97m`.

**Harness** (`tests/integration/cases/30-ui-demo.ps1`, tier `gate`, no SSH host or AI key)
- `ui_demo_build()` (`src/core/ui_demo.c`) gives state `ansi` a terminal text of 16 rows, one per
  SGR 30-37/90-97: a swatch of 30 reverse-video spaces (`ESC[<n>;7m`), then the code and a sample
  sentence in that colour, then two explicit-pair rows (`30;47`, `97;41`) for the eye. Reverse
  video paints the floored foreground as a solid cell background, which a screenshot can sample
  without anti-aliasing.
- Add `ansi` to `$galleryStates` (and to the comment's hand-kept list): the contact sheet then
  shows the palette on all four themes, the visual review of the floor.
- New case `helpers_ansi_contrast_floor`: launch `--ui-demo=ansi --theme "Onyx Light"`, screenshot,
  scan the column at 15% width downward from the tab strip's bottom, collect the runs of
  non-`terminal_bg` colour, and assert there are at least 14 runs (adjacent near-equal swatches,
  37/90, may merge) and each has WCAG contrast >= 4.4 against `terminal_bg` (0.1 of headroom for
  capture rounding). Repeat on `Onyx Synapse` and assert the bright-yellow swatch is `FFFF55`
  within 12 per channel (floor off on dark themes). Needs a small `Get-NutshellContrast` helper in
  `NutshellIT.psm1`; list the case in `tests/integration/README.md`.
- Recommended manual check (not automated: ConPTY output varies by PowerShell version): a local
  PowerShell tab on Onyx Light running `Write-Host hello -ForegroundColor Yellow`,
  `Write-Warning hi` and a typed command, before and after, with the debug log's raw bytes
  confirming `93`/`33;1` reach the parser with a default background.

## 7. Out of scope

- A designed per-theme palette (option 3's baseline); the normal/bright convergence it would fix.
- Dark themes (floor off there; section 8).
- SGR 2 (dim), 3 (italic), 8 (conceal); bold-as-bright; OSC 4/10/11 palette set and query.
- PSReadLine's inline prediction reading darker than typed text on light themes (`444444` is
  8.95:1 there and untouched by the floor; it is PSReadLine's dark-background default).
- Windows PowerShell 5.1's black warning/error background, if ConPTY forwards it as `40`
  (readable already, 19.69:1).
- Removing the unused `foreground_colour`/`background_colour` config fields.

## 8. Open questions for the maintainer

1. **Dark themes.** Turn the floor on for Onyx Synapse and Sage & Sand too? Gain: SGR 30 black
   becomes visible (1.12 -> 4.55 on Onyx Synapse, 1.50 -> 4.72 on Sage & Sand) and Sage & Sand's
   red reads (2.72 -> 4.73, `D57E7E`, visibly pinker; plus blue, magenta, grey and bright red
   lifted slightly). Cost: PSReadLine's inline prediction (1.92 on Onyx Synapse) would be lifted
   to about the grey of parameter tokens and stop looking like a ghost. It is a one-value-per-theme
   change either way.
2. **Normal/bright convergence on light themes.** Acceptable as is, or worth a designed palette
   later (option 3), so that for example 90 and 97 do not both paint `6C6C6C`?
