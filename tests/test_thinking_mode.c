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

/* ---- colour maths (test-only) -------------------------------------- */

static double cm_lin(unsigned int c, int sh)
{
    double v = (double)((c >> sh) & 0xFFu) / 255.0;
    return v <= 0.04045 ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
}

static double cm_f(double t)
{
    return t > 0.008856 ? cbrt(t) : 7.787 * t + 16.0 / 116.0;
}

/* Linear RGB (already clamped to 0..1) to CIELAB, D65. */
static void cm_lab(const double rgb[3], double lab[3])
{
    double x = (0.4124 * rgb[0] + 0.3576 * rgb[1] + 0.1805 * rgb[2]) / 0.95047;
    double y = 0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2];
    double z = (0.0193 * rgb[0] + 0.1192 * rgb[1] + 0.9505 * rgb[2]) / 1.08883;
    lab[0] = 116.0 * cm_f(y) - 16.0;
    lab[1] = 500.0 * (cm_f(x) - cm_f(y));
    lab[2] = 200.0 * (cm_f(y) - cm_f(z));
}

/* Machado, Oliveira and Fernandes (2009), severity 1.0, applied to linear
 * RGB.  kind 0 normal vision, 1 protanopia, 2 deuteranopia. */
static void cm_sim_lab(unsigned int c, int kind, double lab[3])
{
    static const double prot[3][3] = {
        { 0.152286, 1.052583, -0.204868 },
        { 0.114503, 0.786281,  0.099216 },
        { -0.003882, -0.048116, 1.051998 } };
    static const double deut[3][3] = {
        { 0.367322, 0.860646, -0.227968 },
        { 0.280085, 0.672501,  0.047413 },
        { -0.011820, 0.042940, 0.968881 } };
    double v[3] = { cm_lin(c, 16), cm_lin(c, 8), cm_lin(c, 0) };
    double o[3];
    for (int i = 0; i < 3; i++) {
        const double *row = kind == 1 ? prot[i] : deut[i];
        o[i] = kind == 0 ? v[i] : row[0] * v[0] + row[1] * v[1] + row[2] * v[2];
        if (o[i] < 0.0) o[i] = 0.0;
        if (o[i] > 1.0) o[i] = 1.0;
    }
    cm_lab(o, lab);
}

/* CIE76 distance between two colours as seen with the given vision. */
static double cm_de76(unsigned int a, unsigned int b, int kind)
{
    double la[3], lb[3];
    cm_sim_lab(a, kind, la);
    cm_sim_lab(b, kind, lb);
    return sqrt((la[0] - lb[0]) * (la[0] - lb[0]) +
                (la[1] - lb[1]) * (la[1] - lb[1]) +
                (la[2] - lb[2]) * (la[2] - lb[2]));
}

/* The smallest CIE76 distance any two of the four mode colours are apart
 * under the vision kind. */
#define THINK_MIN_MODE_DE76 15.0

/* What tells the four modes apart on a theme: the pill background where the
 * theme has pills (light themes), else the label text (dark themes). */
static unsigned int think_marker(const ThemeChatColors *c, ThinkMode m)
{
    const unsigned int pill = think_mode_pill_colour(c, m);
    return pill ? pill : think_mode_label_colour(c, m);
}

/* Every theme gives the four modes their own colour, told apart by normal,
 * protan and deutan vision (CIE76 >= THINK_MIN_MODE_DE76 for every pair, the
 * Machado 2009 matrices at severity 1.0).  Dark themes draw pastel text
 * (L* 74..88, modest chroma, warm collapsed) on bg_primary at >= 4.5:1; light
 * themes draw text_main on a pastel pill at >= 4.5:1, the pill clearly off
 * bg_primary; a dark theme has no pill. */
int test_thinking_theme_tokens_distinct(void)
{
    TEST_BEGIN();
    static const ThinkMode order[4] = {
        THINK_MODE_COLLAPSED, THINK_MODE_COMPACT,
        THINK_MODE_EXTENDED, THINK_MODE_OPEN
    };
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *t = ui_theme_get(i);
        double bg[3];
        cm_sim_lab(t->bg_primary, 0, bg);
        const int light_theme = bg[0] > 50.0;
        unsigned int col[4];
        for (int k = 0; k < 4; k++) {
            const unsigned int label = think_mode_label_colour(&t->chat, order[k]);
            const unsigned int pill = think_mode_pill_colour(&t->chat, order[k]);
            col[k] = think_marker(&t->chat, order[k]);
            ASSERT_TRUE(label != 0);
            if (light_theme) {
                ASSERT_TRUE(pill != 0);
                ASSERT_EQ(label, t->text_main);
                ASSERT_TRUE(theme_contrast(label, pill) >= 4.5);
                ASSERT_TRUE(cm_de76(pill, t->bg_primary, 0) >= 8.0);
            } else {
                double lab[3];
                ASSERT_EQ(pill, 0u);
                ASSERT_TRUE(theme_contrast(label, t->bg_primary) >= 4.5);
                cm_sim_lab(label, 0, lab);
                ASSERT_TRUE(lab[0] >= 74.0 && lab[0] <= 88.0);
                ASSERT_TRUE(sqrt(lab[1] * lab[1] + lab[2] * lab[2]) <= 45.0);
            }
        }
        /* collapsed is warm, not grey: a clearly positive b* (yellow side) */
        {
            double lab[3];
            cm_sim_lab(col[0], 0, lab);
            ASSERT_TRUE(lab[2] >= 10.0);
        }
        for (int a = 0; a < 4; a++)
            for (int b = a + 1; b < 4; b++)
                for (int kind = 0; kind < 3; kind++)
                    ASSERT_TRUE(cm_de76(col[a], col[b], kind) >=
                                THINK_MIN_MODE_DE76);
    }
    TEST_END();
}

