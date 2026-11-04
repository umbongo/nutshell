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
    ASSERT_EQ(t->current_attr.flags,   0);   /* the 5 is not a BLINK */

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
    ASSERT_EQ(t->current_attr.flags,   0);   /* the 2 is not a DIM */

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

/* ---- Truncated / malformed extended colours leak nothing ----------------- */

/* Asserts the terminal's current attribute is the untouched default. */
#define ASSERT_ATTR_DEFAULT(t) do { \
    ASSERT_EQ((t)->current_attr.flags,    0); \
    ASSERT_EQ((t)->current_attr.fg_mode,  COLOR_DEFAULT); \
    ASSERT_EQ((t)->current_attr.bg_mode,  COLOR_DEFAULT); \
} while (0)

int test_color_truncated_rgb_sets_nothing(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[38;2;255;128m");
    ASSERT_ATTR_DEFAULT(t);          /* no DIM from the 2, no colour */
    feed(t, "\033[48;2;1;2m");
    ASSERT_ATTR_DEFAULT(t);

    term_free(t);
    TEST_END();
}

int test_color_truncated_256_sets_nothing(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[38;5m");
    ASSERT_ATTR_DEFAULT(t);          /* no BLINK from the 5 */
    feed(t, "\033[48;5m");
    ASSERT_ATTR_DEFAULT(t);
    feed(t, "\033[38m");             /* a bare trailing 38 is a no-op */
    ASSERT_ATTR_DEFAULT(t);
    feed(t, "\033[48m");
    ASSERT_ATTR_DEFAULT(t);

    term_free(t);
    TEST_END();
}

int test_color_unknown_selector_consumes_rest(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[38;1m");           /* selector 1 is no bold */
    ASSERT_ATTR_DEFAULT(t);
    feed(t, "\033[38;3;1;2;3;1m");   /* the whole tail is consumed */
    ASSERT_ATTR_DEFAULT(t);
    feed(t, "\033[48;0;1;4;5;7m");
    ASSERT_ATTR_DEFAULT(t);

    term_free(t);
    TEST_END();
}

int test_color_truncated_keeps_earlier_attribute(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[1;38;5m");         /* bold, then a truncated colour */
    ASSERT_EQ(t->current_attr.flags,   TERM_ATTR_BOLD);
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_DEFAULT);

    term_free(t);
    TEST_END();
}

int test_color_valid_colour_does_not_overeat(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[38;5;196;1m");
    ASSERT_EQ(t->current_attr.fg_mode,  COLOR_256);
    ASSERT_EQ(t->current_attr.fg_index, 196);
    ASSERT_EQ(t->current_attr.flags,    TERM_ATTR_BOLD);

    feed(t, "\033[0m\033[48;2;1;2;3;4m");
    ASSERT_EQ(t->current_attr.bg_mode, COLOR_RGB);
    ASSERT_EQ(t->current_attr.bg,      0x010203u);
    ASSERT_EQ(t->current_attr.flags,   TERM_ATTR_UNDERLINE);

    term_free(t);
    TEST_END();
}

/* ---- More than 16 parameters: the overflow is dropped, not merged -------- */

int test_color_cap_truncated_rgb_at_end(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    /* 38 sits at index 14: only 2 and 255 fit in the 16 kept parameters. */
    feed(t, "\033[1;1;1;1;1;1;1;1;1;1;1;1;1;1;38;2;255;0;0m");
    ASSERT_EQ(t->current_attr.flags,   TERM_ATTR_BOLD);
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_DEFAULT);

    term_free(t);
    TEST_END();
}

int test_color_cap_overflow_digits_do_not_merge(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    /* 17 parameters: the 17th (31) must not merge into the 16th (0). */
    feed(t, "\033[0;0;0;0;0;0;0;0;0;0;0;0;0;0;0;0;31m");
    ASSERT_ATTR_DEFAULT(t);

    /* The flag is reset for the next sequence. */
    feed(t, "\033[31m");
    ASSERT_EQ(t->current_attr.fg_mode,  COLOR_ANSI16);
    ASSERT_EQ(t->current_attr.fg_index, 1);

    term_free(t);
    TEST_END();
}

int test_color_cap_first_sixteen_still_apply(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[0;0;0;0;0;0;0;0;0;0;0;0;0;0;0;1;4;7m");
    ASSERT_EQ(t->current_attr.flags, TERM_ATTR_BOLD);

    term_free(t);
    TEST_END();
}

/* ---- Colon sub-parameters ------------------------------------------------ */

int test_color_colon_rgb_with_colourspace(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[38:2::255:0:0m");
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_RGB);
    ASSERT_EQ(t->current_attr.fg,      0xFF0000u);
    ASSERT_EQ(t->current_attr.flags,   0);

    feed(t, "\033[48:2::0:0:255m");
    ASSERT_EQ(t->current_attr.bg_mode, COLOR_RGB);
    ASSERT_EQ(t->current_attr.bg,      0x0000FFu);
    ASSERT_EQ(t->current_attr.flags,   0);

    term_free(t);
    TEST_END();
}

int test_color_colon_rgb_without_colourspace(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[38:2:255:0:0m");
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_RGB);
    ASSERT_EQ(t->current_attr.fg,      0xFF0000u);
    ASSERT_EQ(t->current_attr.flags,   0);

    /* Mixed form is read as the colon form. */
    feed(t, "\033[48;2:1:2:3m");
    ASSERT_EQ(t->current_attr.bg_mode, COLOR_RGB);
    ASSERT_EQ(t->current_attr.bg,      0x010203u);

    term_free(t);
    TEST_END();
}

int test_color_colon_256(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[38:5:196m");
    ASSERT_EQ(t->current_attr.fg_mode,  COLOR_256);
    ASSERT_EQ(t->current_attr.fg_index, 196);
    ASSERT_EQ(t->current_attr.flags,    0);

    feed(t, "\033[48:5:21;1m");      /* a colon run ends at its own ';' */
    ASSERT_EQ(t->current_attr.bg_mode,  COLOR_256);
    ASSERT_EQ(t->current_attr.bg_index, 21);
    ASSERT_EQ(t->current_attr.flags,    TERM_ATTR_BOLD);

    term_free(t);
    TEST_END();
}

int test_color_colon_underline_style_is_underline_only(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[4:3m");
    ASSERT_EQ(t->current_attr.flags,   TERM_ATTR_UNDERLINE);
    ASSERT_EQ(t->current_attr.bg_mode, COLOR_DEFAULT);   /* not "43" */
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_DEFAULT);

    feed(t, "\033[0m\033[4:3;31m");  /* what follows the run still applies */
    ASSERT_EQ(t->current_attr.flags,    TERM_ATTR_UNDERLINE);
    ASSERT_EQ(t->current_attr.fg_mode,  COLOR_ANSI16);
    ASSERT_EQ(t->current_attr.fg_index, 1);

    term_free(t);
    TEST_END();
}

int test_color_colon_underline_colour_is_noop(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[58:2::1:2:3m");
    ASSERT_ATTR_DEFAULT(t);
    feed(t, "\033[58;5;196m");
    ASSERT_ATTR_DEFAULT(t);

    term_free(t);
    TEST_END();
}

int test_color_colon_short_run_is_noop_and_ends_at_semicolon(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);

    feed(t, "\033[38:2:1m");
    ASSERT_ATTR_DEFAULT(t);

    feed(t, "\033[38:2:1;1m");       /* only the colon run is consumed */
    ASSERT_EQ(t->current_attr.flags,   TERM_ATTR_BOLD);
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_DEFAULT);

    term_free(t);
    TEST_END();
}

int test_color_underline_style_zero_clears(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    feed(t, "\033[4:0m");
    ASSERT_EQ(t->current_attr.flags & TERM_ATTR_UNDERLINE, 0);
    feed(t, "\033[4m");
    ASSERT_TRUE((t->current_attr.flags & TERM_ATTR_UNDERLINE) != 0);
    feed(t, "\033[4:0m");
    ASSERT_EQ(t->current_attr.flags & TERM_ATTR_UNDERLINE, 0);
    feed(t, "\033[4:1m");
    ASSERT_TRUE((t->current_attr.flags & TERM_ATTR_UNDERLINE) != 0);
    feed(t, "\033[4:0m");
    feed(t, "\033[4:3m");
    ASSERT_TRUE((t->current_attr.flags & TERM_ATTR_UNDERLINE) != 0);
    feed(t, "\033[0m");
    feed(t, "\033[4:0;1m");
    ASSERT_EQ(t->current_attr.flags, TERM_ATTR_BOLD);
    term_free(t);
    TEST_END();
}

int test_color_empty_first_param_before_semicolon_is_reset(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    feed(t, "\033[31m");
    feed(t, "\033[;1m");
    ASSERT_EQ(t->current_attr.flags, TERM_ATTR_BOLD);
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_DEFAULT);
    term_free(t);
    TEST_END();
}

int test_color_colon_rgb_split_across_calls(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    feed(t, "\033[38:2::25");
    feed(t, "5:0:0m");
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_RGB);
    ASSERT_EQ(t->current_attr.fg, 0xFF0000u);
    term_free(t);
    TEST_END();
}

int test_color_colon_rgb_byte_at_a_time(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    const char *s = "\033[1;38:2::1:2:3;4m";
    for (size_t i = 0; s[i]; i++) term_process(t, s + i, 1);
    ASSERT_EQ(t->current_attr.flags, TERM_ATTR_BOLD | TERM_ATTR_UNDERLINE);
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_RGB);
    ASSERT_EQ(t->current_attr.fg, 0x010203u);
    term_free(t);
    TEST_END();
}

int test_color_colon_256_out_of_range_is_ignored(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    feed(t, "\033[38:5:300;1m");
    ASSERT_EQ(t->current_attr.flags, TERM_ATTR_BOLD);
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_DEFAULT);
    term_free(t);
    TEST_END();
}

int test_color_mixed_semicolon_colon_256(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    feed(t, "\033[38;5:196m");
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_256);
    ASSERT_EQ(t->current_attr.fg_index, 196);
    term_free(t);
    TEST_END();
}

int test_color_colon_rgb_trailing_extra_params(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    feed(t, "\033[38:2:1:2:3:4:5;1m");
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_RGB);
    ASSERT_EQ(t->current_attr.fg, 0x020304u);
    ASSERT_EQ(t->current_attr.flags, TERM_ATTR_BOLD);
    term_free(t);
    TEST_END();
}

int test_color_bold_colon_rgb_underline_one_feed(void) {
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    feed(t, "\033[1;38:2::1:2:3;4m");
    ASSERT_EQ(t->current_attr.flags, TERM_ATTR_BOLD | TERM_ATTR_UNDERLINE);
    ASSERT_EQ(t->current_attr.fg_mode, COLOR_RGB);
    ASSERT_EQ(t->current_attr.fg, 0x010203u);
    term_free(t);
    TEST_END();
}
