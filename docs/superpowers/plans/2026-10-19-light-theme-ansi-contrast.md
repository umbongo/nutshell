# Light-Theme ANSI Contrast Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make indexed terminal colours readable on the two light themes (Onyx Light, Moss & Mist): every 16- and 256-colour foreground drawn on the theme's own background is floored to 4.5:1, SGR 2 (dim) is painted there so PSReadLine's inline prediction stays fainter than typed text, the dark themes paint byte for byte as today, and a theme switch repaints every row.

**Architecture:** The parser keeps the palette index for 16-colour SGRs (the field exists and is written as 0 today) and records dim in a new `TERM_ATTR_DIM` flag. Each theme's resolved `ThemeTokens` gains a `TermPalette`: a 256-entry foreground table built once by `ui_theme_resolve()`, floored with a new `theme_min_contrast_color()` where the theme's `terminal_min_contrast` is non-zero (4.5 on the light themes, 0 on the dark ones). One pure function, `term_colors_resolve()` in `src/core/term_colors.c`, decides every cell's painted pair (table only on the default background, explicit backgrounds raw, truecolour raw, cursor/reverse swap, dim, selection swap); `renderer.c` only calls it, and `window.c` forces a full repaint after Settings changes the theme.

**Tech Stack:** C11; native `gcc` for `make test`; `x86_64-w64-mingw32-gcc` for `make release` and `make wintest`; the repo's `tests/test_framework.h`; Python 3 for byte-exact edits of CRLF files and for decoding the debug log; PowerShell 5.1 harness (`tests/integration/`), run only on the maintainer's Windows box.

**Spec:** `docs/superpowers/specs/2026-10-19-light-theme-ansi-contrast-design.md` (sections 4-7, both Critique sections, "Maintainer decisions (2026-10-19)"). The maintainer's four decisions are in force: dark themes floor 0; the default foreground on an explicit background stays raw; normal/bright convergence accepted; dim off on the dark themes.

**Plan notes** (where the spec's wording and this plan part, and what was chosen; every C block below was compiled and run on this host while the plan was written, and every colour value in a test is the value the code produced, which matched the spec's tables):

