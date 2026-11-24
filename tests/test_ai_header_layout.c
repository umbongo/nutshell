#include "test_framework.h"
#include "ai_header_layout.h"

#include <string.h>

/* The AI panel header, laid out left to right: the session name, then the
 * status slot (activity dot and/or text) up to the button cluster at the
 * right edge. The model chip is gone (2026-11-24). The status is never
 * painted under the buttons, and the name keeps min_name_w before the status
 * narrows. */

static int overlaps(NsRect a, NsRect b)
{
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0) return 0;
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

static AiHeaderMeasure base_measure(int header_w)
{
    AiHeaderMeasure m;
    m.header = (NsRect){ 0, 0, header_w, 32 };
    m.pad = 8;
    m.gap = 8;
    m.buttons_w = 3 * 28 + 2 * 8;   /* 100 */
    m.btn_h = 28;
    m.status_w = 70;
    m.name_w = 60;
    m.min_name_w = 40;
    m.min_status_w = 30;
    return m;
}

int test_ai_header_layout_short_name_and_status_fit_in_order(void)
{
    TEST_BEGIN();
    AiHeaderMeasure m = base_measure(500);
    AiHeaderLayout l;
    ai_header_layout(&m, &l);

    ASSERT_EQ(l.buttons.x + l.buttons.w, 500 - 8);
    ASSERT_EQ(l.buttons.w, 100);
    ASSERT_EQ(l.buttons.y, 2);
    ASSERT_EQ(l.name.x, 8);
    ASSERT_EQ(l.name.w, 60);
    ASSERT_EQ(l.name_clipped, 0);
    ASSERT_EQ(l.status_shown, 1);
    ASSERT_EQ(l.status_clipped, 0);
    ASSERT_EQ(l.status.x, l.name.x + l.name.w + 8);
    ASSERT_EQ(l.status.w, 70);
    ASSERT_TRUE(l.status.x + l.status.w + 8 <= l.buttons.x);
    TEST_END();
}

int test_ai_header_layout_long_status_is_narrowed_and_name_keeps_its_minimum(void)
{
    TEST_BEGIN();
    AiHeaderMeasure m = base_measure(500);
    m.name_w = 200;
    m.status_w = 600;   /* a long note */
    AiHeaderLayout l;
    ai_header_layout(&m, &l);

    ASSERT_EQ(l.status_shown, 1);
    ASSERT_EQ(l.status_clipped, 1);
    ASSERT_EQ(l.name.w, 40);                    /* min_name_w, not 200 */
    ASSERT_EQ(l.name_clipped, 1);
    ASSERT_EQ(l.name.x + l.name.w + 8, l.status.x);
    /* The status runs to a gap short of the buttons. */
    ASSERT_EQ(l.status.x + l.status.w + 8, l.buttons.x);
    ASSERT_TRUE(l.status.w < 600);
    ASSERT_TRUE(!overlaps(l.name, l.status));
    ASSERT_TRUE(!overlaps(l.status, l.buttons));

    /* A name shorter than min_name_w is kept whole. */
    m.name_w = 25;
    ai_header_layout(&m, &l);
    ASSERT_EQ(l.name.w, 25);
    ASSERT_EQ(l.name_clipped, 0);
    ASSERT_EQ(l.status.x, 8 + 25 + 8);
    TEST_END();
}

int test_ai_header_layout_status_that_fits_unclipped_cuts_the_name_instead(void)
{
    TEST_BEGIN();
    AiHeaderMeasure m = base_measure(500);
    m.name_w = 400;
    AiHeaderLayout l;
    ai_header_layout(&m, &l);
    ASSERT_EQ(l.status_clipped, 0);
    ASSERT_EQ(l.status.w, 70);
    ASSERT_EQ(l.name_clipped, 1);
    ASSERT_EQ(l.status.x + l.status.w + 8, l.buttons.x);
    ASSERT_EQ(l.name.x + l.name.w + 8, l.status.x);
    TEST_END();
}

int test_ai_header_layout_long_name_without_status_gets_everything(void)
{
    TEST_BEGIN();
    AiHeaderMeasure m = base_measure(500);
    m.status_w = 0;
    m.name_w = 400;
    AiHeaderLayout l;
    ai_header_layout(&m, &l);
    ASSERT_EQ(l.status_shown, 0);
    ASSERT_EQ(l.status.w, 0);
    ASSERT_EQ(l.name_clipped, 1);
    ASSERT_EQ(l.name.x + l.name.w + 8, l.buttons.x);

    m.name_w = 40;
    ai_header_layout(&m, &l);
    ASSERT_EQ(l.name_clipped, 0);
    ASSERT_EQ(l.name.w, 40);
    TEST_END();
}

