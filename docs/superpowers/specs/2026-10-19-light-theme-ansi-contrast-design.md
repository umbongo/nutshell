# Light-theme ANSI contrast: floored palette tables for terminal text

**Date**: 2026-10-19
**Status**: approved for planning 2026-10-19 after two critique rounds (sections "Critique") and the maintainer's decisions (last section).
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
(indexed), not RGB. The repository holds a *hand-written* fixture of PSReadLine painting the
command token with `ESC[93m` (`tests/test_term.c`, `test_term_at_prompt_powershell_continuation`,
added for prompt detection in v1.2.8), not a recording. The rest of this paragraph is PSReadLine
2.x's and PowerShell 7's defaults from memory; none of it is verified, and the plan's first task
captures the real bytes (section 5.0). Version facts, also unverified: Windows PowerShell 5.1's
inbox PSReadLine 2.0 has no predictions; PSReadLine 2.1's inline prediction may be `38;5;238`;
2.2 and later may use `97;2;3` (bright white, dim, italic).
PSReadLine's other defaults are also 16-colour: default tokens and types `37`, parameters and
operators `90`, numbers and members `97`, strings `36`, variables and keywords `92`. PowerShell 7's
`$PSStyle.Formatting.Warning`/`Verbose`/`Debug` are `33;1` (Nutshell renders bold as a heavier
font only, so this stays palette yellow `0xCCAA33`). `Write-Host -ForegroundColor Yellow` arrives
as `93`. So the problem is not only yellow: on a light theme bright white, bright cyan, bright
green and grey are all close to invisible too.

**Verified 2026-10-20 (Windows PowerShell 5.1).** The maintainer's capture (section 5.0; Windows
PowerShell 5.1, build 26100, PSReadLine 2.0.0, Nutshell v1.2.41 at 192 DPI) confirms the 16-colour
claims for 5.1: command tokens arrive as `93`, arguments and operators `37`, parameters `90`,
members `97`, a variable `92`, and the error record as 256-colour `38;5;9`, all on the default
background, with no inline prediction and no SGR 2 anywhere. PowerShell 7 remains unverified:
there is no `pwsh` on the maintainer's box (`Get-Command pwsh` printed nothing), so the
prediction's `97;2;3`/`38;5;238`, `$PSStyle`'s `33;1` and `Write-Host`'s `93` are still PSReadLine's
and PowerShell 7's documented defaults, tested only with synthetic bytes.

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
  `COLOR_256` keeps its index (`bg_index` likewise). `COLOR_DEFAULT` cells carry no colour and
  are resolved at paint.
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
  cells keep the RGB they were parsed with, which is theme-independent today. Separately,
  `on_settings_clicked_page()` (`src/ui/window.c`) re-inits the renderer and invalidates the
  window on a theme change but marks no terminal row dirty unless the grid size changed,
  `WM_ERASEBKGND` returns 1, and `renderer_draw()` skips clean rows: clean rows keep the old
  theme's pixels (certain from the code, not yet seen on Windows; section 5.6 fixes it).
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
  normal/bright distinction within a hue. It floors explicit/explicit pairs too (hidden
  `37;47` text would show, low-contrast hierarchies in vim, tmux or starship flatten, and the
  same output looks different per theme). And with SGR 2 ignored, PSReadLine 2.2+'s inline
  prediction (`97;2;3`, unverified) would be floored to `6C6C6C`, the same grey as typed
  parameters and numbers. Paint cost needs a cache, which thrashes beyond its slot count
  (gradients, `lolcat`). Not chosen (critique findings 1, 2, 7).

### Option 3: both

A designed per-theme palette as the baseline (normal entries darker than bright, both above 4.5,
so the distinction survives) and the floor as the safety net for 256-colour, truecolour and
explicit backgrounds. Fixes everything option 2 fixes and keeps normal/bright apart, at the cost
of both changes: the cell-format change, 32 hand-tuned values per light theme, a palette test
table, and the floor. More than the report needs; it is a follow-up if the convergence above
bothers the maintainer in use (section "Open for the maintainer").

### Option 4: cheaper alternatives the code suggests

- **4a. Have PowerShell use other colours** (set `$PSStyle`/`Set-PSReadLineOption -Colors` when
  spawning a local shell). Fixes one shell's defaults on one machine; not `Write-Host`, not
  Windows PowerShell's `$Host.PrivateData`, not remote hosts, and it edits the user's shell
  environment. Rejected.
- **4b. Derive a light palette once, at theme-resolve time**, by running each of the 16 entries
  through `theme_role_color()` against `terminal_bg`, store indices in cells, and use the derived
  table only on a default background. Per-cell cost is a lookup, and for the reported case it
  gives exactly option 2's colours. The "parser change" is one assignment per SGR range
  (`fg_index`/`bg_index` already exist and are written as 0). The draft rejected it on cost; that
  estimate was wrong (critique finding 3). **Chosen**, extended to all 256 indexed colours;
  truecolour stays raw (section 4).
