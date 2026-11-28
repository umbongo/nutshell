/* tests/test_thinking_mode.c -- the Thinking row's display mode model. */
#include "test_framework.h"
#include "thinking_mode.h"
#include "stick_scroll.h"
#include "ui_theme.h"
#include "theme.h"
#include <math.h>
#include <string.h>

int test_thinking_mode_next_cycle(void)
{
    TEST_BEGIN();
    ASSERT_EQ(think_mode_next(THINK_MODE_COLLAPSED), THINK_MODE_COMPACT);
    ASSERT_EQ(think_mode_next(THINK_MODE_COMPACT), THINK_MODE_EXTENDED);
    ASSERT_EQ(think_mode_next(THINK_MODE_EXTENDED), THINK_MODE_OPEN);
    ASSERT_EQ(think_mode_next(THINK_MODE_OPEN), THINK_MODE_COLLAPSED);
    /* four clicks come back to collapsed */
    {
        ThinkMode m = THINK_MODE_COLLAPSED;
        for (int i = 0; i < 4; i++) m = think_mode_next(m);
        ASSERT_EQ(m, THINK_MODE_COLLAPSED);
    }
    /* out of range behaves as collapsed */
    ASSERT_EQ(think_mode_next((ThinkMode)-1), THINK_MODE_COMPACT);
    ASSERT_EQ(think_mode_next((ThinkMode)99), THINK_MODE_COMPACT);
    TEST_END();
}

int test_thinking_mode_effective(void)
{
    TEST_BEGIN();
    ASSERT_EQ(think_mode_effective(THINK_PREF_AUTO, 1), THINK_MODE_EXTENDED);
    ASSERT_EQ(think_mode_effective(THINK_PREF_AUTO, 0), THINK_MODE_COLLAPSED);
    ASSERT_EQ(think_mode_effective(THINK_PREF_COLLAPSED, 1), THINK_MODE_COLLAPSED);
    ASSERT_EQ(think_mode_effective(THINK_PREF_COLLAPSED, 0), THINK_MODE_COLLAPSED);
    ASSERT_EQ(think_mode_effective(THINK_PREF_COMPACT, 1), THINK_MODE_COMPACT);
    ASSERT_EQ(think_mode_effective(THINK_PREF_COMPACT, 0), THINK_MODE_COMPACT);
    ASSERT_EQ(think_mode_effective(THINK_PREF_OPEN, 1), THINK_MODE_OPEN);
    ASSERT_EQ(think_mode_effective(THINK_PREF_OPEN, 0), THINK_MODE_OPEN);
    ASSERT_EQ(think_mode_effective(THINK_PREF_EXTENDED, 1), THINK_MODE_EXTENDED);
    ASSERT_EQ(think_mode_effective(THINK_PREF_EXTENDED, 0), THINK_MODE_EXTENDED);
    /* out of range pref acts as AUTO */
    ASSERT_EQ(think_mode_effective((ThinkPref)-1, 1), THINK_MODE_EXTENDED);
    ASSERT_EQ(think_mode_effective((ThinkPref)99, 0), THINK_MODE_COLLAPSED);
    TEST_END();
}

int test_thinking_pref_from_mode(void)
{
    TEST_BEGIN();
    ASSERT_EQ(think_pref_from_mode(THINK_MODE_COLLAPSED), THINK_PREF_COLLAPSED);
    ASSERT_EQ(think_pref_from_mode(THINK_MODE_COMPACT), THINK_PREF_COMPACT);
    ASSERT_EQ(think_pref_from_mode(THINK_MODE_OPEN), THINK_PREF_OPEN);
    ASSERT_EQ(think_pref_from_mode(THINK_MODE_EXTENDED), THINK_PREF_EXTENDED);
    ASSERT_EQ(think_pref_from_mode((ThinkMode)42), THINK_PREF_COLLAPSED);
    TEST_END();
}

