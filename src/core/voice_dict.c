#include "voice_dict.h"

#include <string.h>

void voice_dict_init(VoiceDict *d)
{
    memset(d, 0, sizeof *d);   /* VDS_IDLE, VDL_NONE and every flag are zero */
}

static VoiceDictOut out(unsigned actions, VoiceDictText text)
{
    VoiceDictOut o;
    o.actions = actions;
    o.text = text;
    return o;
}

/* End the recording: unlock typing, back to IDLE with every flag cleared.
 * `arrived` = the final text arrived (live final, batch or fallback result). */
static VoiceDictOut finish(VoiceDict *d, unsigned actions, VoiceDictText text, int arrived)
{
    if (arrived && (d->send_after || d->auto_ok) && !d->no_send)
        actions |= VDA_SEND_BOX;
    actions |= VDA_UNLOCK_TYPING;
    voice_dict_init(d);
    return out(actions, text);
}

static int link_open(const VoiceDict *d)
{
    return d->link == VDL_CONNECTING || d->link == VDL_READY;
}

/* STOP, or SEND/END_SOFT as a stop, from RECORDING. */
static VoiceDictOut stop_recording(VoiceDict *d, unsigned long now)
{
    d->state = VDS_FINISHING;
    if (d->live && d->link != VDL_FAILED)   /* DONE too: FINISHING always has an exit */
        d->final_deadline = now + VOICE_DICT_FINAL_WAIT_MS;
    return out(VDA_END_CAPTURE_KEEP, VDT_NONE);
}

/* The kept audio goes to the batch model (batch recording or fallback). */
static VoiceDictOut submit(VoiceDict *d, unsigned actions)
{
    d->batch_running = 1;
    return out(actions | VDA_SUBMIT_BATCH, VDT_TRANSCRIBING);
}

static VoiceDictOut merge(VoiceDictOut x, VoiceDictOut y)
{
    return out(x.actions | y.actions, y.text != VDT_NONE ? y.text : x.text);
}

/* The capture has ended and delivered its audio (or none). Only meaningful
 * while FINISHING; captured/have_audio are never set in any other state. */
static VoiceDictOut on_captured_finishing(VoiceDict *d, int a)
{
    if (d->captured) return out(0, VDT_NONE);      /* a duplicate */
    d->captured = 1;
    d->have_audio = a ? 1 : 0;
    if (!d->live) {
        if (a) return submit(d, 0);
        return finish(d, 0, VDT_DIDNT_CATCH, 0);
    }
    if (d->fallback) {
        if (a) return submit(d, 0);
        return finish(d, 0, VDT_NONE, 0);          /* nothing to transcribe again */
    }
    if (d->link == VDL_DONE)                       /* the live final came while recording */
        return finish(d, VDA_CLOSE_LINK | (a ? VDA_DROP_AUDIO : 0u), VDT_NONE, 1);
    return out(0, VDT_NONE);                       /* keep the audio until final or failure */
}

static VoiceDictOut on_captured(VoiceDict *d, int a, unsigned long now)
{
    if (d->state == VDS_IDLE)                      /* nothing is waiting for it: free it */
        return out(a ? VDA_DROP_AUDIO : 0u, VDT_NONE);
    if (d->state == VDS_RECORDING) {               /* the audio thread ended it itself */
        VoiceDictOut s = stop_recording(d, now);
        return merge(s, on_captured_finishing(d, a));
    }
    return on_captured_finishing(d, a);
}

static VoiceDictOut on_link_failed(VoiceDict *d, int never_opened)
{
    VoiceDictText t = never_opened ? VDT_LIVE_UNAVAILABLE : VDT_LIVE_STOPPED;
    if (d->state == VDS_IDLE || !d->live || !link_open(d)) return out(0, VDT_NONE);
    d->link = VDL_FAILED;
    d->fallback = 1;
    d->final_deadline = 0;
    if (d->state == VDS_FINISHING && d->captured) {
        if (d->have_audio) return submit(d, VDA_CLOSE_LINK);
        return finish(d, VDA_CLOSE_LINK, VDT_NONE, 0);
    }
    return out(VDA_CLOSE_LINK, t);
}

static VoiceDictOut on_batch_done(VoiceDict *d, int ok)
{
    if (d->state != VDS_FINISHING || !d->batch_running) return out(0, VDT_NONE);
    d->batch_running = 0;
    if (ok) return finish(d, VDA_DELIVER_BATCH | VDA_DROP_AUDIO, VDT_NONE, 1);
    return finish(d, VDA_DROP_AUDIO, d->fallback ? VDT_FALLBACK_FAILED : VDT_NONE, 0);
}

