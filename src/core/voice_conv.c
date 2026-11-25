#include "voice_conv.h"
#include <string.h>

static VoiceConvOut out(unsigned actions, VoiceConvHeader h)
{
    VoiceConvOut o;
    o.actions = actions;
    o.header = h;
    o.note = VCN_NONE;
    o.phrase = VCPH_NONE;
    return o;
}

void voice_conv_init(VoiceConv *c)
{
    memset(c, 0, sizeof *c);
    c->state = VCS_IDLE;
    c->pause = VCP_NONE;
}

int voice_conv_active(const VoiceConv *c) { return c->state != VCS_IDLE; }
int voice_conv_typing_locked(const VoiceConv *c) { return c->typing; }

/* ---- release helpers: only what is open ---- */

static unsigned release_link(VoiceConv *c)
{
    unsigned a = 0;
    if (c->link) { a |= VCA_CLOSE_LINK; c->link = 0; }
    c->link_ready = 0;
    c->final_armed = 0;
    return a;
}

static unsigned release_batch(VoiceConv *c)
{
    if (c->batch) { c->batch = 0; return VCA_CANCEL_BATCH; }
    return 0;
}

static unsigned release_typing(VoiceConv *c)
{
    if (c->typing) { c->typing = 0; return VCA_UNLOCK_TYPING; }
    return 0;
}

static unsigned release_caption(VoiceConv *c)
{
    if (c->stopcap) { c->stopcap = 0; return VCA_SEND_CAPTION; }
    return 0;
}

static unsigned open_capture(VoiceConv *c)
{
    if (!c->capture) { c->capture = 1; return VCA_OPEN_CAPTURE; }
    return 0;
}

/* Every move to IDLE: release all that is open. Never DROP_TURN. */
static VoiceConvOut go_idle(VoiceConv *c, VoiceConvNote note, int cancel_work)
{
    unsigned a = VCA_DROP_HELD | VCA_STOP_SPEECH;
    VoiceConvOut o;
    if (c->capture) { a |= VCA_CLOSE_CAPTURE; c->capture = 0; }
    a |= release_link(c) | release_batch(c) | release_typing(c) | release_caption(c);
    if (c->lock) { a |= VCA_LOCK_UNREG; c->lock = 0; }
    if (c->engine) { a |= VCA_ENGINE_STOP; c->engine = 0; }
    if (cancel_work) a |= VCA_CANCEL_WORK;
    c->state = VCS_IDLE;
    c->pause = VCP_NONE;
    c->turn = 0;
    c->sending = 0;
    c->idle_seen = 0;
    c->speaking_reply = 0;
    c->held = 0;
    c->held_continue = 0;
    c->held_phrase = 0;
    c->card = 0;
    o = out(a, VCH_NONE);
    o.note = note;
    return o;
}

static VoiceConvHeader pause_header(const VoiceConv *c)
{
    return c->pause == VCP_DIALOG ? VCH_PAUSED_DIALOG : VCH_PAUSED_BG;
}

/* While paused the capture stays closed and the header says why. */
static VoiceConvOut enter_listening(VoiceConv *c, unsigned extra, VoiceConvHeader h)
{
    c->state = VCS_LISTENING;
    c->turn = 0;
    c->sending = 0;
    if (c->pause != VCP_NONE) {
        unsigned x = extra;
        if (c->capture) { c->capture = 0; x |= VCA_CLOSE_CAPTURE; }
        return out(x, pause_header(c));
    }
    return out(extra | open_capture(c) | VCA_MODE_LISTEN, h);
}

/* enter_listening for a state that gave up waiting: the silence clock restarts. */
static VoiceConvOut enter_listening_at(VoiceConv *c, unsigned extra, VoiceConvHeader h,
                                       unsigned long now)
{
    c->idle_at = now;
    return enter_listening(c, extra, h);
}