int test_thinking_pref_name_parse(void)
{
    TEST_BEGIN();
    static const ThinkPref prefs[] = { THINK_PREF_AUTO, THINK_PREF_COLLAPSED,
                                       THINK_PREF_COMPACT, THINK_PREF_OPEN,
                                       THINK_PREF_EXTENDED };
    static const char *const names[] = { "auto", "collapsed", "compact", "open",
                                         "extended" };
    for (int i = 0; i < 5; i++) {
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
    ASSERT_EQ(think_pref_parse("Extended"), THINK_PREF_AUTO);
    ASSERT_EQ(think_pref_parse("EXTENDED"), THINK_PREF_AUTO);
    ASSERT_EQ(THINK_PREF_EXTENDED, 4);
    ASSERT_EQ(THINK_MODE_EXTENDED, 3);
    ASSERT_EQ(THINK_PREF_MAX, THINK_PREF_EXTENDED);
    TEST_END();
}

int test_thinking_body_cap(void)
{
    TEST_BEGIN();
    ASSERT_EQ(think_mode_body_cap(THINK_MODE_COLLAPSED, 18, 500), 0);
    ASSERT_EQ(think_mode_body_cap(THINK_MODE_COMPACT, 18, 500), THINK_COMPACT_LINES * 18);
    ASSERT_EQ(think_mode_body_cap(THINK_MODE_OPEN, 18, 500), 500);
    ASSERT_EQ(THINK_EXTENDED_LINES, 25);
    ASSERT_EQ(think_mode_body_cap(THINK_MODE_EXTENDED, 18, 500), 25 * 18);
    ASSERT_EQ(think_mode_body_cap(THINK_MODE_EXTENDED, 0, 500), 0);
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
    c.thinking_extended  = 0x444444;
    ASSERT_EQ(think_mode_label_colour(&c, THINK_MODE_COMPACT), 0x111111u);
    ASSERT_EQ(think_mode_label_colour(&c, THINK_MODE_COLLAPSED), 0x222222u);
    ASSERT_EQ(think_mode_label_colour(&c, THINK_MODE_OPEN), 0x333333u);
    /* Extended has a token of its own */
    ASSERT_EQ(think_mode_label_colour(&c, THINK_MODE_EXTENDED), 0x444444u);
    ASSERT_EQ(think_mode_label_colour(&c, (ThinkMode)-3), 0x222222u);
    ASSERT_EQ(think_mode_label_colour(&c, (ThinkMode)50), 0x222222u);
    TEST_END();
}

int test_thinking_follows_tail(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(think_mode_follows_tail(THINK_MODE_COMPACT));
    ASSERT_TRUE(think_mode_follows_tail(THINK_MODE_EXTENDED));
    ASSERT_TRUE(!think_mode_follows_tail(THINK_MODE_COLLAPSED));
    ASSERT_TRUE(!think_mode_follows_tail(THINK_MODE_OPEN));
    ASSERT_TRUE(!think_mode_follows_tail((ThinkMode)-1));
    ASSERT_TRUE(!think_mode_follows_tail((ThinkMode)77));
    TEST_END();
}

/* Smallest RGB distance (Euclidean, 0..255 per channel) between any two of the
 * four mode colours on one theme. */
#define THINK_MIN_MODE_DIST 40.0

static double think_rgb_dist(unsigned int a, unsigned int b)
{
    double s = 0.0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        double d = (double)((a >> sh) & 0xFFu) - (double)((b >> sh) & 0xFFu);
        s += d * d;
    }
    return sqrt(s);
}

/* Every theme gives each of the four modes its own colour: pairwise at least
 * THINK_MIN_MODE_DIST apart, each readable (WCAG 4.5:1) on the chat surface
 * the label is drawn on (bg_primary), and the strength rises with the mode:
 * collapsed is the quietest, open the strongest (contrast on that surface). */
int test_thinking_theme_tokens_distinct(void)
{
    TEST_BEGIN();
    static const ThinkMode order[4] = {
        THINK_MODE_COLLAPSED, THINK_MODE_COMPACT,
        THINK_MODE_EXTENDED, THINK_MODE_OPEN
    };
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *t = ui_theme_get(i);
        unsigned int col[4];
        double prev = 0.0;
        for (int k = 0; k < 4; k++) {
            col[k] = think_mode_label_colour(&t->chat, order[k]);
            ASSERT_TRUE(col[k] != 0);
            const double r = theme_contrast(col[k], t->bg_primary);
            ASSERT_TRUE(r >= 4.5);
            ASSERT_TRUE(r > prev);   /* strictly stronger with each mode */
            prev = r;
        }
        for (int a = 0; a < 4; a++)
            for (int b = a + 1; b < 4; b++)
                ASSERT_TRUE(think_rgb_dist(col[a], col[b]) >=
                            THINK_MIN_MODE_DIST);
    }
    TEST_END();
}

