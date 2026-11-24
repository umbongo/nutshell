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
    o = out(a, VCH_NONE);
    o.note = note;
    return o;
}

static VoiceConvOut enter_listening(VoiceConv *c, unsigned extra, VoiceConvHeader h)
{
    c->state = VCS_LISTENING;
    c->turn = 0;
    c->sending = 0;
    return out(extra | open_capture(c) | VCA_MODE_LISTEN, h);
}

static VoiceConvOut enter_speaking(VoiceConv *c, unsigned speak, VoiceConvPhrase ph, int reply)
{
    VoiceConvOut o;
    unsigned a = speak | open_capture(c) | VCA_MODE_SPEAK;
    c->state = VCS_SPEAKING;
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
    /* ECHO, REPEAT and CONTINUE are TEXT until the pause/cut-in work. */
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
        c->sent_at = now;
        c->idle_seen = 0;
        return out(a, VCH_WORKING);
    case VSS_EMPTY:
        return ending_to_listening(c, 0, VCH_DIDNT_CATCH);
    case VSS_BUSY: case VSS_NO_SESSION: case VSS_FULL: case VSS_LAUNCH_FAILED:
        a = release_link(c) | release_batch(c) | release_typing(c);
        o = enter_speaking(c, VCA_SPEAK_PHRASE, VCPH_NOT_SENT, 0);
        o.actions |= a;
        /* a refusal's phrase is not a Stop-caption moment */
        if (o.actions & VCA_STOP_CAPTION) { o.actions &= ~VCA_STOP_CAPTION; c->stopcap = 0; }
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
    c->state = VCS_LISTENING;
    c->idle_at = now;   /* the silence clock restarts when listening resumes */
    o = out(extra | VCA_MODE_LISTEN | release_caption(c), VCH_LISTENING);
    o.note = note;
    return o;
}

VoiceConvOut voice_conv_step(VoiceConv *c, VoiceConvEvent ev, int a, int b, unsigned long now)
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
            c->state = VCS_ENDING;
            c->sending = 1;
            return out(VCA_SEND_TYPED, VCH_SENDING);
        }
        if (st == VCS_HEARING) goto turn_end;
        return none;

    case VCE_TURN_START:
        if (st != VCS_LISTENING) return none;   /* cut-in while Speaking: later */
        {
            unsigned x = VCA_LOCK_TYPING;
            c->state = VCS_HEARING;
            c->turn = 1;
            c->typing = 1;
            c->link_ready = 0;
            if (c->live) { c->link = 1; x |= VCA_OPEN_LINK; }
            return out(x, VCH_HEARING);
        }

    case VCE_TURN_ABANDON:
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
        if (st == VCS_ENDING) {
            if (c->live && c->link && c->final_armed && now >= c->final_deadline)
                return live_fallback(c);
            return none;
        }
        if (st == VCS_LISTENING) {
            unsigned long from = c->sent_at > c->idle_at ? c->sent_at : c->idle_at;
            if (now - from >= VOICE_CONV_SILENCE_END_MS) return go_idle(c, VCN_END_SILENCE, 0);
        }
        return none;

    /* Task 8: pausing, held replies and cut-in. */
    case VCE_PAUSE: case VCE_RESUME:
    case VCE_DUCK: case VCE_UNDUCK: case VCE_CUT:
    default:
        return none;
    }
}

long voice_conv_next_deadline(const VoiceConv *c, unsigned long now)
{
    long left;
    switch (c->state) {
    case VCS_ENDING:
        if (!(c->live && c->link && c->final_armed)) return -1;
        left = (long)(c->final_deadline - now);
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