static void hold(VoiceConv *c, int continue_from, int phrase, unsigned long now)
{
    c->held = 1;
    c->held_continue = continue_from;
    c->held_phrase = phrase;
    c->held_at = now;
}

static VoiceConvOut enter_speaking(VoiceConv *c, unsigned speak, VoiceConvPhrase ph, int reply)
{
    VoiceConvOut o;
    unsigned a = speak | open_capture(c) | VCA_MODE_SPEAK;
    c->state = VCS_SPEAKING;
    c->turn = 0;   /* the turn that led here is over; a cut-in sets it again */
    c->speaking_reply = reply;
    c->idle_seen = 0;
    if (!c->stopcap) { c->stopcap = 1; a |= VCA_STOP_CAPTION; }
    o = out(a, VCH_SPEAKING);
    o.phrase = ph;
    return o;
}

/* The chat finished its turn (a = has prose, err = reply was an error). */
static VoiceConvOut work_finished(VoiceConv *c, int prose, int err, unsigned long now)
{
    c->idle_at = now;
    c->idle_seen = 0;
    c->card = 0;
    if (c->pause != VCP_NONE) {
        /* replies are not spoken while paused: held (a phrase like a reply) */
        VoiceConvOut o;
        if (err) hold(c, 0, 1, now); else if (prose) hold(c, 0, 0, now);
        o = enter_listening(c, (err || prose) ? VCA_HOLD_REPLY : 0, VCH_NONE);
        return o;
    }
    if (err) return enter_speaking(c, VCA_SPEAK_PHRASE, VCPH_WENT_WRONG, 0);
    if (prose) return enter_speaking(c, VCA_SPEAK_REPLY, VCPH_NONE, 1);
    return enter_listening(c, 0, VCH_LISTENING);
}

/* The turn's words go to the batch recogniser (live fallback). */
static VoiceConvOut live_fallback(VoiceConv *c)
{
    VoiceConvHeader h = c->link_ready ? VCH_LIVE_STOPPED : VCH_LIVE_UNAVAILABLE;
    unsigned a = release_link(c);
    if (!c->batch) { c->batch = 1; a |= VCA_SUBMIT_BATCH; }
    return out(a, h);
}

/* Leaving ENDING for LISTENING: release the link and any upload. */
static VoiceConvOut ending_to_listening(VoiceConv *c, unsigned extra, VoiceConvHeader h)
{
    unsigned a = extra | release_typing(c) | release_link(c) | release_batch(c);
    return enter_listening(c, a, h);
}

static VoiceConvOut on_transcript(VoiceConv *c, int cls)
{
    if (c->state != VCS_ENDING || c->sending || c->batch) return out(0, VCH_NONE);
    if (cls == VTC_EMPTY)
        return ending_to_listening(c, VCA_DROP_TURN, VCH_DIDNT_CATCH);
    if (cls == VTC_ECHO)
        return ending_to_listening(c, VCA_DROP_TURN, VCH_IGNORED_ECHO);
    if (cls == VTC_REPEAT || cls == VTC_CONTINUE) {
        unsigned x = VCA_DROP_TURN | release_typing(c) | release_link(c) | release_batch(c);
        VoiceConvOut o;
        o = enter_speaking(c, cls == VTC_REPEAT ? VCA_SPEAK_REPEAT : VCA_SPEAK_CONTINUE,
                           VCPH_NONE, 1);
        o.actions |= x;
        o.header = cls == VTC_REPEAT ? VCH_REPEATING : VCH_CARRYING_ON;
        c->turn = 0;
        c->sending = 0;
        return o;
    }
    c->sending = 1;
    return out(VCA_SEND_TURN, VCH_NONE);
}