/* Every theme sets the pill fields (appended to ThemeChatColors): non-zero on
 * the light themes, zero on the dark ones. */
int test_thinking_theme_pill_tokens_set(void)
{
    TEST_BEGIN();
    for (int i = 0; i < NUM_UI_THEMES; i++) {
        const ThemeColors *t = ui_theme_get(i);
        double bg[3];
        cm_sim_lab(t->bg_primary, 0, bg);
        const int light_theme = bg[0] > 50.0;
        const unsigned int p[4] = {
            t->chat.thinking_collapsed_pill, t->chat.thinking_compact_pill,
            t->chat.thinking_extended_pill, t->chat.thinking_open_pill };
        for (int k = 0; k < 4; k++)
            ASSERT_TRUE(light_theme ? p[k] != 0 : p[k] == 0);
    }
    TEST_END();
}

/* The accessor: each mode's own pill token, collapsed for out of range. */
int test_thinking_pill_colour(void)
{
    TEST_BEGIN();
    ThemeChatColors c;
    memset(&c, 0, sizeof c);
    c.thinking_collapsed_pill = 0x0A0A0A;
    c.thinking_compact_pill   = 0x0B0B0B;
    c.thinking_extended_pill  = 0x0C0C0C;
    c.thinking_open_pill      = 0x0D0D0D;
    ASSERT_EQ(think_mode_pill_colour(&c, THINK_MODE_COLLAPSED), 0x0A0A0Au);
    ASSERT_EQ(think_mode_pill_colour(&c, THINK_MODE_COMPACT), 0x0B0B0Bu);
    ASSERT_EQ(think_mode_pill_colour(&c, THINK_MODE_EXTENDED), 0x0C0C0Cu);
    ASSERT_EQ(think_mode_pill_colour(&c, THINK_MODE_OPEN), 0x0D0D0Du);
    ASSERT_EQ(think_mode_pill_colour(&c, (ThinkMode)7), 0x0A0A0Au);
    ASSERT_EQ(think_mode_pill_colour(&c, (ThinkMode)-1), 0x0A0A0Au);
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
    /* extended to open keeps the reader's place too (no reset to the tail):
     * the follow state think_autoscroll_on_pref() leaves is what the list
     * view then settles with */
    BoxScroll o = { 1, 0 };
    box_append(&o, THINK_MODE_EXTENDED, 1000, 400);
    box_wheel_to(&o, 250, 1000, 400);
    ASSERT_EQ(o.stick, 0);
    o.stick = think_autoscroll_on_pref(o.stick, THINK_MODE_EXTENDED,
                                       THINK_PREF_OPEN);
    ASSERT_EQ(o.stick, 0);
    box_append(&o, THINK_MODE_OPEN, 1000, 500);
    ASSERT_EQ(o.y, 250);
    /* a pref change only re-arms following for a row that was or becomes
     * collapsed: it forgets its place */
    ASSERT_EQ(think_autoscroll_on_pref(0, THINK_MODE_COLLAPSED, THINK_PREF_OPEN), 1);
    ASSERT_EQ(think_autoscroll_on_pref(0, THINK_MODE_COLLAPSED, THINK_PREF_COMPACT), 1);
    ASSERT_EQ(think_autoscroll_on_pref(0, THINK_MODE_OPEN, THINK_PREF_COLLAPSED), 1);
    ASSERT_EQ(think_autoscroll_on_pref(0, THINK_MODE_EXTENDED, THINK_PREF_COLLAPSED), 1);
    ASSERT_EQ(think_autoscroll_on_pref(0, THINK_MODE_COMPACT, THINK_PREF_EXTENDED), 0);
    ASSERT_EQ(think_autoscroll_on_pref(0, THINK_MODE_OPEN, THINK_PREF_COMPACT), 0);
    ASSERT_EQ(think_autoscroll_on_pref(0, THINK_MODE_EXTENDED, THINK_PREF_OPEN), 0);
    ASSERT_EQ(think_autoscroll_on_pref(1, THINK_MODE_EXTENDED, THINK_PREF_OPEN), 1);
    ASSERT_EQ(think_autoscroll_on_pref(0, THINK_MODE_COMPACT, THINK_PREF_AUTO), 0);
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
