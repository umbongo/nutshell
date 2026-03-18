#include "test_framework.h"
#include "menubar_line.h"

/* The menu bar bottom border line should use the theme's border color
 * so it's a subtle but visible separator between menu bar and tab bar. */

int test_menubar_line_matches_border(void) {
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *t = ui_theme_get(i);
        unsigned int color = menubar_line_color(t, 0xF0F0F0);
        ASSERT_EQ(color, t->border);
    }
    TEST_END();
}

int test_menubar_line_null_theme_uses_fallback(void) {
    TEST_BEGIN();
    unsigned int color = menubar_line_color(NULL, 0xABCDEF);
    ASSERT_EQ(color, 0xABCDEFu);
    TEST_END();
}

int test_menubar_line_dark_theme_is_dark(void) {
    TEST_BEGIN();
    /* Onyx Synapse (dark) — line color should be dark, not bright */
    const ThemeColors *t = ui_theme_get(0);
    unsigned int color = menubar_line_color(t, 0);
    /* border for Onyx Synapse is 0x2A2A2A — dark but visible */
    unsigned int r = (color >> 16) & 0xFF;
    unsigned int g = (color >>  8) & 0xFF;
    unsigned int b =  color        & 0xFF;
    /* Luminance should be low (< 80/255) for dark themes */
    ASSERT_TRUE(r < 80 && g < 80 && b < 80);
    TEST_END();
}

int test_menubar_line_light_theme_is_light(void) {
    TEST_BEGIN();
    /* Onyx Light (light) — line color should be light */
    const ThemeColors *t = ui_theme_get(1);
    unsigned int color = menubar_line_color(t, 0);
    unsigned int r = (color >> 16) & 0xFF;
    unsigned int g = (color >>  8) & 0xFF;
    unsigned int b =  color        & 0xFF;
    /* Should be bright (> 200/255) for light themes */
    ASSERT_TRUE(r > 200 && g > 200 && b > 200);
    TEST_END();
}

int test_menubar_line_rect_position(void) {
    TEST_BEGIN();
    int x, y, w, h;
    /* Window at (100, 50), client starts at y=90, window 800px wide */
    menubar_line_rect(100, 50, 90, 800, &x, &y, &w, &h);
    ASSERT_EQ(x, 100);
    ASSERT_EQ(y, 89);   /* 1px above client area */
    ASSERT_EQ(w, 800);
    ASSERT_EQ(h, 1);
    TEST_END();
}

int test_menubar_line_rect_null_safe(void) {
    TEST_BEGIN();
    /* Should not crash with NULL output pointers */
    menubar_line_rect(0, 0, 40, 640, NULL, NULL, NULL, NULL);
    /* Just verify no crash */
    ASSERT_TRUE(1);
    TEST_END();
}
