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
    for (int i = 0; i < 256; i++) {
        ASSERT_TRUE(theme_contrast(ol.fg_on_default[i], OL_BG) >= 4.5);
    }
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
    for (int i = 0; i < 256; i++) {
        ASSERT_EQ(os.fg_on_default[i], color256_to_rgb((uint8_t)i));
    }

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
