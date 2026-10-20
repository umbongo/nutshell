#include "test_framework.h"
#include "theme.h"
#include <math.h>

/* Helper: absolute difference ≤ tol */
#define ASSERT_NEAR(a, b, tol) \
    ASSERT_TRUE(fabs((a) - (b)) <= (tol))

/* ---- theme_is_dark -------------------------------------------------------- */

int test_theme_dark_background(void)
{
    TEST_BEGIN();
    /* #0C0C0C — near-black, L ≈ 0.002 */
    ASSERT_TRUE(theme_is_dark(0x0C0C0C) == 1);
    TEST_END();
}

int test_theme_light_background(void)
{
    TEST_BEGIN();
    /* #F2F2F2 — near-white, L ≈ 0.894 */
    ASSERT_TRUE(theme_is_dark(0xF2F2F2) == 0);
    TEST_END();
}

int test_theme_mid_gray(void)
{
    TEST_BEGIN();
    /* #808080 — mid-gray, L ≈ 0.216 → dark */
    ASSERT_TRUE(theme_is_dark(0x808080) == 1);
    TEST_END();
}

/* ---- theme_luminance formula --------------------------------------------- */

int test_theme_luminance_red(void)
{
    TEST_BEGIN();
    /* Pure red: L ≈ 0.2126 (ITU-R BT.709 coefficient for R) */
    ASSERT_NEAR(theme_luminance(0xFF0000), 0.2126, 0.001);
    TEST_END();
}

int test_theme_luminance_green(void)
{
    TEST_BEGIN();
    /* Pure green: L ≈ 0.7152 */
    ASSERT_NEAR(theme_luminance(0x00FF00), 0.7152, 0.001);
    TEST_END();
}

int test_theme_luminance_blue(void)
{
    TEST_BEGIN();
    /* Pure blue: L ≈ 0.0722 */
    ASSERT_NEAR(theme_luminance(0x0000FF), 0.0722, 0.001);
    TEST_END();
}

/* ---- Corner cases -------------------------------------------------------- */

int test_theme_pure_black(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(theme_is_dark(0x000000) == 1);
    ASSERT_NEAR(theme_luminance(0x000000), 0.0, 0.0001);
    TEST_END();
}

int test_theme_pure_white(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(theme_is_dark(0xFFFFFF) == 0);
    ASSERT_NEAR(theme_luminance(0xFFFFFF), 1.0, 0.001);
    TEST_END();
}

/* ---- theme_contrast ------------------------------------------------------- */

int test_theme_contrast_black_white(void)
{
    TEST_BEGIN();
    /* Max possible WCAG contrast ratio is 21:1 */
    ASSERT_NEAR(theme_contrast(0x000000, 0xFFFFFF), 21.0, 0.01);
    TEST_END();
}

int test_theme_contrast_identical(void)
{
    TEST_BEGIN();
    ASSERT_NEAR(theme_contrast(0x808080, 0x808080), 1.0, 0.0001);
    TEST_END();
}

int test_theme_contrast_symmetric(void)
{
    TEST_BEGIN();
    /* Order of arguments must not matter */
    double a = theme_contrast(0x123456, 0xFEDCBA);
    double b = theme_contrast(0xFEDCBA, 0x123456);
    ASSERT_NEAR(a, b, 0.0001);
    TEST_END();
}

/* ---- theme_lstar ------------------------------------------------------------ */

int test_theme_lstar_black_is_zero(void)
{
    TEST_BEGIN();
    ASSERT_NEAR(theme_lstar(0x000000), 0.0, 0.001);
    TEST_END();
}

int test_theme_lstar_white_is_100(void)
{
    TEST_BEGIN();
    ASSERT_NEAR(theme_lstar(0xFFFFFF), 100.0, 0.001);
    TEST_END();
}

int test_theme_lstar_mid_gray(void)
{
    TEST_BEGIN();
    /* #808080: Y ~= 0.2159 -> L* ~= 53.59 (standard sRGB mid-gray result) */
    ASSERT_NEAR(theme_lstar(0x808080), 53.59, 0.05);
    TEST_END();
}

int test_theme_lstar_monotonic(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(theme_lstar(0x000000) < theme_lstar(0x404040));
    ASSERT_TRUE(theme_lstar(0x404040) < theme_lstar(0x808080));
    ASSERT_TRUE(theme_lstar(0x808080) < theme_lstar(0xC0C0C0));
    ASSERT_TRUE(theme_lstar(0xC0C0C0) < theme_lstar(0xFFFFFF));
    TEST_END();
}