- **The capture is in.** Section 5.0's capture arrived on 2026-10-20 (`.superpowers/capture-ps51-20261020.log`, git-ignored): Windows PowerShell 5.1 with PSReadLine 2.0.0, so no inline prediction and no SGR 2; there is no PowerShell 7 on the maintainer's box, so no PS7 capture will come. Task 8 therefore decodes two fixtures from that log and tests the real bytes (93 command tokens, 37 arguments, 90 parameters, the `38;5;9` error record), and covers PS7's prediction with a synthetic test of both documented forms (`97;2;3`, `38;5;238`), marked unverified. The dim design (5.3) is implemented in Tasks 2-3 as specified; on `38;5;238` a prediction would paint `444444`, darker than typed text, which the synthetic test pins and the spec records. Task 8 touches tests only and runs before Task 9.
- **Origin of the harness samples.** The spec puts the terminal's top-left at "the tab strip's bottom-left". `renderer_draw()` is called at `x = g_left_margin` (6 px scaled by DPI), not 0, so Task 7 adds `x=` and `y=` beside the spec's `char_w=` and `cols=` in `render.draw`, and the harness computes the origin as the strip's top-left (the client origin; the strip sits at client 0,0) plus the logged `x`/`y`. Task 6 amends the spec's sentence.
- **Where tests live.** Parser tests go in `tests/test_term.c` (LF), not `tests/test_color.c` (CRLF). The capture tests go in `tests/test_term_conpty.c`, which already reads fixtures. The two fixtures are binary, decoded from named line ranges of the log (Task 8's table gives lines, byte counts and sha256); the licence output in lines 82-89 is excluded.
- **`buffer.h`.** The spec names `src/term/buffer.h`; it does not exist. `TermAttr`, the `TERM_ATTR_*` flags and `color256_to_rgb()` are in `src/term/term.h`.
- **Include chain.** `ui_theme.h` now includes `term_colors.h`, which includes `term.h`, so every `src/ui` file that includes `ui_theme.h` sees `term.h`. Every product object was cross-compiled with this chain while the plan was written; nothing collides.
- **`resolve_cell()`** in `renderer.c` loses its `const Renderer *` parameter (it would be unused, an error under `-Wextra -Werror`) and takes the palette, fetched once per paint from `ns_tokens()`.
- **`theme_role_color()`** keeps its signature and outputs; Task 1 pins each theme's `link` and `info` on `bg_primary` as literals (`5D82BB`, `9175AD`, `0847A6`, `7B4FA3`, `8799A1`, `A18FAF`, `3D6D7A`, `7A5C99`), measured before the refactor.
- **Notes document.** The follow-up (designed light palette, OSC 11, default fg on explicit backgrounds) is already on the open list of `docs/superpowers/specs/2026-09-06-ui-redesign-notes.md`; no task edits it.
- **`main` has moved.** This branch was cut at `c86dea1` (v1.2.37); `origin/main` is at v1.2.41 (PR #71) and touches `tests/runner.c`, `tests/test_ui_demo.c`, `src/core/ui_theme.{c,h}`, `src/core/ui_demo.{c,h}`, `src/ui/window.c` and `tests/integration/README.md` too, in other places. Task 9 merges it; the version is computed then.

## Global Constraints

- **Always run `make test` after changes.** Baseline on this branch: `Tests Run: 2982, Failed: 0`. This plan adds 24 native tests; Task 8's two capture tests skip (and pass) whenever their fixture files are absent, the convention `tests/test_term_conpty.c` already uses.
- **Two compilers**: `x86_64-w64-mingw32-gcc` for the Windows cross-compile, native `gcc` for `make test`. Code must compile clean under both with `-Werror`. `-Wconversion` and `-Wshadow` are on only in the MinGW build, so a missing cast passes `make test` and fails `make release`: every task that changes `src/` cross-compiles its objects with the command it gives (to `/dev/null`, no binary).
- **CRLF files stay CRLF.** `src/term/parser.c`, `src/core/theme.c`, `src/core/theme.h`, `src/core/ui_theme.c`, `src/core/ui_theme.h`, `tests/test_theme.c`, `tests/test_ui_theme.c` and `tests/runner.c` are CRLF. Every edit in this plan, CRLF or LF, is a Python script that converts its snippet to the file's line ending and refuses unless the old text occurs exactly once. Afterwards, for a CRLF file, `grep -c $'\r$' FILE` equals `wc -l < FILE`. New files are LF.
- **MANDATORY: Version Bump Before Every Build** of the product: `src/ui/resource.h` (`APP_VERSION`, `APP_VERSION_BINARY`) and `README.md` (`**Version**:`). This plan does it **once, in Task 9**, with `make clean && make release`, and commits `build/win/nutshell.exe`. Until Task 9 never run `make`, `make all` or `make release`; `make test` and `make wintest` build test runners only.
- **Design-system gates** (`tests/test_ui_tokens.c`, run by `make test`): no new `RGB(` in `src/ui` (`renderer.c` keeps exactly its 2), no `MulDiv(x, dpi, 96)`, no new `CreateFont`. `to_colorref()` unpacks a variable and does not count.
- **Test macros.** `ASSERT_*` are bare `if` statements: inside an `if`/`else`, always brace them. They evaluate their operands again when they fail: never put a call with side effects inside one.
- **Spec values are the authority.** If a pinned colour in a test does not come out as written, stop and report; do not change the expected value to match.
- **Git commits**: stage new files by path and read every `??` line of `git status --short` before committing (`git add -u` skips new files). Write the subject and body as if the maintainer authored them. End every commit with a `Models:` line, then a blank line, then exactly these trailers:
  `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`
  `Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM`
  The `Models:` line in the templates below reads `Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)`; if you are not `claude-sonnet-5`, put your own full model id there.
- This plan ends at local commits on `fix/light-theme-ansi-contrast` in `/home/thomas/nutshell-wt/ansi-contrast` (PR #72). Pushing follows the `feature-workflow` skill; never push to `main`, never merge.
- **Hosts.** Everything here runs on the Linux box `automaton`. Wintest: `wineserver -k; xvfb-run -a make wintest` (about 70 s; if it stops right after `[pty] re-executing with a console of our own` with no case lines, run it once more). The integration harness runs only on the maintainer's Windows box (192 DPI): a harness step ends with "maintainer runs `-Only <case>`", never a local run.
- No settings key, no config change (spec 5.8).

## Review Focus

1. **Dim default text** (`ESC[2m` with the default foreground, as git, npm, pytest and Claude Code print it) on a light theme: expected visibly fainter but still readable, `737375` on Onyx Light (4.35:1), and unchanged on a dark theme. Pinned in Task 3 by `test_term_colors_dim`.
2. **Bold indexed colour** (`Write-Warning`'s `33;1`, `1;93`): expected the same floored colour as without bold, since bold only selects the heavier font (`846E1B`, `727223`). Pinned in Task 3 by `test_term_colors_default_bg_uses_floored_table`.
3. **Colours that already read on the light background** (a light-aware program's `38;5;24`, any of the 54 table entries already at 4.5:1 on Onyx Light): expected painted exactly as asked, not darkened further. Pinned in Task 3 by `test_term_palette_build_floors_or_copies`.
4. **An SGR split across two reads** (ConPTY and SSH deliver bytes in arbitrary pieces: `ESC[9` then `3;2mX`): expected the index and dim still stored. Pinned in Task 2 by `test_term_sgr_split_across_reads_keeps_index_and_dim`.
5. **Dim with reverse video** (`ESC[2;7m`, status lines): expected the pair swapped first and the drawn foreground then blended toward the drawn background (`9F9FA1` on `1D1D1F`), never undimmed (only the cursor cell is). Pinned in Task 3 by `test_term_colors_swaps`.

---

## File Structure

| File | Task | Responsibility |
|---|---|---|
| `src/core/theme.c`, `src/core/theme.h` (CRLF) | 1 | `theme_min_contrast_color()`; `theme_role_color()` shares its walk |
| `tests/test_theme.c` (CRLF) | 1 | the floor, the fallback, the pinned role colours |
| `src/term/term.h`, `src/term/parser.c` (CRLF) | 2 | `TERM_ATTR_DIM`; 16-colour SGRs store their index; SGR 2 / 22 |
| `tests/test_term.c` | 2 | index storage, dim set/clear, split reads |
| `src/core/term_colors.h`, `src/core/term_colors.c` (new) | 3 | `TermPalette`, `term_palette_build()`, `term_colors_resolve()`, `TERM_DIM_TOWARD_BG` |
| `tests/test_term_colors.c` (new) | 3, 4 | the resolver's order and rules; the shipped tables |
| `src/core/ui_theme.h`, `src/core/ui_theme.c` (CRLF) | 4 | `ThemeColors.terminal_min_contrast`, `ThemeTokens.term`, built in `ui_theme_resolve()` |
| `tests/test_ui_theme.c` (CRLF) | 4 | the floor per theme, by name |
| `src/core/ui_demo.c`, `src/core/ui_demo.h` | 5 | the `ansi` demo state and its narrow dock |
| `tests/test_ui_demo.c` | 5 | state list, swatch text |
| `tests/integration/cases/30-ui-demo.ps1`, `tests/integration/README.md` | 6 | `ansi` in the gallery; `helpers_ansi_contrast_floor`, `helpers_theme_switch_repaints` |
| `docs/superpowers/specs/2026-10-19-light-theme-ansi-contrast-design.md` | 6 | the harness origin sentence (the capture's result in sections 1, 5.0 and 6 was recorded with this plan) |
| `src/ui/renderer.c` | 7 | `resolve_cell()` calls `term_colors_resolve()`; `render.draw` logs `char_w`, `cols`, `x`, `y` |
| `src/ui/window.c` | 7 | full repaint after Settings |
| `tests/fixtures/conpty_ps51_tab_completion.bin`, `tests/fixtures/conpty_ps51_commands.bin` (new) | 8 | log lines 15-51 and 90-112 of the 2026-10-20 capture, decoded |
| `tests/test_term_conpty.c` | 8 | end-to-end tests on the capture; the synthetic PS7 prediction test |
| `tests/runner.c` (CRLF) | 1-5, 8 | declares and calls every new native test |
| `src/ui/resource.h`, `README.md`, `build/win/nutshell.exe` | 9 | the branch's one version bump and the rebuilt exe |

---

### Task 1: `theme_min_contrast_color()`, the general form of the AA walk

**Files:**
- Modify: `src/core/theme.c:138-157` (CRLF; `theme_role_color()`)
- Modify: `src/core/theme.h:48-51` (CRLF)
- Test: `tests/test_theme.c` (CRLF; appended)
- Modify: `tests/runner.c` (CRLF; after `test_theme_blend_clamps_t`)

**Interfaces:**
- Consumes: `theme_contrast()`, `theme_lstar()`, `theme_shift_lightness()` (unchanged).
- Produces (used by Task 3): `unsigned int theme_min_contrast_color(unsigned int fg, unsigned int bg, double min_ratio);` declared in `src/core/theme.h`. `fg` unchanged when `min_ratio` is not above 1.0 (including NaN) or `theme_contrast(fg, bg) >= min_ratio`; otherwise walked in 2-L* steps away from `bg` (lighter when `theme_lstar(bg) < 50`), at most 50 steps; if none clears, the better of `0x080808`/`0xF7F7F7` (`0x080808` on a tie).

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_theme.c` and register the four tests (both files are CRLF; the scripts keep that):

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && python3 - <<'PYEOF'
path = 'tests/test_theme.c'
raw = open(path, 'rb').read()
assert raw.endswith(b'\r\n')
text = r'''
/* ---- theme_min_contrast_color (spec 2026-10-19-light-theme-ansi-contrast-
 * design.md, section 5.1) ------------------------------------------------ */

int test_theme_min_contrast_unchanged_when_clear_or_off(void)
{
    TEST_BEGIN();
    /* Already above the ratio: bright yellow on a near-black ground. */
    ASSERT_EQ((int)theme_min_contrast_color(0xFFFF55, 0x121212, 4.5), (int)0xFFFF55);
    /* Off: a ratio of 0, 1.0, below 0 or NaN leaves any pair alone, even fg == bg. */
    ASSERT_EQ((int)theme_min_contrast_color(0xFFFF55, 0xF5F5F7, 0.0), (int)0xFFFF55);
    ASSERT_EQ((int)theme_min_contrast_color(0xFFFF55, 0xF5F5F7, 1.0), (int)0xFFFF55);
    ASSERT_EQ((int)theme_min_contrast_color(0x767676, 0x767676, 1.0), (int)0x767676);
    ASSERT_EQ((int)theme_min_contrast_color(0x767676, 0x767676, -3.0), (int)0x767676);
    ASSERT_EQ((int)theme_min_contrast_color(0xFFFF55, 0xF5F5F7, NAN), (int)0xFFFF55);
    TEST_END();
}

int test_theme_min_contrast_walks_darker_on_light_bg(void)
{
    TEST_BEGIN();
    unsigned int bg = 0xF5F5F7; /* Onyx Light terminal_bg */
    unsigned int got = theme_min_contrast_color(0xFFFF55, bg, 4.5);
    double c = theme_contrast(got, bg);
    ASSERT_TRUE(c >= 4.5);
    ASSERT_TRUE(c < 5.0);
    ASSERT_EQ((int)got, (int)0x727223);
    /* Still yellow: R and G within 0x10 of each other, both above B. */
    unsigned int r = (got >> 16) & 0xFFu, g = (got >> 8) & 0xFFu, b = got & 0xFFu;
    ASSERT_TRUE((r > g ? r - g : g - r) <= 0x10u);
    ASSERT_TRUE(r > b && g > b);
    /* The walk stops at the first step that clears: redo it by hand. */
    unsigned int prev = 0xFFFF55, step = 0xFFFF55;
    for (int i = 0; i < 50 && theme_contrast(step, bg) < 4.5; i++) {
        prev = step;
        step = theme_shift_lightness(step, -2.0);
    }
    ASSERT_EQ((int)step, (int)got);
    ASSERT_TRUE(theme_contrast(prev, bg) < 4.5);
    TEST_END();
}

/* No shade clears the ratio: the better of the walk's two extremes. Not
 * reachable through theme_role_color()'s current callers. */
int test_theme_min_contrast_falls_back_to_better_extreme(void)
{
    TEST_BEGIN();
    /* 767676 has L* 49.6, so the walk goes lighter, but even F7F7F7 is only
     * 4.24 against it; 080808 (4.41) is the better extreme. */
    ASSERT_TRUE(theme_lstar(0x767676) < 50.0);
    ASSERT_TRUE(theme_contrast(0xF7F7F7, 0x767676) < 4.5);
    ASSERT_EQ((int)theme_min_contrast_color(0x777777, 0x767676, 4.5), (int)0x080808);
    /* 21:1 needs pure white on black, which the walk never reaches. */
    ASSERT_EQ((int)theme_min_contrast_color(0x808080, 0x000000, 21.0), (int)0xF7F7F7);
    TEST_END();
}

/* theme_role_color() now shares its walk with theme_min_contrast_color();
 * its outputs are pinned to what they were before (each theme's link and
 * info on bg_primary, src/core/ui_theme.c), and the fallback still wins
 * when no shade clears 4.5. */
int test_theme_role_color_outputs_unchanged(void)
{
    TEST_BEGIN();
    static const struct { unsigned int role, bg, fallback, want; } cases[] = {
        { 0x4A78B7, 0x121212, 0xE0E0E0, 0x5D82BB }, /* Onyx Synapse link */
        { 0x8969A8, 0x121212, 0xE0E0E0, 0x9175AD }, /* Onyx Synapse info */
        { 0x0847A6, 0xF5F5F7, 0x1D1D1F, 0x0847A6 }, /* Onyx Light link   */
        { 0x7B4FA3, 0xF5F5F7, 0x1D1D1F, 0x7B4FA3 }, /* Onyx Light info   */
        { 0x5D7B89, 0x2B2D24, 0xEAE7DC, 0x8799A1 }, /* Sage & Sand link  */
        { 0x896B9D, 0x2B2D24, 0xEAE7DC, 0xA18FAF }, /* Sage & Sand info  */
        { 0x3D6D7A, 0xF1F3F0, 0x354F52, 0x3D6D7A }, /* Moss & Mist link  */
        { 0x7A5C99, 0xF1F3F0, 0x354F52, 0x7A5C99 }, /* Moss & Mist info  */
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        ASSERT_EQ((int)theme_role_color(cases[i].role, cases[i].bg, cases[i].fallback),
                  (int)cases[i].want);
    ASSERT_EQ((int)theme_role_color(0x777777, 0x767676, 0x123456), (int)0x123456);
    TEST_END();
}
'''
raw += text.replace('\n', '\r\n').encode()
open(path, 'wb').write(raw)
print('%s: 4 tests appended (CRLF kept)' % path)
PYEOF
python3 - test_theme_blend_clamps_t "" \
  test_theme_min_contrast_unchanged_when_clear_or_off test_theme_min_contrast_walks_darker_on_light_bg \
  test_theme_min_contrast_falls_back_to_better_extreme test_theme_role_color_outputs_unchanged <<'PYEOF'
# Run as: python3 - <anchor test> <comment, or ""> <test name>... <<'PYEOF'
# Registers test functions in tests/runner.c (CRLF): declarations after the
# anchor's declaration, calls after the anchor's call, in the order given.
import sys
anchor, comment, names = sys.argv[1], sys.argv[2], sys.argv[3:]
p = 'tests/runner.c'
b = open(p, 'rb').read()
da = ('int %s(void);\r\n' % anchor).encode()
ca = ('    failed += %s();\r\n' % anchor).encode()
assert b.count(da) == 1 and b.count(ca) == 1, 'anchor not found exactly once'
decls = (('/* %s */\r\n' % comment) if comment else '') + ''.join('int %s(void);\r\n' % n for n in names)
calls = ''.join('    failed += %s();\r\n' % n for n in names)
b = b.replace(da, da + decls.encode()).replace(ca, ca + calls.encode())
open(p, 'wb').write(b)
print('runner.c: registered %d tests after %s' % (len(names), anchor))
PYEOF
for f in tests/test_theme.c tests/runner.c; do echo "$f CR lines: $(grep -c $'\r$' $f) of $(wc -l < $f)"; done
```

Expected: `tests/test_theme.c: 4 tests appended (CRLF kept)`, `runner.c: registered 4 tests after test_theme_blend_clamps_t`, and both files with equal counts (`tests/test_theme.c CR lines: 333 of 333`, `tests/runner.c CR lines: 6608 of 6608`).

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && make test 2>&1 | grep -E "theme_min_contrast_color|undefined reference" | head -3`
Expected: `warning: implicit declaration of function 'theme_min_contrast_color'` (gcc prints curly quotes) and the link failing with ``undefined reference to `theme_min_contrast_color'``.

- [ ] **Step 3: Write the implementation**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && python3 - <<'PYEOF'
def apply(path, edits):
    raw = open(path, 'rb').read()
    crlf = b'\r\n' in raw
    text = raw.decode('utf-8')
    for old, new in edits:
        if crlf:
            old, new = old.replace('\n', '\r\n'), new.replace('\n', '\r\n')
        assert text.count(old) == 1, (path, text.count(old), old[:60])
        text = text.replace(old, new)
    open(path, 'wb').write(text.encode('utf-8'))
    print('%s: %d edit(s)%s' % (path, len(edits), ' (CRLF kept)' if crlf else ''))

apply('src/core/theme.c', [(r'''unsigned int theme_role_color(unsigned int role, unsigned int bg,
                              unsigned int fallback)
{
    if (theme_contrast(role, bg) >= 4.5) return role;

    /* Keep the theme's own hue: walk the role colour's lightness away from
     * the background (lighter on a dark ground, darker on a light one) in
     * small L* steps until it reads at AA. theme_shift_lightness scales
     * the channels together, so the hue survives; only its lightness
     * moves. The fallback is reached only if even the lightest/darkest
     * reachable shade cannot clear 4.5:1 (never the case for a saturated
     * hue against a mid-grey, but guaranteed legible regardless). */
    double dir = (theme_lstar(bg) < 50.0) ? 2.0 : -2.0;
    unsigned int c = role;
    for (int i = 0; i < 50; i++) {
        c = theme_shift_lightness(c, dir);
        if (theme_contrast(c, bg) >= 4.5) return c;
    }
    return fallback;
}
''', r'''/* Keep fg's own hue: walk its lightness away from bg (lighter on a dark
 * ground, darker on a light one) in 2-L* steps until it reads at
 * min_ratio. theme_shift_lightness scales the channels together, so the
 * hue survives; only its lightness moves. Each step starts from the
 * previous step's (rounded, clamped) result, as theme_role_color() always
 * has, so its outputs are unchanged. Returns 1 with *out set on success,
 * 0 if 50 steps never clear min_ratio. */
static int walk_to_ratio(unsigned int fg, unsigned int bg, double min_ratio,
                         unsigned int *out)
{
    double dir = (theme_lstar(bg) < 50.0) ? 2.0 : -2.0;
    unsigned int c = fg;
    for (int i = 0; i < 50; i++) {
        c = theme_shift_lightness(c, dir);
        if (theme_contrast(c, bg) >= min_ratio) { *out = c; return 1; }
    }
    return 0;
}

unsigned int theme_role_color(unsigned int role, unsigned int bg,
                              unsigned int fallback)
{
    if (theme_contrast(role, bg) >= 4.5) return role;
    /* The fallback is reached only if even the lightest/darkest reachable
     * shade cannot clear 4.5:1 (never the case for a saturated hue against
     * a mid-grey, but guaranteed legible regardless). */
    unsigned int c = 0;
    return walk_to_ratio(role, bg, 4.5, &c) ? c : fallback;
}

unsigned int theme_min_contrast_color(unsigned int fg, unsigned int bg,
                                      double min_ratio)
{
    if (!(min_ratio > 1.0)) return fg;          /* off, and NaN */
    if (theme_contrast(fg, bg) >= min_ratio) return fg;
    unsigned int c = 0;
    if (walk_to_ratio(fg, bg, min_ratio, &c)) return c;
    /* No shade of the hue clears it: the better of the walk's two
     * extremes (theme_shift_lightness never goes past them). */
    return (theme_contrast(0x080808u, bg) >= theme_contrast(0xF7F7F7u, bg))
        ? 0x080808u : 0xF7F7F7u;
}
''')])

apply('src/core/theme.h', [(r'''unsigned int theme_role_color(unsigned int role, unsigned int bg,
                              unsigned int fallback);
''', r'''unsigned int theme_role_color(unsigned int role, unsigned int bg,
                              unsigned int fallback);

/* Minimum-contrast floor, the general form of theme_role_color(): `fg`
 * unchanged when min_ratio <= 1.0 (or NaN) or theme_contrast(fg, bg) >=
 * min_ratio. Otherwise fg's lightness is walked away from bg (lighter on a
 * bg with L* < 50, darker otherwise) in 2-L* steps with
 * theme_shift_lightness() until it clears min_ratio, at most 50 steps. If
 * no step clears it, returns whichever of 0x080808 / 0xF7F7F7 has the
 * higher contrast with bg (0x080808 on a tie). Used to build the terminal's
 * per-theme 256-colour tables (src/core/term_colors.c), never per cell. */
unsigned int theme_min_contrast_color(unsigned int fg, unsigned int bg,
                                      double min_ratio);
''')])
PYEOF
for f in src/core/theme.c src/core/theme.h; do echo "$f CR lines: $(grep -c $'\r$' $f) of $(wc -l < $f)"; done
```

Expected: two `(CRLF kept)` lines, then `src/core/theme.c CR lines: 179 of 179` and `src/core/theme.h CR lines: 64 of 64`.

- [ ] **Step 4: Run the tests to verify they pass, and cross-compile**

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && make test 2>&1 | grep -E "^\[(PASS|FAIL)\] test_theme_(min_contrast|role_color)|^\[FAIL\]|^Tests Run"`
Expected: four `[PASS]` lines (`test_theme_min_contrast_unchanged_when_clear_or_off`, `test_theme_min_contrast_walks_darker_on_light_bg`, `test_theme_min_contrast_falls_back_to_better_extreme`, `test_theme_role_color_outputs_unchanged`), no `[FAIL]`, and `Tests Run: 2986, Failed: 0`.

Run:
```bash
cd /home/thomas/nutshell-wt/ansi-contrast && \
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Os \
  -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -Isrc/ui -c src/core/theme.c -o /dev/null && echo "mingw: theme.c clean"
```
Expected: `mingw: theme.c clean`.

- [ ] **Step 5: Commit**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && git status --short && \
git add src/core/theme.c src/core/theme.h tests/test_theme.c tests/runner.c && \
git commit -F - <<'EOF'
feat(theme): theme_min_contrast_color, the AA walk at any ratio

theme_role_color() walked a colour's lightness away from its background
in 2-L* steps until it read at 4.5:1. That walk is now shared with
theme_min_contrast_color(), which takes the ratio as a parameter, leaves
the colour alone when the ratio is off or already met, and falls back to
the better of the walk's two extremes when no shade clears it. The
terminal's per-theme colour tables are built with it.

theme_role_color() keeps its signature and outputs: each theme's link and
info colours are pinned to the values measured before the change.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM
EOF
```

---

### Task 2: The parser keeps the palette index and records dim

**Files:**
- Modify: `src/term/term.h:8-11` (the flags) and `:36-37` (the index comments)
- Modify: `src/term/parser.c:186-236` (CRLF; `handle_sgr()`)
- Test: `tests/test_term.c` (appended)
- Modify: `tests/runner.c` (CRLF; after `test_term_resize_degenerate_round_trip_keeps_scroll_anchor`)

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces (used by Tasks 3-8): `#define TERM_ATTR_DIM (1 << 4)` in `src/term/term.h`, set by SGR 2, cleared by SGR 22 (with bold) and SGR 0. For SGR 30-37/90-97, `TermAttr.fg_index` is the palette index 0-15 (`fg_mode == COLOR_ANSI16`); for 40-47/100-107, `bg_index` likewise. `fg`/`bg` keep the raw RGB they hold today. `fg_index`/`bg_index` are meaningful whenever the mode is `COLOR_ANSI16` or `COLOR_256`.

- [ ] **Step 1: Write the failing tests**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && python3 - <<'PYEOF'
path = 'tests/test_term.c'
raw = open(path, 'rb').read()
assert b'\r\n' not in raw and raw.endswith(b'\n')
text = r'''
/* ---- SGR palette index and dim (spec 2026-10-19-light-theme-ansi-contrast-
 * design.md, section 5.2) ------------------------------------------------ */

static TermAttr attr_after(const char *seq)
{
    TermAttr a;
    Terminal *t = term_init(4, 20, 10);
    memset(&a, 0, sizeof(a));
    if (!t) return a;
    term_process(t, seq, strlen(seq));
    a = t->current_attr;
    term_free(t);
    return a;
}

/* 16-colour SGRs keep their palette index beside the RGB they always
 * stored, so the renderer can look the index up in the theme's table. */
int test_term_sgr16_keeps_palette_index(void)
{
    TEST_BEGIN();
    static const struct { const char *seq; int fg; int index; uint32_t rgb; } cases[] = {
        { "\x1b[30m",  1, 0,  0x000000u }, { "\x1b[33m",  1, 3,  0xCCAA33u },
        { "\x1b[37m",  1, 7,  0xC0C0C0u }, { "\x1b[90m",  1, 8,  0x808080u },
        { "\x1b[93m",  1, 11, 0xFFFF55u }, { "\x1b[97m",  1, 15, 0xFFFFFFu },
        { "\x1b[40m",  0, 0,  0x000000u }, { "\x1b[43m",  0, 3,  0xCCAA33u },
        { "\x1b[47m",  0, 7,  0xC0C0C0u }, { "\x1b[100m", 0, 8,  0x808080u },
        { "\x1b[103m", 0, 11, 0xFFFF55u }, { "\x1b[107m", 0, 15, 0xFFFFFFu },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TermAttr a = attr_after(cases[i].seq);
        if (cases[i].fg) {
            ASSERT_EQ(a.fg_mode, COLOR_ANSI16);
            ASSERT_EQ(a.fg_index, cases[i].index);
            ASSERT_EQ(a.fg, cases[i].rgb);
        } else {
            ASSERT_EQ(a.bg_mode, COLOR_ANSI16);
            ASSERT_EQ(a.bg_index, cases[i].index);
            ASSERT_EQ(a.bg, cases[i].rgb);
        }
    }
    /* A later truecolour SGR switches the mode and leaves the index stale;
     * the mode is what says not to read it. 39/49 reset both. */
    TermAttr a = attr_after("\x1b[93;38;2;1;2;3m");
    ASSERT_EQ(a.fg_mode, COLOR_RGB);
    ASSERT_EQ(a.fg, 0x010203u);
    a = attr_after("\x1b[93;103m\x1b[39;49m");
    ASSERT_EQ(a.fg_mode, COLOR_DEFAULT);
    ASSERT_EQ(a.fg_index, 0);
    ASSERT_EQ(a.bg_mode, COLOR_DEFAULT);
    ASSERT_EQ(a.bg_index, 0);
    TEST_END();
}

/* SGR 2 sets dim; 22 ("normal intensity") clears dim and bold; 0 clears
 * everything. Italic (3) is not recorded. */
int test_term_sgr_dim_set_and_cleared(void)
{
    TEST_BEGIN();
    ASSERT_TRUE((attr_after("\x1b[2m").flags & TERM_ATTR_DIM) != 0);
    ASSERT_TRUE((attr_after("\x1b[97;2;3m").flags & TERM_ATTR_DIM) != 0);
    ASSERT_EQ(attr_after("\x1b[2m\x1b[22m").flags & TERM_ATTR_DIM, 0);
    ASSERT_EQ(attr_after("\x1b[1;2m\x1b[22m").flags & (TERM_ATTR_DIM | TERM_ATTR_BOLD), 0);
    ASSERT_EQ(attr_after("\x1b[2m\x1b[0m").flags & TERM_ATTR_DIM, 0);
    ASSERT_EQ(attr_after("\x1b[2m\x1b[m").flags & TERM_ATTR_DIM, 0);
    ASSERT_EQ(attr_after("\x1b[3m").flags, 0);
    /* Bold alone still sets and clears as before. */
    ASSERT_EQ(attr_after("\x1b[1m").flags, TERM_ATTR_BOLD);
    ASSERT_EQ(attr_after("\x1b[1m\x1b[22m").flags, 0);
    TEST_END();
}

/* Review focus 4: ConPTY and SSH deliver bytes in whatever pieces a read
 * returns, so an SGR split across two reads must still store its index
 * and dim. */
int test_term_sgr_split_across_reads_keeps_index_and_dim(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(4, 20, 10);
    ASSERT_NOT_NULL(t);
    if (!t) { TEST_END(); }
    term_process(t, "\x1b[9", 3);
    term_process(t, "3;2mX", 5);
    TermCell c = t->lines[term_screen_to_phys(t, 0)]->cells[0];
    ASSERT_EQ(c.codepoint, (uint32_t)'X');
    ASSERT_EQ(c.attr.fg_mode, COLOR_ANSI16);
    ASSERT_EQ(c.attr.fg_index, 11);
    ASSERT_TRUE((c.attr.flags & TERM_ATTR_DIM) != 0);
    term_process(t, "\x1b[4", 3);
    term_process(t, "3mY", 3);
    c = t->lines[term_screen_to_phys(t, 0)]->cells[1];
    ASSERT_EQ(c.attr.bg_mode, COLOR_ANSI16);
    ASSERT_EQ(c.attr.bg_index, 3);
    term_free(t);
    TEST_END();
}
'''
open(path, 'wb').write(raw + text.encode())
print('%s: 3 tests appended' % path)
PYEOF
python3 - test_term_resize_degenerate_round_trip_keeps_scroll_anchor "" \
  test_term_sgr16_keeps_palette_index test_term_sgr_dim_set_and_cleared \
  test_term_sgr_split_across_reads_keeps_index_and_dim <<'PYEOF'
# Run as: python3 - <anchor test> <comment, or ""> <test name>... <<'PYEOF'
# Registers test functions in tests/runner.c (CRLF): declarations after the
# anchor's declaration, calls after the anchor's call, in the order given.
import sys
anchor, comment, names = sys.argv[1], sys.argv[2], sys.argv[3:]
p = 'tests/runner.c'
b = open(p, 'rb').read()
da = ('int %s(void);\r\n' % anchor).encode()
ca = ('    failed += %s();\r\n' % anchor).encode()
assert b.count(da) == 1 and b.count(ca) == 1, 'anchor not found exactly once'
decls = (('/* %s */\r\n' % comment) if comment else '') + ''.join('int %s(void);\r\n' % n for n in names)
calls = ''.join('    failed += %s();\r\n' % n for n in names)
b = b.replace(da, da + decls.encode()).replace(ca, ca + calls.encode())
open(p, 'wb').write(b)
print('runner.c: registered %d tests after %s' % (len(names), anchor))
PYEOF
echo "tests/runner.c CR lines: $(grep -c $'\r$' tests/runner.c) of $(wc -l < tests/runner.c)"
```

Expected: `tests/test_term.c: 3 tests appended`, `runner.c: registered 3 tests after test_term_resize_degenerate_round_trip_keeps_scroll_anchor`, `tests/runner.c CR lines: 6614 of 6614`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && make test 2>&1 | grep -m2 -E "TERM_ATTR_DIM"`
Expected: `error: 'TERM_ATTR_DIM' undeclared` in `tests/test_term.c` (the index assertions cannot run until the flag exists).

- [ ] **Step 3: Write the implementation**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && python3 - <<'PYEOF'
def apply(path, edits):
    raw = open(path, 'rb').read()
    crlf = b'\r\n' in raw
    text = raw.decode('utf-8')
    for old, new in edits:
        if crlf:
            old, new = old.replace('\n', '\r\n'), new.replace('\n', '\r\n')
        assert text.count(old) == 1, (path, text.count(old), old[:60])
        text = text.replace(old, new)
    open(path, 'wb').write(text.encode('utf-8'))
    print('%s: %d edit(s)%s' % (path, len(edits), ' (CRLF kept)' if crlf else ''))

apply('src/term/term.h', [
(r'''#define TERM_ATTR_BLINK     (1 << 3)
''', r'''#define TERM_ATTR_BLINK     (1 << 3)
#define TERM_ATTR_DIM       (1 << 4) /* SGR 2; cleared by 22 and 0. Painted only on a
                                      * theme with a contrast floor (term_colors.h). */
'''),
(r'''    uint8_t  fg_index; /* raw palette index (0-255) when fg_mode == COLOR_256 */
    uint8_t  bg_index; /* raw palette index (0-255) when bg_mode == COLOR_256 */
''', r'''    uint8_t  fg_index; /* palette index (0-255) when fg_mode is COLOR_ANSI16 or COLOR_256 */
    uint8_t  bg_index; /* palette index (0-255) when bg_mode is COLOR_ANSI16 or COLOR_256 */
'''),
])

# The casts matter: the MinGW build compiles with -Wconversion -Werror and
# the native test build does not.
apply('src/term/parser.c', [
(r'''            case 1: term->current_attr.flags |= TERM_ATTR_BOLD;      break;
''', r'''            case 1: term->current_attr.flags |= TERM_ATTR_BOLD;      break;
            case 2: term->current_attr.flags |= TERM_ATTR_DIM;       break;
'''),
(r'''            case 22: term->current_attr.flags &= (uint8_t)~TERM_ATTR_BOLD;      break;
''', r'''            /* 22 is "normal intensity" (ECMA-48): neither bold nor dim. */
            case 22: term->current_attr.flags &= (uint8_t)~(TERM_ATTR_BOLD | TERM_ATTR_DIM); break;
'''),
(r'''                term->current_attr.fg_mode  = COLOR_ANSI16;
                term->current_attr.fg_index = 0;
                term->current_attr.fg       = PALETTE[p - 30];
''', r'''                term->current_attr.fg_mode  = COLOR_ANSI16;
                term->current_attr.fg_index = (uint8_t)(p - 30);
                term->current_attr.fg       = PALETTE[p - 30];
'''),
(r'''                term->current_attr.bg_mode  = COLOR_ANSI16;
                term->current_attr.bg_index = 0;
                term->current_attr.bg       = PALETTE[p - 40];
''', r'''                term->current_attr.bg_mode  = COLOR_ANSI16;
                term->current_attr.bg_index = (uint8_t)(p - 40);
                term->current_attr.bg       = PALETTE[p - 40];
'''),
(r'''                term->current_attr.fg_mode  = COLOR_ANSI16;
                term->current_attr.fg_index = 0;
                term->current_attr.fg       = PALETTE[p - 90 + 8];
''', r'''                term->current_attr.fg_mode  = COLOR_ANSI16;
                term->current_attr.fg_index = (uint8_t)(p - 90 + 8);
                term->current_attr.fg       = PALETTE[p - 90 + 8];
'''),
(r'''                term->current_attr.bg_mode  = COLOR_ANSI16;
                term->current_attr.bg_index = 0;
                term->current_attr.bg       = PALETTE[p - 100 + 8];
''', r'''                term->current_attr.bg_mode  = COLOR_ANSI16;
                term->current_attr.bg_index = (uint8_t)(p - 100 + 8);
                term->current_attr.bg       = PALETTE[p - 100 + 8];
'''),
])
PYEOF
echo "src/term/parser.c CR lines: $(grep -c $'\r$' src/term/parser.c) of $(wc -l < src/term/parser.c)"
```

Expected: `src/term/term.h: 2 edit(s)`, `src/term/parser.c: 6 edit(s) (CRLF kept)`, `src/term/parser.c CR lines: 793 of 793`.

- [ ] **Step 4: Run the tests to verify they pass, and cross-compile**

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && make test 2>&1 | grep -E "^\[(PASS|FAIL)\] test_term_sgr(16|_dim|_split)|^\[FAIL\]|^Tests Run"`
Expected: `[PASS] test_term_sgr16_keeps_palette_index`, `[PASS] test_term_sgr_dim_set_and_cleared`, `[PASS] test_term_sgr_split_across_reads_keeps_index_and_dim`, no `[FAIL]` (every existing `attr.fg == 0xCC3333`-style assertion in `tests/test_term.c` and `tests/test_color.c` still holds), and `Tests Run: 2989, Failed: 0`.

Run:
```bash
cd /home/thomas/nutshell-wt/ansi-contrast && \
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Os \
  -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -Isrc/ui -c src/term/parser.c -o /dev/null && echo "mingw: parser.c clean"
```
Expected: `mingw: parser.c clean`.

- [ ] **Step 5: Commit**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && git status --short && \
git add src/term/term.h src/term/parser.c tests/test_term.c tests/runner.c && \
git commit -F - <<'EOF'
feat(term): keep the 16-colour palette index; record SGR 2 as dim

SGR 30-37, 90-97, 40-47 and 100-107 now store their palette index in
fg_index/bg_index (written as 0 until now) beside the RGB they already
stored, so a cell can be painted from the current theme's table and
re-coloured when the theme changes. SGR 2 sets a new TERM_ATTR_DIM flag;
22 clears it together with bold ("normal intensity"), 0 clears it with
everything else. Nothing paints dim yet.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM
EOF
```

---

### Task 3: One pure resolver, `src/core/term_colors.c`

**Files:**
- Create: `src/core/term_colors.h`, `src/core/term_colors.c` (picked up by the Makefile's `src/core/*.c` wildcard for both `make test` and `make release`; nothing in `WIN_TEST_SRCS` needs it)
- Create: `tests/test_term_colors.c` (picked up by the `tests/*.c` wildcard)
- Modify: `tests/runner.c` (CRLF; after `test_term_sgr_split_across_reads_keeps_index_and_dim`)

**Interfaces:**
- Consumes: `theme_min_contrast_color()` and `theme_blend()` (`src/core/theme.h`, Task 1); `TERM_ATTR_DIM`, `fg_index`/`bg_index` for `COLOR_ANSI16` (Task 2); `color256_to_rgb()` (`src/term/term.h`).
- Produces (used by Tasks 4, 7, 8), in `src/core/term_colors.h`:
  - `#define TERM_DIM_TOWARD_BG 0.40`
  - `typedef struct { uint32_t default_fg, default_bg; double min_contrast; uint32_t fg_on_default[256]; } TermPalette;`
  - `void term_palette_build(TermPalette *p, uint32_t default_fg, uint32_t default_bg, double min_contrast);` (a floor `<= 0` or NaN is stored as 0: raw copy, dim ignored)
  - `#define TERM_DRAW_CURSOR 1u`, `#define TERM_DRAW_SELECTED 2u`
  - `void term_colors_resolve(const TermPalette *p, const TermCell *cell, unsigned draw_flags, uint32_t *out_fg, uint32_t *out_bg);` (0xRRGGBB out; any NULL argument is a no-op)

The rules, in order (spec 5.4): background (default -> `default_bg`, indexed -> `color256_to_rgb(bg_index)`, truecolour -> `attr.bg`); foreground on the default background (default -> `default_fg`, indexed -> `fg_on_default[fg_index]`, truecolour -> `attr.fg`); foreground on an explicit background (all raw); one swap for the cursor or SGR 7; dim (only with a non-zero floor, a non-blank codepoint, and not the cursor cell) `fg = theme_blend(fg, bg, 0.40)`; one more swap for the selection.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_term_colors.c`:

```c
/* tests/test_term_colors.c -- the terminal's colour resolution
 * (src/core/term_colors.c; spec
 * docs/superpowers/specs/2026-10-19-light-theme-ansi-contrast-design.md,
 * sections 4, 5.4 and 6). Cells come from real sequences fed through
 * term_process(), so the parser's index and dim storage is tested together
 * with the resolver. Native, `make test`. */

#include "test_framework.h"
#include "term.h"
#include "term_colors.h"
#include "theme.h"
#include <math.h>
#include <string.h>

/* The two themes' terminal colours, copied from src/core/ui_theme.c, so
 * these cases build the tables directly with term_palette_build(). */
#define OL_FG 0x1D1D1Fu   /* Onyx Light   */
#define OL_BG 0xF5F5F7u
#define OS_FG 0xE0E0E0u   /* Onyx Synapse */
#define OS_BG 0x121212u

/* Feed `bytes` to a fresh 4x20 terminal and return the cell at row 0,
 * column `col`. */
static TermCell cell_of(const char *bytes, int col)
{
    TermCell c;
    memset(&c, 0, sizeof(c));
    Terminal *t = term_init(4, 20, 10);
    if (!t) return c;
    term_process(t, bytes, strlen(bytes));
    c = t->lines[term_screen_to_phys(t, 0)]->cells[col];
    term_free(t);
    return c;
}

/* The pair `bytes`'s first cell paints with on palette `p`. */
static void paint(const TermPalette *p, const char *bytes, unsigned flags,
                  uint32_t *fg, uint32_t *bg)
{
    TermCell c = cell_of(bytes, 0);
    *fg = 0xDEADBEu;
    *bg = 0xDEADBEu;
    term_colors_resolve(p, &c, flags, fg, bg);
}

int test_term_palette_build_floors_or_copies(void)
{
    TEST_BEGIN();
    static TermPalette ol, os, off;
    term_palette_build(&ol, OL_FG, OL_BG, 4.5);
    ASSERT_EQ(ol.default_fg, OL_FG);
    ASSERT_EQ(ol.default_bg, OL_BG);
    ASSERT_TRUE(ol.min_contrast == 4.5);
    ASSERT_EQ(ol.fg_on_default[0],   0x000000u);  /* 19.29: already clear */
    ASSERT_EQ(ol.fg_on_default[1],   0xCC3333u);  /*  4.72: already clear */
    ASSERT_EQ(ol.fg_on_default[3],   0x846E1Bu);  /* yellow                */
    ASSERT_EQ(ol.fg_on_default[8],   0x6C6C6Cu);  /* bright black          */
    ASSERT_EQ(ol.fg_on_default[11],  0x727223u);  /* bright yellow         */
    ASSERT_EQ(ol.fg_on_default[15],  0x6C6C6Cu);  /* bright white          */
    ASSERT_EQ(ol.fg_on_default[255], 0x6E6E6Eu);  /* EEEEEE, 1.07 today    */
    /* A guard, not evidence: every entry clears the floor by construction. */
    for (int i = 0; i < 256; i++)
        ASSERT_TRUE(theme_contrast(ol.fg_on_default[i], OL_BG) >= 4.5);
    /* Review focus 3: an entry that already reads at 4.5 is copied as is --
     * the dark colours a light-aware application picks (38;5;24 is 005F87)
     * paint exactly as asked. 54 of the 256 on Onyx Light. */
    int kept = 0;
    for (int i = 0; i < 256; i++) {
        uint32_t raw = color256_to_rgb((uint8_t)i);
        if (theme_contrast(raw, OL_BG) >= 4.5) {
            kept++;
            ASSERT_EQ(ol.fg_on_default[i], raw);
        }
    }
    ASSERT_EQ(kept, 54);
    ASSERT_EQ(ol.fg_on_default[24], 0x005F87u);

    term_palette_build(&os, OS_FG, OS_BG, 0.0);
    ASSERT_TRUE(os.min_contrast == 0.0);
    for (int i = 0; i < 256; i++)
        ASSERT_EQ(os.fg_on_default[i], color256_to_rgb((uint8_t)i));

    /* A negative or NaN floor is stored as 0: a raw copy, dim ignored. */
    term_palette_build(&off, OL_FG, OL_BG, -1.0);
    ASSERT_TRUE(off.min_contrast == 0.0);
    ASSERT_EQ(off.fg_on_default[11], 0xFFFF55u);
    term_palette_build(&off, OL_FG, OL_BG, NAN);
    ASSERT_TRUE(off.min_contrast == 0.0);
    ASSERT_EQ(off.fg_on_default[11], 0xFFFF55u);
    term_palette_build(NULL, OL_FG, OL_BG, 4.5);   /* must not crash */
    TEST_END();
}

int test_term_colors_default_bg_uses_floored_table(void)
{
    TEST_BEGIN();
    static TermPalette ol, os;
    term_palette_build(&ol, OL_FG, OL_BG, 4.5);
    term_palette_build(&os, OS_FG, OS_BG, 0.0);
    uint32_t fg, bg, fg2, bg2;

    paint(&ol, "\x1b[93mX", 0u, &fg, &bg);
    ASSERT_EQ(fg, 0x727223u);
    ASSERT_EQ(bg, OL_BG);
    paint(&ol, "\x1b[33mX", 0u, &fg, &bg);
    ASSERT_EQ(fg, 0x846E1Bu);
    paint(&ol, "\x1b[38;5;11mX", 0u, &fg2, &bg2);    /* 256-colour 11 == SGR 93 */
    paint(&ol, "\x1b[93mX", 0u, &fg, &bg);
    ASSERT_EQ(fg2, fg);
    ASSERT_EQ(bg2, bg);
    paint(&ol, "\x1b[38;5;226mX", 0u, &fg, &bg);     /* cube yellow FFFF00 */
    ASSERT_TRUE(theme_contrast(fg, OL_BG) >= 4.5);
    paint(&ol, "X", 0u, &fg, &bg);                   /* default fg untouched */
    ASSERT_EQ(fg, OL_FG);
    ASSERT_EQ(bg, OL_BG);
    /* Review focus 2: bold changes the font only, never the colour --
     * Write-Warning's 33;1 is the same floored yellow as 33. */
    paint(&ol, "\x1b[33;1mX", 0u, &fg, &bg);
    ASSERT_EQ(fg, 0x846E1Bu);
    paint(&ol, "\x1b[1;93mX", 0u, &fg, &bg);
    ASSERT_EQ(fg, 0x727223u);

    paint(&os, "\x1b[93mX", 0u, &fg, &bg);           /* floor 0: raw */
    ASSERT_EQ(fg, 0xFFFF55u);
    ASSERT_EQ(bg, OS_BG);
    TEST_END();
}

int test_term_colors_truecolour_is_raw(void)
{
    TEST_BEGIN();
    static TermPalette ol;
    term_palette_build(&ol, OL_FG, OL_BG, 4.5);
    uint32_t fg, bg;
    paint(&ol, "\x1b[38;2;255;255;0mX", 0u, &fg, &bg);
    ASSERT_EQ(fg, 0xFFFF00u);
    ASSERT_EQ(bg, OL_BG);
    /* 93 stores index 11, then the truecolour SGR switches the mode: the
     * stale index must not be read. */
    paint(&ol, "\x1b[93;38;2;1;2;3mX", 0u, &fg, &bg);
    ASSERT_EQ(fg, 0x010203u);
    paint(&ol, "\x1b[48;2;10;20;30mX", 0u, &fg, &bg);
    ASSERT_EQ(fg, OL_FG);
    ASSERT_EQ(bg, 0x0A141Eu);
    TEST_END();
}

/* An explicit background keeps the raw palette on both sides, on a light
 * and on a dark theme alike -- unreadable pairs stay as they are. */
int test_term_colors_explicit_bg_keeps_raw_pairs(void)
{
    TEST_BEGIN();
    static TermPalette pals[2];
    term_palette_build(&pals[0], OL_FG, OL_BG, 4.5);
    term_palette_build(&pals[1], OS_FG, OS_BG, 0.0);
    static const struct { const char *seq; uint32_t fg, bg; } cases[] = {
        { "\x1b[93;46mX",  0xFFFF55u, 0x44BBBBu },   /* 2.17 */
        { "\x1b[33;47mX",  0xCCAA33u, 0xC0C0C0u },   /* 1.23 */
        { "\x1b[30;47mX",  0x000000u, 0xC0C0C0u },   /* 11.54 */
        { "\x1b[97;41mX",  0xFFFFFFu, 0xCC3333u },   /* 5.14 */
        { "\x1b[93;107mX", 0xFFFF55u, 0xFFFFFFu },   /* 1.07 */
        { "\x1b[37;47mX",  0xC0C0C0u, 0xC0C0C0u },   /* hidden: stays hidden */
        { "\x1b[38;5;226;48;5;21mX", 0xFFFF00u, 0x0000FFu },
    };
    for (int t = 0; t < 2; t++) {
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            uint32_t fg, bg;
            paint(&pals[t], cases[i].seq, 0u, &fg, &bg);
            ASSERT_EQ(fg, cases[i].fg);
            ASSERT_EQ(bg, cases[i].bg);
        }
        uint32_t fg, bg;
        paint(&pals[t], "\x1b[44mX", 0u, &fg, &bg);   /* default fg, explicit bg */
        ASSERT_EQ(fg, pals[t].default_fg);
        ASSERT_EQ(bg, 0x5C7FD6u);
    }
    TEST_END();
}

/* Indexed text that is near-invisible on the theme's own background is
 * floored, so it shows; on an explicit background fg == bg is untouched. */
int test_term_colors_near_invisible_and_hidden(void)
{
    TEST_BEGIN();
    static TermPalette ol;
    term_palette_build(&ol, OL_FG, OL_BG, 4.5);
    uint32_t fg, bg;
    paint(&ol, "\x1b[38;5;255mX", 0u, &fg, &bg);      /* EEEEEE, 1.07 today */
    ASSERT_EQ(fg, 0x6E6E6Eu);
    ASSERT_TRUE(theme_contrast(fg, bg) >= 4.5);
    paint(&ol, "\x1b[37;47mX", 0u, &fg, &bg);
    ASSERT_EQ(fg, bg);
    TEST_END();
}

int test_term_colors_dim(void)
{
    TEST_BEGIN();
    static TermPalette ol, os;
    term_palette_build(&ol, OL_FG, OL_BG, 4.5);
    term_palette_build(&os, OS_FG, OS_BG, 0.0);
    uint32_t fg, bg, plain97, plain90;

    paint(&ol, "\x1b[97mX", 0u, &plain97, &bg);
    paint(&ol, "\x1b[90mX", 0u, &plain90, &bg);
    paint(&ol, "\x1b[2;97mX", 0u, &fg, &bg);
    ASSERT_EQ(fg, 0xA3A3A4u);
    ASSERT_EQ(fg, theme_blend(0x6C6C6Cu, OL_BG, TERM_DIM_TOWARD_BG));
    ASSERT_TRUE(fg != plain97);
    ASSERT_TRUE(fg != plain90);
    ASSERT_TRUE(theme_lstar(fg) >= theme_lstar(plain97) + 10.0);

    paint(&os, "\x1b[2;97mX", 0u, &fg, &bg);          /* floor 0: dim ignored */
    ASSERT_EQ(fg, 0xFFFFFFu);

    /* Review focus 1: dim default text (git, npm, pytest) on a light theme
     * is fainter but readable; on a dark theme it is unchanged. */
    paint(&ol, "\x1b[2mX", 0u, &fg, &bg);
    ASSERT_EQ(fg, 0x737375u);
    ASSERT_TRUE(theme_contrast(fg, bg) > 4.3);
    paint(&os, "\x1b[2mX", 0u, &fg, &bg);
    ASSERT_EQ(fg, OS_FG);

    /* 22 clears dim (and bold); 0 clears it. */
    TermCell c = cell_of("\x1b[2m\x1b[22mX", 0);
    ASSERT_EQ(c.attr.flags & TERM_ATTR_DIM, 0);
    c = cell_of("\x1b[1;2m\x1b[22mX", 0);
    ASSERT_EQ(c.attr.flags & (TERM_ATTR_DIM | TERM_ATTR_BOLD), 0);
    c = cell_of("\x1b[2m\x1b[0mX", 0);
    ASSERT_EQ(c.attr.flags & TERM_ATTR_DIM, 0);
    paint(&ol, "\x1b[2;97m\x1b[22mX", 0u, &fg, &bg);
    ASSERT_EQ(fg, plain97);

    /* Dim on an explicit background blends toward that background. */
    paint(&ol, "\x1b[2;97;41mX", 0u, &fg, &bg);
    ASSERT_EQ(bg, 0xCC3333u);
    ASSERT_EQ(fg, theme_blend(0xFFFFFFu, 0xCC3333u, TERM_DIM_TOWARD_BG));
    TEST_END();
}

/* The cursor sits on the prediction's first cell: it paints the undimmed
 * pair swapped, not a faint block. */
int test_term_colors_dim_cursor_cell_is_undimmed(void)
{
    TEST_BEGIN();
    static TermPalette ol;
    term_palette_build(&ol, OL_FG, OL_BG, 4.5);
    uint32_t fg, bg;
    paint(&ol, "\x1b[2;97mX", TERM_DRAW_CURSOR, &fg, &bg);
    ASSERT_EQ(fg, OL_BG);
    ASSERT_EQ(bg, 0x6C6C6Cu);
    TEST_END();
}

/* Erase copies the current attributes, DIM included, into blank cells;
 * blanks are never dimmed, so they paint exactly as undimmed ones. */
int test_term_colors_dim_blanks_are_undimmed(void)
{
    TEST_BEGIN();
    static TermPalette ol;
    term_palette_build(&ol, OL_FG, OL_BG, 4.5);
    uint32_t fg, bg, ufg, ubg;
    TermCell c = cell_of("\x1b[2;97m\x1b[K", 0);
    ASSERT_EQ(c.codepoint, 0u);
    ASSERT_TRUE((c.attr.flags & TERM_ATTR_DIM) != 0);
    paint(&ol, "\x1b[2;97m\x1b[K", 0u, &fg, &bg);
    paint(&ol, "\x1b[97m\x1b[K", 0u, &ufg, &ubg);
    ASSERT_EQ(fg, ufg);
    ASSERT_EQ(bg, ubg);
    paint(&ol, "\x1b[2;97m ", 0u, &fg, &bg);         /* a dim space */
    ASSERT_EQ(fg, 0x6C6C6Cu);
    ASSERT_EQ(bg, OL_BG);
    TEST_END();
}

int test_term_colors_swaps(void)
{
    TEST_BEGIN();
    static TermPalette ol;
    term_palette_build(&ol, OL_FG, OL_BG, 4.5);
    uint32_t fg, bg;
    paint(&ol, "\x1b[7;93mX", 0u, &fg, &bg);          /* the floored pair, swapped */
    ASSERT_EQ(fg, OL_BG);
    ASSERT_EQ(bg, 0x727223u);
    paint(&ol, "\x1b[93mX", TERM_DRAW_CURSOR, &fg, &bg);
    ASSERT_EQ(fg, OL_BG);
    ASSERT_EQ(bg, 0x727223u);
    paint(&ol, "\x1b[7;93mX", TERM_DRAW_CURSOR, &fg, &bg);   /* once, not twice */
    ASSERT_EQ(fg, OL_BG);
    ASSERT_EQ(bg, 0x727223u);
    paint(&ol, "\x1b[93mX", TERM_DRAW_SELECTED, &fg, &bg);
    ASSERT_EQ(fg, OL_BG);
    ASSERT_EQ(bg, 0x727223u);
    paint(&ol, "\x1b[93mX", TERM_DRAW_CURSOR | TERM_DRAW_SELECTED, &fg, &bg);
    ASSERT_EQ(fg, 0x727223u);
    ASSERT_EQ(bg, OL_BG);
    paint(&ol, "\x1b[2;97mX", TERM_DRAW_SELECTED, &fg, &bg);  /* dimmed, then swapped */
    ASSERT_EQ(fg, OL_BG);
    ASSERT_EQ(bg, 0xA3A3A4u);
    /* Review focus 5: dim with reverse (status lines): swapped first, then
     * the drawn foreground blends toward the drawn background. */
    paint(&ol, "\x1b[2;7mX", 0u, &fg, &bg);
    ASSERT_EQ(fg, theme_blend(OL_BG, OL_FG, TERM_DIM_TOWARD_BG));
    ASSERT_EQ(fg, 0x9F9FA1u);
    ASSERT_EQ(bg, OL_FG);
    TEST_END();
}

int test_term_colors_null_args_are_noops(void)
{
    TEST_BEGIN();
    static TermPalette ol;
    term_palette_build(&ol, OL_FG, OL_BG, 4.5);
    TermCell c = cell_of("\x1b[93mX", 0);
    uint32_t fg = 1u, bg = 2u;
    term_colors_resolve(NULL, &c, 0u, &fg, &bg);
    term_colors_resolve(&ol, NULL, 0u, &fg, &bg);
    term_colors_resolve(&ol, &c, 0u, NULL, &bg);
    term_colors_resolve(&ol, &c, 0u, &fg, NULL);
    ASSERT_EQ(fg, 1u);
    ASSERT_EQ(bg, 2u);
    TEST_END();
}

```

Register the ten tests:

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && python3 - test_term_sgr_split_across_reads_keeps_index_and_dim test_term_colors.c \
  test_term_palette_build_floors_or_copies test_term_colors_default_bg_uses_floored_table \
  test_term_colors_truecolour_is_raw test_term_colors_explicit_bg_keeps_raw_pairs \
  test_term_colors_near_invisible_and_hidden test_term_colors_dim \
  test_term_colors_dim_cursor_cell_is_undimmed test_term_colors_dim_blanks_are_undimmed \
  test_term_colors_swaps test_term_colors_null_args_are_noops <<'PYEOF'
# Run as: python3 - <anchor test> <comment, or ""> <test name>... <<'PYEOF'
# Registers test functions in tests/runner.c (CRLF): declarations after the
# anchor's declaration, calls after the anchor's call, in the order given.
import sys
anchor, comment, names = sys.argv[1], sys.argv[2], sys.argv[3:]
p = 'tests/runner.c'
b = open(p, 'rb').read()
da = ('int %s(void);\r\n' % anchor).encode()
ca = ('    failed += %s();\r\n' % anchor).encode()
assert b.count(da) == 1 and b.count(ca) == 1, 'anchor not found exactly once'
decls = (('/* %s */\r\n' % comment) if comment else '') + ''.join('int %s(void);\r\n' % n for n in names)
calls = ''.join('    failed += %s();\r\n' % n for n in names)
b = b.replace(da, da + decls.encode()).replace(ca, ca + calls.encode())
open(p, 'wb').write(b)
print('runner.c: registered %d tests after %s' % (len(names), anchor))
PYEOF
echo "tests/runner.c CR lines: $(grep -c $'\r$' tests/runner.c) of $(wc -l < tests/runner.c)"
```

Expected: `runner.c: registered 10 tests after test_term_sgr_split_across_reads_keeps_index_and_dim`, `tests/runner.c CR lines: 6635 of 6635`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && make test 2>&1 | grep -m1 "term_colors.h"`
Expected: `tests/test_term_colors.c:10:10: fatal error: term_colors.h: No such file or directory`.

- [ ] **Step 3: Write the header and the implementation**

Create `src/core/term_colors.h`:

```c
#ifndef NUTSHELL_TERM_COLORS_H
#define NUTSHELL_TERM_COLORS_H

/* The colours a terminal cell is painted with (spec
 * docs/superpowers/specs/2026-10-19-light-theme-ansi-contrast-design.md,
 * sections 4 and 5.4). Pure C, no Win32: src/ui/renderer.c only calls
 * term_colors_resolve() and converts the result to COLORREF.
 *
 * Every theme's resolved tokens carry one TermPalette (ThemeTokens.term,
 * built by ui_theme_resolve()). On a theme whose floor is non-zero (the
 * light themes) the 256 indexed foregrounds are floored to that contrast
 * ratio against the theme's terminal background, and only cells drawn on
 * that background use the floored table; cells with an explicit background
 * keep the raw palette on both sides. Truecolour is never floored. */

#include <stdint.h>
#include "term.h"

/* A dim (SGR 2) cell's foreground is blended this far toward its
 * background, after the floor, on a theme whose floor is non-zero. */
#define TERM_DIM_TOWARD_BG 0.40

typedef struct {
    uint32_t default_fg, default_bg;  /* the theme's terminal_fg / terminal_bg */
    double   min_contrast;            /* 0 = off: raw table, dim ignored */
    uint32_t fg_on_default[256];      /* color256_to_rgb(i), floored against default_bg
                                         when min_contrast > 0, else a raw copy */
} TermPalette;

/* Fill *p for one theme. min_contrast <= 0 (or NaN) stores 0: the table is
 * a raw copy of color256_to_rgb() and dim is ignored. NULL p is a no-op. */
void term_palette_build(TermPalette *p, uint32_t default_fg, uint32_t default_bg,
                        double min_contrast);

#define TERM_DRAW_CURSOR   1u
#define TERM_DRAW_SELECTED 2u

/* The colours `cell` is painted with, as 0xRRGGBB, in this order:
 *  1. background: default -> default_bg; indexed -> color256_to_rgb(bg_index);
 *     truecolour -> attr.bg;
 *  2. foreground on the default background: default -> default_fg; indexed ->
 *     fg_on_default[fg_index]; truecolour -> attr.fg (never floored);
 *  3. foreground on an explicit background: default -> default_fg; indexed ->
 *     color256_to_rgb(fg_index); truecolour -> attr.fg (all raw);
 *  4. swap once for TERM_DRAW_CURSOR or TERM_ATTR_REVERSE (either, not both);
 *  5. dim: TERM_ATTR_DIM, min_contrast > 0, a non-blank codepoint (not 0 or
 *     ' ') and not the cursor cell -> fg = theme_blend(fg, bg, TERM_DIM_TOWARD_BG);
 *  6. swap again for TERM_DRAW_SELECTED.
 * Any NULL argument is a no-op. */
void term_colors_resolve(const TermPalette *p, const TermCell *cell, unsigned draw_flags,
                         uint32_t *out_fg, uint32_t *out_bg);

#endif /* NUTSHELL_TERM_COLORS_H */
```

Create `src/core/term_colors.c`:

```c
#include "term_colors.h"
#include "theme.h"

void term_palette_build(TermPalette *p, uint32_t default_fg, uint32_t default_bg,
                        double min_contrast)
{
    if (!p) return;
    p->default_fg = default_fg;
    p->default_bg = default_bg;
    p->min_contrast = (min_contrast > 0.0) ? min_contrast : 0.0;
    for (int i = 0; i < 256; i++) {
        uint32_t raw = color256_to_rgb((uint8_t)i);
        p->fg_on_default[i] = (p->min_contrast > 0.0)
            ? theme_min_contrast_color(raw, default_bg, p->min_contrast)
            : raw;
    }
}

static int is_indexed(uint8_t mode)
{
    return mode == COLOR_ANSI16 || mode == COLOR_256;
}

void term_colors_resolve(const TermPalette *p, const TermCell *cell, unsigned draw_flags,
                         uint32_t *out_fg, uint32_t *out_bg)
{
    if (!p || !cell || !out_fg || !out_bg) return;
    const TermAttr *a = &cell->attr;
    const int on_default_bg = (a->bg_mode == COLOR_DEFAULT);
    uint32_t fg, bg, t;

    if (on_default_bg)              bg = p->default_bg;
    else if (is_indexed(a->bg_mode)) bg = color256_to_rgb(a->bg_index);
    else                            bg = a->bg;

    if (is_indexed(a->fg_mode))
        fg = on_default_bg ? p->fg_on_default[a->fg_index] : color256_to_rgb(a->fg_index);
    else if (a->fg_mode == COLOR_RGB)
        fg = a->fg;
    else
        fg = p->default_fg;

    const int cursor = (draw_flags & TERM_DRAW_CURSOR) != 0u;
    if (cursor || (a->flags & TERM_ATTR_REVERSE)) { t = fg; fg = bg; bg = t; }

    if ((a->flags & TERM_ATTR_DIM) && p->min_contrast > 0.0 && !cursor &&
        cell->codepoint != 0u && cell->codepoint != (uint32_t)' ')
        fg = theme_blend(fg, bg, TERM_DIM_TOWARD_BG);

    if (draw_flags & TERM_DRAW_SELECTED) { t = fg; fg = bg; bg = t; }

    *out_fg = fg;
    *out_bg = bg;
}
```

- [ ] **Step 4: Run the tests to verify they pass, and cross-compile**

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && make test 2>&1 | grep -E "^\[(PASS|FAIL)\] test_term_(palette|colors)|^\[FAIL\]|^Tests Run"`
Expected: ten `[PASS]` lines, no `[FAIL]`, and `Tests Run: 2999, Failed: 0`.

Run:
```bash
cd /home/thomas/nutshell-wt/ansi-contrast && \
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Os \
  -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -Isrc/ui -c src/core/term_colors.c -o /dev/null && echo "mingw: term_colors.c clean"
```
Expected: `mingw: term_colors.c clean`.

- [ ] **Step 5: Commit**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && git status --short && \
git add src/core/term_colors.h src/core/term_colors.c tests/test_term_colors.c tests/runner.c && \
git commit -F - <<'EOF'
feat(term): one pure resolver for the colours a cell is painted with

term_colors_resolve() decides a cell's painted pair from a TermPalette:
indexed text on the theme's own background comes from a 256-entry table
floored to the theme's contrast ratio; an explicit background keeps the
raw palette on both sides; truecolour is never floored; the cursor or SGR
7 swaps once, dim blends the foreground 40% toward the background (only
with a non-zero floor, never on a blank or the cursor cell), and the
selection swaps again. term_palette_build() fills the table once per
theme. Pure C, tested natively; nothing uses it yet.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM
EOF
```

---

### Task 4: Every theme carries its table

**Files:**
- Modify: `src/core/ui_theme.h` (CRLF; `#include`, `ThemeColors`, `ThemeTokens`)
- Modify: `src/core/ui_theme.c` (CRLF; the four theme initialisers and `ui_theme_resolve()`)
- Test: `tests/test_ui_theme.c` (CRLF; appended), `tests/test_term_colors.c` (include and two tests appended)
- Modify: `tests/runner.c` (CRLF; after `test_ui_theme_dark_themes_have_light_text` and after `test_term_colors_null_args_are_noops`)

**Interfaces:**
- Consumes: `TermPalette`, `term_palette_build()`, `term_colors_resolve()` (Task 3).
- Produces (used by Tasks 7 and 8): `double terminal_min_contrast;` in `ThemeColors` (Onyx Synapse 0.0, Onyx Light 4.5, Sage & Sand 0.0, Moss & Mist 4.5); `TermPalette term;` in `ThemeTokens`, filled by every `ui_theme_resolve()` call for every theme. The UI reads it as `ns_tokens()->term`.

`Get-NutshellThemeColor` (the harness) reads only `.field = 0xRRGGBB` initialisers by name, so the new `double` field does not disturb it. A `ThemeTokens` grows by about 1 KB.

- [ ] **Step 1: Write the failing tests**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && python3 - <<'PYEOF'
path = 'tests/test_ui_theme.c'
raw = open(path, 'rb').read()
assert raw.endswith(b'\r\n')
text = r'''
/* The terminal's contrast floor, by theme name (spec 2026-10-19-light-theme-
 * ansi-contrast-design.md, section 5.5 and the maintainer's decision 1):
 * the light themes floor indexed text at 4.5, the dark ones not at all.
 * Each floored theme's own default text already clears its floor, which is
 * what lets the resolver leave the default foreground alone. */
int test_ui_theme_terminal_min_contrast_by_name(void) {
    TEST_BEGIN();
    static const struct { const char *name; double floor; } want[] = {
        { "Onyx Synapse", 0.0 }, { "Onyx Light", 4.5 },
        { "Sage & Sand", 0.0 },  { "Moss & Mist", 4.5 },
    };
    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        const ThemeColors *t = ui_theme_get(ui_theme_find(want[i].name));
        ASSERT_STR_EQ(t->name, want[i].name);
        ASSERT_TRUE(t->terminal_min_contrast == want[i].floor);
        if (want[i].floor > 0.0) {
            ASSERT_TRUE(theme_contrast(t->terminal_fg, t->terminal_bg) >= want[i].floor);
        }
    }
    TEST_END();
}
'''
open(path, 'wb').write(raw + text.replace('\n', '\r\n').encode())
print('%s: 1 test appended (CRLF kept)' % path)

path = 'tests/test_term_colors.c'
text = open(path).read()
old = '#include "theme.h"\n#include <math.h>\n'
assert text.count(old) == 1
text = text.replace(old, '#include "theme.h"\n#include "ui_theme.h"\n#include <math.h>\n')
text += '\n' + r'''
/* ---- The shipped tables (ui_theme_resolve(), section 5.5) ------------- */

/* Every theme's resolved tokens carry a populated table: floored on the
 * light themes, a raw copy on the dark ones. */
int test_term_colors_every_theme_has_its_table(void)
{
    TEST_BEGIN();
    static ThemeTokens tok;
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *base = ui_theme_get(i);
        memset(&tok, 0, sizeof(tok));
        ui_theme_resolve(base, &tok);
        ASSERT_EQ(tok.term.default_fg, base->terminal_fg);
        ASSERT_EQ(tok.term.default_bg, base->terminal_bg);
        ASSERT_TRUE(tok.term.min_contrast == base->terminal_min_contrast);
        ASSERT_TRUE(tok.term.fg_on_default[11] != 0u);
        for (int k = 0; k < 256; k++) {
            uint32_t v = tok.term.fg_on_default[k];
            if (base->terminal_min_contrast > 0.0) {
                ASSERT_TRUE(theme_contrast(v, base->terminal_bg) >= base->terminal_min_contrast);
            } else {
                ASSERT_EQ(v, color256_to_rgb((uint8_t)k));
            }
        }
    }
    static const struct { const char *name; int index; uint32_t want; } named[] = {
        { "Onyx Synapse", 11, 0xFFFF55u }, { "Onyx Light", 11, 0x727223u },
        { "Onyx Light",    3, 0x846E1Bu }, { "Moss & Mist", 3, 0x7E6919u },
        { "Moss & Mist",  11, 0x727223u }, { "Sage & Sand", 11, 0xFFFF55u },
    };
    for (size_t n = 0; n < sizeof(named) / sizeof(named[0]); n++) {
        ui_theme_resolve(ui_theme_get(ui_theme_find(named[n].name)), &tok);
        ASSERT_EQ(tok.term.fg_on_default[named[n].index], named[n].want);
    }
    TEST_END();
}

/* One stored cell, parsed once, painted with each theme's table: what
 * makes scrollback re-colour on a theme switch with no re-parse. */
int test_term_colors_theme_switch_recolours_stored_cell(void)
{
    TEST_BEGIN();
    static ThemeTokens os, ol;
    ui_theme_resolve(ui_theme_get(ui_theme_find("Onyx Synapse")), &os);
    ui_theme_resolve(ui_theme_get(ui_theme_find("Onyx Light")), &ol);
    TermCell c = cell_of("\x1b[93mX", 0);
    uint32_t fg = 0u, bg = 0u;
    term_colors_resolve(&os.term, &c, 0u, &fg, &bg);
    ASSERT_EQ(fg, 0xFFFF55u);
    ASSERT_EQ(bg, 0x121212u);
    term_colors_resolve(&ol.term, &c, 0u, &fg, &bg);
    ASSERT_EQ(fg, 0x727223u);
    ASSERT_EQ(bg, 0xF5F5F7u);
    term_colors_resolve(&os.term, &c, 0u, &fg, &bg);
    ASSERT_EQ(fg, 0xFFFF55u);
    TEST_END();
}

'''
open(path, 'w').write(text)
print('%s: include and 2 tests added' % path)
PYEOF
python3 - test_ui_theme_dark_themes_have_light_text "" test_ui_theme_terminal_min_contrast_by_name <<'PYEOF'
# Run as: python3 - <anchor test> <comment, or ""> <test name>... <<'PYEOF'
# Registers test functions in tests/runner.c (CRLF): declarations after the
# anchor's declaration, calls after the anchor's call, in the order given.
import sys
anchor, comment, names = sys.argv[1], sys.argv[2], sys.argv[3:]
p = 'tests/runner.c'
b = open(p, 'rb').read()
da = ('int %s(void);\r\n' % anchor).encode()
ca = ('    failed += %s();\r\n' % anchor).encode()
assert b.count(da) == 1 and b.count(ca) == 1, 'anchor not found exactly once'
decls = (('/* %s */\r\n' % comment) if comment else '') + ''.join('int %s(void);\r\n' % n for n in names)
calls = ''.join('    failed += %s();\r\n' % n for n in names)
b = b.replace(da, da + decls.encode()).replace(ca, ca + calls.encode())
open(p, 'wb').write(b)
print('runner.c: registered %d tests after %s' % (len(names), anchor))
PYEOF
python3 - test_term_colors_null_args_are_noops "" \
  test_term_colors_every_theme_has_its_table test_term_colors_theme_switch_recolours_stored_cell <<'PYEOF'
# Run as: python3 - <anchor test> <comment, or ""> <test name>... <<'PYEOF'
# Registers test functions in tests/runner.c (CRLF): declarations after the
# anchor's declaration, calls after the anchor's call, in the order given.
import sys
anchor, comment, names = sys.argv[1], sys.argv[2], sys.argv[3:]
p = 'tests/runner.c'
b = open(p, 'rb').read()
da = ('int %s(void);\r\n' % anchor).encode()
ca = ('    failed += %s();\r\n' % anchor).encode()
assert b.count(da) == 1 and b.count(ca) == 1, 'anchor not found exactly once'
decls = (('/* %s */\r\n' % comment) if comment else '') + ''.join('int %s(void);\r\n' % n for n in names)
calls = ''.join('    failed += %s();\r\n' % n for n in names)
b = b.replace(da, da + decls.encode()).replace(ca, ca + calls.encode())
open(p, 'wb').write(b)
print('runner.c: registered %d tests after %s' % (len(names), anchor))
PYEOF
for f in tests/test_ui_theme.c tests/runner.c; do echo "$f CR lines: $(grep -c $'\r$' $f) of $(wc -l < $f)"; done
```

Expected: the two "added" lines, two `runner.c: registered ...` lines, `tests/test_ui_theme.c CR lines: 304 of 304`, `tests/runner.c CR lines: 6641 of 6641`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && make test 2>&1 | grep -E "has no member named '(terminal_min_contrast|term)'" | head -3`
Expected: `error: 'ThemeTokens' has no member named 'term'` and `error: 'ThemeColors' has no member named 'terminal_min_contrast'` (gcc prints the names in curly quotes).

- [ ] **Step 3: Write the implementation**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && python3 - <<'PYEOF'
def apply(path, edits):
    raw = open(path, 'rb').read()
    crlf = b'\r\n' in raw
    text = raw.decode('utf-8')
    for old, new in edits:
        if crlf:
            old, new = old.replace('\n', '\r\n'), new.replace('\n', '\r\n')
        assert text.count(old) == 1, (path, text.count(old), old[:60])
        text = text.replace(old, new)
    open(path, 'wb').write(text.encode('utf-8'))
    print('%s: %d edit(s)%s' % (path, len(edits), ' (CRLF kept)' if crlf else ''))

apply('src/core/ui_theme.h', [
(r'''#define NUM_UI_THEMES 4
''', r'''#include "term_colors.h"

#define NUM_UI_THEMES 4
'''),
(r'''    unsigned int terminal_bg;   /* Terminal default background */
''', r'''    unsigned int terminal_bg;   /* Terminal default background */
    double terminal_min_contrast; /* Floor for indexed terminal text on terminal_bg
                                   * (WCAG ratio; 0 = off: raw palette, SGR 2 ignored) */
'''),
(r'''    unsigned int terminal_fg, terminal_bg;
    ThemeChatColors chat;   /* copied through unchanged for the migration window */
''', r'''    unsigned int terminal_fg, terminal_bg;
    /* The terminal's colour tables for this theme (src/core/term_colors.h):
     * built from terminal_fg/terminal_bg/terminal_min_contrast by every
     * ui_theme_resolve(), for every theme. renderer.c paints every cell
     * through term_colors_resolve(&ns_tokens()->term, ...). */
    TermPalette term;
    ThemeChatColors chat;   /* copied through unchanged for the migration window */
'''),
])

apply('src/core/ui_theme.c', [
(r'''        .terminal_fg  = 0xE0E0E0,
        .terminal_bg  = 0x121212,
''', r'''        .terminal_fg  = 0xE0E0E0,
        .terminal_bg  = 0x121212,
        .terminal_min_contrast = 0.0, /* dark: today's palette, byte for byte */
'''),
(r'''        .terminal_fg  = 0x1D1D1F,
        .terminal_bg  = 0xF5F5F7,
''', r'''        .terminal_fg  = 0x1D1D1F,
        .terminal_bg  = 0xF5F5F7,
        .terminal_min_contrast = 4.5, /* indexed text floored to WCAG AA */
'''),
(r'''        .terminal_fg  = 0xEAE7DC,
        .terminal_bg  = 0x2B2D24,
''', r'''        .terminal_fg  = 0xEAE7DC,
        .terminal_bg  = 0x2B2D24,
        .terminal_min_contrast = 0.0, /* dark: today's palette, byte for byte */
'''),
(r'''        .terminal_fg  = 0x354F52,
        .terminal_bg  = 0xF1F3F0,
''', r'''        .terminal_fg  = 0x354F52,
        .terminal_bg  = 0xF1F3F0,
        .terminal_min_contrast = 4.5, /* indexed text floored to WCAG AA */
'''),
(r'''    out->terminal_fg = base->terminal_fg;
    out->terminal_bg = base->terminal_bg;
''', r'''    out->terminal_fg = base->terminal_fg;
    out->terminal_bg = base->terminal_bg;
    term_palette_build(&out->term, base->terminal_fg, base->terminal_bg,
                       base->terminal_min_contrast);
'''),
])
PYEOF
for f in src/core/ui_theme.h src/core/ui_theme.c; do echo "$f CR lines: $(grep -c $'\r$' $f) of $(wc -l < $f)"; done
```

Expected: `src/core/ui_theme.h: 3 edit(s) (CRLF kept)`, `src/core/ui_theme.c: 5 edit(s) (CRLF kept)`, `src/core/ui_theme.h CR lines: 115 of 115`, `src/core/ui_theme.c CR lines: 241 of 241`.

- [ ] **Step 4: Run the tests to verify they pass, and cross-compile every object**

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && make test 2>&1 | grep -E "^\[(PASS|FAIL)\] test_(ui_theme_terminal_min|term_colors_every|term_colors_theme_switch|ui_tokens)|^\[FAIL\]|^Tests Run"`
Expected: `[PASS] test_ui_theme_terminal_min_contrast_by_name`, `[PASS] test_term_colors_every_theme_has_its_table`, `[PASS] test_term_colors_theme_switch_recolours_stored_cell`, every `test_ui_tokens_*` line `[PASS]`, no `[FAIL]`, and `Tests Run: 3002, Failed: 0`.

`ui_theme.h` is included by most of `src/ui`, which now also sees `term.h`; compile every product object once (to `/dev/null`, no binary, no version bump needed):

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && fails=0; \
for f in src/*.c src/core/*.c src/config/*.c src/crypto/*.c src/term/*.c src/ssh/*.c src/ui/*.c; do \
  [ -f "$f" ] || continue; \
  x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Os \
    -Isrc -Isrc/core -Isrc/config -Isrc/crypto -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include \
    -Isrc/term -Isrc/ssh -Isrc/ui -c "$f" -o /dev/null || { echo "FAILED: $f"; fails=$((fails+1)); }; \
done; echo "mingw: $fails object(s) failed"
```
Expected: `mingw: 0 object(s) failed` and no compiler output.

- [ ] **Step 5: Commit**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && git status --short && \
git add src/core/ui_theme.h src/core/ui_theme.c tests/test_ui_theme.c tests/test_term_colors.c tests/runner.c && \
git commit -F - <<'EOF'
feat(theme): every theme carries its terminal colour table

ThemeColors gains terminal_min_contrast: 4.5 on Onyx Light and Moss &
Mist, 0 on Onyx Synapse and Sage & Sand, so the dark themes keep today's
palette byte for byte. ui_theme_resolve() builds ThemeTokens.term from it
for every theme, so the table is part of the same resolved struct the UI
already reads its colours from, with no unbuilt state. Tests pin the
floor by theme name, a populated table on every theme, and one stored
cell re-colouring from table to table without a re-parse.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM
EOF
```

---

### Task 5: The `ansi` demo state

**Files:**
- Modify: `src/core/ui_demo.c` (`STATE_NAMES`, a new `ANSI_TERM_TEXT`, `ui_demo_build()`, `ui_demo_panel_width_px()`)
- Modify: `src/core/ui_demo.h` (comments)
- Test: `tests/test_ui_demo.c` (includes, `test_ui_demo_states_list_ends_in_all`, one new test)
- Modify: `tests/runner.c` (CRLF; after `test_ui_demo_panel_width_only_for_policy_compact`)

**Interfaces:**
- Consumes: `TERM_ATTR_DIM` and the 16-colour index (Task 2) only in the test.
- Produces (used by Task 6, and by `create_demo_session()` in `src/ui/window.c` with no change there): `--ui-demo=ansi` is a valid state, inserted before `"local"` so every tail anchor of `test_ui_demo_states_list_ends_in_all` holds (17 states). `ui_demo_build("ansi", ...)` writes 17 lines with no trailing newline: line N (0-15) is `NNN ` in the default colour, then `ESC[3Nm`/`ESC[9(N-8)m`, four U+2588, ` Ab`, `ESC[0m`; line 16 is `dim ` and the same in `ESC[2;97m`. Each line is 11 columns; the four blocks are columns 4-7, so the harness samples column 5. `ui_demo_panel_width_px("ansi", dpi)` returns `ns_scale(260, dpi)`, like `"policy-compact"`.

- [ ] **Step 1: Write the failing tests**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && python3 - <<'PYEOF'
def apply(path, edits):
    raw = open(path, 'rb').read()
    crlf = b'\r\n' in raw
    text = raw.decode('utf-8')
    for old, new in edits:
        if crlf:
            old, new = old.replace('\n', '\r\n'), new.replace('\n', '\r\n')
        assert text.count(old) == 1, (path, text.count(old), old[:60])
        text = text.replace(old, new)
    open(path, 'wb').write(text.encode('utf-8'))
    print('%s: %d edit(s)%s' % (path, len(edits), ' (CRLF kept)' if crlf else ''))

apply('tests/test_ui_demo.c', [
(r'''#include "ns_scale.h"
#include <string.h>
''', r'''#include "ns_scale.h"
#include "term.h"
#include <stdio.h>
#include <string.h>
'''),
(r'''    ASSERT_EQ(count, 16);
    ASSERT_STR_EQ(states[count - 1], "all");
''', r'''    ASSERT_EQ(count, 17);
    ASSERT_STR_EQ(states[count - 1], "all");
'''),
(r'''    ASSERT_STR_EQ(states[count - 7], "local");
    TEST_END();
''', r'''    ASSERT_STR_EQ(states[count - 7], "local");
    ASSERT_STR_EQ(states[count - 8], "ansi");
    TEST_END();
'''),
])

path = 'tests/test_ui_demo.c'
text = open(path).read()
text += r'''
/* "ansi" (spec 2026-10-19-light-theme-ansi-contrast-design.md, section 6):
 * an empty conversation, 17 swatch lines in index order (0-15, then dim
 * bright white), none wider than 24 columns, no trailing newline so the
 * cursor ends on line 16 and nothing scrolls, and the narrow dock. */
int test_ui_demo_ansi_swatches(void)
{
    TEST_BEGIN();
    AiConversation conv;
    ApprovalQueue approval, approval2;
    char text[8192];
    text[0] = '\0';
    ASSERT_TRUE(ui_demo_state_valid("ansi"));
    int built = ui_demo_build("ansi", &conv, &approval, &approval2,
                              text, sizeof(text));
    ASSERT_EQ(built, 0);
    if (built != 0) { TEST_END(); }
    ASSERT_EQ(conv.msg_count, 1);
    ASSERT_EQ(approval.count, 0);
    ASSERT_EQ(approval2.count, 0);
    ASSERT_TRUE(strstr(text, "web-01") == NULL);
    size_t len = strlen(text);
    ASSERT_TRUE(len > 2u);
    ASSERT_TRUE(!(text[len - 2] == '\r' && text[len - 1] == '\n'));
    int crlf = 0;
    for (const char *p = strstr(text, "\r\n"); p; p = strstr(p + 2, "\r\n")) crlf++;
    ASSERT_EQ(crlf, 16);

    Terminal *t = term_init(30, 40, 100);
    ASSERT_NOT_NULL(t);
    if (!t) { TEST_END(); }
    term_process(t, text, len);
    ASSERT_EQ(t->cursor.row, 16);
    ASSERT_EQ(t->cursor.col, 11);
    for (int r = 0; r < 17; r++) {
        const TermRow *row = t->lines[term_screen_to_phys(t, r)];
        ASSERT_TRUE(row->len <= 24);
        char label[4] = { (char)row->cells[0].codepoint, (char)row->cells[1].codepoint,
                          (char)row->cells[2].codepoint, '\0' };
        char want[8];
        if (r < 16) (void)snprintf(want, sizeof(want), "%03d", r);
        else        (void)snprintf(want, sizeof(want), "dim");
        ASSERT_STR_EQ(label, want);
        for (int c = 4; c < 8; c++) {
            const TermAttr *a = &row->cells[c].attr;
            ASSERT_EQ(row->cells[c].codepoint, 0x2588u);
            ASSERT_EQ(a->fg_mode, COLOR_ANSI16);
            ASSERT_EQ(a->fg_index, (r < 16) ? r : 15);
            ASSERT_EQ((a->flags & TERM_ATTR_DIM) != 0, r == 16);
            ASSERT_EQ(a->bg_mode, COLOR_DEFAULT);
        }
    }
    ASSERT_EQ(t->lines[term_screen_to_phys(t, 17)]->len, 0);
    term_free(t);

    ASSERT_EQ(ui_demo_panel_width_px("ansi", 96), 260);
    ASSERT_EQ(ui_demo_panel_width_px("ansi", 192), 520);
    ASSERT_TRUE(!ui_demo_scrolls_to_top("ansi"));
    TEST_END();
}
'''
open(path, 'w').write(text)
print('%s: 1 test appended' % path)
PYEOF
python3 - test_ui_demo_panel_width_only_for_policy_compact "" test_ui_demo_ansi_swatches <<'PYEOF'
# Run as: python3 - <anchor test> <comment, or ""> <test name>... <<'PYEOF'
# Registers test functions in tests/runner.c (CRLF): declarations after the
# anchor's declaration, calls after the anchor's call, in the order given.
import sys
anchor, comment, names = sys.argv[1], sys.argv[2], sys.argv[3:]
p = 'tests/runner.c'
b = open(p, 'rb').read()
da = ('int %s(void);\r\n' % anchor).encode()
ca = ('    failed += %s();\r\n' % anchor).encode()
assert b.count(da) == 1 and b.count(ca) == 1, 'anchor not found exactly once'
decls = (('/* %s */\r\n' % comment) if comment else '') + ''.join('int %s(void);\r\n' % n for n in names)
calls = ''.join('    failed += %s();\r\n' % n for n in names)
b = b.replace(da, da + decls.encode()).replace(ca, ca + calls.encode())
open(p, 'wb').write(b)
print('runner.c: registered %d tests after %s' % (len(names), anchor))
PYEOF
echo "tests/runner.c CR lines: $(grep -c $'\r$' tests/runner.c) of $(wc -l < tests/runner.c)"
```

Expected: `tests/test_ui_demo.c: 3 edit(s)`, `tests/test_ui_demo.c: 1 test appended`, `runner.c: registered 1 tests after test_ui_demo_panel_width_only_for_policy_compact`, `tests/runner.c CR lines: 6643 of 6643`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && make test 2>&1 | grep -E "^\[FAIL\]|^Tests Run"`
Expected: `[FAIL] test_ui_demo_states_list_ends_in_all` (count 16 != 17), `[FAIL] test_ui_demo_ansi_swatches` (`ui_demo_state_valid("ansi")` is false), and `Tests Run: 3003, Failed: 2`.

- [ ] **Step 3: Write the implementation**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && python3 - <<'PYEOF'
def apply(path, edits):
    raw = open(path, 'rb').read()
    crlf = b'\r\n' in raw
    text = raw.decode('utf-8')
    for old, new in edits:
        if crlf:
            old, new = old.replace('\n', '\r\n'), new.replace('\n', '\r\n')
        assert text.count(old) == 1, (path, text.count(old), old[:60])
        text = text.replace(old, new)
    open(path, 'wb').write(text.encode('utf-8'))
    print('%s: %d edit(s)%s' % (path, len(edits), ' (CRLF kept)' if crlf else ''))

apply('src/core/ui_demo.c', [
(r''' * State names, in the order ui_demo_states() reports them.  "all" is
 * always last and always means "union of the panel states" (not "local",
 * "thinking", "cmddetail", "settings-debug", "policy-auto" or
 * "policy-compact", which reuse or swap content).
 * --------------------------------------------------------------------- */
static const char *const STATE_NAMES[] = {
    "chat", "approval", "executing", "tool", "error", "empty",
    "nokey", "nosession", "batches", "local", "thinking", "cmddetail",
    "settings-debug", "policy-auto", "policy-compact", "all"
};''', r''' * State names, in the order ui_demo_states() reports them.  "all" is
 * always last and always means "union of the panel states" (not "ansi",
 * "local", "thinking", "cmddetail", "settings-debug", "policy-auto" or
 * "policy-compact", which reuse or swap content). "ansi" sits before
 * "local" so the names tests/test_ui_demo.c anchors from the end of the
 * list keep their positions.
 * --------------------------------------------------------------------- */
static const char *const STATE_NAMES[] = {
    "chat", "approval", "executing", "tool", "error", "empty",
    "nokey", "nosession", "batches", "ansi", "local", "thinking", "cmddetail",
    "settings-debug", "policy-auto", "policy-compact", "all"
};'''),
(r'''/* ---------------------------------------------------------------------
 * Entry point
 * --------------------------------------------------------------------- */''', r'''/* ---------------------------------------------------------------------
 * "ansi" (spec 2026-10-19-light-theme-ansi-contrast-design.md section 6):
 * the 16-colour palette as it paints on the current theme, for the eye
 * (the gallery) and for the harness's pixel samples
 * (tests/integration/cases/30-ui-demo.ps1). Line N (0-15) is the index,
 * a four-cell full block (U+2588) and "Ab", in palette index N; line 16
 * is the same in SGR 2;97 (dim bright white, PSReadLine's inline
 * prediction). 11 columns a line, so nothing wraps on any supported grid;
 * no trailing newline, so 17 rows hold it all without scrolling. The
 * harness samples the centre of each line's second block cell (column 5).
 * --------------------------------------------------------------------- */
#define ANSI_BLOCK4 "\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88"
static const char ANSI_TERM_TEXT[] =
    "000 \x1b[30m" ANSI_BLOCK4 " Ab\x1b[0m\r\n"
    "001 \x1b[31m" ANSI_BLOCK4 " Ab\x1b[0m\r\n"
    "002 \x1b[32m" ANSI_BLOCK4 " Ab\x1b[0m\r\n"
    "003 \x1b[33m" ANSI_BLOCK4 " Ab\x1b[0m\r\n"
    "004 \x1b[34m" ANSI_BLOCK4 " Ab\x1b[0m\r\n"
    "005 \x1b[35m" ANSI_BLOCK4 " Ab\x1b[0m\r\n"
    "006 \x1b[36m" ANSI_BLOCK4 " Ab\x1b[0m\r\n"
    "007 \x1b[37m" ANSI_BLOCK4 " Ab\x1b[0m\r\n"
    "008 \x1b[90m" ANSI_BLOCK4 " Ab\x1b[0m\r\n"
    "009 \x1b[91m" ANSI_BLOCK4 " Ab\x1b[0m\r\n"
    "010 \x1b[92m" ANSI_BLOCK4 " Ab\x1b[0m\r\n"
    "011 \x1b[93m" ANSI_BLOCK4 " Ab\x1b[0m\r\n"
    "012 \x1b[94m" ANSI_BLOCK4 " Ab\x1b[0m\r\n"
    "013 \x1b[95m" ANSI_BLOCK4 " Ab\x1b[0m\r\n"
    "014 \x1b[96m" ANSI_BLOCK4 " Ab\x1b[0m\r\n"
    "015 \x1b[97m" ANSI_BLOCK4 " Ab\x1b[0m\r\n"
    "dim \x1b[2;97m" ANSI_BLOCK4 " Ab\x1b[0m";

/* ---------------------------------------------------------------------
 * Entry point
 * --------------------------------------------------------------------- */'''),
(r'''    /* "local" swaps the transcript for a local-shell one; "all" keeps the
     * SSH transcript, since it is the union of the panel states and the
     * local state adds nothing to the conversation. */
    if (term_text && term_cap > 0) {
        (void)snprintf(term_text, term_cap, "%s",
                       (strcmp(state, "local") == 0) ? LOCAL_TERM_TEXT
                                                     : DEMO_TERM_TEXT);
    }''', r'''    /* "local" swaps the transcript for a local-shell one and "ansi" for
     * the palette swatches; "all" keeps the SSH transcript, since it is the
     * union of the panel states and neither adds to the conversation. */
    if (term_text && term_cap > 0) {
        const char *text = DEMO_TERM_TEXT;
        if (strcmp(state, "local") == 0) text = LOCAL_TERM_TEXT;
        else if (strcmp(state, "ansi") == 0) text = ANSI_TERM_TEXT;
        (void)snprintf(term_text, term_cap, "%s", text);
    }'''),
(r'''int ui_demo_panel_width_px(const char *state, int dpi)
{
    if (state && strcmp(state, "policy-compact") == 0)
        return ns_scale(260, dpi);
    return 0;
}''', r'''int ui_demo_panel_width_px(const char *state, int dpi)
{
    /* "ansi" takes the same narrow dock, so the terminal keeps every
     * swatch line whole (and at least 24 columns) at any supported DPI. */
    if (state && (strcmp(state, "policy-compact") == 0 ||
                  strcmp(state, "ansi") == 0))
        return ns_scale(260, dpi);
    return 0;
}'''),
])

apply('src/core/ui_demo.h', [
(r''' * ui_demo_policy() and, for "policy-compact", docks the panel at
 * ui_demo_panel_width_px(). Neither is part of "all". */''', r''' * ui_demo_policy() and, for "policy-compact", docks the panel at
 * ui_demo_panel_width_px(). Neither is part of "all".
 *
 * "ansi" (2026-10-19, light-theme ANSI contrast) swaps the terminal
 * transcript for 17 palette swatch lines (indices 0-15, then dim bright
 * white) and docks the panel at ui_demo_panel_width_px(); the conversation
 * is the system message only. Not part of "all". */'''),
(r'''/* Docked panel width in device pixels for `state` at `dpi`, or 0 for "use
 * the normal default". Only "policy-compact" has one: ns_scale(260, dpi),
 * narrow enough for the status line's compact labels. */''', r'''/* Docked panel width in device pixels for `state` at `dpi`, or 0 for "use
 * the normal default". "policy-compact" and "ansi" have one:
 * ns_scale(260, dpi), narrow enough for the status line's compact labels
 * and wide enough a terminal for the swatch lines. */'''),
])
PYEOF
```

Expected: `src/core/ui_demo.c: 4 edit(s)` and `src/core/ui_demo.h: 2 edit(s)`.

- [ ] **Step 4: Run the tests to verify they pass, and cross-compile**

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && make test 2>&1 | grep -E "^\[(PASS|FAIL)\] test_ui_demo_(states_list|ansi|panel_width)|^\[FAIL\]|^Tests Run"`
Expected: `[PASS] test_ui_demo_states_list_ends_in_all`, `[PASS] test_ui_demo_panel_width_only_for_policy_compact`, `[PASS] test_ui_demo_ansi_swatches`, no `[FAIL]`, and `Tests Run: 3003, Failed: 0`.

Run:
```bash
cd /home/thomas/nutshell-wt/ansi-contrast && \
x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Os \
  -Isrc -Isrc/core -Isrc/config -Isrc/crypto -Isrc/term -Isrc/ssh -Isrc/ui -c src/core/ui_demo.c -o /dev/null && echo "mingw: ui_demo.c clean"
```
Expected: `mingw: ui_demo.c clean`.

- [ ] **Step 5: Commit**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && git status --short && \
git add src/core/ui_demo.c src/core/ui_demo.h tests/test_ui_demo.c tests/runner.c && \
git commit -F - <<'EOF'
feat(ui-demo): an "ansi" state with one swatch per palette entry

--ui-demo=ansi shows the 16-colour palette as it paints on the chosen
theme: 17 short lines, each a four-cell full block in one palette entry,
the last in dim bright white (PSReadLine's inline prediction), docked at
the narrow policy-compact width so nothing wraps. The gallery shows it on
every theme and the harness samples it. It sits before "local" in the
state list, so only the count changes.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM
EOF
```

---

### Task 6: Harness cases for the floor and the theme switch

**Files:**
- Modify: `tests/integration/cases/30-ui-demo.ps1` (`$galleryStates` and its comment; two cases appended)
- Modify: `tests/integration/README.md:40`, `:180`, `:187` (case list, gallery count, two table rows)
- Modify: `docs/superpowers/specs/2026-10-19-light-theme-ansi-contrast-design.md:502-503` (the origin sentence)

**Interfaces:**
- Consumes: `--ui-demo=ansi` and its line layout (Task 5); the `render.draw` fields `rows`, `char_h`, `char_w`, `cols`, `x`, `y` (Task 7 adds the last four; the cases are written first and run after Task 9's build); `IDC_SCHEME_COMBO` = 1020 (`src/ui/settings.c:42`); harness helpers `New-NutshellTestEnv -Settings`, `Start-Nutshell -ExtraArgs`, `Get-NutshellWorkArea`, `Set-NutshellWindowSize`, `Save-NutshellScreenshot`, `Get-NutshellPixel`, `Get-NutshellThemeColor`, `Open-NutshellSettings -Page`, `Get-NutshellControl`, `Select-NutshellCombo`, `Close-NutshellDialog` (`tests/integration/NutshellIT.psm1`) and the driver's `Assert-True`, `ConvertTo-NutshellFileToken`.
- Produces: gate-tier cases `helpers_ansi_contrast_floor` and `helpers_theme_switch_repaints`; `ansi` in `ui_gallery` (16 states x 4 themes = 64 captures).

These cases are the tests for Task 7's renderer and window changes, which cannot run natively; they are written before that code and run on the maintainer's Windows box after Task 9 (Task 9, Step 6). `helpers_theme_switch_repaints` is expected to fail against an exe built without Task 7 (lines 11 and 15 keep `FFFF55`/`FFFFFF`).

- [ ] **Step 1: Add `ansi` to the gallery and append the two cases**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && python3 - <<'PYEOF'
def apply(path, edits):
    raw = open(path, 'rb').read()
    crlf = b'\r\n' in raw
    text = raw.decode('utf-8')
    for old, new in edits:
        if crlf:
            old, new = old.replace('\n', '\r\n'), new.replace('\n', '\r\n')
        assert text.count(old) == 1, (path, text.count(old), old[:60])
        text = text.replace(old, new)
    open(path, 'wb').write(text.encode('utf-8'))
    print('%s: %d edit(s)%s' % (path, len(edits), ' (CRLF kept)' if crlf else ''))

apply('tests/integration/cases/30-ui-demo.ps1', [
(r'''# the capture shows the Thinking box at its 50-line cap with a scrollbar.
''', r'''# the capture shows the Thinking box at its 50-line cap with a scrollbar.
# "ansi" (2026-10-19) is the 16-colour palette as swatches, so the contact
# sheet shows what each theme's contrast floor does to it (raw on the dark
# themes, floored to 4.5:1 on the light ones).
'''),
(r'''"nokey", "nosession", "batches", "thinking", "cmddetail",''',
 r'''"nokey", "nosession", "batches", "ansi", "thinking", "cmddetail",'''),
])

path = 'tests/integration/cases/30-ui-demo.ps1'
text = open(path).read()
assert text.endswith('\n')
text += r'''
# ---- Light-theme ANSI contrast (spec 2026-10-19-light-theme-ansi-contrast-design.md, section 6) ----
# Both cases run --ui-demo=ansi: 17 swatch lines (palette entries 0-15, then
# dim bright white; src/core/ui_demo.c's ANSI_TERM_TEXT), docked at the narrow
# policy-compact width. Each line's sample is the centre of its second block
# cell (column 5): the cell size, the grid and the terminal's origin come from
# the last render.draw event (debugging on, render category at Trace), so
# nothing here assumes a DPI or a window size. The origin is the tab strip's
# top-left (the client origin: the strip sits at client 0,0) plus the x/y
# renderer_draw() was called with (the left margin and the tab height).
# Keystroke-free like ui_gallery: --ui-demo never connects.

$script:AnsiDemoSettings = @{ debug_enabled = $true; debug_level_render = "trace"; debug_log_dir = "debuglogs" }

function Get-AnsiDemoRelLuminance {
    param([int[]] $Rgb)
    $lin = @(foreach ($c in $Rgb) {
        $v = $c / 255.0
        if ($v -le 0.04045) { $v / 12.92 } else { [Math]::Pow(($v + 0.055) / 1.055, 2.4) }
    })
    return 0.2126 * $lin[0] + 0.7152 * $lin[1] + 0.0722 * $lin[2]
}

function Get-AnsiDemoContrast {
    <# WCAG 2.x ratio, theme_contrast()'s formula (src/core/theme.c). #>
    param([int[]] $A, [int[]] $B)
    $la = Get-AnsiDemoRelLuminance -Rgb $A
    $lb = Get-AnsiDemoRelLuminance -Rgb $B
    return ([Math]::Max($la, $lb) + 0.05) / ([Math]::Min($la, $lb) + 0.05)
}

function Get-AnsiDemoLStar {
    <# CIE L*, theme_lstar()'s formula. #>
    param([int[]] $Rgb)
    $y = Get-AnsiDemoRelLuminance -Rgb $Rgb
    $f = if ($y -gt (216.0 / 24389.0)) { [Math]::Pow($y, 1.0 / 3.0) } else { ((24389.0 / 27.0) * $y + 16.0) / 116.0 }
    return 116.0 * $f - 16.0
}

function Test-AnsiDemoNear {
    param([int[]] $Rgb, [int[]] $Want, [int] $Tolerance = 12)
    for ($k = 0; $k -lt 3; $k++) {
        if ([Math]::Abs($Rgb[$k] - $Want[$k]) -gt $Tolerance) { return $false }
    }
    return $true
}

function Get-AnsiDemoGeometry {
    <# The last render.draw event's grid and cell size, and the terminal's
       top-left in a Save-NutshellScreenshot capture (window coordinates). #>
    param([Parameter(Mandatory)] $Session)
    $dir = Join-Path $Session.Env.Root "debuglogs"
    $m = $null
    $deadline = (Get-Date).AddSeconds(8)
    while ((Get-Date) -lt $deadline) {
        $log = Get-ChildItem -Path (Join-Path $dir "nutshell-*-p*.log") -ErrorAction SilentlyContinue |
               Where-Object { $_.Name -notmatch "\.issue\d+\.log$" } |
               Sort-Object LastWriteTime | Select-Object -Last 1
        if ($log) {
            $lines = @(Get-Content $log.FullName | Where-Object { $_ -match ' render\.draw ' })
            if ($lines.Count -gt 0) {
                $m = [regex]::Match($lines[$lines.Count - 1],
                    '\brows=(\d+) .*\bchar_h=(\d+) char_w=(\d+) cols=(\d+) x=(-?\d+) y=(-?\d+)')
                if ($m.Success) { break }
            }
        }
        Start-Sleep -Milliseconds 300
    }
    if (-not $m -or -not $m.Success) { throw "no render.draw event with char_w/cols/x/y in $dir" }
    $tabs = [NutshellNative]::FindWindowEx($Session.Main, [IntPtr]::Zero, "Nutshell_Tabs", [IntPtr]::Zero)
    if ($tabs -eq [IntPtr]::Zero) { throw "tab strip (class Nutshell_Tabs) not found under the main window" }
    $wr = New-Object NutshellNative+RECT
    $tr = New-Object NutshellNative+RECT
    [NutshellNative]::GetWindowRect($Session.Main, [ref]$wr) | Out-Null
    [NutshellNative]::GetWindowRect($tabs, [ref]$tr) | Out-Null
    $g = [pscustomobject]@{
        Rows  = [int]$m.Groups[1].Value
        CharH = [int]$m.Groups[2].Value
        CharW = [int]$m.Groups[3].Value
        Cols  = [int]$m.Groups[4].Value
        Left  = ($tr.L - $wr.L) + [int]$m.Groups[5].Value
        Top   = ($tr.T - $wr.T) + [int]$m.Groups[6].Value
    }
    $g | Add-Member -NotePropertyName Text -NotePropertyValue ("rows={0} cols={1} char={2}x{3} origin=({4},{5})" -f $g.Rows, $g.Cols, $g.CharW, $g.CharH, $g.Left, $g.Top)
    return $g
}

function Get-AnsiDemoSwatch {
    <# The pixel at the centre of line $Line's second block cell (column 5). #>
    param([Parameter(Mandatory)] [string] $Path, [Parameter(Mandatory)] $Geo, [Parameter(Mandatory)] [int] $Line)
    $x = [int]($Geo.Left + 5.5 * $Geo.CharW)
    $y = [int]($Geo.Top + ($Line + 0.5) * $Geo.CharH)
    $p = Get-NutshellPixel -Path $Path -X $x -Y $y
    return [pscustomobject]@{
        Line = $Line; X = $x; Y = $y
        Rgb  = @([int]$p.R, [int]$p.G, [int]$p.B)
        Hex  = ('{0:X2}{1:X2}{2:X2}' -f $p.R, $p.G, $p.B)
    }
}

function Get-AnsiDemoSamples {
    <# Size the window to the work area's full height, capture it, read the
       grid, refuse to sample a grid the 17 swatch lines do not fit, and check
       alignment on lines 0 and 1 (palette black and red, the same on every
       theme: above 4.5 on the light ones, raw on the dark ones). Returns the
       17 samples and the geometry. #>
    param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)] [string] $Path, [Parameter(Mandatory)] [string] $What)
    Save-NutshellScreenshot -Session $Session -Path $Path | Out-Null
    $geo = Get-AnsiDemoGeometry -Session $Session
    if ($geo.Rows -lt 17 -or $geo.Cols -lt 24) { throw "$What grid too small to sample: $($geo.Text)" }
    $s = @(for ($i = 0; $i -le 16; $i++) { Get-AnsiDemoSwatch -Path $Path -Geo $geo -Line $i })
    Assert-True (Test-AnsiDemoNear -Rgb $s[0].Rgb -Want @(0x00, 0x00, 0x00)) "$What line 0 is $($s[0].Hex), expected 000000: the swatches are not where the grid says ($($geo.Text))"
    Assert-True (Test-AnsiDemoNear -Rgb $s[1].Rgb -Want @(0xCC, 0x33, 0x33)) "$What line 1 is $($s[1].Hex), expected CC3333: the swatches are not where the grid says ($($geo.Text))"
    return [pscustomobject]@{ Samples = $s; Geo = $geo }
}

function Start-AnsiDemo {
    param([Parameter(Mandatory)] $TestEnv, [Parameter(Mandatory)] [string] $Theme)
    $session = Start-Nutshell -Env $TestEnv -ExtraArgs @("--ui-demo=ansi", "--theme", $Theme)
    $wa = Get-NutshellWorkArea
    Set-NutshellWindowSize -Session $session -Width 1400 -Height $wa.Height | Out-Null
    Start-Sleep -Milliseconds 1000   # the repaint after the resize reaches the log (250 ms writer)
    return $session
}

if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains "helpers_ansi_contrast_floor")) {
    $name = "helpers_ansi_contrast_floor"
    Write-Host ("[RUN ] " + $name)
    $ok = $false; $detail = ""
    $notes = New-Object System.Collections.ArrayList
    try {
        foreach ($themeName in @("Onyx Light", "Onyx Synapse")) {
            $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath -Settings $script:AnsiDemoSettings
            $session = $null
            try {
                $session = Start-AnsiDemo -TestEnv $testEnv -Theme $themeName
                $path = Join-Path $Artifacts ("{0}-{1}.png" -f $name, (ConvertTo-NutshellFileToken $themeName))
                $r = Get-AnsiDemoSamples -Session $session -Path $path -What $themeName
                $s = $r.Samples
                if ($themeName -eq "Onyx Light") {
                    $bg = Get-NutshellThemeColor -Name $themeName -Token "terminal_bg"
                    $bgRgb = @($bg.R, $bg.G, $bg.B)
                    for ($i = 0; $i -le 15; $i++) {
                        $c = Get-AnsiDemoContrast -A $s[$i].Rgb -B $bgRgb
                        Assert-True ($c -ge 4.4) ("{0} line {1} is {2}, {3:N2}:1 against terminal_bg (want >= 4.4)" -f $themeName, $i, $s[$i].Hex, $c)
                    }
                    Assert-True (Test-AnsiDemoNear -Rgb $s[11].Rgb -Want @(0x72, 0x72, 0x23)) "$themeName line 11 (SGR 93) is $($s[11].Hex), expected 727223"
                    $l15 = Get-AnsiDemoLStar -Rgb $s[15].Rgb
                    $l16 = Get-AnsiDemoLStar -Rgb $s[16].Rgb
                    Assert-True ($l16 -ge $l15 + 10.0) ("{0} line 16 (dim 97) is {1}, L* {2:N1}; line 15 (97) is {3}, L* {4:N1}: dim should be at least 10 L* lighter" -f $themeName, $s[16].Hex, $l16, $s[15].Hex, $l15)
                } else {
                    Assert-True (Test-AnsiDemoNear -Rgb $s[11].Rgb -Want @(0xFF, 0xFF, 0x55)) "$themeName line 11 (SGR 93) is $($s[11].Hex), expected the raw FFFF55"
                    Assert-True (Test-AnsiDemoNear -Rgb $s[16].Rgb -Want @(0xFF, 0xFF, 0xFF)) "$themeName line 16 (dim 97) is $($s[16].Hex), expected FFFFFF (dim ignored on a dark theme)"
                }
                [void]$notes.Add(("{0} ({1}): line 11 {2}, line 15 {3}, line 16 {4}" -f $themeName, $r.Geo.Text, $s[11].Hex, $s[15].Hex, $s[16].Hex))
            } finally {
                if ($session) { Stop-Nutshell -Session $session }
                for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
                    Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
                    if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
                }
            }
        }
        $detail = $notes -join "; "
        $ok = $true
    } catch {
        $detail = $_.Exception.Message
    }
    if ($ok) { Write-Host ("[PASS] " + $name) } else { Write-Host ("[FAIL] " + $name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $name; Passed = $ok; Detail = $detail })
}