static VoiceConvOut on_sent(VoiceConv *c, int status, unsigned long now)
{
    VoiceConvOut o;
    unsigned a;
    if (c->state != VCS_ENDING || !c->sending) return out(0, VCH_NONE);
    c->sending = 0;
    switch (status) {
    case VSS_SENT:
        a = release_link(c) | release_batch(c) | release_typing(c);
        if (c->capture) { a |= VCA_CLOSE_CAPTURE; c->capture = 0; }
        c->state = VCS_WORKING;
        c->card = 0;
        c->turn = 0;
        c->sent_at = now;
        c->idle_seen = 0;
        return out(a, c->pause != VCP_NONE ? pause_header(c) : VCH_WORKING);
    case VSS_EMPTY:
        return ending_to_listening(c, 0, VCH_DIDNT_CATCH);
    case VSS_BUSY: case VSS_NO_SESSION: case VSS_FULL: case VSS_LAUNCH_FAILED:
        a = release_link(c) | release_batch(c) | release_typing(c);
        if (c->pause != VCP_NONE) {   /* nothing is spoken while paused: the note shows */
            o = enter_listening(c, a, VCH_NONE);
            o.note = status == VSS_BUSY ? VCN_BUSY : status == VSS_NO_SESSION ? VCN_NO_SESSION
                   : status == VSS_FULL ? VCN_FULL : VCN_LAUNCH;
            return o;
        }
        o = enter_speaking(c, VCA_SPEAK_PHRASE, VCPH_NOT_SENT, 0);
        o.actions |= a;
        /* a refusal's phrase is not a Stop-caption moment */
        if (o.actions & VCA_STOP_CAPTION) { o.actions &= ~(unsigned)VCA_STOP_CAPTION; c->stopcap = 0; }
        o.note = status == VSS_BUSY ? VCN_BUSY : status == VSS_NO_SESSION ? VCN_NO_SESSION
               : status == VSS_FULL ? VCN_FULL : VCN_LAUNCH;
        return o;
    default:
        return out(0, VCH_NONE);
    }
}

static VoiceConvOut speaking_stopped(VoiceConv *c, unsigned extra, VoiceConvNote note,
                                     unsigned long now)
{
    VoiceConvOut o;
    /* a cut-in turn already open (AEC) carries on as the turn being heard */
    c->state = c->turn ? VCS_HEARING : VCS_LISTENING;
    c->idle_at = now;   /* the silence clock restarts when listening resumes */
    o = out(extra | VCA_MODE_LISTEN | release_caption(c),
            c->turn ? VCH_HEARING : VCH_LISTENING);
    o.note = note;
    return o;
}


static VoiceConvOut on_pause(VoiceConv *c, int reason, unsigned long now)
{
    VoiceConvOut none = out(0, VCH_NONE);
    unsigned x = 0;
    if (reason != VCP_BACKGROUND && reason != VCP_DIALOG) return none;
    if (c->pause != VCP_NONE) {          /* already paused: only the reason */
        c->pause = (VoiceConvPause)reason;
        return out(0, pause_header(c));
    }
    switch (c->state) {
    case VCS_LISTENING:
        break;
    case VCS_HEARING:
    case VCS_ENDING:
        if (c->state == VCS_ENDING && c->sending) {   /* already handed over: pause on arrival */
            c->pause = (VoiceConvPause)reason;
            if (c->capture) { c->capture = 0; x |= VCA_CLOSE_CAPTURE; }
            return out(x, pause_header(c));
        }
        x |= VCA_DROP_TURN | release_link(c) | release_batch(c) | release_typing(c);
        c->turn = 0;
        break;
    case VCS_SPEAKING:
        x |= VCA_STOP_SPEECH | release_caption(c);
        if (c->speaking_reply) {         /* held from its cut-off */
            hold(c, 1, 0, now);
            x |= VCA_HOLD_REPLY;
        }
        if (c->turn) {
            x |= VCA_DROP_TURN | release_link(c) | release_typing(c);
            c->turn = 0;
        }
        break;
    case VCS_WORKING:
        c->pause = (VoiceConvPause)reason;
        /* an approval phrase being spoken is cut off (nothing is spoken while paused);
         * the card stays on screen */
        return out(VCA_STOP_SPEECH, pause_header(c));
    default:
        return none;
    }
    c->pause = (VoiceConvPause)reason;
    c->state = VCS_LISTENING;
    if (c->capture) { c->capture = 0; x |= VCA_CLOSE_CAPTURE; }
    return out(x, pause_header(c));
}

