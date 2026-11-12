/* tests/test_thinking_mode.c -- the Thinking row's display mode model. */
#include "test_framework.h"
#include "thinking_mode.h"
#include "ui_theme.h"
#include <string.h>

int test_thinking_mode_next_cycle(void)
{
    TEST_BEGIN();
    ASSERT_EQ(think_mode_next(THINK_MODE_COLLAPSED), THINK_MODE_COMPACT);
    ASSERT_EQ(think_mode_next(THINK_MODE_COMPACT), THINK_MODE_OPEN);
    ASSERT_EQ(think_mode_next(THINK_MODE_OPEN), THINK_MODE_COLLAPSED);
    /* out of range behaves as collapsed */
    ASSERT_EQ(think_mode_next((ThinkMode)-1), THINK_MODE_COMPACT);
    ASSERT_EQ(think_mode_next((ThinkMode)99), THINK_MODE_COMPACT);
    TEST_END();
}

int test_thinking_mode_effective(void)
{
    TEST_BEGIN();
    ASSERT_EQ(think_mode_effective(THINK_PREF_AUTO, 1), THINK_MODE_COMPACT);
    ASSERT_EQ(think_mode_effective(THINK_PREF_AUTO, 0), THINK_MODE_COLLAPSED);
    ASSERT_EQ(think_mode_effective(THINK_PREF_COLLAPSED, 1), THINK_MODE_COLLAPSED);
    ASSERT_EQ(think_mode_effective(THINK_PREF_COLLAPSED, 0), THINK_MODE_COLLAPSED);
    ASSERT_EQ(think_mode_effective(THINK_PREF_COMPACT, 1), THINK_MODE_COMPACT);
    ASSERT_EQ(think_mode_effective(THINK_PREF_COMPACT, 0), THINK_MODE_COMPACT);
    ASSERT_EQ(think_mode_effective(THINK_PREF_OPEN, 1), THINK_MODE_OPEN);
    ASSERT_EQ(think_mode_effective(THINK_PREF_OPEN, 0), THINK_MODE_OPEN);
    /* out of range pref acts as AUTO */
    ASSERT_EQ(think_mode_effective((ThinkPref)-1, 1), THINK_MODE_COMPACT);
    ASSERT_EQ(think_mode_effective((ThinkPref)99, 0), THINK_MODE_COLLAPSED);
    TEST_END();
}

int test_thinking_pref_from_mode(void)
{
    TEST_BEGIN();
    ASSERT_EQ(think_pref_from_mode(THINK_MODE_COLLAPSED), THINK_PREF_COLLAPSED);
    ASSERT_EQ(think_pref_from_mode(THINK_MODE_COMPACT), THINK_PREF_COMPACT);
    ASSERT_EQ(think_pref_from_mode(THINK_MODE_OPEN), THINK_PREF_OPEN);
    ASSERT_EQ(think_pref_from_mode((ThinkMode)42), THINK_PREF_COLLAPSED);
    TEST_END();
}

int test_thinking_pref_name_parse(void)
{
    TEST_BEGIN();
    static const ThinkPref prefs[] = { THINK_PREF_AUTO, THINK_PREF_COLLAPSED,
                                       THINK_PREF_COMPACT, THINK_PREF_OPEN };
    static const char *const names[] = { "auto", "collapsed", "compact", "open" };
    for (int i = 0; i < 4; i++) {
        ASSERT_STR_EQ(think_pref_name(prefs[i]), names[i]);
        ASSERT_EQ(think_pref_parse(names[i]), prefs[i]);
        ASSERT_EQ(think_pref_parse(think_pref_name(prefs[i])), prefs[i]);
    }
    ASSERT_STR_EQ(think_pref_name((ThinkPref)-1), "auto");
    ASSERT_STR_EQ(think_pref_name((ThinkPref)77), "auto");
    ASSERT_EQ(think_pref_parse(NULL), THINK_PREF_AUTO);
    ASSERT_EQ(think_pref_parse(""), THINK_PREF_AUTO);
    ASSERT_EQ(think_pref_parse("bogus"), THINK_PREF_AUTO);
    ASSERT_EQ(think_pref_parse("Open"), THINK_PREF_AUTO);
    ASSERT_EQ(think_pref_parse("COMPACT"), THINK_PREF_AUTO);
    TEST_END();
}