/* The box's own scroll state while reasoning streams (compact and extended):
 * the same stick-to-bottom rule as the chat list.  box_wheel() is what the
 * wheel handler does: scroll, then stick = stick_scroll_after_user(). */
typedef struct { int stick; int y; } BoxScroll;

static void box_append(BoxScroll *b, ThinkMode m, int full_h, int vis_h)
{
    b->y = think_box_settle_scroll(m, b->stick, b->y, full_h, vis_h);
}

static void box_wheel_to(BoxScroll *b, int y, int full_h, int vis_h)
{
    int max = full_h > vis_h ? full_h - vis_h : 0;
    b->y = y < 0 ? 0 : (y > max ? max : y);
    b->stick = stick_scroll_after_user(b->y, max);
}

int test_thinking_box_wheel_modes(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(!think_mode_wheel_scrolls(THINK_MODE_COLLAPSED));
    ASSERT_TRUE(think_mode_wheel_scrolls(THINK_MODE_COMPACT));
    ASSERT_TRUE(think_mode_wheel_scrolls(THINK_MODE_EXTENDED));
    ASSERT_TRUE(think_mode_wheel_scrolls(THINK_MODE_OPEN));
    ASSERT_TRUE(!think_mode_wheel_scrolls((ThinkMode)-1));
    ASSERT_TRUE(!think_mode_wheel_scrolls((ThinkMode)9));
    TEST_END();
}

/* Who gets a wheel notch over a Thinking box: a compact or extended box takes
 * it only while its reasoning streams AND its text overflows AND the thread
 * has not been wheel-scrolled in the last THINK_WHEEL_LATCH_MS; the open box
 * always (the caller still checks overflow and the box limits). */
int test_thinking_wheel_takes(void)
{
    TEST_BEGIN();
    ASSERT_EQ((int)THINK_WHEEL_LATCH_MS, 300);
    /* finished compact / extended box: the thread */
    ASSERT_TRUE(!think_wheel_takes(THINK_MODE_COMPACT, 0, 1, 5000u, 0, 0u));
    ASSERT_TRUE(!think_wheel_takes(THINK_MODE_EXTENDED, 0, 1, 5000u, 0, 0u));
    /* streaming, overflowing, thread never scrolled: the box */
    ASSERT_TRUE(think_wheel_takes(THINK_MODE_COMPACT, 1, 1, 5000u, 0, 0u));
    ASSERT_TRUE(think_wheel_takes(THINK_MODE_EXTENDED, 1, 1, 5000u, 0, 0u));
    /* content fits: the thread */
    ASSERT_TRUE(!think_wheel_takes(THINK_MODE_COMPACT, 1, 0, 5000u, 0, 0u));
    ASSERT_TRUE(!think_wheel_takes(THINK_MODE_EXTENDED, 1, 0, 5000u, 0, 0u));
    /* the latch: within 300 ms of a thread scroll the thread keeps the wheel */
    ASSERT_TRUE(!think_wheel_takes(THINK_MODE_COMPACT, 1, 1, 5000u, 1, 4999u));
    ASSERT_TRUE(!think_wheel_takes(THINK_MODE_COMPACT, 1, 1, 5000u, 1, 5000u));
    ASSERT_TRUE(!think_wheel_takes(THINK_MODE_EXTENDED, 1, 1, 5299u, 1, 5000u));
    ASSERT_TRUE(think_wheel_takes(THINK_MODE_EXTENDED, 1, 1, 5300u, 1, 5000u));
    ASSERT_TRUE(think_wheel_takes(THINK_MODE_COMPACT, 1, 1, 9000u, 1, 5000u));
    /* the latch across the tick counter's wrap */
    ASSERT_TRUE(!think_wheel_takes(THINK_MODE_COMPACT, 1, 1, 0x20u, 1, 0xFFFFFF00u));
    ASSERT_TRUE(think_wheel_takes(THINK_MODE_COMPACT, 1, 1, 0x200u, 1, 0xFFFFFF00u));
    /* the open box keeps its old rule: it takes the wheel whatever the rest */
    ASSERT_TRUE(think_wheel_takes(THINK_MODE_OPEN, 0, 1, 5000u, 0, 0u));
    ASSERT_TRUE(think_wheel_takes(THINK_MODE_OPEN, 1, 1, 5000u, 1, 4999u));
    ASSERT_TRUE(think_wheel_takes(THINK_MODE_OPEN, 0, 0, 5000u, 0, 0u));
    /* collapsed and out-of-range modes have no body */
    ASSERT_TRUE(!think_wheel_takes(THINK_MODE_COLLAPSED, 1, 1, 5000u, 0, 0u));
    ASSERT_TRUE(!think_wheel_takes((ThinkMode)9, 1, 1, 5000u, 0, 0u));
    ASSERT_TRUE(!think_wheel_takes((ThinkMode)-1, 1, 1, 5000u, 0, 0u));
    TEST_END();
}