static VoiceConvOut on_resume(VoiceConv *c, unsigned long now)
{
    if (c->pause == VCP_NONE) return out(0, VCH_NONE);
    c->pause = VCP_NONE;
    if (c->state == VCS_WORKING) {
        if (c->card) {
            VoiceConvOut o = out(VCA_SPEAK_PHRASE, VCH_APPROVAL);
            o.phrase = c->card - 1 == VCARD_COMMAND ? VCPH_APPROVE_COMMAND
                     : c->card - 1 == VCARD_WRITE ? VCPH_APPROVE_WRITE : VCPH_APPROVE_OTHER;
            c->card = 0;
            return o;
        }
        return out(0, VCH_WORKING);
    }
    if (c->state != VCS_LISTENING) return out(0, VCH_NONE);
    if (c->held) {
        int fresh = (now - c->held_at) < VOICE_CONV_HELD_MAX_MS;
        int cont = c->held_continue, ph = c->held_phrase;
        c->held = c->held_continue = c->held_phrase = 0;
        if (fresh) {
            if (ph) return enter_speaking(c, VCA_SPEAK_PHRASE, VCPH_WENT_WRONG, 0);
            return enter_speaking(c, cont ? VCA_SPEAK_CONTINUE : VCA_SPEAK_REPLY, VCPH_NONE, 1);
        }
        return enter_listening(c, VCA_DROP_HELD, VCH_LISTENING);
    }
    return enter_listening(c, 0, VCH_LISTENING);
}

