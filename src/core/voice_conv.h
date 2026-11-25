#ifndef VOICE_CONV_H
#define VOICE_CONV_H

/* Conversation-mode controller: the turn cycle (listen, hear, end the turn,
 * send, work, speak) and every way it ends. Pure and time-injected like
 * voice_dict: the Win32 glue feeds events and carries out the returned
 * action bits. Spec: voice-live design 5.1, 5.2, 5.6, 5.7, 12, 13.
 *
 * Conventions:
 *  - An event that means nothing in the current state returns {0} and
 *    changes nothing.
 *  - Release actions are emitted only for what the machine knows is open, so
 *    applying every action to the ledger balances it.
 *  - Every move to IDLE releases everything open, carries STOP_SPEECH and
 *    ENGINE_STOP, and DROP_HELD. It never carries DROP_TURN: the words of an
 *    unfinished turn stay in the box.
 */

#include "voice_echo.h"

typedef enum { VCS_IDLE = 0, VCS_LISTENING, VCS_HEARING, VCS_ENDING, VCS_WORKING,
               VCS_SPEAKING } VoiceConvState;
typedef enum { VCP_NONE = 0, VCP_BACKGROUND, VCP_DIALOG } VoiceConvPause;
typedef enum { VSS_SENT = 0, VSS_BUSY, VSS_NO_SESSION, VSS_FULL, VSS_LAUNCH_FAILED,
               VSS_EMPTY } VoiceSendStatus;
typedef enum { VCARD_COMMAND = 0, VCARD_WRITE, VCARD_OTHER } VoiceCardKind;
typedef enum { VCX_PANEL_CLOSED = 1, VCX_OFF, VCX_SETTINGS, VCX_LOCK, VCX_CHAT_CHANGED,
               VCX_DEVICE_LOST } VoiceConvEnd;
typedef enum {
    VCE_START,        /* a = live provider (1/0) */
    VCE_PRESS,        /* the key or the mic button */
    VCE_ESCAPE,       /* Escape with focus in the AI panel */
    VCE_STOP,         /* the panel's Stop (or Send showing Stop while Speaking) */
    VCE_SEND,         /* Send or Enter while Listening or Hearing */
    VCE_PAUSE,        /* a = VoiceConvPause */
    VCE_RESUME,
    VCE_TURN_START, VCE_TURN_END, VCE_TURN_ABANDON,   /* from the listener (END covers CAP) */
    VCE_DUCK, VCE_UNDUCK, VCE_CUT,
    VCE_LINK_READY, VCE_LINK_FAILED, VCE_LINK_FINAL,
    VCE_BATCH_OK, VCE_BATCH_FAIL,
    VCE_TRANSCRIPT,   /* a = VoiceTurnClass, after LINK_FINAL or BATCH_OK */
    VCE_SENT,         /* a = VoiceSendStatus */
    VCE_CARD,         /* a = VoiceCardKind: a card became pending */
    VCE_CHAT_IDLE,    /* "chat turn idle": a = has prose, b = the reply was an error */
    VCE_IDLE_POLL,    /* once a second while Working: a = chat idle now, b = prose | err << 1 */
    VCE_SPEECH_DONE,  /* what was being spoken was read to the end */
    VCE_SPEECH_FAILED,/* speech could not start */
    VCE_END,          /* a = VoiceConvEnd */
    VCE_EXTERNAL_SEND,/* the box was sent from outside the conversation (typed, a command) */
    VCE_SPEECH_PROGRESS, /* the speech advanced (a sentence began or ended) */
    VCE_TICK
} VoiceConvEvent;
enum {  /* action bits */
    VCA_ENGINE_START = 1u << 0,  VCA_ENGINE_STOP  = 1u << 1,
    VCA_LOCK_REG     = 1u << 2,  VCA_LOCK_UNREG   = 1u << 3,
    VCA_OPEN_CAPTURE = 1u << 4,  VCA_CLOSE_CAPTURE= 1u << 5,
    VCA_MODE_LISTEN  = 1u << 6,  VCA_MODE_SPEAK   = 1u << 7,  VCA_MODE_HOLD = 1u << 8,
    VCA_OPEN_LINK    = 1u << 9,  VCA_END_TURN_AUDIO = 1u << 10, VCA_CLOSE_LINK = 1u << 11,
    VCA_SUBMIT_BATCH = 1u << 12, VCA_CANCEL_BATCH = 1u << 13,
    VCA_DROP_TURN    = 1u << 14, /* discard the turn's audio and remove its words from the box */
    VCA_LOCK_TYPING  = 1u << 15, VCA_UNLOCK_TYPING= 1u << 16,
    VCA_SEND_TURN    = 1u << 17, /* send the box as a voice turn */
    VCA_SEND_TYPED   = 1u << 18, /* send the box as a typed message */
    VCA_SPEAK_REPLY  = 1u << 19, /* fetch the reply and speak it from the start */
    VCA_SPEAK_REPEAT = 1u << 20, /* the last reply from the start */
    VCA_SPEAK_CONTINUE = 1u << 21, /* the last (or held) reply from its cut-off */
    VCA_SPEAK_PHRASE = 1u << 22, /* out.phrase */
    VCA_STOP_SPEECH  = 1u << 23, VCA_DUCK = 1u << 24, VCA_UNDUCK = 1u << 25,
    VCA_CANCEL_WORK  = 1u << 26, /* cancel the assistant's work as Stop does */
    VCA_HOLD_REPLY   = 1u << 27, /* keep the reply (and cut-off) for resume */
    VCA_DROP_HELD    = 1u << 28,
    VCA_STOP_CAPTION = 1u << 29, /* Send shows Stop (Speaking) */
    VCA_SEND_CAPTION = 1u << 30  /* Send shows Send again */
};
typedef enum { VCH_NONE = 0, VCH_LISTENING, VCH_HEARING, VCH_SENDING, VCH_TRANSCRIBING,
               VCH_WORKING, VCH_SPEAKING, VCH_PAUSED_BG, VCH_PAUSED_DIALOG, VCH_APPROVAL,
               VCH_DIDNT_CATCH, VCH_IGNORED_ECHO, VCH_REPEATING, VCH_CARRYING_ON,
               VCH_LIVE_UNAVAILABLE, VCH_LIVE_STOPPED, VCH_AI_FAILED } VoiceConvHeader;
