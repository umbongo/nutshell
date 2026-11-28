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
 *    credits the 6 candidates that opened it) accumulate from the first one,
 *    real speech being modulated: the count resets only after 15 consecutive
 *    non-speech windows (300 ms; controller ruling). At 10 (200 ms) VLN_DUCK, VLN_TURN_START and the
 *    pre-roll; at 20 (400 ms) VLN_CUT and the mode becomes VLM_LISTEN with the
 *    turn carrying on (the endpointer takes over). 15 windows (300 ms) without
 *    speech before the cut: VLN_UNDUCK, VLN_TURN_ABANDON.
 *    Windows' canceller has not converged when a reply starts, so when residual
 *    echo is heard in the first 200 ms after voice_listen_audio_started() (the
 *    speech engine's first audio signal), for the first second (50 windows) from
 *    there a speech window counts only if it also is a half-duplex candidate
 *    (10 dB over the echo learned from that same audio). A converged canceller
 *    leaves no residual, and then the detector alone decides from the start.
 *    A user who speaks inside the strict second is cut in on after it, within
 *    about 1.6 s of starting. If the signal has not come 3 s (150 windows)
 *    after the mode was set, the first window at floor + 12 dB starts the
 *    period instead.
 *  - VLM_SPEAK_HD (no echo cancellation): the detector is not run (floor
 *    frozen). The echo estimate starts at the floor. The learning period
 *    starts at voice_listen_audio_started(), not at the mode change (the first
 *    audio of an AI reply often comes later) and not at the first loud
 *    microphone window (a user with no audible echo would be learned as the
 *    echo; 3 s without the signal falls back to that rule): during VL_HD_PRIME = 25 windows (500 ms) from there the estimate
 *    follows the capture level up at once (the builder's call: else our own
 *    reply, entering from a floor 30 dB under it, would count as a cut-in
 *    until the 0.5 dB steps caught up); afterwards it follows the capture
 *    level up by at most 0.5 dB a window and down by 0.1 dB a window, and only
 *    down while the echo is audible: a window at room-noise level (a gap or a
 *    stall in the audio) leaves it alone. Nothing is a candidate before that.
 *    A user who speaks during the learning period is learned as the echo, so
 *    after it an estimate that nothing confirms fades: once no window has come
 *    within 6 dB of the estimate (or louder) for 25 windows (500 ms), it falls
 *    1 dB a window down to -33 dBFS, never below. (A user at -15 dBFS over a
 *    -60 dBFS room is cut in on within 500 ms of trying again, 1 s after the
 *    estimate began to fall.) The ceiling keeps real echo that pauses and
 *    returns at its old level from becoming a candidate: that holds for echo up
 *    to about -24 dBFS at the microphone. A user who talks without a pause of
 *    500 ms from inside the learning period keeps confirming the estimate and
 *    is not heard until a pause; the voice is the same as a loud echo to this
 *    code.
 *    It does not move while a cut-in is accumulating (the count is above 0),
 *    so neither the user's voice nor its modulation dips lift it. A candidate window has level >= max(floor + 12, echo + 10) and
 *        >= -55 dBFS. Candidates accumulate under the same 15-window rule. At 10 candidates VLN_DUCK
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
    int hd_on;                  /* the reply's audio is playing (voice_listen_audio_started, or the fallback) */
    int mode_win;               /* windows since the speak mode was set, capped at 150 (3 s) */
    int hd_win;                 /* windows since then (learning period) */
    int hd_resid;               /* the reply's audio was audible in its first 200 ms: AEC's strict second applies */
    int hd_conf;                /* windows since one came within 6 dB of the estimate (after learning) */
    int hd_age;                 /* windows since then, capped at 51 (AEC's strict first second) */
} VoiceListen;

void voice_listen_init(VoiceListen *l, int pause_ms);
void voice_listen_set_mode(VoiceListen *l, VoiceListenMode m);  /* resets cut-in counters and
                                         any turn in progress (abandoned silently, no event) */
void voice_listen_audio_started(VoiceListen *l);   /* the reply's audio is playing now (the speech
                                         engine's first audio signal): the echo-learning period and
                                         AEC's strict first second start here. No-op outside the
                                         speak modes. */
void voice_listen_feed(VoiceListen *l, const int16_t *s, size_t n, const VoiceListenSink *k);
void voice_listen_wipe(VoiceListen *l);   /* secure_zero the ring and partial window */

#endif
