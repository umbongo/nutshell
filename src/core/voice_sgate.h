#ifndef NUTSHELL_CORE_VOICE_SGATE_H
#define NUTSHELL_CORE_VOICE_SGATE_H

#include <stddef.h>
#include <stdint.h>

/* Streaming speech gate for live dictation: only speech is sent to the
 * provider.  Input is 16 kHz mono int16, any chunk size; it is judged in
 * 20 ms windows (a partial window waits for its remainder).
 *
 * Closed -> open after VSG_OPEN_WINDOWS consecutive speech windows; on opening
 * the whole pre-roll ring (the last up-to-15 windows, 300 ms, which already
 * includes the opening windows) is emitted, each window once.  While open every
 * window is emitted; after hangover_ms of consecutive non-speech windows (those
 * windows are emitted too) the gate closes and closed() is called.  The ring is
 * emptied on opening and on closing, so a later utterance never replays older
 * audio.  Audio is user content: the ring and partial window are wiped with
 * secure_zero on init and reset. */

#define VSG_WIN 320
#define VSG_PREROLL_MS 300
#define VSG_PREROLL_WINDOWS (VSG_PREROLL_MS / 20)
#define VSG_OPEN_WINDOWS 2

typedef struct {
    void (*audio)(void *u, const int16_t *s, size_t n);
    void (*closed)(void *u);
    void *u;
} VoiceSgateSink;

typedef struct {
    int16_t ring[VSG_PREROLL_WINDOWS][VSG_WIN];
    int16_t part[VSG_WIN];
    size_t npart;            /* samples in the partial window */
    size_t ring_head;        /* slot of the oldest window */
    size_t ring_count;       /* windows held, <= VSG_PREROLL_WINDOWS */
    int open;
    int speech_run;          /* consecutive speech windows while closed */
    int silence_run;         /* consecutive non-speech windows while open */
    int threshold_rms;       /* int16 RMS at or above which a window is speech */
    int hangover_windows;
} VoiceSgate;

void voice_sgate_init(VoiceSgate *g, int threshold_rms, int hangover_ms);
/* Wipe all audio and return to the closed state; keeps threshold and hangover. */
void voice_sgate_reset(VoiceSgate *g);
void voice_sgate_feed(VoiceSgate *g, const int16_t *s, size_t n, const VoiceSgateSink *k);
/* Stop: emit any partial window if open, then closed() if it was open. */
void voice_sgate_flush(VoiceSgate *g, const VoiceSgateSink *k);
int  voice_sgate_open(const VoiceSgate *g);

#endif