static VoiceConvOut step_inner(VoiceConv *c, VoiceConvEvent ev, int a, int b, unsigned long now)
{
    VoiceConvOut none = out(0, VCH_NONE);
    VoiceConvState st = c->state;

    if (st == VCS_IDLE) {
        if (ev != VCE_START) return none;
        c->live = a ? 1 : 0;
        c->engine = c->lock = c->capture = 1;
        c->sent_at = c->idle_at = now;
        c->state = VCS_LISTENING;
        return out(VCA_ENGINE_START | VCA_LOCK_REG | VCA_OPEN_CAPTURE | VCA_MODE_LISTEN,
                   VCH_LISTENING);
    }

    switch (ev) {
    case VCE_START:
        return none;

    case VCE_PRESS:
        if (st == VCS_SPEAKING) return speaking_stopped(c, VCA_STOP_SPEECH, VCN_NONE, now);
        return go_idle(c, VCN_NONE, 0);
    case VCE_ESCAPE:
        if (st == VCS_SPEAKING) return speaking_stopped(c, VCA_STOP_SPEECH, VCN_NONE, now);
        return go_idle(c, VCN_NONE, st == VCS_WORKING);
    case VCE_STOP:
        if (st == VCS_SPEAKING) return speaking_stopped(c, VCA_STOP_SPEECH, VCN_NONE, now);
        return go_idle(c, VCN_NONE, 0);

    case VCE_END:
        return go_idle(c, a == VCX_SETTINGS ? VCN_END_SETTINGS
                        : a == VCX_LOCK ? VCN_END_LOCK
                        : a == VCX_CHAT_CHANGED ? VCN_END_CHAT : VCN_NONE, 0);

    case VCE_SEND:
        if (st == VCS_LISTENING) {
            /* typing is accepted while paused too; a held reply is not spoken over it */
            unsigned x = VCA_SEND_TYPED;
            c->state = VCS_ENDING;
            c->sending = 1;
            if (c->held) {
                x |= VCA_DROP_HELD;
                c->held = c->held_continue = c->held_phrase = 0;
            }
            return out(x, c->pause != VCP_NONE ? pause_header(c) : VCH_SENDING);
        }
        if (st == VCS_HEARING) goto turn_end;
        return none;

    case VCE_TURN_START: {
        unsigned x = VCA_LOCK_TYPING;
        int ok = (st == VCS_LISTENING && c->pause == VCP_NONE)
              || ((st == VCS_HEARING || st == VCS_SPEAKING) && !c->turn);
        if (!ok) return none;
        /* Speaking: an AEC cut-in candidate, the state stays until CUT */
        if (st != VCS_SPEAKING) c->state = VCS_HEARING;
        c->turn = 1;
        c->typing = 1;
        c->link_ready = 0;
        if (c->live) { c->link = 1; x |= VCA_OPEN_LINK; }
        return out(x, st == VCS_SPEAKING ? VCH_SPEAKING : VCH_HEARING);
    }

    case VCE_TURN_ABANDON:
        if (st == VCS_SPEAKING) {   /* a cough: the reply carries on */
            if (!c->turn) return none;
            c->turn = 0;
            return out(VCA_DROP_TURN | release_link(c) | release_typing(c), VCH_SPEAKING);
        }
        if (st != VCS_HEARING) return none;
        c->state = VCS_LISTENING;
        c->turn = 0;
        return out(VCA_DROP_TURN | release_link(c) | release_typing(c), VCH_LISTENING);

    case VCE_TURN_END:
        if (st != VCS_HEARING) return none;
    turn_end:
        {
            unsigned x = VCA_MODE_HOLD;
            VoiceConvHeader h;
            c->state = VCS_ENDING;
            if (c->live && c->link) {
                x |= VCA_END_TURN_AUDIO;
                c->final_armed = 1;
                c->final_deadline = now + VOICE_CONV_FINAL_WAIT_MS;
                h = VCH_SENDING;
            } else {
                x |= VCA_SUBMIT_BATCH;
                c->batch = 1;
                h = VCH_TRANSCRIBING;
            }
            return out(x, h);
        }

    case VCE_LINK_READY:
        if (st == VCS_HEARING || st == VCS_ENDING) { if (c->link) c->link_ready = 1; }
        return none;
    case VCE_LINK_FAILED:
        if (st == VCS_HEARING && c->link) {
            /* record it now: at the end of the turn it goes straight to the batch */
            VoiceConvHeader h = c->link_ready ? VCH_LIVE_STOPPED : VCH_LIVE_UNAVAILABLE;
            return out(release_link(c), h);
        }
        if (st != VCS_ENDING || !c->link) return none;
        return live_fallback(c);
    case VCE_LINK_FINAL:
        if (st == VCS_ENDING) c->final_armed = 0;
        return none;
    case VCE_BATCH_OK:
        if (st == VCS_ENDING) c->batch = 0;
        return none;
    case VCE_BATCH_FAIL:
        if (st != VCS_ENDING || !c->batch) return none;
        c->batch = 0;
        return ending_to_listening(c, 0, VCH_LISTENING);

    case VCE_TRANSCRIPT:
        return on_transcript(c, a);
    case VCE_SENT:
        return on_sent(c, a, now);

    case VCE_CARD:
        if (st != VCS_WORKING) return none;
        if (c->pause != VCP_NONE) { c->card = a + 1; return none; }   /* announced on resume */
        {
            VoiceConvOut o = out(VCA_SPEAK_PHRASE, VCH_APPROVAL);
            o.phrase = a == VCARD_COMMAND ? VCPH_APPROVE_COMMAND
                     : a == VCARD_WRITE ? VCPH_APPROVE_WRITE : VCPH_APPROVE_OTHER;
            return o;
        }

    case VCE_CHAT_IDLE:
        if (st != VCS_WORKING) return none;
        return work_finished(c, a != 0, b != 0, now);
    case VCE_IDLE_POLL:
        if (st != VCS_WORKING) return none;
        if (!a) { c->idle_seen = 0; return none; }
        if (!c->idle_seen) { c->idle_seen = 1; c->idle_since = now; }
        if (now - c->idle_since >= VOICE_CONV_IDLE_SAFETY_MS)
            return work_finished(c, (b & 1) != 0, (b & 2) != 0, now);
        return none;

    case VCE_SPEECH_DONE:
        if (st != VCS_SPEAKING) return none;
        return speaking_stopped(c, 0, VCN_NONE, now);
    case VCE_SPEECH_FAILED:
        if (st != VCS_SPEAKING) return none;
        return speaking_stopped(c, 0, c->speaking_reply ? VCN_NO_SPEECH : VCN_NONE, now);

    case VCE_TICK:
        if (st == VCS_HEARING) {
            if (now - c->state_at >= VOICE_CONV_HEARING_MAX_MS) {
                unsigned x = VCA_DROP_TURN | release_link(c) | release_typing(c);
                c->turn = 0;
                return enter_listening_at(c, x, VCH_LISTENING, now);
            }
            return none;
        }
        if (st == VCS_SPEAKING) {
            if (now - c->state_at >= VOICE_CONV_SPEAKING_MAX_MS)
                return speaking_stopped(c, VCA_STOP_SPEECH, VCN_NONE, now);
            return none;
        }
        if (st == VCS_ENDING) {
            if (c->live && c->link && c->final_armed && now >= c->final_deadline)
                return live_fallback(c);
            if (now - c->state_at >= VOICE_CONV_ENDING_MAX_MS) {
                c->sending = 0;
                c->idle_at = now;
                return ending_to_listening(c, VCA_DROP_TURN, VCH_DIDNT_CATCH);
            }
            return none;
        }
        if (st == VCS_LISTENING) {
            unsigned long from = c->sent_at > c->idle_at ? c->sent_at : c->idle_at;
            if (now - from >= VOICE_CONV_SILENCE_END_MS) return go_idle(c, VCN_END_SILENCE, 0);
        }
        return none;

    case VCE_PAUSE: return on_pause(c, a, now);
    case VCE_RESUME: return on_resume(c, now);

    case VCE_DUCK:   return st == VCS_SPEAKING ? out(VCA_DUCK, VCH_NONE) : none;
    case VCE_UNDUCK: return st == VCS_SPEAKING ? out(VCA_UNDUCK, VCH_NONE) : none;
    case VCE_CUT:
        if (st != VCS_SPEAKING) return none;
        c->state = VCS_HEARING;   /* the turn opens now (AEC) or at TURN_START (half-duplex) */
        /* no mode command: the listener has moved itself to LISTEN with the turn carrying on,
         * and a mode command would wipe that turn */
        return out(VCA_STOP_SPEECH | VCA_UNDUCK | release_caption(c), VCH_HEARING);

    default:
        return none;
    }
}

