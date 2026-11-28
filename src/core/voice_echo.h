#ifndef NUTSHELL_VOICE_ECHO_H
#define NUTSHELL_VOICE_ECHO_H

/*
 * voice_echo -- pure turn logic for conversation mode: the echo filter that
 * drops the microphone picking up Nutshell's own speech, the local
 * "repeat" / "continue" phrases, and the classifier that combines them.
 * Spec: docs/superpowers/specs/2026-11-24-voice-live-conversation-design.md
 * sections 9.4 and 10.
 */

#include <stddef.h>

#define VOICE_ECHO_AFTER_MS     2000
#define VOICE_PHRASE_MAX_AGE_MS 300000

/* 1 when `transcript` is an echo of `spoken`: it has at least 3 words and the
 * union of its runs of 3 or more contiguous words that also occur
 * contiguously in `spoken` covers at least 90% of its words. Words split on
 * any byte that is not an ASCII letter or digit and not >= 0x80; ASCII is
 * lower-cased. */
int voice_echo_match(const char *spoken, const char *transcript);

/* 1 when `transcript` has 1 or 2 words and they occur in `recent` (two words:
 * next to each other, in order): a fragment of the assistant's own words, as
 * heard back after a cut-in. `recent` is the part of the reply said in the
 * VOICE_ECHO_RECENT_MS before the cut. Same word rules as above. A barge-in
 * word ("stop", "wait", "no", "yes", "cancel", "hold on", "pause", "quiet",
 * case and punctuation ignored) is never a fragment, whatever `recent` holds. */
int voice_echo_fragment(const char *recent, const char *transcript);

/* The word events of the speech being played (the offset of each word's first
 * byte in the spoken text, with the time it was reported), newest last, so the
 * text said in the seconds before a cut can be found. */
#define VOICE_ECHO_RECENT_MS 1500
#define VOICE_TRAIL_MAX 64
typedef struct {
    unsigned long at[VOICE_TRAIL_MAX];
    size_t off[VOICE_TRAIL_MAX];
    int n, head;                 /* entries held, index of the oldest */
} VoiceWordTrail;
void voice_trail_reset(VoiceWordTrail *t);
void voice_trail_note(VoiceWordTrail *t, unsigned long at, size_t offset);
/* Copies into out (NUL-terminated, at most cap - 1 bytes) the part of `spoken` from the first
 * word reported within `ms` before cut_at to the end of the last word reported at or before
 * cut_at. Returns the length; 0 and "" when no word was reported in that span. */
size_t voice_trail_recent(const VoiceWordTrail *t, const char *spoken, unsigned long cut_at,
                          unsigned long ms, char *out, size_t cap);

typedef enum { VPH_NONE = 0, VPH_REPEAT, VPH_CONTINUE } VoicePhrase;

/* A short whole-utterance command: at most 6 words, optional leading
 * "please" / "can you" / "could you" (with "please"), optional trailing
 * "please", and exactly one of the phrases. */
VoicePhrase voice_phrase_match(const char *transcript);

typedef enum { VTC_EMPTY = 0, VTC_ECHO, VTC_REPEAT, VTC_CONTINUE, VTC_TEXT } VoiceTurnClass;

typedef struct {
    const char   *spoken;        /* the whole text last spoken (reply or phrase), or NULL */
    int           playing;       /* speech is playing now */
    unsigned long spoken_end_at; /* when it stopped playing */
    const char   *reply;         /* the last spoken reply's speech text, or NULL */
    unsigned long reply_at;      /* when it was last spoken (end or cut) */
    int           read_to_end;   /* it was read to the end */
    int           cut_in;        /* the turn began with a cut-in on `spoken`: it is checked as echo
                                  * however late it arrives */
    const char   *recent;        /* with cut_in: the part of `spoken` said in the 1.5 s before the
                                  * cut (voice_trail_recent), or NULL */
} VoiceReplyCtx;

VoiceTurnClass voice_turn_classify(const char *transcript, const VoiceReplyCtx *r,
                                   unsigned long now);

#endif