/* ---- theme_shift_lightness -------------------------------------------------- */

int test_theme_shift_lightness_up_increases(void)
{
    TEST_BEGIN();
    unsigned int base = 0x404040;
    unsigned int shifted = theme_shift_lightness(base, 6.0);
    ASSERT_TRUE(theme_lstar(shifted) > theme_lstar(base));
    TEST_END();
}

int test_theme_shift_lightness_down_decreases(void)
{
    TEST_BEGIN();
    unsigned int base = 0xA0A0A0;
    unsigned int shifted = theme_shift_lightness(base, -6.0);
    ASSERT_TRUE(theme_lstar(shifted) < theme_lstar(base));
    TEST_END();
}

int test_theme_shift_lightness_moves_by_requested_amount(void)
{
    TEST_BEGIN();
    /* Away from the clamp bounds, the L* delta should land close to what
     * was requested (before whole-integer channel rounding). */
    unsigned int base = 0x404040;
    unsigned int shifted = theme_shift_lightness(base, 6.0);
    ASSERT_NEAR(theme_lstar(shifted) - theme_lstar(base), 6.0, 0.5);
    TEST_END();
}

int test_theme_shift_lightness_never_clips_black(void)
{
    TEST_BEGIN();
    /* Repeated large downward shifts from near-black must never reach
     * pure black on any channel. */
    unsigned int c = 0x040404;
    for (int i = 0; i < 10; i++) {
        c = theme_shift_lightness(c, -50.0);
        unsigned int r = (c >> 16) & 0xFFu, g = (c >> 8) & 0xFFu, b = c & 0xFFu;
        ASSERT_TRUE(r != 0 && g != 0 && b != 0);
        ASSERT_TRUE(c != 0x000000);
    }
    TEST_END();
}

int test_theme_shift_lightness_never_clips_white(void)
{
    TEST_BEGIN();
    unsigned int c = 0xFBFBFB;
    for (int i = 0; i < 10; i++) {
        c = theme_shift_lightness(c, 50.0);
        unsigned int r = (c >> 16) & 0xFFu, g = (c >> 8) & 0xFFu, b = c & 0xFFu;
        ASSERT_TRUE(r != 255 && g != 255 && b != 255);
        ASSERT_TRUE(c != 0xFFFFFF);
    }
    TEST_END();
}

int test_theme_shift_lightness_bounds(void)
{
    TEST_BEGIN();
    /* Extreme shifts must land within the documented whole-colour bounds */
    unsigned int lightened = theme_shift_lightness(0x000000, 500.0);
    unsigned int darkened  = theme_shift_lightness(0xFFFFFF, -500.0);
    ASSERT_TRUE(lightened <= 0xF7F7F7);
    ASSERT_TRUE(darkened  >= 0x080808);
    TEST_END();
}

int test_theme_shift_lightness_zero_delta_noop(void)
{
    TEST_BEGIN();
    unsigned int base = 0x808080;
    unsigned int shifted = theme_shift_lightness(base, 0.0);
    ASSERT_NEAR(theme_lstar(shifted), theme_lstar(base), 0.5);
    TEST_END();
}

/* ---- theme_blend ----------------------------------------------------------- */

int test_theme_blend_t0_is_a(void)
{
    TEST_BEGIN();
    ASSERT_EQ((int)theme_blend(0x102030, 0xF0E0D0, 0.0), (int)0x102030);
    TEST_END();
}

int test_theme_blend_t1_is_b(void)
{
    TEST_BEGIN();
    ASSERT_EQ((int)theme_blend(0x102030, 0xF0E0D0, 1.0), (int)0xF0E0D0);
    TEST_END();
}

int test_theme_blend_midpoint(void)
{
    TEST_BEGIN();
    unsigned int mid = theme_blend(0x000000, 0xFFFFFF, 0.5);
    unsigned int r = (mid >> 16) & 0xFFu, g = (mid >> 8) & 0xFFu, b = mid & 0xFFu;
    /* Rounding to nearest int: 127 or 128 */
    ASSERT_TRUE(r == 127 || r == 128);
    ASSERT_TRUE(g == 127 || g == 128);
    ASSERT_TRUE(b == 127 || b == 128);
    TEST_END();
}

int test_theme_blend_clamps_t(void)
{
    TEST_BEGIN();
    ASSERT_EQ((int)theme_blend(0x102030, 0xF0E0D0, -1.0), (int)0x102030);
    ASSERT_EQ((int)theme_blend(0x102030, 0xF0E0D0, 2.0), (int)0xF0E0D0);
    TEST_END();
}

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
