#ifndef NUTSHELL_VOICE_VAD_H
#define NUTSHELL_VOICE_VAD_H

#include <stddef.h>
#include <stdint.h>

/*
 * voice_vad -- adaptive speech detector and pause endpointer for the
 * conversation mode (spec 2026-11-24-voice-live-conversation-design.md 5.3).
 * Portable, no heap, no Win32. Fed one 20 ms window (VVAD_WIN samples at
 * 16 kHz) at a time.
 *
 * Detector numbers (the builder's call, spec 5.3):
 *  - the noise floor starts at the first window's level, falls at once to any
 *    lower level, and on a window that is not a candidate and outside a speech
 *    burst rises by at most 0.05 dB (2.5 dB/s). Frozen: it does not move.
 *  - a window is a candidate when level >= floor + 12 dB + extra_db and
 *    level >= -55 dBFS;
 *  - a burst starts when 6 of the last 8 windows are candidates (that window
 *    returns VV_SPEECH) and ends after 15 consecutive non-candidate windows
 *    (300 ms); inside a burst a candidate window is VV_SPEECH;
 *  - stationarity: once a burst has run 50 windows (1 s), if the last 50
 *    windows span less than 4 dB the burst is noise: the floor jumps to their
 *    mean, the burst ends and that window returns VV_NOISE_RESET. (So speech
 *    that is flat for a second counts as noise; real speech varies by more.)
 *
 *  - impulse: a burst whose peak (a flat top counts as its first window; a later
 *    window must top it by more than 1 dB to move it) is in its first 2 windows
 *    (counted from its first candidate window), in which no window rises 3 dB or more over the
 *    window before it, and whose last candidate window is at least 15 dB below
 *    the peak (a monotonic decay: a door slam with its tail) is an impulse. The 15 dB
 *    (it was 6) is a ruling (review round 3) that dropping a real word costs
 *    more than letting a slam through: a slam decays 30 dB or more, a falling
 *    word about 8. Only candidate
 *    windows are judged (quiet tail windows are ignored), and a burst of more
 *    than 40 candidate windows is never an impulse. At
 *    the burst's end the detector returns VV_IMPULSE instead of VV_SILENCE and
 *    the endpointer takes that burst's windows back out of the turn's speech.
 *  - digital silence: windows at or below -85 dBFS never change the floor (and
 *    never start it), and the floor never goes below -75 dBFS, so a muted or
 *    warming-up mic does not sink the floor.
 *  Known limit: slowly modulated real noise (a fan or HVAC with more than 4 dB
 *  swing over 1 s) is not reset by the stationarity rule; it is capped at 120 s
 *  by the endpointer. For the hand checks.
 *
 * Endpointer: VEP_START on the first VV_SPEECH outside a turn (the six
 * candidate windows that opened the burst are credited as speech); inside a
 * turn VEP_END when pause_ms has passed since the last speech window and the
 * turn holds at least VOICE_SHORT_TAP_MS of speech windows, else VEP_ABANDON;
 * VEP_CAP 6000 windows (120 s) after VEP_START; VV_NOISE_RESET gives
 * VEP_ABANDON. After END, CAP or ABANDON it is out of the turn.
 */

#define VVAD_WIN 320                 /* 20 ms at 16 kHz */
#define VVAD_RING 50
#define VVAD_START_NEED 6      /* candidates (of the last 8) that open a burst; the endpointer credits them */

/* RMS in dBFS of n samples, floored at -90 (n == 0 gives -90). */
double voice_window_db(const int16_t *s, size_t n);

typedef enum { VV_SILENCE = 0, VV_SPEECH, VV_NOISE_RESET, VV_IMPULSE } VoiceVadOut;

typedef struct {
    double floor_db;
    double extra_db;
    double ring[VVAD_RING];
    unsigned hist;          /* last 8 candidate flags, bit 0 newest */
    int ring_pos;
    int burst_windows;      /* windows seen in the current burst */
    int nc_run;             /* consecutive non-candidate windows in a burst */
    int in_burst;
    int frozen;
    int started;
    double lev[8];          /* last 8 window levels, lev[7] newest */
    /* impulse tracking, from the first candidate window of the burst */
    int imp_pos;
    int imp_cands;
    int imp_peak_pos;
    int imp_rose;
    double imp_peak, imp_prev, imp_last_cand;
} VoiceVad;

void        voice_vad_init(VoiceVad *v);
void        voice_vad_set_extra_db(VoiceVad *v, double extra_db);  /* added to the margin (cut-in) */
void        voice_vad_freeze_floor(VoiceVad *v, int frozen);       /* while our speech plays */
VoiceVadOut voice_vad_window(VoiceVad *v, double level_db);        /* one 20 ms window */
double      voice_vad_floor_db(const VoiceVad *v);

typedef enum { VEP_NONE = 0, VEP_START, VEP_END, VEP_CAP, VEP_ABANDON } VoiceEpOut;

typedef struct {
    int pause_windows;
    int in_turn;
    int since_speech;       /* windows since the last speech window */
    int speech_windows;
    int turn_windows;
    int burst_speech;       /* speech windows of the burst in progress */
} VoiceEndpoint;

void       voice_ep_init(VoiceEndpoint *e, int pause_ms);   /* pause clamped 500..3000 */
void       voice_ep_reset(VoiceEndpoint *e);                /* out of any turn */
VoiceEpOut voice_ep_window(VoiceEndpoint *e, VoiceVadOut w);
int        voice_ep_in_turn(const VoiceEndpoint *e);

#endif
