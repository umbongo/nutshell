#ifndef NUTSHELL_VOICE_ECHO_H
#define NUTSHELL_VOICE_ECHO_H

/*
 * voice_echo -- pure turn logic for conversation mode: the echo filter that
 * drops the microphone picking up Nutshell's own speech, the local
 * "repeat" / "continue" phrases, and the classifier that combines them.
 * Spec: docs/superpowers/specs/2026-11-24-voice-live-conversation-design.md
 * sections 9.4 and 10.
 */

#define VOICE_ECHO_AFTER_MS     2000
#define VOICE_PHRASE_MAX_AGE_MS 300000

/* 1 when `transcript` is an echo of `spoken`: it has at least 3 words and the
 * union of its runs of 3 or more contiguous words that also occur
 * contiguously in `spoken` covers at least 90% of its words. Words split on
 * any byte that is not an ASCII letter or digit and not >= 0x80; ASCII is
 * lower-cased. */
int voice_echo_match(const char *spoken, const char *transcript);

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
} VoiceReplyCtx;

VoiceTurnClass voice_turn_classify(const char *transcript, const VoiceReplyCtx *r,
                                   unsigned long now);

#endif
