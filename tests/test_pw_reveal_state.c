/* tests/test_pw_reveal_state.c -- src/core/pw_reveal_state.h: the
 * hold-to-reveal state machine behind the password eye button. */
#include "test_framework.h"
#include "pw_reveal_state.h"

int test_pw_reveal_press_reveals_only_with_text(void)
{
    TEST_BEGIN();
    PwRevealState s;
    pw_reveal_init(&s);
    ASSERT_EQ(pw_reveal_feed(&s, PWR_PRESS, 1), 1);
    ASSERT_EQ(s.revealed, 1);
    pw_reveal_init(&s);
    ASSERT_EQ(pw_reveal_feed(&s, PWR_PRESS, 0), 0);
    ASSERT_EQ(s.revealed, 0);
    TEST_END();
}

int test_pw_reveal_release_masks(void)
{
    TEST_BEGIN();
    PwRevealState s;
    pw_reveal_init(&s);
    pw_reveal_feed(&s, PWR_PRESS, 1);
    ASSERT_EQ(pw_reveal_feed(&s, PWR_RELEASE, 1), 1);
    ASSERT_EQ(s.revealed, 0);
    ASSERT_EQ(s.pressed, 0);
    ASSERT_EQ(pw_reveal_feed(&s, PWR_RELEASE, 1), 0);
    TEST_END();
}

int test_pw_reveal_capture_lost_and_deactivate_mask(void)
{
    TEST_BEGIN();
    PwRevealState s;
    pw_reveal_init(&s);
    pw_reveal_feed(&s, PWR_PRESS, 1);
    pw_reveal_feed(&s, PWR_CAPTURE_LOST, 1);
    ASSERT_EQ(s.revealed, 0);
    ASSERT_EQ(s.pressed, 0);
    pw_reveal_feed(&s, PWR_PRESS, 1);
    pw_reveal_feed(&s, PWR_DEACTIVATE, 1);
    ASSERT_EQ(s.revealed, 0);
    ASSERT_EQ(s.pressed, 0);
    TEST_END();
}

int test_pw_reveal_leave_masks_enter_while_held_reveals(void)
{
    TEST_BEGIN();
    PwRevealState s;
    pw_reveal_init(&s);
    pw_reveal_feed(&s, PWR_PRESS, 1);
    pw_reveal_feed(&s, PWR_LEAVE, 1);
    ASSERT_EQ(s.revealed, 0);
    ASSERT_EQ(s.pressed, 1);
    pw_reveal_feed(&s, PWR_ENTER, 1);
    ASSERT_EQ(s.revealed, 1);
    pw_reveal_feed(&s, PWR_RELEASE, 1);
    ASSERT_EQ(s.revealed, 0);
    TEST_END();
}

int test_pw_reveal_enter_without_press_stays_masked(void)
{
    TEST_BEGIN();
    PwRevealState s;
    pw_reveal_init(&s);
    ASSERT_EQ(pw_reveal_feed(&s, PWR_ENTER, 1), 0);
    ASSERT_EQ(s.revealed, 0);
    TEST_END();
}

int test_pw_reveal_emptied_masks_until_next_press(void)
{
    TEST_BEGIN();
    PwRevealState s;
    pw_reveal_init(&s);
    pw_reveal_feed(&s, PWR_PRESS, 1);
    ASSERT_EQ(pw_reveal_feed(&s, PWR_EMPTIED, 0), 1);
    ASSERT_EQ(s.revealed, 0);
    pw_reveal_feed(&s, PWR_ENTER, 0);
    ASSERT_EQ(s.revealed, 0);
    pw_reveal_feed(&s, PWR_RELEASE, 0);
    pw_reveal_feed(&s, PWR_PRESS, 1);
    ASSERT_EQ(s.revealed, 1);
    TEST_END();
}

int test_pw_reveal_destroy_masks_and_clears_press(void)
{
    TEST_BEGIN();
    PwRevealState s;
    pw_reveal_init(&s);
    pw_reveal_feed(&s, PWR_PRESS, 1);
    pw_reveal_feed(&s, PWR_DESTROY, 1);
    ASSERT_EQ(s.revealed, 0);
    ASSERT_EQ(s.pressed, 0);
    TEST_END();
}

int test_pw_reveal_null_safe(void)
{
    TEST_BEGIN();
    pw_reveal_init(NULL);
    ASSERT_EQ(pw_reveal_feed(NULL, PWR_PRESS, 1), 0);
    TEST_END();
}
