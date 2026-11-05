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

/* ---- Theme switch tests (pure logic behind ai_chat_set_theme) ---- */

int test_ui_theme_switch_changes_pointer(void) {
    TEST_BEGIN();
    /* Switching from one scheme to another yields a different theme pointer */
    const ThemeColors *a = ui_theme_get(ui_theme_find("Onyx Synapse"));
    const ThemeColors *b = ui_theme_get(ui_theme_find("Onyx Light"));
    ASSERT_TRUE(a != b);
    ASSERT_TRUE(a->bg_primary != b->bg_primary);
    TEST_END();
}

int test_ui_theme_switch_all_pairs_differ(void) {
    TEST_BEGIN();
    /* Every pair of themes must produce distinct bg_primary colors */
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        for (int j = i + 1; j < NUM_UI_THEMES; j++) {
            const ThemeColors *a = ui_theme_get(i);
            const ThemeColors *b = ui_theme_get(j);
            ASSERT_TRUE(a->bg_primary != b->bg_primary);
        }
    }
    TEST_END();
}

int test_ui_theme_switch_roundtrip(void) {
    TEST_BEGIN();
    /* Switch away and back: same pointer returned */
    const ThemeColors *orig = ui_theme_get(ui_theme_find("Sage & Sand"));
    const ThemeColors *other = ui_theme_get(ui_theme_find("Moss & Mist"));
    ASSERT_TRUE(orig != other);
    const ThemeColors *back = ui_theme_get(ui_theme_find("Sage & Sand"));
    ASSERT_TRUE(orig == back);
    TEST_END();
}

int test_ui_theme_switch_unknown_falls_back(void) {
    TEST_BEGIN();
    /* Switching to unknown scheme falls back to theme 0 */
    const ThemeColors *def = ui_theme_get(0);
    const ThemeColors *unk = ui_theme_get(ui_theme_find("NonExistent"));
    ASSERT_TRUE(def == unk);
    TEST_END();
}

int test_ui_theme_switch_null_falls_back(void) {
    TEST_BEGIN();
    /* Switching with NULL falls back to theme 0 */
    const ThemeColors *def = ui_theme_get(0);
    const ThemeColors *nul = ui_theme_get(ui_theme_find(NULL));
    ASSERT_TRUE(def == nul);
    TEST_END();
}

int test_ui_theme_switch_colors_consistent(void) {
    TEST_BEGIN();
    /* After switching, all fields of the returned theme are self-consistent:
     * bg_secondary differs from bg_primary, text contrasts bg */
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const char *name = ui_theme_name(i);
        int idx = ui_theme_find(name);
        const ThemeColors *t = ui_theme_get(idx);
        ASSERT_TRUE(t->bg_primary != t->bg_secondary);
        ASSERT_TRUE(t->text_main != t->bg_primary);
        ASSERT_TRUE(t->terminal_fg != t->terminal_bg);
    }
    TEST_END();
}

int test_ui_theme_switch_preserves_name(void) {
    TEST_BEGIN();
    /* The name field of the resolved theme matches what we looked up */
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const char *name = ui_theme_name(i);
        int idx = ui_theme_find(name);
        const ThemeColors *t = ui_theme_get(idx);
        ASSERT_STR_EQ(t->name, name);
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

/* ---- ui_theme_md_role -------------------------------------------------- */

/* rgb_alpha()'s truncating float blend, written out independently of
 * ui_theme.c so the test checks the arithmetic, not a shared helper. */
static unsigned int md_test_blend(unsigned int fg, unsigned int bg, float a) {
    unsigned int out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        float f = (float)((fg >> sh) & 0xFF), b = (float)((bg >> sh) & 0xFF);
        out |= ((unsigned int)(f * a + b * (1.0f - a)) & 0xFF) << sh;
    }
    return out;
}

/* A theme with only the fields ui_theme_md_role reads set. */
static ThemeColors md_test_theme(unsigned int bg, unsigned int cmd_bg,
                                 unsigned int dim, unsigned int accent,
                                 unsigned int hue, unsigned int text_main) {
    ThemeColors c;
    memset(&c, 0, sizeof c);
    c.bg_primary = bg;
    c.chat.cmd_bg = cmd_bg;
    c.text_dim = dim;
    c.accent = accent;
    c.link = hue;
    c.info = hue;
    c.text_main = text_main;
    return c;
}