# The path --theme never exercises (spec 5.6): a theme changed in Settings
# while the terminal shows indexed text. Lines 11 and 15 are far from the
# cursor (end of line 16), so without the forced repaint they would keep the
# old theme's pixels.
if (($ActiveTiers -contains "gate") -and ($Only.Count -eq 0 -or $Only -contains "helpers_theme_switch_repaints")) {
    $name = "helpers_theme_switch_repaints"
    Write-Host ("[RUN ] " + $name)
    $testEnv = New-NutshellTestEnv -Exe $Exe -HostName $HostName -User $User -KeyPath $KeyPath -Settings $script:AnsiDemoSettings
    $session = $null
    $ok = $false; $detail = ""
    try {
        $session = Start-AnsiDemo -TestEnv $testEnv -Theme "Onyx Synapse"
        $before = Get-AnsiDemoSamples -Session $session -Path (Join-Path $Artifacts "$name-before.png") -What "Onyx Synapse"
        Assert-True (Test-AnsiDemoNear -Rgb $before.Samples[11].Rgb -Want @(0xFF, 0xFF, 0x55)) "before the switch line 11 is $($before.Samples[11].Hex), expected FFFF55"

        $dlg = Open-NutshellSettings -Session $session -Page "Appearance"
        $combo = Get-NutshellControl -Dialog $dlg -Id 1020   # IDC_SCHEME_COMBO, src/ui/settings.c
        if ($combo -eq [IntPtr]::Zero) { throw "colour scheme combo (1020) not found on the Appearance page" }
        Select-NutshellCombo -Control $combo -Item "Onyx Light"
        Close-NutshellDialog -Dialog $dlg -Button OK          # Settings' IDOK is Save
        Start-Sleep -Milliseconds 1500

        $after = Get-AnsiDemoSamples -Session $session -Path (Join-Path $Artifacts "$name-after.png") -What "after the switch"
        $s = $after.Samples
        Assert-True (Test-AnsiDemoNear -Rgb $s[11].Rgb -Want @(0x72, 0x72, 0x23)) "after the switch line 11 is $($s[11].Hex), expected Onyx Light's 727223 (FFFF55 means the row kept Onyx Synapse's pixels)"
        Assert-True (Test-AnsiDemoNear -Rgb $s[15].Rgb -Want @(0x6C, 0x6C, 0x6C)) "after the switch line 15 is $($s[15].Hex), expected Onyx Light's 6C6C6C (FFFFFF means the row kept Onyx Synapse's pixels)"
        $detail = "Onyx Synapse -> Onyx Light in Settings: line 11 $($s[11].Hex), line 15 $($s[15].Hex) ($($after.Geo.Text))"
        $ok = $true
    } catch {
        $detail = $_.Exception.Message
        if ($session) { try { Save-NutshellScreenshot -Session $session -Path (Join-Path $Artifacts "$name-FAIL.png") | Out-Null } catch {} }
    } finally {
        if ($session) { Stop-Nutshell -Session $session }
        for ($try = 0; $try -lt 5 -and (Test-Path $testEnv.Root); $try++) {
            Remove-Item -Recurse -Force $testEnv.Root -ErrorAction SilentlyContinue
            if (Test-Path $testEnv.Root) { Start-Sleep -Milliseconds 400 }
        }
    }
    if ($ok) { Write-Host ("[PASS] " + $name) } else { Write-Host ("[FAIL] " + $name + " -- " + $detail) }
    [void]$results.Add([pscustomobject]@{ Name = $name; Passed = $ok; Detail = $detail })
}
'''
open(path, 'w').write(text)
print('%s: 2 cases appended' % path)
PYEOF
grep -c 'helpers_ansi_contrast_floor\|helpers_theme_switch_repaints' tests/integration/cases/30-ui-demo.ps1
```

Expected: `tests/integration/cases/30-ui-demo.ps1: 2 edit(s)`, `tests/integration/cases/30-ui-demo.ps1: 2 cases appended`, and `4` (each case name on two lines: its `-contains` gate and its `$name`).

- [ ] **Step 2: List the cases in the README and correct the spec's origin sentence**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && python3 - <<'PYEOF'
def apply(path, edits):
    raw = open(path, 'rb').read()
    crlf = b'\r\n' in raw
    text = raw.decode('utf-8')
    for old, new in edits:
        if crlf:
            old, new = old.replace('\n', '\r\n'), new.replace('\n', '\r\n')
        assert text.count(old) == 1, (path, text.count(old), old[:60])
        text = text.replace(old, new)
    open(path, 'wb').write(text.encode('utf-8'))
    print('%s: %d edit(s)%s' % (path, len(edits), ' (CRLF kept)' if crlf else ''))

apply('tests/integration/README.md', [
(r'''| `cases\30-ui-demo.ps1` | `ui_gallery`, `approval_card_run_selected_settles`, `helpers_theme_pixel_matches_token` |''',
 r'''| `cases\30-ui-demo.ps1` | `ui_gallery`, `approval_card_run_selected_settles`, `helpers_theme_pixel_matches_token`, `helpers_ansi_contrast_floor`, `helpers_theme_switch_repaints` |'''),
(r'''(15 states x 4 themes = 60 captures)''', r'''(16 states x 4 themes = 64 captures)'''),
(r'''| `helpers_theme_pixel_matches_token` | `--ui-demo=chat --theme "Onyx Light"`; a sampled background pixel matches `Get-NutshellThemeColor`'s `bg_primary` within tolerance; no SSH host or AI key needed |
''', r'''| `helpers_theme_pixel_matches_token` | `--ui-demo=chat --theme "Onyx Light"`; a sampled background pixel matches `Get-NutshellThemeColor`'s `bg_primary` within tolerance; no SSH host or AI key needed |
| `helpers_ansi_contrast_floor` | `--ui-demo=ansi`, window at the work area's full height, debugging on with `render` at Trace: the cell size, grid and origin come from the last `render.draw` event (`char_w`, `char_h`, `rows`, `cols`, `x`, `y`); FAIL with the grid in the detail if it is under 17 x 24 or lines 0/1 are not `000000`/`CC3333`. On Onyx Light the 16 palette swatches each read at >= 4.4:1 against `terminal_bg`, line 11 (SGR 93) is `727223` and line 16 (dim 97) is at least 10 L* lighter than line 15; on Onyx Synapse line 11 is the raw `FFFF55` and line 16 `FFFFFF` (spec `2026-10-19-light-theme-ansi-contrast-design.md` section 6); no SSH host or AI key needed |
| `helpers_theme_switch_repaints` | `--ui-demo=ansi --theme "Onyx Synapse"`, then Settings > Appearance > Onyx Light > Save: lines 11 and 15, far from the cursor, are `727223`/`6C6C6C`, not the old theme's `FFFF55`/`FFFFFF` (the forced repaint after a theme change); no SSH host or AI key needed |
'''),
])

apply('docs/superpowers/specs/2026-10-19-light-theme-ansi-contrast-design.md', [
(r'''The terminal's top-left is
  the tab strip's bottom-left (`Get-NutshellTabStripRegion`); line N's sample is the centre of''',
 r'''The terminal's top-left is
  the tab strip's top-left (the client origin; the strip sits at client 0,0) plus the `x=` and
  `y=` the same event logs (`renderer_draw()` starts at the left margin, not at 0; corrected by
  the plan, which adds both fields); line N's sample is the centre of'''),
])
PYEOF
```

Expected: `tests/integration/README.md: 3 edit(s)` and `docs/superpowers/specs/2026-10-19-light-theme-ansi-contrast-design.md: 1 edit(s)`.

- [ ] **Step 3: Check the script parses**

PowerShell is not installed on this host, so check what can be checked here: balanced braces and parentheses in the appended text, and no tab characters (the file uses spaces).

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && python3 - <<'PYEOF'
text = open('tests/integration/cases/30-ui-demo.ps1').read()
tail = text[text.index('# ---- Light-theme ANSI contrast'):]
for o, c in (('{', '}'), ('(', ')'), ('[', ']')):
    assert tail.count(o) == tail.count(c), (o, tail.count(o), tail.count(c))
assert '\t' not in tail
print('30-ui-demo.ps1: appended block balanced, %d lines' % tail.count('\n'))
PYEOF
```

