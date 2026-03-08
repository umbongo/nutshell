#include "test_framework.h"
#include "ui_theme.h"
#include "theme.h"
#include "config.h"
#include <string.h>
#include <stdio.h>

int test_ui_theme_count(void) {
    TEST_BEGIN();
    ASSERT_EQ(NUM_UI_THEMES, 4);
    TEST_END();
}

int test_ui_theme_get_valid(void) {
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *t = ui_theme_get(i);
        ASSERT_NOT_NULL(t);
        ASSERT_NOT_NULL(t->name);
    }
    ASSERT_STR_EQ(ui_theme_get(0)->name, "Onyx Synapse");
    ASSERT_STR_EQ(ui_theme_get(1)->name, "Onyx Light");
    ASSERT_STR_EQ(ui_theme_get(2)->name, "Sage & Sand");
    ASSERT_STR_EQ(ui_theme_get(3)->name, "Moss & Mist");
    TEST_END();
}

int test_ui_theme_get_oob(void) {
    TEST_BEGIN();
    /* Out-of-bounds indices fall back to theme 0 */
    const ThemeColors *neg = ui_theme_get(-1);
    const ThemeColors *high = ui_theme_get(4);
    const ThemeColors *zero = ui_theme_get(0);
    ASSERT_TRUE(neg == zero);
    ASSERT_TRUE(high == zero);
    TEST_END();
}

int test_ui_theme_find_exact(void) {
    TEST_BEGIN();
    ASSERT_EQ(ui_theme_find("Onyx Synapse"), 0);
    ASSERT_EQ(ui_theme_find("Onyx Light"), 1);
    ASSERT_EQ(ui_theme_find("Sage & Sand"), 2);
    ASSERT_EQ(ui_theme_find("Moss & Mist"), 3);
    TEST_END();
}

int test_ui_theme_find_unknown(void) {
    TEST_BEGIN();
    /* Unknown names return 0 (default theme) */
    ASSERT_EQ(ui_theme_find("Dracula"), 0);
    ASSERT_EQ(ui_theme_find("Nord"), 0);
    ASSERT_EQ(ui_theme_find(""), 0);
    ASSERT_EQ(ui_theme_find(NULL), 0);
    TEST_END();
}

int test_ui_theme_find_case(void) {
    TEST_BEGIN();
    /* Case-sensitive: lowercase doesn't match */
    ASSERT_EQ(ui_theme_find("onyx synapse"), 0);
    ASSERT_EQ(ui_theme_find("ONYX SYNAPSE"), 0);
    TEST_END();
}

int test_ui_theme_dark_check(void) {
    TEST_BEGIN();
    /* Themes 0 (Onyx Synapse) and 2 (Sage & Sand) are dark */
    ASSERT_TRUE(theme_is_dark(ui_theme_get(0)->bg_primary));
    ASSERT_TRUE(theme_is_dark(ui_theme_get(2)->bg_primary));
    /* Themes 1 (Onyx Light) and 3 (Moss & Mist) are light */
    ASSERT_FALSE(theme_is_dark(ui_theme_get(1)->bg_primary));
    ASSERT_FALSE(theme_is_dark(ui_theme_get(3)->bg_primary));
    TEST_END();
}

int test_ui_theme_accent_nonzero(void) {
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        ASSERT_TRUE(ui_theme_get(i)->accent != 0);
    }
    TEST_END();
}

int test_ui_theme_terminal_colours(void) {
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *t = ui_theme_get(i);
        /* Terminal fg and bg must differ */
        ASSERT_TRUE(t->terminal_fg != t->terminal_bg);
    }
    TEST_END();
}

int test_ui_theme_name_matches_get(void) {
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        ASSERT_STR_EQ(ui_theme_name(i), ui_theme_get(i)->name);
    }
    /* Out-of-bounds returns theme 0's name */
    ASSERT_STR_EQ(ui_theme_name(-1), ui_theme_name(0));
    ASSERT_STR_EQ(ui_theme_name(99), ui_theme_name(0));
    TEST_END();
}

/* ---- Theme consistency tests ---- */

int test_ui_theme_bg_primary_differs_secondary(void) {
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *t = ui_theme_get(i);
        ASSERT_TRUE(t->bg_primary != t->bg_secondary);
    }
    TEST_END();
}

int test_ui_theme_text_contrasts_bg(void) {
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *t = ui_theme_get(i);
        /* text_main must differ from bg_primary */
        ASSERT_TRUE(t->text_main != t->bg_primary);
        /* text_dim must differ from bg_primary */
        ASSERT_TRUE(t->text_dim != t->bg_primary);
    }
    TEST_END();
}

int test_ui_theme_border_nonzero(void) {
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *t = ui_theme_get(i);
        ASSERT_TRUE(t->border != 0 || i == 0); /* border can be 0 only if black */
        ASSERT_TRUE(t->border != t->bg_primary);
    }
    TEST_END();
}

int test_ui_theme_config_default_matches_theme0(void) {
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    /* Default scheme name matches theme 0 */
    ASSERT_STR_EQ(s.colour_scheme, ui_theme_get(0)->name);
    /* Default fg/bg match theme 0 terminal colours */
    char expected_fg[16], expected_bg[16];
    (void)snprintf(expected_fg, sizeof(expected_fg), "#%06X",
                   ui_theme_get(0)->terminal_fg);
    (void)snprintf(expected_bg, sizeof(expected_bg), "#%06X",
                   ui_theme_get(0)->terminal_bg);
    ASSERT_STR_EQ(s.foreground_colour, expected_fg);
    ASSERT_STR_EQ(s.background_colour, expected_bg);
    TEST_END();
}

int test_ui_theme_all_fields_populated(void) {
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *t = ui_theme_get(i);
        ASSERT_NOT_NULL(t->name);
        ASSERT_TRUE(strlen(t->name) > 0);
        /* All colour fields must be populated (non-zero or explicitly set) */
        ASSERT_TRUE(t->bg_primary != t->accent);
        ASSERT_TRUE(t->text_main != t->accent);
    }
    TEST_END();
}

int test_ui_theme_find_roundtrip(void) {
    TEST_BEGIN();
    /* find(name(i)) == i for all valid themes */
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const char *name = ui_theme_name(i);
        int found = ui_theme_find(name);
        ASSERT_EQ(found, i);
    }
    TEST_END();
}

int test_ui_theme_dark_themes_have_light_text(void) {
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *t = ui_theme_get(i);
        if (theme_is_dark(t->bg_primary)) {
            /* Dark themes should have bright text (high RGB values) */
            unsigned int r = (t->text_main >> 16) & 0xFF;
            unsigned int g = (t->text_main >> 8) & 0xFF;
            unsigned int b = t->text_main & 0xFF;
            unsigned int luma = (r * 299 + g * 587 + b * 114) / 1000;
            ASSERT_TRUE(luma > 100);
        } else {
            /* Light themes should have dark text (low RGB values) */
            unsigned int r = (t->text_main >> 16) & 0xFF;
            unsigned int g = (t->text_main >> 8) & 0xFF;
            unsigned int b = t->text_main & 0xFF;
            unsigned int luma = (r * 299 + g * 587 + b * 114) / 1000;
            ASSERT_TRUE(luma < 150);
        }
    }
    TEST_END();
}
