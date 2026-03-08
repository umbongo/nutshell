#include "test_framework.h"
#include "term.h"
#include <string.h>

/* Helper to feed a null-terminated escape sequence into the terminal */
static void feed(Terminal *t, const char *s) {
    term_process(t, s, strlen(s));
}

/* ---- color256_to_rgb palette tests --------------------------------------- */

int test_color256_palette_ansi(void) {
    TEST_BEGIN();
    /* Indices 0-15 must match the ANSI palette (brightened for dark bg) */
    ASSERT_EQ(color256_to_rgb(0),  0x000000u); /* black        */
    ASSERT_EQ(color256_to_rgb(1),  0xCC3333u); /* red          */
    ASSERT_EQ(color256_to_rgb(2),  0x4CB84Cu); /* green        */
    ASSERT_EQ(color256_to_rgb(7),  0xC0C0C0u); /* light grey   */
    ASSERT_EQ(color256_to_rgb(8),  0x808080u); /* dark grey    */
    ASSERT_EQ(color256_to_rgb(9),  0xFF5555u); /* bright red   */
    ASSERT_EQ(color256_to_rgb(10), 0x55FF55u); /* bright green */
    ASSERT_EQ(color256_to_rgb(15), 0xFFFFFFu); /* white        */
    TEST_END();
}

int test_color256_palette_cube(void) {
    TEST_BEGIN();
    /* Index 16: 6×6×6 cube entry (0,0,0) → #000000 */
    ASSERT_EQ(color256_to_rgb(16), 0x000000u);
    /* Index 231: cube entry (5,5,5) → #FFFFFF */
    ASSERT_EQ(color256_to_rgb(231), 0xFFFFFFu);
    /* Index 46: n=30, r=0, g=5, b=0 → gv=55+200=255 → #00FF00 */
    ASSERT_EQ(color256_to_rgb(46), 0x00FF00u);
    /* Index 196: n=180, r=5, g=0, b=0 → rv=255 → #FF0000 */
    ASSERT_EQ(color256_to_rgb(196), 0xFF0000u);
    /* Index 21: n=5, r=0, g=0, b=5 → bv=255 → #0000FF */
    ASSERT_EQ(color256_to_rgb(21), 0x0000FFu);
    TEST_END();
}

int test_color256_palette_gray(void) {
    TEST_BEGIN();
    /* Index 232: v = 8 + 10*0 = 8 → #080808 */
    ASSERT_EQ(color256_to_rgb(232), 0x080808u);
    /* Index 255: v = 8 + 10*23 = 238 = 0xEE → #EEEEEE */
    ASSERT_EQ(color256_to_rgb(255), 0xEEEEEEu);
    /* Index 244 (mid-ramp): v = 8 + 10*12 = 128 → #808080 */
    ASSERT_EQ(color256_to_rgb(244), 0x808080u);
    TEST_END();
}

/* ---- SGR 38;5;n — 256-colour foreground ---------------------------------- */