int test_thinking_body_cap(void)
{
    TEST_BEGIN();
    ASSERT_EQ(think_mode_body_cap(THINK_MODE_COLLAPSED, 18, 500), 0);
    ASSERT_EQ(think_mode_body_cap(THINK_MODE_COMPACT, 18, 500), THINK_COMPACT_LINES * 18);
    ASSERT_EQ(think_mode_body_cap(THINK_MODE_OPEN, 18, 500), 500);
    ASSERT_EQ(think_mode_body_cap(THINK_MODE_COMPACT, 0, 500), 0);
    ASSERT_EQ(think_mode_body_cap(THINK_MODE_COLLAPSED, 0, 500), 0);
    ASSERT_EQ(think_mode_body_cap(THINK_MODE_OPEN, 0, 500), 500);
    ASSERT_EQ(think_mode_body_cap((ThinkMode)9, 18, 500), 0);
    TEST_END();
}

int test_thinking_compact_offset(void)
{
    TEST_BEGIN();
    ASSERT_EQ(think_compact_offset(50, 72), 0);   /* shorter than the box */
    ASSERT_EQ(think_compact_offset(72, 72), 0);   /* exactly full */
    ASSERT_EQ(think_compact_offset(200, 72), 128);
    ASSERT_EQ(think_compact_offset(0, 0), 0);
    TEST_END();
}

int test_thinking_label_colour(void)
{
    TEST_BEGIN();
    ThemeChatColors c;
    memset(&c, 0, sizeof c);
    c.thinking_compact   = 0x111111;
    c.thinking_collapsed = 0x222222;
    c.thinking_open      = 0x333333;
    ASSERT_EQ(think_mode_label_colour(&c, THINK_MODE_COMPACT), 0x111111u);
    ASSERT_EQ(think_mode_label_colour(&c, THINK_MODE_COLLAPSED), 0x222222u);
    ASSERT_EQ(think_mode_label_colour(&c, THINK_MODE_OPEN), 0x333333u);
    ASSERT_EQ(think_mode_label_colour(&c, (ThinkMode)-3), 0x222222u);
    ASSERT_EQ(think_mode_label_colour(&c, (ThinkMode)50), 0x222222u);
    TEST_END();
}

/* Every theme sets all three label colours, and they differ. */
int test_thinking_theme_tokens_distinct(void)
{
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeChatColors *c = &ui_theme_get(i)->chat;
        ASSERT_TRUE(c->thinking_compact != 0);
        ASSERT_TRUE(c->thinking_collapsed != 0);
        ASSERT_TRUE(c->thinking_open != 0);
        ASSERT_TRUE(c->thinking_compact != c->thinking_collapsed);
        ASSERT_TRUE(c->thinking_compact != c->thinking_open);
        ASSERT_TRUE(c->thinking_collapsed != c->thinking_open);
    }
    TEST_END();
}

int test_thinking_trimmed_len(void)
{
    TEST_BEGIN();
    ASSERT_EQ((int)think_trimmed_len("abc", 3), 3);
    ASSERT_EQ((int)think_trimmed_len("abc\n\n", 5), 3);
    ASSERT_EQ((int)think_trimmed_len("abc \t\r\n\f\v", 9), 3);
    ASSERT_EQ((int)think_trimmed_len("a b\n c\n", 7), 6);   /* inner kept */
    ASSERT_EQ((int)think_trimmed_len("  \n\n", 4), 0);      /* all blank */
    ASSERT_EQ((int)think_trimmed_len("", 0), 0);
    ASSERT_EQ((int)think_trimmed_len(NULL, 5), 0);
    ASSERT_EQ((int)think_trimmed_len("abc\n\n", 4), 3);    /* honours len */
    TEST_END();
}
