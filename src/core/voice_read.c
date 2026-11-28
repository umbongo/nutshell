#include "voice_read.h"

#include "voice_aispeech.h"
#include "voice_config.h"

#include <string.h>

void voice_read_init(VoiceRead *r)
{
    memset(r, 0, sizeof *r);
}

int voice_read_shown(int voice_active, int mode)
{
    return voice_active && mode == VOICE_MODE_DICTATION;
}

int voice_read_gate(int voice_active, int mode, int recording)
{
    return voice_read_shown(voice_active, mode) && !recording;
}

static void go_idle(VoiceRead *r)
{
    r->state = VRS_IDLE;
    r->item_id = 0;
}

static void drop_ctx(VoiceRead *r)
{
    r->ctx = 0;
    r->ready = 0;
    /* Whatever the dropped context still has queued carries its old serial;
     * a new one means nothing of it can match the next context. */
    r->serial++;
}

VoiceReadOut voice_read_on_press(VoiceRead *r, const VoiceReadPress *p)
{
    VoiceReadOut o;
    memset(&o, 0, sizeof o);
    if (!p->gate)
        return o;
    if (r->state != VRS_IDLE && r->item_id == p->item_id) {
        go_idle(r);
        o.actions = VRA_STOP | VRA_VIEW;
        return o;
    }
    if (r->state != VRS_IDLE) {
        go_idle(r);
        o.actions |= VRA_STOP | VRA_VIEW;
    }
    if (!p->has_text)
        return o;
    if (r->ctx && r->engine != p->engine) {
        o.actions |= VRA_RELEASE;
        drop_ctx(r);
    }
    r->item_id = p->item_id;
    if (r->ctx && r->ready) {
        o.actions |= VRA_SPEAK;
        o.serial = ++r->serial;
        r->state = VRS_SPEAKING;
    } else if (r->ctx) {
        r->state = VRS_STARTING;
    } else {
        o.actions |= VRA_START;
        o.engine = p->engine;
        r->engine = p->engine;
        r->state = VRS_STARTING;
    }
    o.actions |= VRA_VIEW;
    return o;
}

VoiceReadOut voice_read_on_started(VoiceRead *r, int ok)
{
    VoiceReadOut o;
    memset(&o, 0, sizeof o);
    if (r->state != VRS_STARTING || r->ctx)
        return o;
    if (!ok) {
        o.actions = VRA_ERROR | VRA_VIEW;
        o.err_engine = r->engine;
        go_idle(r);
        return o;
    }
    r->ctx = 1;
    r->ready = 0;
    if (r->engine == VOICE_SPEECH_AI) {
        r->ready = 1;
        r->state = VRS_SPEAKING;
        o.actions = VRA_SPEAK;
        o.serial = ++r->serial;
    }
    return o;
}

VoiceReadOut voice_read_on_engine(VoiceRead *r, VoiceReadMsg kind, unsigned reply_id, long hr)
{
    VoiceReadOut o;
    memset(&o, 0, sizeof o);
    switch (kind) {
    case VRM_READY:
        if (r->ctx && !r->ready) {
            r->ready = 1;
            if (r->state == VRS_STARTING) {
                r->state = VRS_SPEAKING;
                o.actions = VRA_SPEAK;
                o.serial = ++r->serial;
            }
        }
        break;
    case VRM_DONE:
        if (r->state == VRS_SPEAKING && reply_id == r->serial) {
            go_idle(r);
            o.actions = VRA_VIEW;
        }
        break;
    case VRM_ERROR:
        if (!r->ctx || (reply_id != 0 && reply_id != r->serial))
            break;
        o.actions = VRA_RELEASE;
        drop_ctx(r);
        if (r->state != VRS_IDLE) {
            o.actions |= VRA_ERROR | VRA_VIEW;
            o.err_engine = r->engine;
            o.hr = hr;
            go_idle(r);
        }
        break;
    }
    return o;
}

VoiceReadOut voice_read_on_stop(VoiceRead *r, VoiceReadStop why)
{
    VoiceReadOut o;
    memset(&o, 0, sizeof o);
    if (r->state != VRS_IDLE) {
        go_idle(r);
        o.actions = VRA_STOP | VRA_VIEW;
    }
    if ((why == VRX_PANEL || why == VRX_SETTINGS || why == VRX_SAMPLE) && r->ctx) {
        o.actions |= VRA_RELEASE;
        drop_ctx(r);
    }
    return o;
}

void voice_read_error_text(int engine, long hr, const char *resolve_err, char *out, size_t cap)
{
    static const char head[] = "Could not read the reply aloud: ";
    char reason[200];
    size_t n;
    if (cap == 0)
        return;
    if (engine != VOICE_SPEECH_AI) {
        strcpy(reason, "the Windows voice did not start");
    } else if (hr == 0) {
        strcpy(reason, "AI speech did not start");
    } else {
        voice_ais_failure_reason(hr, resolve_err, reason, sizeof reason);
    }
    out[0] = '\0';
    n = 0;
    {
        const char *parts[3];
        size_t i;
        parts[0] = head;
        parts[1] = reason;
        parts[2] = ".";
        for (i = 0; i < 3; i++) {
            size_t l = strlen(parts[i]);
            if (l > cap - 1 - n)
                l = cap - 1 - n;
            memcpy(out + n, parts[i], l);
            n += l;
        }
        out[n] = '\0';
    }
}
