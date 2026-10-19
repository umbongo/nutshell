/* tests/test_sel_contrast.c -- the selection highlight shows on every
 * surface it is drawn on, in every theme (spec 2026-10-18 section 6): the
 * hue ui_theme_sel_hue() picks, blended at 30% over the panel, the code
 * background, the Thinking body, the table-header fill and the user bubble,
 * must land at least MIN_STEP channel-steps away from that surface. */
#include "test_framework.h"
#include "ui_theme.h"
#include <stdio.h>
#include <stdlib.h>

/* rgb_alpha() (src/ui/ns_draw.c) on packed 0x00RRGGBB: per channel
 * fg * a + bg * (1 - a), truncated. */
static unsigned int blend(unsigned int fg, unsigned int bg, float a)
{
    unsigned int out = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        float f = (float)((fg >> sh) & 0xFF), b = (float)((bg >> sh) & 0xFF);
        out |= ((unsigned int)(f * a + b * (1.0f - a)) & 0xFF) << sh;
    }
    return out;
}

/* Sum of the per-channel differences. */
static int dist(unsigned int x, unsigned int y)
{
    int d = 0;
    for (int sh = 16; sh >= 0; sh -= 8)
        d += abs((int)((x >> sh) & 0xFF) - (int)((y >> sh) & 0xFF));
    return d;
}

#define SEL_ALPHA 0.30f
#define MIN_STEP  40

int test_sel_highlight_visible_on_every_surface(void)
{
    TEST_BEGIN();
    for (int t = 0; t < NUM_UI_THEMES; t++) {
        const ThemeColors *c = ui_theme_get(t);
        ThemeTokens k;
        ui_theme_resolve(c, &k);
        unsigned int header = blend(c->text_dim, c->bg_primary, 0.12f);
        unsigned int surf[5] = { c->bg_primary, c->chat.cmd_bg,
                                 k.bg_secondary.base, header,
                                 c->chat.user_bubble };
        unsigned int hue[5] = { ui_theme_sel_hue(&k, 0), ui_theme_sel_hue(&k, 0),
                                ui_theme_sel_hue(&k, 0), ui_theme_sel_hue(&k, 0),
                                ui_theme_sel_hue(&k, 1) };
        static const char *const name[5] = { "panel", "code", "thinking",
                                             "table header", "user bubble" };
        for (int i = 0; i < 5; i++) {
            int d = dist(blend(hue[i], surf[i], SEL_ALPHA), surf[i]);
            if (d < MIN_STEP)
                printf("  %s: highlight on the %s is only %d from it\n",
                       c->name, name[i], d);
            ASSERT_TRUE(d >= MIN_STEP);
        }
    }
    TEST_END();
}

int test_sel_hue_is_focus_except_on_the_bubble(void)
{
    TEST_BEGIN();
    ThemeTokens k;
    ui_theme_resolve(ui_theme_get(0), &k);
    ASSERT_EQ(ui_theme_sel_hue(&k, 0), k.focus);
    ASSERT_EQ(ui_theme_sel_hue(&k, 1), k.chat.user_text);
    ASSERT_EQ(ui_theme_sel_hue(NULL, 0), 0);
    TEST_END();
}