VoiceConvOut voice_conv_step(VoiceConv *c, VoiceConvEvent ev, int a, int b, unsigned long now)
{
    const VoiceConvState before = c->state;
    VoiceConvOut o = step_inner(c, ev, a, b, now);
    if (c->state != before) c->state_at = now;
    return o;
}

long voice_conv_next_deadline(const VoiceConv *c, unsigned long now)
{
    long left;
    switch (c->state) {
    case VCS_HEARING:
        left = (long)(c->state_at + VOICE_CONV_HEARING_MAX_MS - now);
        return left < 0 ? 0 : left;
    case VCS_SPEAKING:
        left = (long)(c->state_at + VOICE_CONV_SPEAKING_MAX_MS - now);
        return left < 0 ? 0 : left;
    case VCS_ENDING:
        left = (long)(c->state_at + VOICE_CONV_ENDING_MAX_MS - now);
        if (c->live && c->link && c->final_armed) {
            long f = (long)(c->final_deadline - now);
            if (f < left) left = f;
        }
        return left < 0 ? 0 : left;
    case VCS_WORKING:
        return VOICE_CONV_IDLE_POLL_MS;
    case VCS_LISTENING: {
        unsigned long from = c->sent_at > c->idle_at ? c->sent_at : c->idle_at;
        left = (long)(from + VOICE_CONV_SILENCE_END_MS - now);
        return left < 0 ? 0 : left;
    }
    default:
        return -1;
    }
}