typedef enum { VCN_NONE = 0, VCN_END_SETTINGS, VCN_END_LOCK, VCN_END_SILENCE, VCN_END_CHAT,
               VCN_END_STUCK, VCN_BUSY, VCN_NO_SESSION, VCN_FULL, VCN_LAUNCH, VCN_NO_SPEECH } VoiceConvNote;
typedef enum { VCPH_NONE = 0, VCPH_APPROVE_COMMAND, VCPH_APPROVE_WRITE, VCPH_APPROVE_OTHER,
               VCPH_NOT_SENT, VCPH_WENT_WRONG } VoiceConvPhrase;
typedef struct { unsigned actions; VoiceConvHeader header; VoiceConvNote note;
                 VoiceConvPhrase phrase; } VoiceConvOut;
typedef struct {
    VoiceConvState state;
    VoiceConvPause pause;
    int live;
    int engine, lock, capture, link, batch, turn, typing, stopcap;  /* open resources */
    int link_ready;               /* the live link became ready this turn */
    int final_armed;              /* the 3 s wait for the final text is armed */
    int sending;                  /* a turn or typed message was handed to the glue */
    int idle_seen;                /* IDLE_POLL has seen the chat idle since idle_since */
    int local_speech;             /* the speech is a local repeat or continue, not the chat's reply */
    int speaking_reply;           /* SPEAKING a reply (1) or a phrase (0) */
    int held, held_continue;      /* a held reply (held_phrase: the WENT_WRONG phrase) */
    int held_phrase;
    int held_local;               /* the held speech was a local repeat or continue */
    int card;                     /* a card raised while paused: VoiceCardKind + 1, 0 none */
    unsigned long sent_at, idle_at, idle_since, final_deadline, held_at;
    unsigned long state_at;       /* when the state last changed (the watchdogs' clock) */
    unsigned long ending_max;     /* this Ending's deadline in ms: the default, or the upload's budget */
} VoiceConv;
#define VOICE_CONV_FINAL_WAIT_MS   3000
#define VOICE_CONV_IDLE_POLL_MS    1000
#define VOICE_CONV_IDLE_SAFETY_MS  3000
#define VOICE_CONV_SILENCE_END_MS  180000
#define VOICE_CONV_HELD_MAX_MS     30000
/* Watchdogs for a state that waits on an event that may never come (a lost end, a lost
 * transcript, a lost speech-done). Hearing: the listener's own turn cap is 120 s, plus 10 s.
 * Ending: longer than the upload's own timeouts (88 s send plus 72 s receive for a 120 s
 * turn, plus a retry), 180 s by default and longer for a long turn (voice_conv_set_ending_budget);
 * at the deadline the conversation ends with VCN_END_STUCK. Speaking: a very long reply
 * read aloud, 10 minutes. On expiry of the others the conversation goes back to Listening. */
#define VOICE_CONV_HEARING_MAX_MS  130000
#define VOICE_CONV_ENDING_MAX_MS   180000
#define VOICE_CONV_SPEAKING_MAX_MS 600000
void         voice_conv_init(VoiceConv *c);
/* The glue calls this when it submits a turn's upload: the Ending deadline becomes
 * max(VOICE_CONV_ENDING_MAX_MS, upload_ms + 10000), upload_ms being the upload's own timeouts
 * with its one retry. Every entry to Ending starts again from the default. */
void         voice_conv_set_ending_budget(VoiceConv *c, unsigned long upload_ms);
VoiceConvOut voice_conv_step(VoiceConv *c, VoiceConvEvent ev, int a, int b, unsigned long now);
long         voice_conv_next_deadline(const VoiceConv *c, unsigned long now); /* ms or -1 */
int          voice_conv_active(const VoiceConv *c);          /* state != IDLE */
int          voice_conv_typing_locked(const VoiceConv *c);
const char  *voice_conv_header_text(VoiceConvHeader h);
const char  *voice_conv_note_text(VoiceConvNote n);
const char  *voice_conv_phrase_text(VoiceConvPhrase p);
VoiceCardKind voice_card_kind(const char *tool_name);    /* NULL = an [EXEC] command batch */

#endif