int test_ai_header_layout_slot_below_the_minimum_drops_the_status(void)
{
    TEST_BEGIN();
    /* avail = 200-16-108 = 76; slot = 76 - 40 - 8 = 28 < min_status_w 30. */
    AiHeaderMeasure m = base_measure(200);
    m.name_w = 200;
    m.status_w = 90;
    AiHeaderLayout l;
    ai_header_layout(&m, &l);
    ASSERT_EQ(l.status_shown, 0);
    ASSERT_EQ(l.status.w, 0);
    ASSERT_EQ(l.status_clipped, 0);
    ASSERT_EQ(l.name.w, 76);
    ASSERT_EQ(l.name_clipped, 1);

    /* One pixel wider: the slot is exactly the minimum and is kept. */
    m = base_measure(202);
    m.name_w = 200;
    m.status_w = 90;
    ai_header_layout(&m, &l);
    ASSERT_EQ(l.status_shown, 1);
    ASSERT_EQ(l.status.w, 30);
    ASSERT_EQ(l.status_clipped, 1);

    /* Narrower than the buttons: nothing left for the name either. */
    m = base_measure(100);
    ai_header_layout(&m, &l);
    ASSERT_EQ(l.status_shown, 0);
    ASSERT_EQ(l.name.w, 0);
    ASSERT_EQ(l.name_clipped, 1);
    TEST_END();
}

int test_ai_header_layout_no_buttons_no_status(void)
{
    TEST_BEGIN();
    AiHeaderMeasure m = base_measure(200);
    m.buttons_w = 0;
    m.status_w = 0;
    m.name_w = 500;
    AiHeaderLayout l;
    ai_header_layout(&m, &l);
    ASSERT_EQ(l.buttons.w, 0);
    ASSERT_EQ(l.name.x, 8);
    ASSERT_EQ(l.name.w, 200 - 8 - 8);
    ASSERT_EQ(l.name_clipped, 1);
    TEST_END();
}

int test_ai_header_layout_width_sweep_never_overlaps(void)
{
    TEST_BEGIN();
    int bad = 0;
    for (int dpi = 96; dpi <= 192; dpi += 96) {
        int k = dpi / 96;
        for (int hw = 0; hw <= 1200; hw += 7) {
            for (int nw = 0; nw <= 500; nw += 50) {
                for (int sw = 0; sw <= 600; sw += 40) {
                    AiHeaderMeasure m = base_measure(hw);
                    m.pad *= k; m.gap *= k; m.buttons_w *= k; m.btn_h *= k;
                    m.header.h *= k;
                    m.name_w = nw * k; m.status_w = sw * k;
                    m.min_name_w *= k; m.min_status_w *= k;
                    AiHeaderLayout l;
                    ai_header_layout(&m, &l);
                    if (overlaps(l.name, l.status)) bad++;
                    if (overlaps(l.name, l.buttons)) bad++;
                    if (overlaps(l.status, l.buttons)) bad++;
                    if (l.name.w < 0 || l.status.w < 0) bad++;
                    if (l.status_shown && l.status.w <= 0) bad++;
                    if (l.status_shown && l.status.w > m.status_w) bad++;
                    if (l.status_shown && l.status.w < m.min_status_w) bad++;
                    if (!l.status_shown && l.status.w != 0) bad++;
                    if (l.status_shown && l.status_clipped != (l.status.w < m.status_w)) bad++;
                    if (!l.name_clipped && l.name.w != m.name_w) bad++;
                    if (l.name_clipped && l.name.w >= m.name_w) bad++;
                    /* a placed status never leaves the name under its minimum
                     * unless the row itself is too narrow for it */
                    int room = hw - 2 * m.pad - (m.buttons_w + m.gap);
                    int want = m.min_name_w < m.name_w ? m.min_name_w : m.name_w;
                    if (l.status_shown && room >= want && l.name.w < want) bad++;
                    /* the status ends a gap short of the buttons or fits earlier */
                    if (l.status_shown && l.status.x + l.status.w + m.gap > l.buttons.x) bad++;
                }
            }
        }
    }
    ASSERT_EQ(bad, 0);
    TEST_END();
}

int test_ai_header_layout_null_arguments_do_nothing(void)
{
    TEST_BEGIN();
    AiHeaderLayout l;
    l.name.w = 77;
    ai_header_layout(NULL, &l);
    ASSERT_EQ(l.name.w, 77);
    AiHeaderMeasure m = base_measure(500);
    ai_header_layout(&m, NULL);   /* must not crash */
    TEST_END();
}

int test_ai_header_status_pick(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(ai_header_status_pick("Retrying", "Listening", "Thinking"), "Retrying");
    ASSERT_STR_EQ(ai_header_status_pick("", "Listening", "Thinking"), "Listening");
    ASSERT_STR_EQ(ai_header_status_pick(NULL, "Listening", "Thinking"), "Listening");
    ASSERT_STR_EQ(ai_header_status_pick("", "", "Thinking"), "Thinking");
    ASSERT_STR_EQ(ai_header_status_pick(NULL, NULL, "Executing"), "Executing");
    ASSERT_NULL(ai_header_status_pick("", "", ""));
    ASSERT_NULL(ai_header_status_pick(NULL, NULL, NULL));
    TEST_END();
}