int test_color_256_fg(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[38;5;196m");
    ASSERT_EQ(t->current_attr.fg_mode,  COLOR_256);
    ASSERT_EQ(t->current_attr.fg_index, 196);
    ASSERT_EQ(t->current_attr.fg,       0xFF0000u); /* bright red */

    term_free(t);
    TEST_END();
}

/* ---- SGR 48;5;n — 256-colour background ---------------------------------- */

int test_color_256_bg(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[48;5;21m");
    ASSERT_EQ(t->current_attr.bg_mode,  COLOR_256);
    ASSERT_EQ(t->current_attr.bg_index, 21);
    ASSERT_EQ(t->current_attr.bg,       0x0000FFu); /* blue */

    term_free(t);
    TEST_END();
}

/* ---- SGR 38;2;r;g;b — truecolor foreground ------------------------------- */

int test_color_rgb_fg(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[38;2;255;128;0m");
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_RGB);
    ASSERT_EQ(t->current_attr.fg,      0xFF8000u);

    term_free(t);
    TEST_END();
}

/* ---- SGR 48;2;r;g;b — truecolor background ------------------------------- */

int test_color_rgb_bg(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[48;2;10;20;30m");
    ASSERT_EQ(t->current_attr.bg_mode, COLOR_RGB);
    ASSERT_EQ(t->current_attr.bg, (10u << 16) | (20u << 8) | 30u);

    term_free(t);
    TEST_END();
}

/* ---- SGR 0 resets to COLOR_DEFAULT --------------------------------------- */

int test_color_sgr_reset(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    /* Set 256-colour fg, then reset */
    feed(t, "\033[38;5;82m");
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_256);

    feed(t, "\033[0m");
    ASSERT_EQ(t->current_attr.fg_mode,  COLOR_DEFAULT);
    ASSERT_EQ(t->current_attr.fg_index, 0);
    ASSERT_EQ(t->current_attr.bg_mode,  COLOR_DEFAULT);
    ASSERT_EQ(t->current_attr.fg,       0u);

    term_free(t);
    TEST_END();
}

/* ---- SGR 39 / 49 restore COLOR_DEFAULT fg / bg --------------------------- */

int test_color_sgr_default_fg(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    /* Set explicit fg, then restore default */
    feed(t, "\033[31m");   /* red */
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_ANSI16);
    feed(t, "\033[39m");   /* default fg */
    ASSERT_EQ(t->current_attr.fg_mode,  COLOR_DEFAULT);
    ASSERT_EQ(t->current_attr.fg_index, 0);

    term_free(t);
    TEST_END();
}

int test_color_sgr_default_bg(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    /* Set explicit bg, then restore default */
    feed(t, "\033[41m");   /* red bg */
    ASSERT_EQ(t->current_attr.bg_mode, COLOR_ANSI16);
    feed(t, "\033[49m");   /* default bg */
    ASSERT_EQ(t->current_attr.bg_mode,  COLOR_DEFAULT);
    ASSERT_EQ(t->current_attr.bg_index, 0);

    term_free(t);
    TEST_END();
}

/* ---- Mixed SGR in one sequence ------------------------------------------ */

int test_color_mixed_sgr(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    /* Bold + 256-colour fg (index 82) + truecolor bg (0, 0, 128) */
    feed(t, "\033[1;38;5;82;48;2;0;0;128m");
    ASSERT_TRUE(t->current_attr.flags & TERM_ATTR_BOLD);
    ASSERT_EQ(t->current_attr.fg_mode,  COLOR_256);
    ASSERT_EQ(t->current_attr.fg_index, 82);
    ASSERT_EQ(t->current_attr.fg, color256_to_rgb(82));
    ASSERT_EQ(t->current_attr.bg_mode, COLOR_RGB);
    ASSERT_EQ(t->current_attr.bg, 128u); /* 0x000080 */

    term_free(t);
    TEST_END();
}

/* ---- 256-colour overrides classic SGR without resetting other attrs ------ */

int test_color_256_interleaved(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[1m");           /* bold */
    feed(t, "\033[31m");          /* ANSI red fg */
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_ANSI16);

    feed(t, "\033[38;5;200m");   /* 256-colour fg overrides red */
    ASSERT_EQ(t->current_attr.fg_mode,  COLOR_256);
    ASSERT_EQ(t->current_attr.fg_index, 200);
    /* Bold must still be set */
    ASSERT_TRUE(t->current_attr.flags & TERM_ATTR_BOLD);

    term_free(t);
    TEST_END();
}

/* ---- Negative: SGR 38;5 without index param ------------------------------ */

int test_color_256_missing_param(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    uint32_t saved_fg = t->current_attr.fg;
    ColorMode saved_mode = t->current_attr.fg_mode;

    /* \033[38;5m — index param absent; must not crash or change fg */
    feed(t, "\033[38;5m");
    ASSERT_EQ(t->current_attr.fg,      saved_fg);
    ASSERT_EQ(t->current_attr.fg_mode, saved_mode);

    term_free(t);
    TEST_END();
}

/* ---- Negative: SGR 38;2 with only two sub-params ------------------------- */

int test_color_rgb_short_param(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    uint32_t saved_fg = t->current_attr.fg;
    ColorMode saved_mode = t->current_attr.fg_mode;

    /* Missing blue component; must not crash or change fg */
    feed(t, "\033[38;2;255;128m");
    ASSERT_EQ(t->current_attr.fg,      saved_fg);
    ASSERT_EQ(t->current_attr.fg_mode, saved_mode);

    term_free(t);
    TEST_END();
}

/* ---- Negative: 256-colour index out of range ----------------------------- */

int test_color_256_oob(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    uint32_t saved_fg = t->current_attr.fg;

    /* Index 256 is invalid; must be ignored */
    feed(t, "\033[38;5;256m");
    ASSERT_EQ(t->current_attr.fg, saved_fg);

    term_free(t);
    TEST_END();
}

/* ---- Corner: truecolor with all-zero RGB (black) ------------------------- */

int test_color_rgb_black(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[38;2;0;0;0m");
    /* Mode must be COLOR_RGB (distinguishes explicit black from default) */
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_RGB);
    ASSERT_EQ(t->current_attr.fg, 0u);

    term_free(t);
    TEST_END();
}

/* ---- Corner: truecolor with max RGB (white) ------------------------------ */

int test_color_rgb_white(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[38;2;255;255;255m");
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_RGB);
    ASSERT_EQ(t->current_attr.fg, 0xFFFFFFu);

    term_free(t);
    TEST_END();
}