- **4c. Map yellow to the theme's `warning` token** on light themes. Below AA (2.99, 4.05) and
  only one hue. Rejected.

## 4. Decision

**Option 4b over the whole indexed range: the cell keeps its palette index, every theme's
resolved token struct carries a 256-entry foreground table floored to 4.5:1 against its
`terminal_bg` (a raw copy where the floor is 0), and the table is used only on the
theme-default background.** Truecolour foregrounds are not floored: they paint as today. A cell
with an explicit background keeps the raw palette colours for both sides. The floor is a
per-theme value: 4.5 on Onyx Light and Moss & Mist, 0 on Onyx Synapse and Sage & Sand. Dim
(SGR 2) is honoured only where the floor is non-zero: after the floor, a dim cell's foreground is
blended a fixed fraction toward its background, so PSReadLine's inline prediction stays fainter
than typed text. **The dark themes paint exactly as today**, byte for byte: raw table, dim
ignored as it is now. All colour resolution, swaps included, is one pure `src/core` function that
the native tests cover; `renderer.c` only calls it.

**The numbers stand.** On the default background the derived table is exactly option 2's floor
(same function, background and threshold), so the section 1 "today" figures and the "after"
figures below are unchanged. They were recomputed for each revision with the same program, and
the critic's two independent recomputations matched. After the change, on both light themes
every one of the 16 palette entries reads at 4.5:1 or better on the default background:

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
95 -> 4.84/4.72; 30 and 31 are already above 4.5 and unchanged.) Of the 256 indexed colours, 202
are below 4.5 on Onyx Light and 204 on Moss & Mist; every one is reachable (no fallback). One
table builds in 0.74 ms (gcc -O2, Linux), at each theme resolution.

Reasons, in order:
1. It fixes the report and the thirteen other unreadable palette entries for everything
   PowerShell emits (16-colour on the default background), and 256-colour text too.
2. Paint cost is a table lookup: no cache, no per-cell walk, nothing to bound.
3. Scrollback re-colours on a theme switch because the cell stores the index and the table is
   looked up at paint.
