#include "test_framework.h"
#include "ai_header_layout.h"

/* The AI panel header, laid out right to left: the button cluster, the
 * model chip, the status word (dot + activity word), and the session name
 * with whatever is left. The word must never sit under the chip or the
 * buttons (the v1.2.73 bug: a long session name pushed it there). */

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
    m.chip_w = 90;
    m.chip_h = 20;
    m.status_w = 70;
    m.name_w = 60;
    return m;
}

int test_ai_header_layout_short_name_keeps_everything_in_order(void)
{
    TEST_BEGIN();
    AiHeaderMeasure m = base_measure(500);
    AiHeaderLayout l;
    ai_header_layout(&m, &l);

    /* Right to left: buttons end pad from the edge, chip a gap left of them. */
    ASSERT_EQ(l.buttons.x + l.buttons.w, 500 - 8);
    ASSERT_EQ(l.buttons.w, 100);
    ASSERT_EQ(l.chip.x + l.chip.w + 8, l.buttons.x);
    ASSERT_EQ(l.chip.w, 90);
    /* The name sits at the left, in full; the word follows it. */
    ASSERT_EQ(l.name.x, 8);
    ASSERT_EQ(l.name.w, 60);
    ASSERT_EQ(l.name_clipped, 0);
    ASSERT_EQ(l.status_shown, 1);
    ASSERT_EQ(l.status.x, l.name.x + l.name.w + 8);
    ASSERT_EQ(l.status.w, 70);
    ASSERT_TRUE(l.status.x + l.status.w <= l.chip.x);
    /* Chip and buttons are centred vertically in the header. */
    ASSERT_EQ(l.chip.y, 6);
    ASSERT_EQ(l.buttons.y, 2);
    TEST_END();
}

int test_ai_header_layout_long_name_is_ellipsised_and_word_stays_clear(void)
{
    TEST_BEGIN();
    AiHeaderMeasure m = base_measure(500);
    m.name_w = 400;   /* "automaton.dev.westernsydney.edu" and then some */
    AiHeaderLayout l;
    ai_header_layout(&m, &l);

    ASSERT_EQ(l.name_clipped, 1);
    ASSERT_EQ(l.status_shown, 1);
    ASSERT_EQ(l.status.w, 70);
    /* The word is flush against the chip (one gap), not under it. */
    ASSERT_EQ(l.status.x + l.status.w + 8, l.chip.x);
    /* The name takes what is left of the row, a gap short of the word. */
    ASSERT_EQ(l.name.x, 8);
    ASSERT_EQ(l.name.x + l.name.w + 8, l.status.x);
    ASSERT_TRUE(!overlaps(l.name, l.status));
    ASSERT_TRUE(!overlaps(l.status, l.chip));
    ASSERT_TRUE(!overlaps(l.chip, l.buttons));
    ASSERT_TRUE(!overlaps(l.name, l.chip));
    TEST_END();
}

int test_ai_header_layout_name_that_just_fits_is_not_clipped(void)
{
    TEST_BEGIN();
    AiHeaderMeasure m = base_measure(500);
    AiHeaderLayout probe;
    ai_header_layout(&m, &probe);
    /* Room for the name is the span from the left pad to the word's slot. */
    int room = probe.chip.x - 8 - 70 - 8 - 8;
    m.name_w = room;
    AiHeaderLayout l;
    ai_header_layout(&m, &l);
    ASSERT_EQ(l.name_clipped, 0);
    ASSERT_EQ(l.name.w, room);
    ASSERT_EQ(l.status.x, l.name.x + l.name.w + 8);
    ASSERT_EQ(l.status.x + l.status.w + 8, l.chip.x);

    m.name_w = room + 1;
    ai_header_layout(&m, &l);
    ASSERT_EQ(l.name_clipped, 1);
    ASSERT_EQ(l.name.w, room);
    ASSERT_EQ(l.status.x + l.status.w + 8, l.chip.x);
    TEST_END();
}