int test_thinking_box_scroll_follow_hold_resume(void)
{
    TEST_BEGIN();
    const int vis = 80;
    BoxScroll b = { 1, 0 };
    /* streaming appends while at the bottom follow the tail */
    box_append(&b, THINK_MODE_COMPACT, 60, vis);       /* fits: offset 0 */
    ASSERT_EQ(b.y, 0);
    box_append(&b, THINK_MODE_COMPACT, 200, vis);
    ASSERT_EQ(b.y, 120);
    box_append(&b, THINK_MODE_COMPACT, 260, vis);
    ASSERT_EQ(b.y, 180);
    /* a scroll up stops following... */
    box_wheel_to(&b, 100, 260, vis);
    ASSERT_EQ(b.stick, 0);
    ASSERT_EQ(b.y, 100);
    /* ...and later appends do not move the view */
    box_append(&b, THINK_MODE_COMPACT, 320, vis);
    ASSERT_EQ(b.y, 100);
    box_append(&b, THINK_MODE_COMPACT, 500, vis);
    ASSERT_EQ(b.y, 100);
    /* scrolling back to the bottom resumes following */
    box_wheel_to(&b, 10000, 500, vis);
    ASSERT_EQ(b.stick, 1);
    ASSERT_EQ(b.y, 420);
    box_append(&b, THINK_MODE_COMPACT, 560, vis);
    ASSERT_EQ(b.y, 480);
    TEST_END();
}

int test_thinking_box_scroll_mode_change_holds_place(void)
{
    TEST_BEGIN();
    /* compact (80 px box) to extended (400 px box) while released: the
     * reader's offset is kept, only re-clamped to the new range */
    BoxScroll b = { 1, 0 };
    box_append(&b, THINK_MODE_COMPACT, 1000, 80);
    box_wheel_to(&b, 300, 1000, 80);
    ASSERT_EQ(b.stick, 0);
    box_append(&b, THINK_MODE_EXTENDED, 1000, 400);
    ASSERT_EQ(b.y, 300);
    /* the box grows past the reader's offset: clamped to the new maximum */
    box_append(&b, THINK_MODE_EXTENDED, 600, 400);
    ASSERT_EQ(b.y, 200);
    /* a following box stays on the tail across the mode change */
    BoxScroll f = { 1, 0 };
    box_append(&f, THINK_MODE_COMPACT, 1000, 80);
    ASSERT_EQ(f.y, 920);
    box_append(&f, THINK_MODE_EXTENDED, 1000, 400);
    ASSERT_EQ(f.y, 600);
    /* collapsed has no body: offset 0 whatever the state */
    box_append(&f, THINK_MODE_COLLAPSED, 1000, 0);
    ASSERT_EQ(f.y, 0);
    /* content that fits the box leaves nothing to scroll */
    BoxScroll s = { 0, 50 };
    box_append(&s, THINK_MODE_COMPACT, 40, 80);
    ASSERT_EQ(s.y, 0);
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