Expected: `30-ui-demo.ps1: appended block balanced, ... lines`. The real run is the maintainer's (Task 9, Step 6).

- [ ] **Step 4: Commit**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && git status --short && \
git add tests/integration/cases/30-ui-demo.ps1 tests/integration/README.md \
        docs/superpowers/specs/2026-10-19-light-theme-ansi-contrast-design.md && \
git commit -F - <<'EOF'
test(harness): sample the ANSI swatches; a theme switch repaints

Two gate cases on --ui-demo=ansi, sampling the centre of each swatch from
the cell size, grid and origin the render.draw debug event logs, so no
DPI or window size is assumed: on Onyx Light every palette entry reads at
4.5:1 against the terminal background, bright yellow is 727223 and dim
bright white is clearly lighter than bright white; on Onyx Synapse the
palette is raw and dim is ignored. The second switches Onyx Synapse to
Onyx Light in Settings and checks rows away from the cursor took the new
colours. The gallery shows the ansi state on every theme.

The spec's harness origin gains the renderer's left margin.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM
EOF
```

---

### Task 7: The renderer paints through the resolver; Settings repaints every row

**Files:**
- Modify: `src/ui/renderer.c:5-6` (includes), `:130-149` (`resolve_cell()`), `:172` (the palette), `:274` and `:315` (the two calls), `:438-443` (`render.draw`)
- Modify: `src/ui/window.c:1765-1770` (`on_settings_clicked_page()`)

**Interfaces:**
- Consumes: `ns_tokens()` (`src/ui/ns_tokens.h`, returns `&g_tokens` in `window.c`); `ThemeTokens.term` (Task 4); `term_colors_resolve()`, `TERM_DRAW_CURSOR`, `TERM_DRAW_SELECTED` (Task 3); `force_full_terminal_repaint(HWND, Terminal *)` (`window.c:512`, static, defined above its new caller).
- Produces: `render.draw` gains `char_w=%d cols=%d x=%d y=%d` after `char_h=%d` (Task 6's cases parse `\bchar_h=(\d+) char_w=(\d+) cols=(\d+) x=(-?\d+) y=(-?\d+)`).

`src/ui` is excluded from `make test`. The tests for this task are Task 6's harness cases, run after Task 9's build, and the design-system gate in `make test` (`renderer.c` keeps exactly 2 literal `RGB(`; `to_colorref()` does not count). This task adds no native test, so `make test` stays at `Tests Run: 3003`.

- [ ] **Step 1: Write the change**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && python3 - <<'PYEOF'
def apply(path, edits):
    raw = open(path, 'rb').read()
    crlf = b'\r\n' in raw
    text = raw.decode('utf-8')
    for old, new in edits:
        if crlf:
            old, new = old.replace('\n', '\r\n'), new.replace('\n', '\r\n')
        assert text.count(old) == 1, (path, text.count(old), old[:60])
        text = text.replace(old, new)
    open(path, 'wb').write(text.encode('utf-8'))
    print('%s: %d edit(s)%s' % (path, len(edits), ' (CRLF kept)' if crlf else ''))

apply('src/ui/renderer.c', [
(r'''#include "theme.h"
#include "dlog.h"
''', r'''#include "theme.h"
#include "term_colors.h"
#include "ns_tokens.h"
#include "dlog.h"
'''),
(r'''/* Resolve a cell's fg, bg, and bold flag, accounting for reverse + selection. */
static void resolve_cell(const Renderer *r, const TermCell *cell,
                          const Terminal *term, int row_idx, int col_idx,
                          const Selection *sel,
                          COLORREF *out_fg, COLORREF *out_bg,
                          bool *out_bold)
{
    COLORREF fg = (cell->attr.fg_mode == COLOR_DEFAULT) ? r->defaultFg : to_colorref(cell->attr.fg);
    COLORREF bg = (cell->attr.bg_mode == COLOR_DEFAULT) ? r->defaultBg : to_colorref(cell->attr.bg);
    bool reverse = (cell->attr.flags & TERM_ATTR_REVERSE) ||
                   (term->cursor.visible && term->scrollback_offset == 0 &&
                    term->cursor.row == row_idx && term->cursor.col == col_idx);
    if (reverse) { COLORREF tmp = fg; fg = bg; bg = tmp; }
    if (cell_in_selection(sel, row_idx, col_idx)) {
        COLORREF tmp = fg; fg = bg; bg = tmp;
    }
    *out_fg = fg;
    *out_bg = bg;
    *out_bold = (cell->attr.flags & TERM_ATTR_BOLD) != 0;
}
''', r'''/* Resolve a cell's fg, bg, and bold flag. Every colour decision -- the
 * theme's floored table, explicit backgrounds, dim, reverse, the cursor
 * and the selection -- is term_colors_resolve()'s (src/core/term_colors.c,
 * tested natively); this only says which cell is the cursor and which are
 * selected, and converts to COLORREF. */
static void resolve_cell(const TermPalette *pal, const TermCell *cell,
                          const Terminal *term, int row_idx, int col_idx,
                          const Selection *sel,
                          COLORREF *out_fg, COLORREF *out_bg,
                          bool *out_bold)
{
    unsigned draw_flags = 0u;
    if (term->cursor.visible && term->scrollback_offset == 0 &&
        term->cursor.row == row_idx && term->cursor.col == col_idx)
        draw_flags |= TERM_DRAW_CURSOR;
    if (cell_in_selection(sel, row_idx, col_idx))
        draw_flags |= TERM_DRAW_SELECTED;
    uint32_t fg = 0u, bg = 0u;
    term_colors_resolve(pal, cell, draw_flags, &fg, &bg);
    *out_fg = to_colorref(fg);
    *out_bg = to_colorref(bg);
    *out_bold = (cell->attr.flags & TERM_ATTR_BOLD) != 0;
}
'''),
(r'''    bool has_sel = sel && sel->valid;
''', r'''    bool has_sel = sel && sel->valid;
    /* The current theme's colour tables, rebuilt by every ui_theme_resolve(). */
    const TermPalette *pal = &ns_tokens()->term;
'''),
(r'''            resolve_cell(r, cell, term, row_idx, col_idx, sel, &fg, &bg, &bold);
            uint32_t cp = cell->codepoint;
''', r'''            resolve_cell(pal, cell, term, row_idx, col_idx, sel, &fg, &bg, &bold);
            uint32_t cp = cell->codepoint;
'''),
(r'''                resolve_cell(r, cell, term, row_idx, col_idx, sel, &fg, &bg, &bold);
                if (fg != run_fg''', r'''                resolve_cell(pal, cell, term, row_idx, col_idx, sel, &fg, &bg, &bold);
                if (fg != run_fg'''),
(r'''         "char_h=%d us=%lld",
         term->rows, rows_in_rect, rows_covered, rows_painted, rows_left_dirty,
         r->charHeight, us);''', r'''         "char_h=%d char_w=%d cols=%d x=%d y=%d us=%lld",
         term->rows, rows_in_rect, rows_covered, rows_painted, rows_left_dirty,
         r->charHeight, r->charWidth, term->cols, x, y, us);'''),
])

apply('src/ui/window.c', [
(r'''    SendMessage(parent, WM_SIZE, SIZE_RESTORED,
                MAKELPARAM(rc.right, rc.bottom));
    InvalidateRect(parent, NULL, TRUE);
    /* Force tab strip to repaint with new theme */
''', r'''    SendMessage(parent, WM_SIZE, SIZE_RESTORED,
                MAKELPARAM(rc.right, rc.bottom));
    /* WM_SIZE marks rows dirty only when the grid size changed, and
     * renderer_draw() skips clean rows before looking at a cell, so a theme
     * change alone (new terminal_fg/bg, new colour tables) would leave every
     * clean row in the old theme's colours. Repaint them all. Other tabs
     * repaint fully when selected (on_tab_select). */
    force_full_terminal_repaint(parent,
        g_active_session ? g_active_session->term : NULL);
    InvalidateRect(parent, NULL, TRUE);
    /* Force tab strip to repaint with new theme */
'''),
])
PYEOF
grep -c 'RGB(' src/ui/renderer.c
```

Expected: `src/ui/renderer.c: 6 edit(s)`, `src/ui/window.c: 1 edit(s)`, and `3` (`to_colorref()`'s unpacking `RGB(` plus the two literal fallbacks in `renderer_init()`, as before).

- [ ] **Step 2: Cross-compile both objects and run the native suite**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && for f in src/ui/renderer.c src/ui/window.c; do \
  x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 -Wconversion -Os \
    -Isrc -Isrc/core -Isrc/config -Isrc/crypto -I$HOME/vcpkg/installed/x64-mingw-gcc-static/include \
    -Isrc/term -Isrc/ssh -Isrc/ui -c "$f" -o /dev/null && echo "mingw: $f clean"; done
```
Expected: `mingw: src/ui/renderer.c clean` and `mingw: src/ui/window.c clean`, no compiler output.

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && make test 2>&1 | grep -E "^\[FAIL\]|^Tests Run"`
Expected: no `[FAIL]` (the `test_ui_tokens` gates scan `src/ui` and still count 2 literal `RGB(` in `renderer.c`) and `Tests Run: 3003, Failed: 0`.

- [ ] **Step 3: Commit**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && git status --short && \
git add src/ui/renderer.c src/ui/window.c && \
git commit -F - <<'EOF'
fix(render): paint terminal cells through the theme's colour table

resolve_cell() now says only which cell is the cursor and which are
selected, and takes the colours from term_colors_resolve() with the
current theme's table (ns_tokens()->term). On Onyx Light and Moss & Mist
indexed text on the terminal background reads at 4.5:1 at least -- the
yellow PowerShell prints is 727223 rather than FFFF55 -- and dim text is
painted; the dark themes paint as before.

Changing the theme in Settings resized nothing, so WM_SIZE marked no row
dirty and every clean row kept the old theme's colours; it now forces a
full terminal repaint. render.draw logs the cell width, the column count
and the paint origin for the harness.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM
EOF
```

---

### Task 8: PowerShell captured through ConPTY: fixtures and end-to-end tests

**Files:**
- Input (git-ignored, already in the worktree): `.superpowers/capture-ps51-20261020.log`, the maintainer's debug log of 2026-10-20 (Nutshell v1.2.41, a local Windows PowerShell 5.1 tab, build 26100, PSReadLine 2.0.0, 29 x 71 at 192 DPI; 87 `term.rx` lines, all session `s2`)
- Create: `.superpowers/decode_term_rx.py` (throwaway, ignored)
- Create: `tests/fixtures/conpty_ps51_tab_completion.bin`, `tests/fixtures/conpty_ps51_commands.bin`
- Modify: `tests/test_term_conpty.c` (includes; three tests appended)
- Modify: `tests/runner.c` (CRLF; after `test_term_conpty_opening_sequence_is_consumed`)

**Interfaces:**
- Consumes: `read_fixture()` (static in `tests/test_term_conpty.c`); `ui_theme_get()`, `ui_theme_resolve()`, `ThemeTokens.term`, `ThemeColors.terminal_min_contrast` (Task 4); `term_colors_resolve()` (Task 3); `TERM_ATTR_DIM` and the 16-colour index (Task 2).
- Produces: nothing later tasks use. Tests only; it runs before Task 9.

What the capture holds (checked while the plan was written): no inline prediction and no SGR 2 anywhere (PSReadLine 2.0 has none); `Get-Command pwsh` printed nothing, so no PowerShell 7 capture exists or will come. SGRs: `\e[93m` command tokens (38 times), `\e[37m` operators and arguments (23), `\e[90m` parameters (7), `\e[97m` members (2), `\e[92m` a variable (1), `\e[38;5;9m` the error record (2), `\e[m` resets; plus `\e[?9001h\e[?1004h` at spawn, `\e[2J`/`\e[3J` clears and `\e[NX` erases. Log lines 82-89 hold the Windows licence output (`ActivationID` and more) and must never reach a fixture. The fixtures are:

| Fixture | Log lines | Records, bytes | sha256 | What it is |
|---|---|---|---|---|
| `conpty_ps51_tab_completion.bin` | 15-51 | 34, 757 | `ad7397e1ab051797edc216e567b327afaf94c2e9cb5bcd3caec7a71222ee66c2` | the spawn, the first prompt, `Cl` typed and Tab cycled through 12 completions to `Clear-Host` (each redraw: `\e[m\e[?25l`, `\e[93m\e[1;20H<token>`, `\e[NX` or spaces, `\x08`) |
| `conpty_ps51_commands.bin` | 90-112 | 20, 1042 | `987c29a91b8c422ab41fe645c9956f5828f9661c62c61cdf13ee60ffaab80f65` | a three-command line (93, 37, 90) and its error in `38;5;9`, recalled with Up and run again, then `Get-Command pwsh -ErrorAction SilentlyContinue` and the next prompt |

Decoding (the inverse of `dlog_escape_bytes()`, `src/core/dlog_format.c`): `\e` is ESC 0x1B, `\r` and `\n` are CR and LF (so `\r\n` is the two bytes 0D 0A), `\t` is TAB, `\"` is `"`, `\\` is one backslash (the prompt `PS C:\\Users\\thoma>` is `PS C:\Users\thoma>`), `\xHH` is that byte (`\x08` is Backspace, `\x07` BEL); any other character is its UTF-8 bytes. Only `term.rx` lines in the range are decoded; `term.key`, `term.paste`, `term.clear` and `term.title` lines between them are skipped. Each record's decoded length must equal its `len=`.

PowerShell 7's inline prediction stays covered by a synthetic test, marked unverified in its comment and in the spec: `97;2;3` (PSReadLine 2.2 and later) and `38;5;238` (2.1), from PSReadLine's documented defaults.

- [ ] **Step 1: Write the decoder**

```bash
mkdir -p /home/thomas/nutshell-wt/ansi-contrast/.superpowers && cat > /home/thomas/nutshell-wt/ansi-contrast/.superpowers/decode_term_rx.py <<'PYEOF'
#!/usr/bin/env python3
"""Decode a range of a Nutshell debug log's term.rx records back into the
bytes the emulator was fed (spec 2026-10-19-light-theme-ansi-contrast-design.md,
section 5.0).

  decode_term_rx.py LOG FIRST LAST OUT

FIRST and LAST are 1-based line numbers of the log, inclusive. Every term.rx
line in the range is decoded, in order, and written to OUT; other lines in
the range (term.key, term.paste, ...) are skipped. All the records must come
from one session, and none may be marked truncated=1.

The log escapes ESC as \\e, CR/LF/TAB as \\r \\n \\t, '"' and '\\' with a
backslash and other control bytes as \\xHH (Backspace is \\x08); UTF-8 text is
written as is (dlog_escape_bytes(), src/core/dlog_format.c)."""
import re
import sys

PAT = re.compile(r' term (s\d+) \S+ term\.rx data="(.*)" len=(\d+)( truncated=1)?\s*$')
SIMPLE = {'e': b'\x1b', 'r': b'\r', 'n': b'\n', 't': b'\t', '"': b'"', '\\': b'\\'}


def unescape(text, where):
    out = bytearray()
    i = 0
    while i < len(text):
        ch = text[i]
        if ch != '\\':
            out += ch.encode('utf-8')
            i += 1
            continue
        nxt = text[i + 1:i + 2]
        if nxt in SIMPLE:
            out += SIMPLE[nxt]
            i += 2
        elif nxt == 'x' and re.fullmatch(r'[0-9a-f]{2}', text[i + 2:i + 4]):
            out.append(int(text[i + 2:i + 4], 16))
            i += 4
        else:
            sys.exit('%s: unknown escape %r' % (where, text[i:i + 4]))
    return bytes(out)


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    log, first, last, dest = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    data = bytearray()
    count = 0
    sids = set()
    with open(log, encoding='utf-8') as f:
        for n, line in enumerate(f, 1):
            if n < first or n > last:
                continue
            m = PAT.search(line)
            if not m:
                continue
            if m.group(4):
                sys.exit('line %d: truncated=1' % n)
            rec = unescape(m.group(2), 'line %d' % n)
            if len(rec) != int(m.group(3)):
                sys.exit('line %d: decoded %d bytes, the log says len=%s' % (n, len(rec), m.group(3)))
            sids.add(m.group(1))
            data += rec
            count += 1
    if count == 0 or len(sids) != 1:
        sys.exit('lines %d-%d: %d records from sessions %s' % (first, last, count, sorted(sids)))
    with open(dest, 'wb') as f:
        f.write(data)
    print('%s: %d records, %d bytes, lines %d-%d of %s' % (dest, count, len(data), first, last, sids.pop()))


if __name__ == '__main__':
    main()
PYEOF
```

- [ ] **Step 2: Write the tests (they skip until the fixtures exist)**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && python3 - <<'PYEOF'
path = 'tests/test_term_conpty.c'
text = open(path).read()
old = '#include "term_extract.h"\n'
assert text.count(old) == 1 and '\r\n' not in text
text = text.replace(old, old + '#include "term_colors.h"\n#include "ui_theme.h"\n#include "theme.h"\n')
text += r'''
/* ===========================================================================
 * Windows PowerShell 5.1 through ConPTY (spec
 * docs/superpowers/specs/2026-10-19-light-theme-ansi-contrast-design.md,
 * sections 1 and 5.0). Captured on the maintainer's Windows box on
 * 2026-10-20 (Nutshell v1.2.41, a local PowerShell tab at 29 x 71, 192 DPI;
 * PSReadLine 2.0.0, which has no inline prediction) from the debug log's
 * term.rx lines, with the log's escaping undone:
 *   conpty_ps51_tab_completion.bin  log lines 15-51: the spawn, the prompt,
 *                                   "Cl" typed and Tab cycled to Clear-Host
 *   conpty_ps51_commands.bin        log lines 90-112: a three-command line,
 *                                   its error, recalled and run again, then
 *                                   Get-Command pwsh -ErrorAction ...
 * The hand-written `93m` string in tests/test_term.c stays: it tests prompt
 * detection, which these captures do not need.
 * ===========================================================================
 */

#define PS51_TAB_FIXTURE      "tests/fixtures/conpty_ps51_tab_completion.bin"
#define PS51_COMMANDS_FIXTURE "tests/fixtures/conpty_ps51_commands.bin"

/* A row's printable ASCII, '?' for anything else, trailing blanks cut. */
static void row_text(const TermRow *row, int cols, char *out, size_t cap)
{
    size_t n = 0;
    for (int c = 0; c < cols && n + 1u < cap; c++) {
        uint32_t cp = row->cells[c].codepoint;
        out[n++] = (cp >= 0x20u && cp < 0x7Fu) ? (char)cp : (cp == 0u ? ' ' : '?');
    }
    while (n > 0u && out[n - 1u] == ' ') n--;
    out[n] = '\0';
}

/* The cell's painted foreground on theme `t` (0..NUM_UI_THEMES-1). */
static uint32_t painted_fg(int t, const TermCell *cell)
{
    static ThemeTokens tok;
    ui_theme_resolve(ui_theme_get(t), &tok);
    uint32_t fg = 0u, bg = 0u;
    term_colors_resolve(&tok.term, cell, 0u, &fg, &bg);
    return fg;
}

/* What a palette cell must paint on every theme: the floored value on the
 * light themes (and at least their ratio), the raw palette on the dark. */
static void check_indexed(const TermCell *cell, uint8_t mode, uint8_t index,
                          uint32_t on_onyx_light, uint32_t on_moss_mist, int *fail)
{
    if (cell->attr.fg_mode != mode || cell->attr.fg_index != index ||
        cell->attr.bg_mode != COLOR_DEFAULT) {
        printf("  cell U+%04X: fg_mode=%d fg_index=%d bg_mode=%d, expected %d/%d on the default bg\n",
               (unsigned)cell->codepoint, (int)cell->attr.fg_mode, (int)cell->attr.fg_index,
               (int)cell->attr.bg_mode, (int)mode, (int)index);
        *fail = 1;
        return;
    }
    for (int t = 0; t < NUM_UI_THEMES; t++) {
        const ThemeColors *base = ui_theme_get(t);
        uint32_t fg = painted_fg(t, cell);
        uint32_t want = color256_to_rgb(index);
        if (strcmp(base->name, "Onyx Light") == 0) want = on_onyx_light;
        if (strcmp(base->name, "Moss & Mist") == 0) want = on_moss_mist;
        if (fg != want) {
            printf("  index %d on %s paints %06X, expected %06X\n", (int)index,
                   base->name, (unsigned)fg, (unsigned)want);
            *fail = 1;
        }
        if (base->terminal_min_contrast > 0.0 &&
            theme_contrast(fg, base->terminal_bg) < base->terminal_min_contrast) {
            printf("  index %d on %s is below its floor\n", (int)index, base->name);
            *fail = 1;
        }
    }
}

/* PSReadLine 2.0's command token (SGR 93), redrawn on every Tab with
 * cursor moves, ECH and Backspace, fed a byte at a time: it ends as
 * "Clear-Host" in palette index 11, floored to 727223 on the light themes
 * and the raw FFFF55 on the dark ones. */
int test_term_conpty_ps51_tab_completion_token_is_floored(void)
{
    TEST_BEGIN();
    size_t len = 0;
    char *bytes = read_fixture(PS51_TAB_FIXTURE, &len);
    if (!bytes) {
        printf("  [ps51 fixture] %s not readable from cwd -- skipping\n", PS51_TAB_FIXTURE);
        TEST_END();
    }
    ASSERT_EQ(len, 757u);
    Terminal *term = term_init(29, 71, 500);
    ASSERT_NOT_NULL(term);
    if (!term) { free(bytes); TEST_END(); }
    for (size_t i = 0; i < len; i++) term_process(term, bytes + i, 1u);

    const TermRow *row = term->lines[term_screen_to_phys(term, 0)];
    char text[128];
    row_text(row, term->cols, text, sizeof(text));
    ASSERT_STR_EQ(text, "PS C:\\Users\\thoma> Clear-Host");
    ASSERT_EQ(term->cursor.row, 0);
    ASSERT_EQ(term->cursor.col, 29);
    int fail = 0;
    for (int c = 19; c < 29; c++)
        check_indexed(&row->cells[c], COLOR_ANSI16, 11, 0x727223u, 0x727223u, &fail);
    ASSERT_EQ(fail, 0);
    ASSERT_EQ(row->cells[0].attr.fg_mode, COLOR_DEFAULT);
    ASSERT_EQ(painted_fg(1, &row->cells[0]), 0x1D1D1Fu);   /* Onyx Light terminal_fg */
    term_free(term);
    free(bytes);
    TEST_END();
}

/* The rest of PSReadLine 2.0's palette as it arrives: command tokens 93,
 * arguments 37, parameters 90, and the error record in 256-colour 9. All
 * on the default background, so all floored on the light themes and raw on
 * the dark ones. */
int test_term_conpty_ps51_commands_are_floored(void)
{
    TEST_BEGIN();
    size_t len = 0;
    char *bytes = read_fixture(PS51_COMMANDS_FIXTURE, &len);
    if (!bytes) {
        printf("  [ps51 fixture] %s not readable from cwd -- skipping\n", PS51_COMMANDS_FIXTURE);
        TEST_END();
    }
    ASSERT_EQ(len, 1042u);
    Terminal *term = term_init(29, 71, 500);
    ASSERT_NOT_NULL(term);
    if (!term) { free(bytes); TEST_END(); }
    size_t first = len / 3u;
    term_process(term, bytes, first);
    term_process(term, bytes + first, len - first);

    const TermRow *cmd = NULL, *err = NULL;
    for (int r = 0; r < term->rows; r++) {
        const TermRow *row = term->lines[term_screen_to_phys(term, r)];
        char text[128];
        row_text(row, term->cols, text, sizeof(text));
        if (strcmp(text, "PS C:\\Users\\thoma> Get-Command pwsh -ErrorAction SilentlyContinue") == 0)
            cmd = row;
        static const char err_head[] = "InvalidArgument: (:) [Select-Object]";
        if (strncmp(text, err_head, sizeof(err_head) - 1u) == 0)
            err = row;
    }
    ASSERT_NOT_NULL(cmd);
    ASSERT_NOT_NULL(err);
    int fail = 0;
    if (cmd) {
        check_indexed(&cmd->cells[19], COLOR_ANSI16, 11, 0x727223u, 0x727223u, &fail); /* G of Get-Command */
        check_indexed(&cmd->cells[31], COLOR_ANSI16, 7,  0x707070u, 0x6B6B6Bu, &fail); /* p of pwsh */
        check_indexed(&cmd->cells[36], COLOR_ANSI16, 8,  0x6C6C6Cu, 0x6C6C6Cu, &fail); /* - of -ErrorAction */
    }
    if (err)
        check_indexed(&err->cells[0], COLOR_256, 9, 0xC24040u, 0xC24040u, &fail);    /* I of InvalidArgument */
    ASSERT_EQ(fail, 0);
    term_free(term);
    free(bytes);
    TEST_END();
}

/* SYNTHETIC, UNVERIFIED: PowerShell 7's inline prediction. No PowerShell 7
 * capture exists (the maintainer's box has none; PSReadLine 2.0 has no
 * predictions), so these bytes are written by hand from PSReadLine's
 * documented defaults: 2.2 and later paint the prediction 97;2;3, 2.1
 * 38;5;238. With dim it paints fainter than the typed token on both light
 * themes; 238 (444444) is above 4.5:1 already, so it paints as sent --
 * apart from the typed text but darker than it. Replace with a capture if
 * one is ever made. */
int test_term_conpty_pwsh7_prediction_synthetic(void)
{
    TEST_BEGIN();
    static const struct { const char *bytes; uint32_t on_onyx_light, on_moss_mist; } v[2] = {
        { "\x1b[2J\x1b[HPS C:\\> \x1b[93mG\x1b[97;2;3met-ChildItem\x1b[0m\x1b[12D",
          0xA3A3A4u, 0xA1A2A1u },
        { "\x1b[2J\x1b[HPS C:\\> \x1b[93mG\x1b[38;5;238met-ChildItem\x1b[0m\x1b[12D",
          0x444444u, 0x444444u },
    };
    for (int k = 0; k < 2; k++) {
        Terminal *term = term_init(29, 71, 500);
        ASSERT_NOT_NULL(term);
        if (!term) continue;
        term_process(term, v[k].bytes, strlen(v[k].bytes));
        const TermRow *row = term->lines[term_screen_to_phys(term, 0)];
        ASSERT_EQ(term->cursor.col, 9);                    /* back after the typed G */
        const TermCell *typed = &row->cells[8], *pred = &row->cells[9];
        ASSERT_EQ(typed->codepoint, (uint32_t)'G');
        ASSERT_EQ(pred->codepoint, (uint32_t)'e');
        for (int t = 0; t < NUM_UI_THEMES; t++) {
            const ThemeColors *base = ui_theme_get(t);
            uint32_t pf = painted_fg(t, pred), tf = painted_fg(t, typed);
            if (strcmp(base->name, "Onyx Light") == 0) { ASSERT_EQ(pf, v[k].on_onyx_light); }
            if (strcmp(base->name, "Moss & Mist") == 0) { ASSERT_EQ(pf, v[k].on_moss_mist); }
            if (base->terminal_min_contrast > 0.0) {
                ASSERT_TRUE(pf != tf);
                if (k == 0) { ASSERT_TRUE(theme_lstar(pf) > theme_lstar(tf) + 10.0); }
            }
        }
        term_free(term);
    }
    TEST_END();
}
'''
open(path, 'w').write(text)
print('%s: includes and 3 tests added' % path)
PYEOF
python3 - test_term_conpty_opening_sequence_is_consumed "" \
  test_term_conpty_ps51_tab_completion_token_is_floored test_term_conpty_ps51_commands_are_floored \
  test_term_conpty_pwsh7_prediction_synthetic <<'PYEOF'
# Run as: python3 - <anchor test> <comment, or ""> <test name>... <<'PYEOF'
# Registers test functions in tests/runner.c (CRLF): declarations after the
# anchor's declaration, calls after the anchor's call, in the order given.
import sys
anchor, comment, names = sys.argv[1], sys.argv[2], sys.argv[3:]
p = 'tests/runner.c'
b = open(p, 'rb').read()
da = ('int %s(void);\r\n' % anchor).encode()
ca = ('    failed += %s();\r\n' % anchor).encode()
assert b.count(da) == 1 and b.count(ca) == 1, 'anchor not found exactly once'
decls = (('/* %s */\r\n' % comment) if comment else '') + ''.join('int %s(void);\r\n' % n for n in names)
calls = ''.join('    failed += %s();\r\n' % n for n in names)
b = b.replace(da, da + decls.encode()).replace(ca, ca + calls.encode())
open(p, 'wb').write(b)
print('runner.c: registered %d tests after %s' % (len(names), anchor))
PYEOF
echo "tests/runner.c CR lines: $(grep -c $'\r$' tests/runner.c) of $(wc -l < tests/runner.c)"
```

Expected: `tests/test_term_conpty.c: includes and 3 tests added`, `runner.c: registered 3 tests after test_term_conpty_opening_sequence_is_consumed`, `tests/runner.c CR lines: 6649 of 6649`.

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && make test 2>&1 | grep -A1 -E "^\[RUN \] test_term_conpty_(ps51|pwsh7)|^Tests Run"`
Expected: the two `ps51` tests print `[ps51 fixture] tests/fixtures/conpty_ps51_....bin not readable from cwd -- skipping`; `test_term_conpty_pwsh7_prediction_synthetic` passes (its bytes are in the test and Tasks 2-4 are in); `Tests Run: 3006, Failed: 0`.

- [ ] **Step 3: Decode the two fixtures**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && L=.superpowers/capture-ps51-20261020.log && \
python3 .superpowers/decode_term_rx.py $L 15 51 tests/fixtures/conpty_ps51_tab_completion.bin && \
python3 .superpowers/decode_term_rx.py $L 90 112 tests/fixtures/conpty_ps51_commands.bin && \
sha256sum tests/fixtures/conpty_ps51_tab_completion.bin tests/fixtures/conpty_ps51_commands.bin && \
{ grep -c -a -E 'ActivationID|Licens|Product Key' tests/fixtures/conpty_ps51_commands.bin tests/fixtures/conpty_ps51_tab_completion.bin || true; }
```
Expected: `tests/fixtures/conpty_ps51_tab_completion.bin: 34 records, 757 bytes, lines 15-51 of s2`, `tests/fixtures/conpty_ps51_commands.bin: 20 records, 1042 bytes, lines 90-112 of s2`, the two sha256 values in the table above, and `0` for both files in the last check (`Get-ADLicenseInfo` is typed on lines 71-79, outside both ranges). Any other byte count or hash: stop and compare the log with this task's table.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && make test 2>&1 | grep -A4 -E "^\[RUN \] test_term_conpty_(ps51|pwsh7)" | grep -E "RUN|PASS|FAIL|  "; make test 2>&1 | grep -E "^\[FAIL\]|^Tests Run"`
Expected: `[PASS]` for `test_term_conpty_ps51_tab_completion_token_is_floored`, `test_term_conpty_ps51_commands_are_floored` and `test_term_conpty_pwsh7_prediction_synthetic`, with no indented lines under them (a failing check prints the cell, the theme and the colour); no `[FAIL]`; `Tests Run: 3006, Failed: 0`. If a `ps51` test fails, stop and report its printed lines: the fixture bytes are the maintainer's, so the expected values are not changed to match.

- [ ] **Step 5: Commit**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && git status --short && \
git add tests/fixtures/conpty_ps51_tab_completion.bin tests/fixtures/conpty_ps51_commands.bin \
        tests/test_term_conpty.c tests/runner.c && \
git commit -F - <<'EOF'
test(term): Windows PowerShell 5.1 captured through ConPTY, end to end

Two fixtures decoded from the debug log of the maintainer's local
PowerShell 5.1 tab (PSReadLine 2.0.0, 2026-10-20): Tab completion of a
command token, and a three-command line with its error record. The
command token (93), arguments (37), parameters (90) and the error in
256-colour 9 all arrive on the default background, so they paint floored
on Onyx Light and Moss & Mist (727223, 707070/6B6B6B, 6C6C6C, C24040) and
exactly as sent on the dark themes. The licence output in the same log is
not part of either fixture.

PowerShell 7's inline prediction has no capture (there is no pwsh on the
maintainer's box); a synthetic test, marked unverified, covers both of
its documented forms, 97;2;3 and 38;5;238.

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM
EOF
```

`git status --short` must show no `.superpowers/` file (ignored) and no other `??` line.

---

### Task 9: Merge main, version bump, release build, exe

**Files:**
- Modify: `src/ui/resource.h:4-5`
- Modify: `README.md:5`
- Modify: `build/win/nutshell.exe` (rebuilt)

**Interfaces:**
- Consumes: Tasks 1-8 committed.
- Produces: one version everywhere the `Version bump` check reads it (`APP_VERSION`, `APP_VERSION_BINARY`, README's `**Version**:` line, the exe's FileVersion), one patch above `origin/main`'s at the time of this task. The version is **not** fixed in this plan: this branch was cut at `c86dea1` (v1.2.37) and `main` was at v1.2.41 when the plan was written and may have moved again; the bump is computed from `origin/main` in Step 1.

- [ ] **Step 1: Merge `main` and read its version**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && git fetch origin main && git merge origin/main
```

Resolve any conflict by keeping both sides' intent. `main` touched `tests/runner.c`, `tests/test_ui_demo.c`, `src/core/ui_theme.{c,h}`, `src/core/ui_demo.{c,h}`, `src/ui/window.c` and `tests/integration/README.md` after this branch was cut, in other places than this plan; a conflict in `tests/runner.c` is the union of both sides' registrations (CRLF preserved: afterwards `grep -c $'\r$' tests/runner.c` equals `wc -l < tests/runner.c`). If `main` added a `--ui-demo` state, `test_ui_demo_states_list_ends_in_all`'s count is this plan's 17 plus main's additions, `"ansi"` must still sit immediately before `"local"`, and `$galleryStates` in `tests/integration/cases/30-ui-demo.ps1` must list every state but `"local"`.

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && make test 2>&1 | grep -E "^\[FAIL\]|^Tests Run"`
Expected: `Failed: 0`. Note the count: the merged tree's baseline plus this plan's 24 tests.

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && git show origin/main:src/ui/resource.h | grep -E 'APP_VERSION(_BINARY)? '`
Expected: two lines, e.g. `#define APP_VERSION        "1.2.41"` and `#define APP_VERSION_BINARY  1,2,41,0`. Call that version M. The new version N is M with the patch raised by one (after 1.2.99 comes 1.3.0, `1,3,0,0`).

- [ ] **Step 2: Bump the version in both files to N**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && python3 - <<'PYEOF'
import re, subprocess
m = re.search(r'#define APP_VERSION\s+"(\d+)\.(\d+)\.(\d+)"',
              subprocess.run(['git', 'show', 'origin/main:src/ui/resource.h'],
                             capture_output=True, text=True, check=True).stdout)
major, minor, patch = (int(x) for x in m.groups())
major, minor, patch = (major, minor + 1, 0) if patch == 99 else (major, minor, patch + 1)
n = '%d.%d.%d' % (major, minor, patch)

path = 'src/ui/resource.h'
raw = open(path, 'rb').read().decode('utf-8')
raw, k1 = re.subn(r'(#define APP_VERSION\s+)"\d+\.\d+\.\d+"', r'\g<1>"%s"' % n, raw)
raw, k2 = re.subn(r'(#define APP_VERSION_BINARY\s+)\d+,\d+,\d+,0', r'\g<1>%d,%d,%d,0' % (major, minor, patch), raw)
assert k1 == 1 and k2 == 1
open(path, 'wb').write(raw.encode('utf-8'))

path = 'README.md'
raw = open(path, 'rb').read().decode('utf-8')
raw, k3 = re.subn(r'(\*\*Version\*\*: v)\d+\.\d+\.\d+', r'\g<1>%s' % n, raw, count=1)
assert k3 == 1
open(path, 'wb').write(raw.encode('utf-8'))
print('version: %s.%s.%s (origin/main) -> %s' % (m.group(1), m.group(2), m.group(3), n))
PYEOF
grep -E 'APP_VERSION(_BINARY)? ' src/ui/resource.h; sed -n 5p README.md
```

Expected: `version: M -> N` with the real numbers, the two `#define` lines showing N, and `**Version**: vN \`. No other commit on this branch bumps the version.

- [ ] **Step 3: Build the release**

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && make clean && make release 2>&1 | tail -5`
Expected: the link of `build/win/nutshell.exe` with no warning or error, then UPX's table ending in a line for `nutshell.exe` and `Packed 1 file.`

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && strings -el build/win/nutshell.exe | grep -m1 -E "^$(sed -n 's/#define APP_VERSION *"\(.*\)"/\1/p' src/ui/resource.h)$"`
Expected: the version string N.

- [ ] **Step 4: Run every test**

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && make test 2>&1 | grep -E "^\[FAIL\]|^Tests Run"`
Expected: `Failed: 0`; the count as in Step 1.

Run: `cd /home/thomas/nutshell-wt/ansi-contrast && wineserver -k; xvfb-run -a make wintest > build/wintest.log 2>&1; grep -E "^\[FAIL\]|Tests Run" build/wintest.log`
Expected: `Failed: 0` in both the pty child and the main run (on this branch before the merge: `[pty child] Tests Run: 7, Failed: 0` and `Tests Run: 9, Failed: 0`; `main`'s merge may add cases). The wintest compiles `src/term/parser.c` with `-Werror -Wconversion`; rerun once if the pty section prints nothing (a Wine start-up flake).

- [ ] **Step 5: Commit and check the gate locally**

```bash
cd /home/thomas/nutshell-wt/ansi-contrast && git status --short && \
N=$(sed -n 's/#define APP_VERSION *"\(.*\)"/\1/p' src/ui/resource.h) && \
git add src/ui/resource.h README.md build/win/nutshell.exe && \
git commit -F - <<EOF
build: v$N for readable ANSI colours on the light themes

Models: claude-sonnet-5 (implementation); claude-opus-5-5 (plan); claude-fable-5-1 (orchestration)

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Cczi6ipaKBtKnozR7vkgFM
EOF
bash .github/scripts/check-version.sh origin/main
```

(The heredoc is unquoted so `$N` expands; nothing else in it starts with `$`.) Expected: the four `check-version: ... = N` lines, the list of changed build inputs, `check-version: base (...) APP_VERSION = M`, and `check-version: PASS (M -> N)`.

`git status --short` must show nothing left over (`build/wintest.log` is ignored as `*.log`; the objects `make release` leaves are ignored as `*.o`).

- [ ] **Step 6: The maintainer runs the harness cases (Windows, 192 DPI)**

After pulling the branch on the Windows box (the committed exe is the one under test):

```powershell
.\tests\integration\Run-Integration.ps1 -HostName tompi -User thomas -KeyPath $HOME\.ssh\thomas -Tier gate -Only helpers_ansi_contrast_floor,helpers_theme_switch_repaints,ui_gallery,helpers_theme_pixel_matches_token
```

Expected: `[PASS]` for all four. `helpers_ansi_contrast_floor`'s detail names the grid (`rows=... cols=... char=WxH origin=(x,y)`) and lines 11, 15 and 16 per theme (Onyx Light `727223`, `6C6C6C`, about `A3A3A4`; Onyx Synapse `FFFF55`, `FFFFFF`, `FFFFFF`); `helpers_theme_switch_repaints` names lines 11 and 15 after the switch; `ui_gallery` writes 64 captures, among them `Onyx-Light-ansi.png` and `Moss-and-Mist-ansi.png` for the eye. A FAIL whose detail says the grid is too small or lines 0/1 are not `000000`/`CC3333` means the sampling, not the colours, is wrong: per the spec's section 6 the harness coverage then degrades to the gallery capture plus the native tests, and that is recorded in the PR body. This run is the maintainer's; the implementer stops at the pushed branch and does not wait on it.

---

## Self-review

Run against the spec after the plan was written.

**Spec coverage.** 5.0 capture: done by the maintainer on 2026-10-20 (PowerShell 5.1, PSReadLine 2.0.0); Task 8 turns it into fixtures and end-to-end tests, and the PS7 prediction into a synthetic test, marked unverified. 5.1 `theme_min_contrast_color()`, shared walk, unchanged `theme_role_color()`: Task 1. 5.2 index stored with casts, `TERM_ATTR_DIM` set by 2 and cleared by 22/0, erase copying DIM: Task 2 (erase-with-DIM blanks are tested in Task 3's `test_term_colors_dim_blanks_are_undimmed`). 5.3 dim at 0.40 after the floor, light themes only: Task 3. 5.4 the resolver and its six-step order, no cache, no NULL fallback in the renderer: Tasks 3 and 7. 5.5 `terminal_min_contrast` per theme, `ThemeTokens.term` built for every theme: Task 4. 5.6 `resolve_cell()` through the resolver, `render.draw` fields, forced repaint after Settings: Task 7. 5.7 data flow: Tasks 2-4 and 7. 5.8 no settings: nothing to do. Section 6 tests: `test_theme.c` (Task 1), `test_term_colors.c` (Tasks 3-4), parser asserts (Task 2, in `test_term.c`), `test_ui_theme.c` (Task 4), `test_ui_demo.c` (Task 5), harness cases and gallery and README (Task 6), end to end on the capture (Task 8). Maintainer decisions 1-4: dark floors 0 (Task 4), default fg on explicit bg raw (Task 3, `test_term_colors_explicit_bg_keeps_raw_pairs`), convergence accepted (no task; already on the notes list), dim off on dark themes (Task 3, `test_term_colors_dim`). Section 7 out-of-scope items: none implemented.

**Placeholder scan.** No step says "TBD", "similar to" or "add tests" without code. One deliberate execution-time value remains, with the rule that fills it: the version N (Task 9, computed by the script from `origin/main`).

**Type consistency.** `theme_min_contrast_color(unsigned int, unsigned int, double)` (Task 1) is what `term_palette_build()` calls (Task 3). `TermPalette`'s fields `default_fg`, `default_bg`, `min_contrast`, `fg_on_default[256]` are the names Tasks 3, 4 and 8 use; `ThemeTokens.term` (Task 4) is what Task 7's `&ns_tokens()->term` and Task 8's `&tok.term` read; `TERM_DRAW_CURSOR`/`TERM_DRAW_SELECTED`/`TERM_DIM_TOWARD_BG` are defined once in `term_colors.h`. `render.draw`'s field order in Task 7 (`char_h char_w cols x y`) is the order Task 6's regex expects.

**Review Focus.** Each of the five lines has its test in the owning task (Tasks 2 and 3). Checked and left out: a theme switch on an inactive tab or while scrolled back (already handled: `on_tab_select()` forces a full repaint and a scrolled-back paint redraws every row, and the native `test_term_colors_theme_switch_recolours_stored_cell` covers the colour side); truecolour and explicit-background readability (out of scope by decision).

**Fixes made during the review.** The `ansi` test returned before reading the buffer when `ui_demo_build()` fails (it would otherwise read an uninitialised buffer in the red run); the Task 6 grep count was corrected to 4; the renderer line references were re-taken from the file; after the capture arrived, Task 8 was rewritten against it: the fixtures' line ranges, byte counts and hashes and every expected colour were taken from running the tests on the decoded bytes; the first draft's `InvalidArgument` prefix length and the synthetic prompt's columns were off by one and two and were corrected before the plan was saved.