/* Every surface the role is painted on clears UI_TEXT_MIN_RATIO. */
static int md_test_clears(const ThemeColors *c, UiMdRole role, unsigned int col) {
    unsigned int focus = c->overrides.focus ? c->overrides.focus : c->accent;
    /* The selection tint is the focus moved UI_SEL_SHIFT L* toward the
     * background, written out here rather than read from ui_theme.c. */
    focus = theme_shift_lightness(focus, theme_is_dark(c->bg_primary)
                                             ? -UI_SEL_SHIFT : UI_SEL_SHIFT);
    unsigned int surf[3];
    surf[0] = c->bg_primary;
    if (role == UI_MD_CODE) {
        surf[1] = c->chat.cmd_bg;
        surf[2] = md_test_blend(focus, c->chat.cmd_bg, UI_SEL_ALPHA);
    } else {
        surf[1] = md_test_blend(c->text_dim, c->bg_primary, UI_DIM_TINT_ALPHA);
        surf[2] = md_test_blend(focus, c->bg_primary, UI_SEL_ALPHA);
    }
    for (int i = 0; i < 3; i++)
        if (theme_contrast(col, surf[i]) < UI_TEXT_MIN_RATIO) return 0;
    return 1;
}

int test_ui_theme_md_role_unchanged_when_it_clears(void) {
    TEST_BEGIN();
    /* White on black: far above 4.7 on every surface of either role. */
    ThemeColors d = md_test_theme(0x000000, 0x101010, 0x202020, 0xFFFFFF,
                                  0xFFFFFF, 0xCCCCCC);
    ASSERT_EQ((int)ui_theme_md_role(&d, UI_MD_STRUCTURE), 0xFFFFFF);
    ASSERT_EQ((int)ui_theme_md_role(&d, UI_MD_CODE), 0xFFFFFF);
    /* Black on white, the light polarity. */
    ThemeColors l = md_test_theme(0xFFFFFF, 0xF0F0F0, 0xE0E0E0, 0x000000,
                                  0x000000, 0x333333);
    ASSERT_EQ((int)ui_theme_md_role(&l, UI_MD_STRUCTURE), 0x000000);
    ASSERT_EQ((int)ui_theme_md_role(&l, UI_MD_CODE), 0x000000);
    TEST_END();
}

int test_ui_theme_md_role_walks_away_from_the_background(void) {
    TEST_BEGIN();
    /* Dark: a dim blue-grey that fails moves lighter and then clears. */
    ThemeColors d = md_test_theme(0x101018, 0x202028, 0x404050, 0x6080C0,
                                  0x3A3A5A, 0xEEEEEE);
    UiMdRole roles[2] = { UI_MD_STRUCTURE, UI_MD_CODE };
    for (int k = 0; k < 2; k++) {
        ASSERT_TRUE(!md_test_clears(&d, roles[k], d.link));
        unsigned int got = ui_theme_md_role(&d, roles[k]);
        ASSERT_TRUE(got != d.text_main);
        ASSERT_TRUE(theme_lstar(got) > theme_lstar(d.link));
        ASSERT_TRUE(md_test_clears(&d, roles[k], got));
    }
    /* Light: a pale hue that fails moves darker and then clears. */
    ThemeColors l = md_test_theme(0xF8F8F0, 0xE8E8E0, 0xB0B0A0, 0x406080,
                                  0xB0C8A0, 0x111111);
    for (int k = 0; k < 2; k++) {
        ASSERT_TRUE(!md_test_clears(&l, roles[k], l.link));
        unsigned int got = ui_theme_md_role(&l, roles[k]);
        ASSERT_TRUE(got != l.text_main);
        ASSERT_TRUE(theme_lstar(got) < theme_lstar(l.link));
        ASSERT_TRUE(md_test_clears(&l, roles[k], got));
    }
    TEST_END();
}

int test_ui_theme_md_role_falls_back_to_text_main(void) {
    TEST_BEGIN();
    /* A mid-grey ground with a black accent: the selection surface is a
     * darker grey, and no shade of the hue reads 4.7 on it and on the ground
     * in 50 steps either way. */
    ThemeColors c = md_test_theme(0x808080, 0x808080, 0x808080, 0x000000,
                                  0x909090, 0xABCDEF);
    ASSERT_EQ((int)ui_theme_md_role(&c, UI_MD_STRUCTURE), 0xABCDEF);
    ASSERT_EQ((int)ui_theme_md_role(&c, UI_MD_CODE), 0xABCDEF);
    ASSERT_EQ((int)ui_theme_md_role(NULL, UI_MD_CODE), 0);
    TEST_END();
}

int test_ui_theme_md_tokens_match_md_role(void) {
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *c = ui_theme_get(i);
        ThemeTokens t;
        ui_theme_resolve(c, &t);
        ASSERT_EQ((int)t.md_structure, (int)ui_theme_md_role(c, UI_MD_STRUCTURE));
        ASSERT_EQ((int)t.md_code, (int)ui_theme_md_role(c, UI_MD_CODE));
    }
    TEST_END();
}