int test_ai_header_layout_no_status_word_gives_the_name_everything(void)
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
    ASSERT_EQ(l.name.x + l.name.w + 8, l.chip.x);

    m.name_w = 40;
    ai_header_layout(&m, &l);
    ASSERT_EQ(l.name_clipped, 0);
    ASSERT_EQ(l.name.w, 40);
    TEST_END();
}

int test_ai_header_layout_no_room_for_the_word_drops_it(void)
{
    TEST_BEGIN();
    /* 8 pad + 100 buttons + 8 gap + 90 chip + 8 gap = 214 from the right;
     * 8 pad on the left leaves 500-... shrink the header until the word
     * (70) no longer fits beside the chip. */
    AiHeaderMeasure m = base_measure(300);   /* room = 300-8-214 = 78+... */
    m.name_w = 200;
    m.status_w = 90;
    AiHeaderLayout l;
    ai_header_layout(&m, &l);
    ASSERT_EQ(l.status_shown, 0);
    ASSERT_EQ(l.status.w, 0);
    ASSERT_TRUE(!overlaps(l.name, l.chip));
    ASSERT_TRUE(!overlaps(l.name, l.buttons));
    ASSERT_EQ(l.name.x + l.name.w + 8 <= l.chip.x, 1);

    /* Narrower still: nothing is left for the name, and nothing overlaps. */
    m = base_measure(150);
    ai_header_layout(&m, &l);
    ASSERT_EQ(l.status_shown, 0);
    ASSERT_EQ(l.name.w, 0);
    ASSERT_EQ(l.name_clipped, 1);
    ASSERT_TRUE(!overlaps(l.chip, l.buttons));
    TEST_END();
}

int test_ai_header_layout_zero_width_chip_takes_no_gap(void)
{
    TEST_BEGIN();
    AiHeaderMeasure m = base_measure(500);
    m.chip_w = 0;
    m.name_w = 400;
    AiHeaderLayout l;
    ai_header_layout(&m, &l);
    ASSERT_EQ(l.chip.w, 0);
    /* With no chip the word sits one gap left of the buttons. */
    ASSERT_EQ(l.status.x + l.status.w + 8, l.buttons.x);
    ASSERT_EQ(l.status_shown, 1);
    ASSERT_TRUE(!overlaps(l.name, l.status));
    TEST_END();
}

int test_ai_header_layout_no_buttons_no_chip_no_word(void)
{
    TEST_BEGIN();
    AiHeaderMeasure m = base_measure(200);
    m.buttons_w = 0;
    m.chip_w = 0;
    m.status_w = 0;
    m.name_w = 500;
    AiHeaderLayout l;
    ai_header_layout(&m, &l);
    ASSERT_EQ(l.buttons.w, 0);
    ASSERT_EQ(l.chip.w, 0);
    ASSERT_EQ(l.name.x, 8);
    ASSERT_EQ(l.name.w, 200 - 8 - 8);
    ASSERT_EQ(l.name_clipped, 1);
    TEST_END();
}

int test_ai_header_layout_width_sweep_never_overlaps(void)
{
    TEST_BEGIN();
    int bad = 0;
    for (int hw = 0; hw <= 600; hw += 7) {
        for (int nw = 0; nw <= 500; nw += 50) {
            for (int sw = 0; sw <= 120; sw += 40) {
                for (int cw = 0; cw <= 100; cw += 50) {
                    AiHeaderMeasure m = base_measure(hw);
                    m.name_w = nw; m.status_w = sw; m.chip_w = cw;
                    AiHeaderLayout l;
                    ai_header_layout(&m, &l);
                    if (overlaps(l.name, l.status)) bad++;
                    if (overlaps(l.name, l.chip)) bad++;
                    if (overlaps(l.name, l.buttons)) bad++;
                    if (overlaps(l.status, l.chip)) bad++;
                    if (overlaps(l.status, l.buttons)) bad++;
                    if (overlaps(l.chip, l.buttons)) bad++;
                    if (l.name.w < 0 || l.status.w < 0 || l.chip.w < 0) bad++;
                    if (l.status_shown && l.status.w != sw) bad++;
                    if (!l.status_shown && l.status.w != 0) bad++;
                    if (!l.name_clipped && l.name.w != nw) bad++;
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