const char *voice_conv_header_text(VoiceConvHeader h)
{
    switch (h) {
    case VCH_LISTENING:        return "Listening\xE2\x80\xA6 speak, then pause to send";
    case VCH_HEARING:          return "Hearing you\xE2\x80\xA6";
    case VCH_SENDING:          return "Sending\xE2\x80\xA6";
    case VCH_TRANSCRIBING:     return "Transcribing\xE2\x80\xA6";
    case VCH_WORKING:          return "Working\xE2\x80\xA6 press Escape to stop";
    case VCH_SPEAKING:         return "Speaking\xE2\x80\xA6 talk to cut in";
    case VCH_PAUSED_BG:        return "Paused while Nutshell is in the background";
    case VCH_PAUSED_DIALOG:    return "Paused while a dialog is open";
    case VCH_APPROVAL:         return "Waiting for your approval";
    case VCH_DIDNT_CATCH:      return "Didn't catch that";
    case VCH_IGNORED_ECHO:     return "Ignored the assistant's own voice";
    case VCH_REPEATING:        return "Repeating";
    case VCH_CARRYING_ON:      return "Carrying on";
    case VCH_LIVE_UNAVAILABLE: return "Live text is unavailable: the words will appear when you stop";
    case VCH_LIVE_STOPPED:     return "Live text stopped: the rest will appear when you stop";
    case VCH_NONE:
    default:                   return "";
    }
}

const char *voice_conv_note_text(VoiceConvNote n)
{
    switch (n) {
    case VCN_END_SETTINGS: return "Conversation ended: the voice settings changed";
    case VCN_END_LOCK:     return "Conversation ended: Windows was locked";
    case VCN_END_SILENCE:  return "Conversation ended: nothing said for 3 minutes";
    case VCN_END_CHAT:     return "Conversation ended: the chat changed";
    case VCN_BUSY:         return "The assistant is busy";
    case VCN_NO_SESSION:   return "Connect a session first";
    case VCN_FULL:         return "The conversation is full: start a New Chat";
    case VCN_LAUNCH:       return "The request could not start";
    case VCN_NO_SPEECH:    return "Could not speak the reply: it is on screen";
    case VCN_NONE:
    default:               return "";
    }
}

const char *voice_conv_phrase_text(VoiceConvPhrase p)
{
    switch (p) {
    case VCPH_APPROVE_COMMAND: return "I need your approval to run a command.";
    case VCPH_APPROVE_WRITE:   return "I need your approval to write a file.";
    case VCPH_APPROVE_OTHER:   return "I need your approval for the next step.";
    case VCPH_NOT_SENT:        return "I couldn't send that. The details are on screen.";
    case VCPH_WENT_WRONG:      return "Something went wrong. The details are on screen.";
    case VCPH_NONE:
    default:                   return "";
    }
}

/* The tool gate's table today holds run_command, web_fetch and web_search:
 * none writes a file. The names below are the file-writing tools a future
 * tool would carry; a write tool must be added here when it is added. */
VoiceCardKind voice_card_kind(const char *tool_name)
{
    static const char *const writers[] = {
        "write_file", "edit_file", "create_file", "append_file", "replace_in_file",
        "str_replace", "apply_patch", "delete_file", "move_file"
    };
    size_t i;
    if (!tool_name || strcmp(tool_name, "run_command") == 0) return VCARD_COMMAND;
    for (i = 0; i < sizeof writers / sizeof writers[0]; i++)
        if (strcmp(tool_name, writers[i]) == 0) return VCARD_WRITE;
    return VCARD_OTHER;
}
