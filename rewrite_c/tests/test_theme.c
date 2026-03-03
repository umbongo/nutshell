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
