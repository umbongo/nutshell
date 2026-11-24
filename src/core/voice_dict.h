#ifndef VOICE_DICT_H
#define VOICE_DICT_H

/* Dictation recording controller: the lifecycle of one dictation recording
 * after the key machine (voice_ptt) has opened it, live or batch. Pure and
 * time-injected: the Win32 glue feeds it events and carries out the returned
 * actions. Spec: voice-live design §4.3 (sending), §4.4 (every end), §4.5
 * (live connection failure).
 *
 * Conventions the glue relies on:
 *  - An event that means nothing in the current state returns {0, VDT_NONE}
 *    and changes nothing. In particular VDE_CAPTURED in VDS_IDLE (the live
 *    final already finished the recording) returns nothing: the glue frees
 *    that capture's audio itself.
 *  - VDE_CAPTURED in IDLE with a = 1 returns VDA_DROP_AUDIO only (the glue
 *    frees that audio; nothing else changes); with a = 0 nothing.
 *  - VDE_CAPTURED while RECORDING means the audio thread ended the capture
 *    itself (the length cap or a fault): it is handled as VDE_STOP followed by
 *    the CAPTURED handling, in one step (actions OR'ed). captured/have_audio
 *    are only ever set while FINISHING.
 *  - The 3 s wait is armed whenever a live recording enters FINISHING with the
 *    link not FAILED (DONE included), so FINISHING always has an exit. A TICK
 *    past it with the link DONE finishes with the send rule (the final did
 *    arrive); with the link still open it finishes without sending.
 *  - VDA_SEND_BOX may arrive with an empty box (a live final with no words):
 *    the glue then shows "Didn't catch that" and does not send.
 *  - Finishing (UNLOCK_TYPING, back to IDLE) clears every per-recording flag,
 *    so the next VDE_START begins clean.
 *  - VDA_SEND_BOX is set whenever the finish rule says "send": (send_after or
 *    auto_ok) and not no_send and the final text arrived (live final, or the
 *    batch/fallback result). That is one rule for live and batch alike.
 *    For a batch recording or a fallback, VDA_DELIVER_BATCH and VDA_SEND_BOX
 *    arrive together: the glue delivers (appends, or replaces the live text
 *    for a fallback) FIRST and then sends. DELIVER_BATCH never sends on its
 *    own; today's auto-send path is expressed as SEND_BOX too.
 *  - VDA_CLOSE_LINK, VDA_END_CAPTURE_DISCARD and VDA_CANCEL_BATCH are
 *    idempotent for the glue: they apply only to what is still open.
 *  - A live recording whose final text is empty is still a normal finish; the
 *    glue shows "Didn't catch that" when the box gained nothing.
 *  - A live recording's CAPTURED(0) only records that no audio is kept.
 *  - After a stop the audio thread itself sends the gate flush and end
 *    messages, so there is no "finish link" action.
 *  - Send pressed while a fallback upload runs waits for it (no 3 s limit:
 *    the upload's own timeouts bound it) and returns VDT_WILL_SEND.
 *  - The final deadline with the link still open and audio kept (or the
 *    capture still due) is a link failure: fallback, CLOSE_LINK, the batch
 *    upload (when the capture is in), VDT_LIVE_STOPPED. Send stays pending and
 *    follows the fallback result. With no audio at all it finishes without
 *    sending; the glue shows "Didn't catch that" when the box gained nothing.
 *  - Escape while FINISHING with an upload running cancels it (CANCEL_BATCH,
 *    the live text stays, nothing is sent, VDT_CANCELLED).
 */

typedef enum { VDS_IDLE = 0, VDS_RECORDING, VDS_FINISHING } VoiceDictState;
typedef enum { VDL_NONE = 0, VDL_CONNECTING, VDL_READY, VDL_FAILED, VDL_DONE } VoiceDictLink;

typedef enum {
    VDE_START,          /* a = live (1/0), b = auto_send allowed (auto_send on and box empty) */
    VDE_STOP,           /* key, mic button, the cap: finish */
    VDE_SEND,           /* Send or Enter while RECORDING or FINISHING */
    VDE_ESCAPE,         /* Escape in the panel: RECORDING discards; FINISHING with an upload cancels it */
    VDE_DISCARD,        /* a real discard that is not Escape (Hold short tap, chord): same as
                         * Escape while RECORDING, nothing otherwise */
    VDE_END_SOFT,       /* deactivate, modal, chat change, device lost: finish, never send */
    VDE_END_HARD,       /* voice/AI off, voice settings changed: close at once, never send */
    VDE_LINK_READY,
    VDE_LINK_FAILED,    /* a = 1: it never opened */
    VDE_LINK_FINAL,     /* the provider's text for this recording is complete */
    VDE_CAPTURED,       /* a = 1: audio kept and has speech; 0: none or silent */
    VDE_BATCH_OK,       /* the batch upload (batch recording or fallback) */
    VDE_BATCH_FAIL,
    VDE_TICK
} VoiceDictEvent;

enum {  /* action bits */
    VDA_OPEN_CAPTURE        = 1u << 0,
    VDA_OPEN_LINK           = 1u << 1,
    VDA_END_CAPTURE_KEEP    = 1u << 2,
    VDA_END_CAPTURE_DISCARD = 1u << 3,
    VDA_CLOSE_LINK          = 1u << 4,
    VDA_REMOVE_WORDS        = 1u << 5,
    VDA_SUBMIT_BATCH        = 1u << 6,   /* upload the kept audio: batch recording or fallback */
    VDA_DROP_AUDIO          = 1u << 7,
    VDA_SEND_BOX            = 1u << 8,
    VDA_LOCK_TYPING         = 1u << 9,
    VDA_UNLOCK_TYPING       = 1u << 10,
    VDA_CANCEL_BATCH        = 1u << 11,
    VDA_DELIVER_BATCH       = 1u << 12
};

typedef enum {
    VDT_NONE = 0, VDT_LIVE_UNAVAILABLE, VDT_LIVE_STOPPED, VDT_FALLBACK_FAILED,
    VDT_FINAL_LATE, VDT_DIDNT_CATCH, VDT_TRANSCRIBING, VDT_WILL_SEND, VDT_CANCELLED
} VoiceDictText;

typedef struct { unsigned actions; VoiceDictText text; } VoiceDictOut;

typedef struct {
    VoiceDictState state;
    VoiceDictLink  link;
    int live, auto_ok, send_after, no_send, have_audio, fallback, batch_running;
    int captured;                 /* VDE_CAPTURED has arrived for this recording */
    unsigned long final_deadline;
} VoiceDict;

#define VOICE_DICT_FINAL_WAIT_MS 3000

void voice_dict_init(VoiceDict *d);
VoiceDictOut voice_dict_step(VoiceDict *d, VoiceDictEvent ev, int a, int b, unsigned long now);
/* Milliseconds until the wait for the final text runs out; -1 when none is
 * armed (not FINISHING, batch, or link FAILED); clamped at 0. */
long voice_dict_next_deadline(const VoiceDict *d, unsigned long now);
const char *voice_dict_text(VoiceDictText t);

#endif