4. Applications' own pairs are left alone. The floor touches only indexed text drawn on the
   theme's background, the one colour the application did not choose. Cells where the
   application set the background keep the raw palette on both sides, including pairs that are
   unreadable on purpose (`37;47`, 1.00:1) or by accident (`33;47`, 1.23:1; `93;107`, 1.07:1), and
   they look the same on every theme. That is a trade-off, not a guarantee: an explicit-background
   cell is exactly as readable as today, no more. (The first draft's claim that the floor "cannot
   damage deliberate pairs" was false for its own rule, which floored explicit pairs too.)
5. The whole resolution is in `src/core` and the table is built inside `ui_theme_resolve()`, so
   the merge gate's native tests check the colours, the order of floor, dim and swaps, and that
   every theme has a populated table.

The strongest reason against: within a hue, colours converge on the same 4.5:1 shade on a light
background. 90 and 97 are both `6C6C6C` on Onyx Light, and the grey ramp flattens and can reorder
by a few units: 244-250 all become `6C6C6C`, 251 `707070`, 252 `6E6E6E`, 253 `6D6D6D`, 254
`707070`, 255 `6E6E6E` (a lighter input may come out darker). Grey hierarchies (line numbers
against gutters, column shading) flatten. Documented, not fixed; a designed palette (option 3) is
the follow-up if it bothers the maintainer in use.

## 5. Design

### 5.0 First task of the plan: a short capture

Nothing below the colour maths rests on recorded PowerShell output: the `ESC[93m` string in
`tests/test_term.c` is hand-written, and the prediction's SGR is unverified (section 1). The
plan's first task, before any code: the maintainer turns debugging on with the `term` category at
Trace and "Include terminal content" ticked, so the debug log records received bytes as `term.rx`
lines (the path the `debug_terminal_log_written` harness case checks), and in a local PowerShell 7
tab:

1. runs `(Get-Module PSReadLine).Version` and notes the result;
2. runs `Clear-Host`, types at most three characters so an inline prediction appears, presses
   Escape;
3. after another `Clear-Host`, runs `Write-Host x -ForegroundColor Yellow` and `Write-Warning x`.

Keeping each step short keeps each `term.rx` record well inside `dlog_content()`'s cut of about
4,000 escaped bytes (`src/core/dlog.c`); any record marked `truncated=1` is discarded and the step
repeated. The log escapes the bytes (`ESC` as `\e` and so on); the fixture is made by decoding
that escaping mechanically. Secret scrubbing replaces known secret values with `[REDACTED]`; it
cannot match an SGR sequence, so colours survive it. A Windows PowerShell 5.1 tab is captured
only for step 3: its inbox PSReadLine 2.0 has no predictions.

Then:
- the dim design (5.3) is confirmed against the captured bytes: that ConPTY forwards SGR 2, and
  which foreground the prediction uses. If the prediction carries no SGR 2 (PSReadLine 2.1's
  `38;5;238` would not), 5.3 is revisited before implementation, not after;
- the capture is added as a second fixture beside the existing `93m` one. The existing fixture and
  its `>> ` continuation-prompt test stay: they test prompt detection, which the capture does not
  contain.

**Done (2026-10-20).** The maintainer captured a local Windows PowerShell 5.1 tab (PSReadLine
2.0.0, no predictions; log `.superpowers/capture-ps51-20261020.log`, git-ignored). Two fixtures
are decoded from it, `tests/fixtures/conpty_ps51_tab_completion.bin` (log lines 15-51) and
`conpty_ps51_commands.bin` (lines 90-112); the licence output in lines 82-89 is excluded. No
PowerShell 7 capture exists or will come (no `pwsh` on the maintainer's box), so 5.3's dim design
is implemented as specified but not confirmed against real bytes: the PS7 prediction is covered by
a synthetic test of both `97;2;3` and `38;5;238`, marked unverified. With `38;5;238` the prediction
would paint `444444`, above 4.5:1 already and so as sent: apart from typed text but darker than it.

### 5.1 The walk, generalised (`src/core/theme.c`)

```c
/* fg unchanged when min_ratio <= 1.0 or theme_contrast(fg, bg) >= min_ratio. Otherwise fg's
 * lightness is walked away from bg (lighter on a bg with L* < 50, darker otherwise) in 2-L*
 * steps with theme_shift_lightness() until it clears min_ratio, at most 50 steps. If no step
 * clears it, returns whichever of 0x080808 / 0xF7F7F7 has the higher contrast with bg. */
unsigned int theme_min_contrast_color(unsigned int fg, unsigned int bg, double min_ratio);
```

`theme_role_color()` keeps its signature and exact outputs (it keeps its own `fallback`); both
share one static walk. Its existing callers exercise only the lighter direction (they run on the
dark themes' backgrounds), so the darker direction and the extreme fallback are covered by the
new tests only. In this design it is called only at table build, never per cell.

### 5.2 Parser: keep the index, record dim (`src/term/parser.c`, `src/term/term.h`)

- `handle_sgr()` stores the palette index for 16-colour SGRs instead of 0, with explicit casts as
  the 256-colour branch already has: `fg_index = (uint8_t)(p - 30)` (30-37),
  `(uint8_t)(p - 90 + 8)` (90-97), `bg_index = (uint8_t)(p - 40)` (40-47),
  `(uint8_t)(p - 100 + 8)` (100-107). The casts matter because the MinGW build compiles with
  `-Wconversion -Werror` and the native test build does not, so a missing cast passes `Native
  tests` and fails only `make release`; per the existing rule, the cross build is run before each
  push. `TermAttr.fg_index`/`bg_index` already exist (`term.h`), so no field is added. `fg`/`bg`
  keep the raw RGB they hold today, so `test_term.c`'s `attr.fg == 0xCC3333` assertions stand; no
  test asserts `fg_index == 0` after a 16-colour SGR. `COLOR_ANSI16` and `COLOR_256` now both
  mean "indexed"; index 0-15 of `COLOR_256` is the same palette entry. The resolver switches on
  `fg_mode`, so a stale `fg_index` left by a later truecolour SGR (`ESC[93;38;2;1;2;3m`) is never
  read.
- New `TERM_ATTR_DIM (1 << 4)`: set by SGR 2, cleared by SGR 22 (which already clears bold; per
  ECMA-48 22 is "normal intensity", clearing both) and by SGR 0. `flags` is `uint8_t` with bits
  0-3 used. Italic (SGR 3) is not recorded: nothing would render it. Erase and scroll-fill copy
  `current_attr`, DIM included, as they do for every flag today; blank cells are never dimmed
  (5.4), so that cannot show.

### 5.3 Dim (light themes only)

`TERM_DIM_TOWARD_BG 0.40`: on a theme whose floor is non-zero, a dim cell's resolved foreground
(after the floor) is `theme_blend(fg, bg, 0.40)`. Blending after the floor is the only order that
keeps dim visible: dimming first and flooring after would undo it, and a separate lower ratio for
dim cells would need a second table. Measured for PSReadLine's prediction if it is `97;2` (to be
confirmed by 5.0):

| theme | prediction today | prediction after | floored 90/97 (typed tokens) | dim default fg |
|---|---|---|---|---|
| Onyx Light | `FFFFFF` 1.09 | `A3A3A4` 2.31, L* 67.0 | `6C6C6C`, L* 45.6 | `737375` 4.35 |
| Moss & Mist | `FFFFFF` 1.12 | `A1A2A1` 2.30, L* 66.5 | `6C6C6C`, L* 45.6 | `809191` 2.95 |
| Onyx Synapse, Sage & Sand | dim ignored, unchanged | unchanged | raw | unchanged |

On the light themes the prediction moves 21 L* lighter than the typed tokens and stays legible as
a hint (2.3:1). Dim lowers contrast by design, and two results are accepted as such: dim default
text on Moss & Mist is 2.95:1 (below 3:1; plain default text there is 7.87), and dim yellow
`ESC[2;33m` on Onyx Light is `B1A473` at 2.29:1. Fractions tried: at 0.33 the prediction is
2.61:1, 17.6 L* from typed tokens (a weaker cue); at 0.50 it is 1.97:1. 0.40 is the middle.

### 5.4 One pure resolver (new `src/core/term_colors.c`/`.h`)

```c
typedef struct {
    uint32_t default_fg, default_bg;  /* the theme's terminal_fg / terminal_bg */
    double   min_contrast;            /* 0 = off: raw table, dim ignored */
    uint32_t fg_on_default[256];      /* color256_to_rgb(i), floored against default_bg
                                         when min_contrast > 0, else a raw copy */
} TermPalette;

void term_palette_build(TermPalette *p, uint32_t default_fg, uint32_t default_bg,
                        double min_contrast);

#define TERM_DRAW_CURSOR   1u
#define TERM_DRAW_SELECTED 2u
/* The colours a cell is painted with, as 0xRRGGBB. */
void term_colors_resolve(const TermPalette *p, const TermCell *cell, unsigned draw_flags,
                         uint32_t *out_fg, uint32_t *out_bg);
```

`term_colors_resolve()`, in this order:
1. **Background.** Default -> `default_bg`. Indexed -> `color256_to_rgb(bg_index)`. RGB ->
   `attr.bg`.
2. **Foreground on the default background.** Default -> `default_fg` (the theme guarantees it:
   15.46 and 7.87 on the light themes). Indexed -> `fg_on_default[fg_index]`. RGB -> `attr.fg`,
   unfloored.
3. **Foreground on an explicit background.** Default -> `default_fg`; indexed ->
   `color256_to_rgb(fg_index)`; RGB -> `attr.fg`. Raw.
4. **Cursor.** If `TERM_DRAW_CURSOR` or `TERM_ATTR_REVERSE`, swap once (today's `||`).
5. **Dim**, only if `TERM_ATTR_DIM`, `min_contrast > 0`, the codepoint is not blank (0 or space)
   and the cell is not the cursor cell: `fg = theme_blend(fg, bg, TERM_DIM_TOWARD_BG)`. The
   cursor therefore always paints undimmed, which matters because PSReadLine puts the cursor on
   the prediction's first cell (dimmed it would be a 2.31:1 cursor block).
6. **Selection.** If `TERM_DRAW_SELECTED`, swap again. Contrast is symmetric
   (`test_theme_contrast_symmetric`), so a floored pair stays readable after either swap.

No cache, no glyph exemptions, no `theme_min_contrast_color()` call on the paint path. A yellow
`█` progress bar on the default background becomes dark yellow like any other yellow text;
half-block images and Powerline segments that set an explicit background are untouched by rule 3.

### 5.5 Theme tables (`src/core/ui_theme.c`/`.h`)

`ThemeColors` gains `double terminal_min_contrast;` (4.5 for Onyx Light and Moss & Mist, 0 for
Onyx Synapse and Sage & Sand). `ThemeTokens` gains `TermPalette term;` (about 1 KB), and
`ui_theme_resolve()` always calls `term_palette_build(&out->term, base->terminal_fg,
base->terminal_bg, base->terminal_min_contrast)`, for every theme (a raw copy where the floor is
0). There is no unbuilt state to fall back from: the table is part of the same resolved struct
the UI already reads its colours from (`ns_tokens()`, `g_tokens` in `window.c`), and
`ui_theme_resolve()` runs at every `g_theme =` site before the first paint. The value is per
theme, not an `is_dark` rule, so the dark-theme question is a data change.
`Get-NutshellThemeColor`'s regex reads only `0xRRGGBB` fields and is unaffected. A test that
puts a `ThemeTokens` on the stack grows by 1 KB, well inside the 16 MB the Windows test runner
links with.

### 5.6 Renderer and window (`src/ui/renderer.c`/`.h`, `src/ui/window.c`)

- `resolve_cell()` computes `draw_flags` (cursor cell, selected cell, as today), calls
  `term_colors_resolve(&ns_tokens()->term, cell, draw_flags, &fg, &bg)` and converts both with
  `to_colorref()`. Bold is still read from `attr.flags`. No pointer is stored in `Renderer`, so no
  init or wiring step can be skipped, and there is no NULL branch. No new literal `RGB(`; the
  gate's `renderer.c` count stays 2.
- `renderer_draw()`'s existing `render.draw` debug event gains `char_w=` and `cols=` beside
  `char_h=` and `rows=`, for the harness (section 6).
- **Theme switch.** `on_settings_clicked_page()` calls `force_full_terminal_repaint()`
  unconditionally after re-applying the theme. From the code this is a bug today: the renderer is
  re-initialised and the window invalidated, but `WM_SIZE` marks rows dirty only if the grid
  size changed, `WM_ERASEBKGND` returns 1, and `renderer_draw()` skips every clean row before any
  cell check, so clean rows keep the old theme's pixels while the cursor row repaints. The same
  applies to default-coloured text without this change.

### 5.7 Data flow

`handle_sgr()` stores index (indexed) or RGB (truecolour) plus flags -> `renderer_draw()` ->
`resolve_cell()` -> `term_colors_resolve(&ns_tokens()->term, ...)` -> `dispbuf_cell_clean()`
compares the final colours -> `ExtTextOutW`. A theme switch re-runs `ui_theme_resolve()`, which
rebuilds the table, and the forced repaint re-resolves every visible cell; scrolling back
re-resolves the rest.

### 5.8 Settings

None. The floor changes only indexed text on the theme's own background that reads below AA
there. If a need appears, a hidden config key overriding the theme's ratio is a small later
addition.

## 6. Tests

Native first (`make test`); declare each in `tests/runner.c`. Run the MinGW cross build before
pushing (5.2).

**`tests/test_theme.c`**
- `theme_min_contrast_color`: unchanged when already above the ratio (`FFFF55` on `121212` at
  4.5) and for any pair at ratio 0 or 1.0; `FFFF55` on `F5F5F7` at 4.5 gives >= 4.5 and < 5.0,
  still yellow (R and G within 0x10, both above B); the step before the result is below 4.5.
- Darker direction and fallback (not reached by `theme_role_color()`'s current callers): fg
  `777777` on bg `767676` (L* 49.6) at 4.5 walks lighter, stops short (`F7F7F7` is 4.24) and
  returns `080808` (4.41), the better extreme.
- `theme_role_color()` regression: each theme's `link` and `info` on `bg_primary` byte-identical
  to before the refactor (current values pinned).

**`tests/test_term_colors.c`** (new). Cells come from feeding real sequences to `term_process()`,
not hand-built attributes, so the parser's index storage is tested with the resolver. Palettes
come from `ui_theme_resolve()` of the named theme, so the tests use the shipped tables.
- Tables: resolving each of the four themes populates `term` (Onyx Synapse's
  `fg_on_default[11]` is `FFFF55`, Onyx Light's `727223`, neither zero); on the light themes all
  256 entries clear 4.5 (by construction; a guard, not evidence); on the dark themes the table
  equals `color256_to_rgb()` entry for entry.
- Default background: `ESC[93m` on Onyx Light resolves to `727223`; `ESC[33m` to `846E1B`;
  `ESC[38;5;11m` equals `ESC[93m`; `ESC[38;5;226m` >= 4.5; on Onyx Synapse `ESC[93m` is
  `FFFF55`.
- Truecolour is raw: `ESC[38;2;255;255;0m` on Onyx Light resolves to `FFFF00`;
  `ESC[93;38;2;1;2;3m` resolves to `010203` (the stale `fg_index` 11 is not read).
- Explicit background: `ESC[93;46m` (2.17), `ESC[33;47m` (1.23), `ESC[30;47m` (11.54),
  `ESC[97;41m` (5.14) and `ESC[93;107m` (1.07) resolve to the raw palette on both sides, on every
  theme; default fg on `ESC[44m` is `terminal_fg` on `5C7FD6`.
- Near-invisible and hidden indexed text: on the default background `ESC[38;5;255m` (`EEEEEE`,
  1.07 on Onyx Light) is floored to `6E6E6E` (4.68), so it shows; on an explicit background
  `ESC[37;47m` (fg == bg, 1.00) is untouched, so it stays hidden.
- Dim: `ESC[2;97mX` on Onyx Light is `A3A3A4` (`theme_blend(6C6C6C, F5F5F7, 0.40)`), distinct
  from `ESC[97m` and `ESC[90m`; on Onyx Synapse `ESC[2;97mX` is `FFFFFF` (dim ignored on a
  zero-floor theme); `ESC[2m` then `ESC[22m` clears it; `ESC[1;2m` then `ESC[22m` clears both;
  `ESC[0m` clears it; dim on an explicit background blends toward that background.
- Dim and the cursor: the cursor on a dim `X` cell paints the undimmed pair swapped (fg
  `F5F5F7`, bg `6C6C6C`), not a 2.31:1 block.
- Dim blanks: `ESC[2m` then `ESC[K` leaves cells with DIM set and codepoint 0; they resolve to
  exactly the undimmed colours, as does a dim space.
- Swaps: `ESC[7;93m` returns the floored pair swapped (bg `727223`, not `FFFF55`);
  `TERM_DRAW_CURSOR` alone swaps once; the cursor on an `ESC[7m` cell swaps once (today's `||`);
  `TERM_DRAW_SELECTED` swaps again; a selected dim cell is dimmed and then swapped.
- Theme switch: one stored `ESC[93m` cell resolves to `FFFF55` with Onyx Synapse's table and
  `727223` with Onyx Light's, with no re-parse (the scrollback property).
- End to end (`tests/test_term_conpty.c`): the two 5.1 fixtures are fed in; the command token
  (`93`), argument (`37`), parameter (`90`) and error (`38;5;9`) cells paint floored on both light
  themes and raw on the dark ones. Synthetic and unverified: a PS7 prediction in `97;2;3` and in
  `38;5;238` paints a colour distinct from the typed token on both light themes (the dim one at
  least 10 L* lighter).

**`tests/test_color.c` / `tests/test_term.c`**: asserts that `ESC[33m` stores `fg_index == 3`,
`ESC[93m` 11, `ESC[43m` `bg_index == 3`, `ESC[103m` 11; existing RGB asserts unchanged. The
existing `93m` fixture and `test_term_at_prompt_powershell_continuation` stay; the 5.1 capture's
two fixtures are added beside it.

**`tests/test_ui_theme.c`**: the per-theme values by name: Onyx Light 4.5, Moss & Mist 4.5, Onyx
Synapse 0, Sage & Sand 0; each light theme's `terminal_fg` on `terminal_bg` clears its ratio,
which is what lets rule 2 leave the default foreground alone.

**`tests/test_ui_demo.c`**: a new demo state `ansi` is valid. It is inserted before `local`
(after `batches`), so every tail anchor of `test_ui_demo_states_list_ends_in_all` (`[count-1]`
"all" through `[count-7]` "local", including `[count-5] == "cmddetail"`) still holds; only the
count changes, 16 -> 17. Its terminal text is exactly 17 lines, none longer than 24 columns, in
index order.

**Harness** (`tests/integration/cases/30-ui-demo.ps1`, tier `gate`, no SSH host or AI key)
- **The `ansi` demo text** (`ui_demo_build()`, `src/core/ui_demo.c`): 17 lines, one swatch per
  line, each at most 24 columns so nothing wraps even at 37 columns. Line N (0-15) is
  `NNN ████ Ab`: the index, a four-cell block of U+2588 in palette index N
  (`ESC[3Nm`/`ESC[9Nm`), two glyphs. Line 16 is the same in `ESC[2;97m` (dim). No trailing
  newline, so the cursor sits at the end of line 16 and nothing scrolls while the grid has at
  least 17 rows. Explicit-pair rows are left to the native tests.
- **Dock.** `local` docks the panel like every other state, so `ansi` takes the narrowest
  supported dock instead: `ui_demo_panel_width_px("ansi", dpi)` returns `ns_scale(260, dpi)`, the
  value `policy-compact` already uses. Reflow (`term_reflow_buffer()`) cannot wrap a 24-column
  line while the grid keeps at least 24 columns.
- **Where it samples.** The case enables debugging with the render category at Trace (through
  `New-NutshellTestEnv -Settings`), sets the window to the work area's full height, reads `rows=`,
  `cols=`, `char_w=` and `char_h=` from the last `render.draw` event, and refuses to sample (FAIL
  with the grid in the detail) unless `rows >= 17` and `cols >= 24`. The terminal's top-left is
  the tab strip's top-left (the client origin; the strip sits at client 0,0) plus the `x=` and
  `y=` the same event logs (`renderer_draw()` starts at the left margin, not at 0; corrected by
  the plan, which adds both fields); line N's sample is the centre of
  its second block cell: x = left + 5.5 * `char_w`, y = top + (N + 0.5) * `char_h`. The centre
  pixel of a full block has no anti-aliasing. If the plan finds the log fields or the origin
  unreliable, the case degrades to the gallery capture (visual check) plus the native tests, and
  the spec's harness coverage says so.
- Add `ansi` to `$galleryStates` (and its hand-kept comment), so the contact sheet shows the
  palette on all four themes for the eye.
- **`helpers_ansi_contrast_floor`**: `--ui-demo=ansi --theme "Onyx Light"`; lines 0-15 each have
  contrast >= 4.4 against `terminal_bg` (0.1 headroom for capture rounding), line 11 is `727223`
  within 12 per channel, line 16 (dim) is at least 10 L* lighter than line 15. A control run on
  `Onyx Synapse` asserts line 11 is `FFFF55` and line 16 `FFFFFF` within 12 (raw table, dim
  ignored).
- **`helpers_theme_switch_repaints`**: `--ui-demo=ansi --theme "Onyx Synapse"`, then
  `Open-NutshellSettings`, select Onyx Light, close with OK, screenshot. Lines 11 and 15 (far from
  the cursor line) are the Onyx Light colours (`727223`, `6C6C6C`), not the Onyx Synapse ones
  (`FFFF55`, `FFFFFF`). This is the path `--theme` never exercises (5.6).
- List both cases in `tests/integration/README.md`.

## 7. Out of scope

- **Truecolour foregrounds** are not floored. Flooring them needs `theme_min_contrast_color()`
  per cell on the paint path: 0.09 us per check and up to 7.4 us per walk (4.2 us average against
  mid-greys; gcc -O2, Linux; unmeasured under MinGW's `-Os -flto`), about 37-65 ms per paint for a
  screen of below-floor truecolour text, and full repaints are the normal case while a selection
  is live or the view is scrolled back. Nothing in the report needs it. If it is ever wanted, a
  small direct-mapped (fg -> floored fg) cache per `TermPalette`, cleared at build, is the way in.
  Truecolour fg == bg tricks (neovim's hidden `~` lines with a transparent background) therefore
  keep working.
- **Dim on the dark themes**: ignored as today (section "Open for the maintainer").
- **Default foreground on an explicit background**: PowerShell 7's
  `$PSStyle.FileInfo.Directory` (`44;1`, unverified) paints `terminal_fg` on `5C7FD6` at 4.38
  (Onyx Light), 2.28 (Moss & Mist), 2.91 (Onyx Synapse). PSReadLine's ListView selected row
  (default fg on an explicit `48;5;238`, unverified) is 1.73:1 on Onyx Light and 1.11:1 on Moss &
  Mist. Both are explicit-background pairs and stay raw.
- **Explicit backgrounds generally**: `Write-Host x -ForegroundColor Yellow -BackgroundColor
  White` arrives as `93;107` (1.07:1) and stays raw. "Fixes PowerShell yellow" means yellow on
  the theme's background.
- **Grey-ramp flattening** (section 4): documented, not fixed.
- A designed per-theme palette (option 3); the normal/bright convergence it would fix.
- The dark themes' floor (0 in this spec).
- Italic (SGR 3), conceal (SGR 8), bold-as-bright.
- OSC 4/10/11. Answering OSC 11 (background query), so that `bat`, `delta`, vim and `glow` can
  choose a light palette themselves, is cheap and complementary; it goes on the notes document's
  open list as its own item.
- Windows PowerShell 5.1's black warning/error background, if ConPTY forwards it as `40`
  (readable already, 19.69:1).
- Removing the unused `foreground_colour`/`background_colour` config fields.

## Critique (round 1, 2026-10-19)

Critic: claude-opus-5-5, read-only, on `300f1cc` (`.superpowers/critique-ansi-contrast.md`).
Dispositions by the main session (`.superpowers/critique-ansi-contrast-dispositions.md`), one
line each:

1. PSReadLine's prediction (`97;2;3`) would look like typed text under the floor. ACCEPT: dim
   (SGR 2) in scope as a blend after the floor; italic out; bytes captured first (5.0, 5.3).
2. "Cannot damage deliberate pairs" was false; explicit/explicit pairs were floored. ACCEPT: the
   floor applies only on the theme-default background; explicit-background cells keep raw
   colours; section 4 reworded.
3. Option 4b was rejected on a wrong cost estimate. ACCEPT, decision changed to 4b: index kept,
   256-entry floored table per theme, truecolour floored on the fly with no cache.
4. Nothing in the merge gate checks the renderer uses the floor. ACCEPT: all resolution,
   swaps included, in one pure `src/core` function (5.4); `renderer.c` only calls it.
5. Inserting `ansi` before `settings-debug` breaks the tail anchors. ACCEPT: inserted before
   `local` so every anchor holds and only the count changes (see the note below).
6. The stale theme-switch repaint is certain from the code. ACCEPT:
   `force_full_terminal_repaint()` unconditional; mid-session theme-switch harness case added.
7. The cache thrashes beyond 256 distinct pairs. ACCEPT as moot: no cache; the walk's measured
   cost is kept as the bound for the truecolour path (5.4).
8. Harness geometry underspecified; the pair rows would fail the scan. ACCEPT: samples by cell
   coordinates in deterministic swatch rows, pair rows below the sampled range; degrades to the
   gallery plus native tests if no reliable cell size.
9. The `93m` "capture" is a hand-written fixture. ACCEPT: replaced by a captured ConPTY stream
   as the plan's first task (5.0).
10. Block-element exemption leaves yellow progress bars invisible. ACCEPT as moot: no glyph
    exemptions; such bars become dark yellow.
11. The `terminal_min_contrast` test re-encoded the `is_dark` rule. ACCEPT: values pinned by
    theme name.
12. Tautological tests; fg == bg untested. ACCEPT: fg == bg pinned on the default background
    (floored) and on an explicit one (untouched); the all-entries test kept as a guard only.
13. Smaller items (`44;1` on dark themes, OSC 11, compliance). ACCEPT in part: dark themes stay
    at 0 with the open question; OSC 11 goes on the notes open list, out of scope here.

Note on 5: the dispositions file says "appended after `settings-debug` and before `all`", but
anywhere between `local` (`[count-7]`) and `all` moves at least one anchored name, including
`[count-5]`. The stated intent (keep the anchors, change only the count) is met only by inserting
before `local`, which is what this revision does.

## Critique (round 2, 2026-10-19)

Critic: claude-opus-5-5, read-only, fresh pass on `a8e794c`
(`.superpowers/critique-ansi-contrast-round2.md`). Dispositions by the main session
(`.superpowers/critique-ansi-contrast-round2-dispositions.md`), one line each. Round 2 is the
ceiling; what is still open goes to the maintainer below and in the PR body.

1. The harness geometry breaks at high DPI (50 % dock, reflow, 60-70-column lines). ACCEPT: one
   swatch per line, at most 24 columns; narrowest supported dock (`local` docks too); sampling by
   cell coordinates from `render.draw`'s new `char_w`; fallback to gallery plus native tests kept.
2. The renderer's NULL-palette fallback hides a wiring miss. ACCEPT: no fallback; the table is
   part of `ThemeTokens`, built in `ui_theme_resolve()` for every theme, read through
   `ns_tokens()`; a native test checks all four themes are populated.
3. The truecolour walk on the paint path is unbounded and uncached. ACCEPT via the cheaper route:
   truecolour is not floored; out of scope with the measured cost and the cache as the way in.
4. Dim changes all four themes and leaves dim text below 3:1. ACCEPT: dim only on themes with a
   non-zero floor; dark themes byte-identical; the 2.95 and 2.29 figures stated as accepted.
5. The cursor goes faint on the prediction's first cell; erase leaves DIM blanks. ACCEPT: cursor
   swap before dim (cursor cell undimmed), blanks never dimmed; tests for both.
6. The `term.rx` capture is lossy and replacing the fixture deletes a prompt test. ACCEPT: short
   capture steps, truncated records discarded, escaping decoded, scrubbing noted; the `93m`
   fixture kept and the capture added beside it.
7. PSReadLine version assumptions (5.1 has no predictions; 2.1 used `38;5;238`; ListView).
   ACCEPT: stated as unverified in section 1, version recorded in the capture, ListView row in
   section 7.
8. The parser snippet fails only the `-Wconversion` Windows build. ACCEPT: explicit casts; the
   MinGW cross build before each push restated.
9. The floored grey ramp flattens and reorders. ACCEPT as a documented limitation (section 4).
10. Truecolour fg == bg is revealed on purpose. ACCEPT as moot (finding 3); the fg == bg tests
    cover indexed colours only.
11. Explicit backgrounds stay as unreadable as today. ACCEPT as already documented (sections 4
    and 7).

## Open for the maintainer

1. **Dark themes.** Set the floor to 4.5 on Onyx Synapse and Sage & Sand too? Gain: SGR 30 black
   on the default background becomes visible (1.12 -> 4.55 on Onyx Synapse, 1.50 -> 4.72 on Sage
   & Sand), and Sage & Sand's red reads (2.72 -> 4.73, `D57E7E`, visibly pinker; blue, magenta,
   grey and bright red lift slightly). Cost: their look changes. One value per theme either way.
2. **Default foreground on an explicit background.** Leave it raw (this spec), or floor
   `terminal_fg` against palette backgrounds with a second 16-entry table? `44;1` directories are
   2.28:1 on Moss & Mist; PSReadLine's ListView selected row is 1.73 / 1.11.
3. **Normal/bright convergence on light themes** (and the grey ramp): acceptable in use, or worth
   a designed palette later (option 3)?
4. **Dim on the dark themes.** Honour SGR 2 there later too? Today a `97;2` prediction paints
   `FFFFFF`, brighter than typed text (`E0E0E0`); a 0.40 blend would give `A0A0A0` (7.16:1) on
   Onyx Synapse and `AAABA7` (6.05:1) on Sage & Sand, but it also dims every `ESC[2m` user (git,
   npm, pytest, Claude Code) on the themes most people use.

## Maintainer decisions (2026-10-19)

The four questions above, answered by the maintainer ("accept all 4", the recommendations
as put): 

1. **Dark themes stay at a floor of 0.** Onyx Synapse and Sage & Sand paint byte-identical
   to today; this change is invisible there.
2. **Default foreground on an explicit background stays raw.** No second table in this
   change; the `44;1` directory cells and PSReadLine's ListView row are a recorded
   limitation.
3. **The normal/bright convergence on light themes is accepted.** A designed light palette
   (option 3) is a follow-up, recorded on the notes document's open list.
4. **Dim stays off on the dark themes.** SGR 2 is honoured only where the floor is non-zero.

With these the spec is approved for planning; the status line at the top records it.
