#include "test_framework.h"
#include "voice_test.h"

int test_voice_test_transitions(void)
{
    TEST_BEGIN();
    int running;

    /* stopped */
    running = 0;
    ASSERT_EQ(voice_test_next(&running, VT_EV_PRESS, 1), VT_ACT_START);
    ASSERT_EQ(running, 1);
    running = 0;
    ASSERT_EQ(voice_test_next(&running, VT_EV_PRESS, 0), VT_ACT_NONE);
    ASSERT_EQ(running, 0);
    for (int ev = VT_EV_CLOSE; ev <= VT_EV_CAPTURE_FAILED; ev++) {
        for (int cs = 0; cs <= 1; cs++) {
            running = 0;
            ASSERT_EQ(voice_test_next(&running, (VoiceTestEvent)ev, cs), VT_ACT_NONE);
            ASSERT_EQ(running, 0);
        }
    }

    /* running: can_start is irrelevant */
    for (int cs = 0; cs <= 1; cs++) {
        static const VoiceTestEvent stops[] = { VT_EV_PRESS, VT_EV_CLOSE, VT_EV_PAGE_AWAY,
                                                VT_EV_VOICE_OFF, VT_EV_CAPTURE_FAILED };
        for (size_t i = 0; i < sizeof stops / sizeof stops[0]; i++) {
            running = 1;
            ASSERT_EQ(voice_test_next(&running, stops[i], cs), VT_ACT_STOP);
            ASSERT_EQ(running, 0);
        }
        running = 1;
        ASSERT_EQ(voice_test_next(&running, VT_EV_DEVICE_CHANGED, cs), VT_ACT_RESTART);
        ASSERT_EQ(running, 1);
    }
    TEST_END();
}

int test_voice_test_button_label(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(voice_test_button_label(0), "Test");
    ASSERT_STR_EQ(voice_test_button_label(1), "Stop test");
    TEST_END();
}