VoiceDictOut voice_dict_step(VoiceDict *d, VoiceDictEvent ev, int a, int b, unsigned long now)
{
    switch (ev) {
    case VDE_START:
        if (d->state != VDS_IDLE) return out(0, VDT_NONE);
        voice_dict_init(d);
        d->state = VDS_RECORDING;
        d->live = a ? 1 : 0;
        d->auto_ok = b ? 1 : 0;
        if (d->live) {
            d->link = VDL_CONNECTING;
            return out(VDA_OPEN_CAPTURE | VDA_LOCK_TYPING | VDA_OPEN_LINK, VDT_NONE);
        }
        return out(VDA_OPEN_CAPTURE | VDA_LOCK_TYPING, VDT_NONE);

    case VDE_STOP:
        if (d->state != VDS_RECORDING) return out(0, VDT_NONE);
        return stop_recording(d, now);

    case VDE_SEND:
        if (d->state == VDS_IDLE) return out(0, VDT_NONE);
        d->send_after = 1;
        if (d->state == VDS_RECORDING) return stop_recording(d, now);
        return out(0, d->batch_running ? VDT_WILL_SEND : VDT_NONE);

    case VDE_ESCAPE:
        if (d->state == VDS_FINISHING && d->batch_running)
            return finish(d, VDA_CANCEL_BATCH | VDA_DROP_AUDIO, VDT_CANCELLED, 0);
        /* fall through */
    case VDE_DISCARD:
        if (d->state != VDS_RECORDING) return out(0, VDT_NONE);
        voice_dict_init(d);
        return out(VDA_END_CAPTURE_DISCARD | VDA_CLOSE_LINK | VDA_REMOVE_WORDS |
                   VDA_UNLOCK_TYPING, VDT_NONE);

    case VDE_END_SOFT:
        if (d->state == VDS_IDLE) return out(0, VDT_NONE);
        d->no_send = 1;
        if (d->state == VDS_FINISHING) return out(0, VDT_NONE);
        if (!d->live) {
            voice_dict_init(d);
            return out(VDA_END_CAPTURE_DISCARD | VDA_UNLOCK_TYPING, VDT_NONE);
        }
        return stop_recording(d, now);

    case VDE_END_HARD:
        if (d->state == VDS_IDLE) return out(0, VDT_NONE);
        voice_dict_init(d);
        return out(VDA_END_CAPTURE_DISCARD | VDA_CLOSE_LINK | VDA_CANCEL_BATCH |
                   VDA_DROP_AUDIO | VDA_UNLOCK_TYPING, VDT_NONE);

    case VDE_LINK_READY:
        if (d->state != VDS_IDLE && d->link == VDL_CONNECTING) d->link = VDL_READY;
        return out(0, VDT_NONE);

    case VDE_LINK_FAILED:
        return on_link_failed(d, a);

    case VDE_LINK_FINAL:
        if (d->state == VDS_IDLE || !link_open(d)) return out(0, VDT_NONE);
        d->link = VDL_DONE;
        d->final_deadline = 0;
        if (d->state == VDS_RECORDING) return out(0, VDT_NONE);
        return finish(d, VDA_CLOSE_LINK | (d->have_audio ? VDA_DROP_AUDIO : 0u), VDT_NONE, 1);

    case VDE_CAPTURED:
        return on_captured(d, a, now);

    case VDE_BATCH_OK:
        return on_batch_done(d, 1);
    case VDE_BATCH_FAIL:
        return on_batch_done(d, 0);

    case VDE_TICK:
        if (d->state == VDS_FINISHING && d->live && d->link != VDL_FAILED &&
            (long)(now - d->final_deadline) >= 0) {
            if (d->link == VDL_DONE)               /* the final arrived; the capture was lost */
                return finish(d, VDA_CLOSE_LINK | VDA_DROP_AUDIO, VDT_NONE, 1);
            if (!d->captured || d->have_audio) {   /* audio kept or still due: a link failure */
                d->link = VDL_FAILED;
                d->fallback = 1;
                d->final_deadline = 0;
                if (d->captured) {
                    VoiceDictOut u = submit(d, VDA_CLOSE_LINK);
                    u.text = VDT_LIVE_STOPPED;
                    return u;
                }
                return out(VDA_CLOSE_LINK, VDT_LIVE_STOPPED);
            }
            return finish(d, VDA_CLOSE_LINK | VDA_DROP_AUDIO,
                          d->send_after ? VDT_FINAL_LATE : VDT_NONE, 0);
        }
        return out(0, VDT_NONE);
    }
    return out(0, VDT_NONE);
}

long voice_dict_next_deadline(const VoiceDict *d, unsigned long now)
{
    long left;
    if (d->state != VDS_FINISHING || !d->live || d->link == VDL_FAILED) return -1;
    left = (long)(d->final_deadline - now);
    return left < 0 ? 0 : left;
}

const char *voice_dict_text(VoiceDictText t)
{
    switch (t) {
    case VDT_LIVE_UNAVAILABLE: return "Live text is unavailable: the words will appear when you stop";
    case VDT_LIVE_STOPPED:     return "Live text stopped: the rest will appear when you stop";
    case VDT_FALLBACK_FAILED:  return "The recording could not be transcribed again: the text in the box is what arrived live";
    case VDT_FINAL_LATE:       return "The last words did not arrive. Check the text and press Send.";
    case VDT_DIDNT_CATCH:      return "Didn't catch that";
    case VDT_TRANSCRIBING:     return "Transcribing\xE2\x80\xA6";
    case VDT_WILL_SEND:        return "Will send when the transcript arrives";
    case VDT_CANCELLED:        return "Transcription cancelled";
    case VDT_NONE:
    default:                   return "";
    }
}
