#ifndef VOICE_LEDGER_H
#define VOICE_LEDGER_H

/* Resource ledger: acquire/release counts per voice resource kind, so a test
 * can prove every end of a recording returns everything it took.
 *
 * Process-wide, UI-thread-only by rule (no locking): each acquire and release
 * happens on the UI thread at the call that creates, or joins/closes, the
 * resource. The last four kinds are seams for the conversation PRs. */
typedef enum {
    VRES_CAPTURE = 0,
    VRES_LIVE_SESSION,
    VRES_LIVE_THREAD,
    VRES_STT_UPLOAD,
    VRES_SPVOICE,
    VRES_SPEECH_REQ,
    VRES_RENDER,
    VRES_SESSION_LOCK,
    VRES_COUNT
} VoiceRes;

void voice_ledger_reset(void);               /* zero every count and the underflow flag */
void voice_ledger_acquire(VoiceRes r);
void voice_ledger_release(VoiceRes r);       /* below zero: clamps at 0, sets the underflow flag */
int  voice_ledger_count(VoiceRes r);         /* 0 for an out-of-range kind */
int  voice_ledger_all_zero(void);
int  voice_ledger_underflow(void);

#endif
