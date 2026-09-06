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
