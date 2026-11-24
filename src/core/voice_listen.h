#ifndef NUTSHELL_VOICE_LISTEN_H
#define NUTSHELL_VOICE_LISTEN_H

#include "voice_vad.h"
#include <stddef.h>
#include <stdint.h>

/*
 * voice_listen -- the conversation listener: pre-roll, turns and two-stage
 * cut-in (spec 2026-11-24-voice-live-conversation-design.md 5.3, 9.1, 9.2).
 * Portable, no heap, no Win32; the audio thread feeds it every captured
 * buffer and it cuts the stream into 20 ms windows (VVAD_WIN samples).
 *
 * Modes:
 *  - VLM_HOLD: windows update the detector's floor only; no events, no audio.
 *  - VLM_LISTEN: VLN_TURN_START, then the pre-roll ring (each window once,
 *    oldest first, the window that started the turn last), then every window
 *    to the turn's end; VEP_END/CAP/ABANDON become VLN_TURN_END/CAP/ABANDON.
 *    The ring (VL_PREROLL = 15 windows, 300 ms) holds the windows of the
 *    burst's start (the detector needs VVAD_START_NEED = 6 candidates) plus
 *    the lead-in; it is emptied at turn start and end.
 *  - VLM_SPEAK_AEC (echo cancellation on): floor frozen, detector margin
 *    +6 dB. Cut-in speech windows (VV_SPEECH; the burst's first VV_SPEECH
 *    credits the 6 candidates that opened it): at 10 (200 ms), with no more
 *    than 2 non-speech windows between them, VLN_DUCK, VLN_TURN_START and the
 *    pre-roll; at 20 (400 ms) VLN_CUT and the mode becomes VLM_LISTEN with the
 *    turn carrying on (the endpointer takes over). 15 windows (300 ms) without
 *    speech before the cut: VLN_UNDUCK, VLN_TURN_ABANDON.
 *  - VLM_SPEAK_HD (no echo cancellation): the detector is not run (floor
 *    frozen). The echo estimate starts at the floor; during the first
 *    VL_HD_PRIME = 25 windows (500 ms) it follows the capture level at once
 *    (else our own reply, entering from a floor 30 dB under it, would count as
 *    a cut-in until the 0.5 dB steps caught up); afterwards it follows the
 *    capture level up by at most 0.5 dB a window and down by 0.1 dB a window.
 *    It does not rise on a candidate window, so the user's voice does not lift
 *    it. A candidate window has level >= max(floor + 12, echo + 10) and
 *    >= -55 dBFS. At 10 candidates (with the same 2-window gap rule) VLN_DUCK
 *    only; at 20 VLN_CUT, VLN_TURN_START with no pre-roll and no audio from
 *    before the cut (the cut window is the first audio), then listening.
 *    Un-duck: 15 windows without a candidate after the duck gives VLN_UNDUCK
 *    only (no turn had started).
 * These numbers are the builder's call. Measured timing: with voice from
 * window V, the detector's first VV_SPEECH is window V+5, so DUCK and (AEC)
 * TURN_START fall on window V+9 (AEC) and V+9 (HD, candidate 10), CUT on V+19;
 * see the tests.
 *
 * Audio is user content: the ring and the partial window are secure_zero'd
 * when a turn starts or ends, on set_mode and in voice_listen_wipe.
 */

#define VL_PREROLL 15
#define VL_HD_PRIME 25

typedef enum { VLM_HOLD = 0, VLM_LISTEN, VLM_SPEAK_AEC, VLM_SPEAK_HD } VoiceListenMode;
typedef enum { VLN_TURN_START = 1, VLN_TURN_END, VLN_TURN_CAP, VLN_TURN_ABANDON,
               VLN_DUCK, VLN_UNDUCK, VLN_CUT } VoiceListenEvent;

typedef struct {
    void (*audio)(void *u, const int16_t *s, size_t n);   /* this turn's audio, in order */
    void (*event)(void *u, VoiceListenEvent e);
    void *u;
} VoiceListenSink;

typedef struct {
    VoiceVad vad;
    VoiceEndpoint ep;
    int16_t ring[VL_PREROLL][VVAD_WIN];
    int ring_n;                 /* windows held */
    int ring_head;              /* index of the oldest */
    int16_t part[VVAD_WIN];
    size_t part_n;
    VoiceListenMode mode;
    /* cut-in */
    int ci_speech;              /* speech/candidate windows, with the start credit */
    int ci_gap;                 /* consecutive non-speech windows */
    int ci_total;               /* windows since the first speech window */
    int ci_started;             /* a cut-in turn has started (AEC) / duck given (HD) */
    int ci_ducked;
    double echo_db;
    int hd_win;                 /* windows since entering HD */
} VoiceListen;

void voice_listen_init(VoiceListen *l, int pause_ms);
void voice_listen_set_mode(VoiceListen *l, VoiceListenMode m);  /* resets cut-in counters and
                                         any turn in progress (abandoned silently, no event) */
void voice_listen_feed(VoiceListen *l, const int16_t *s, size_t n, const VoiceListenSink *k);
void voice_listen_wipe(VoiceListen *l);   /* secure_zero the ring and partial window */

#endif
