#ifndef NUTSHELL_VOICE_TEST_H
#define NUTSHELL_VOICE_TEST_H

/* State machine of Settings' microphone Test toggle. Pure, no Win32. */

typedef enum {
    VT_EV_PRESS, VT_EV_CLOSE, VT_EV_PAGE_AWAY,
    VT_EV_DEVICE_CHANGED, VT_EV_VOICE_OFF, VT_EV_CAPTURE_FAILED
} VoiceTestEvent;

typedef enum { VT_ACT_NONE, VT_ACT_START, VT_ACT_STOP, VT_ACT_RESTART } VoiceTestAction;

/* Stopped: PRESS starts only when can_start (device list loaded, voice and
 * AI ticked and running); everything else is ignored. Running: PRESS, CLOSE,
 * PAGE_AWAY, VOICE_OFF and CAPTURE_FAILED stop; DEVICE_CHANGED restarts so
 * the meter follows the new microphone. Updates *running. */
VoiceTestAction voice_test_next(int *running, VoiceTestEvent ev, int can_start);

const char *voice_test_button_label(int running);

#endif
