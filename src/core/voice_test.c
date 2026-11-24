#include "voice_test.h"

VoiceTestAction voice_test_next(int *running, VoiceTestEvent ev, int can_start)
{
    if (!*running) {
        if (ev == VT_EV_PRESS && can_start) {
            *running = 1;
            return VT_ACT_START;
        }
        return VT_ACT_NONE;
    }
    if (ev == VT_EV_DEVICE_CHANGED) return VT_ACT_RESTART;
    *running = 0;
    return VT_ACT_STOP;
}

const char *voice_test_button_label(int running)
{
    return running ? "Stop test" : "Test";
}
