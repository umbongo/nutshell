#include "voice_read_win.h"

#include <stdlib.h>
#include <string.h>
#include "opt_feature.h"
#include "secure_zero.h"
#include "voice_aispeech.h"
#include "voice_config.h"

/* UI-thread state. */
static const VoiceReadOps *g_ops;
static VoiceRead g_m;
static char *g_text;            /* the turn's text until it is spoken or the reading ends */
static VoiceBinding g_bind;     /* the chat the press came from */
static uint64_t g_digest;       /* voice_settings_digest at the held context's start */
static unsigned g_gen;          /* generation of the held context; 0 = none held */
static int g_running, g_shown, g_recording;

static void text_drop(void)
{
    if (g_text != NULL) {
        secure_zero(g_text, strlen(g_text));
        free(g_text);
        g_text = NULL;
    }
}

static void push_view(HWND panel)
{
    if (g_ops != NULL && g_ops->view != NULL)
        g_ops->view(panel, g_shown, g_m.item_id, g_recording);
}

static void run(VoiceReadOut o, const Settings *s);

static void run_start(const VoiceReadOut *o, const Settings *s)
{
    int ok = g_ops->start != NULL && g_ops->start(VOICE_READ_TOKEN, o->engine);
    if (ok) {
        unsigned gen = g_ops->context_gen != NULL ? g_ops->context_gen(VOICE_READ_TOKEN) : 0;
        if (gen == 0) {               /* a context nobody can tell apart from a stale one */
            if (g_ops->quit != NULL)
                g_ops->quit(VOICE_READ_TOKEN);
            ok = 0;
        } else {
            g_digest = s != NULL ? voice_settings_digest(s) : 0;
            g_gen = gen;
        }
    }
    run(voice_read_on_started(&g_m, ok), s);
}

static void run(VoiceReadOut o, const Settings *s)
{
    if (g_ops == NULL)
        return;
    if ((o.actions & VRA_STOP) && g_ops->stop != NULL)
        g_ops->stop(VOICE_READ_TOKEN);
    if (o.actions & VRA_RELEASE) {
        if (g_ops->quit != NULL)
            g_ops->quit(VOICE_READ_TOKEN);
        g_gen = 0;
    }
    if (o.actions & VRA_START)
        run_start(&o, s);
    if ((o.actions & VRA_SPEAK) && g_text != NULL && g_ops->speak != NULL) {
        g_ops->speak(VOICE_READ_TOKEN, o.serial, g_text, 0);
        text_drop();
    }
    if ((o.actions & VRA_ERROR) && g_ops->note != NULL) {
        char reason[200], text[320];
        const char *rs = NULL;
        reason[0] = '\0';
        if (o.err_engine == VOICE_SPEECH_AI && o.hr == VOICE_AIS_HR_RESOLVE
            && g_ops->resolve_error != NULL && g_ops->resolve_error(reason, sizeof reason))
            rs = reason;
        voice_read_error_text(o.err_engine, o.hr, rs, text, sizeof text);
        g_ops->note(text);
    }
    if (o.actions & VRA_VIEW)
        push_view(NULL);
    if (g_m.state == VRS_IDLE)
        text_drop();
}

void voice_read_win_set_ops(const VoiceReadOps *ops)
{
    if (g_ops != NULL && (g_m.ctx || g_m.state != VRS_IDLE)) {
        /* Release what is held through the ops that started it. */
        if (g_m.state != VRS_IDLE) {
            run(voice_read_on_stop(&g_m, VRX_PANEL), NULL);     /* stop, quit, view */
        } else {
            if (g_ops->quit != NULL)
                g_ops->quit(VOICE_READ_TOKEN);
            g_m.ctx = 0;
            g_gen = 0;
            push_view(NULL);
        }
    }
    text_drop();
    g_ops = ops;
    voice_read_init(&g_m);
    memset(&g_bind, 0, sizeof g_bind);
    g_digest = 0;
    g_gen = 0;
    g_running = g_shown = g_recording = 0;
}

void voice_read_win_press(const Settings *s, int item_id, int recording)
{
    VoiceReadPress p;
    char *text = NULL;
    int gate;
    if (g_ops == NULL || s == NULL)
        return;
    gate = g_running && voice_read_gate(opt_feature_active(s, OPT_VOICE), s->voice_mode,
                                        recording || g_recording);
    if (!gate)
        return;
    if (g_ops->context_gen == NULL) {      /* required: nothing is started without it */
        if (g_ops->note != NULL) {
            char msgtxt[320];
            voice_read_error_text(s->voice_speech_engine, 0, NULL, msgtxt, sizeof msgtxt);
            g_ops->note(msgtxt);
        }
        return;
    }
    if (g_ops->turn_text != NULL)
        text = g_ops->turn_text(item_id);
    p.item_id = item_id;
    p.engine = s->voice_speech_engine;
    p.gate = gate;
    p.has_text = text != NULL && text[0] != '\0';
    if (p.has_text) {
        text_drop();
        g_text = text;
    } else if (text != NULL) {
        free(text);
    }
    if (g_ops->binding != NULL)
        g_ops->binding(&g_bind);
    run(voice_read_on_press(&g_m, &p), s);
}

void voice_read_win_stop(VoiceReadStop why)
{
    run(voice_read_on_stop(&g_m, why), NULL);
}

void voice_read_win_on_tts(const VoiceTtsMsg *m)
{
    VoiceReadMsg kind;
    if (g_ops == NULL || m == NULL || m->token != VOICE_READ_TOKEN)
        return;
    switch (m->kind) {
    case VTT_READY: kind = VRM_READY; break;
    case VTT_DONE:  kind = VRM_DONE;  break;
    case VTT_ERROR: kind = VRM_ERROR; break;
    case VTT_EXITED:                  /* the held context's own thread ended: an error, no reply */
        kind = VRM_ERROR;
        break;
    default: return;                  /* VTT_WORD */
    }
    /* Only the held context's own messages count: one of a released context
     * (a late VTT_ERROR with reply id 0, a late VTT_READY) must not reach
     * the next one. */
    if (!g_m.ctx || g_gen == 0 || m->gen != g_gen)
        return;
    run(voice_read_on_engine(&g_m, kind, m->kind == VTT_EXITED ? 0 : m->reply_id, m->hr), NULL);
}

void voice_read_win_apply(const Settings *s, int running)
{
    if (g_ops == NULL || s == NULL)
        return;
    g_running = running;
    g_shown = running && voice_read_shown(opt_feature_active(s, OPT_VOICE), s->voice_mode);
    if (!g_shown || (g_m.ctx && voice_settings_digest(s) != g_digest))
        voice_read_win_stop(VRX_SETTINGS);
    push_view(NULL);
}

void voice_read_win_set_recording(int on)
{
    g_recording = on ? 1 : 0;
    if (g_recording && g_ops != NULL)
        voice_read_win_stop(VRX_DICTATION);   /* the microphone must not hear the speakers */
    push_view(NULL);
}

void voice_read_win_chat_check(void)
{
    VoiceBinding cur;
    if (g_ops == NULL || g_ops->binding == NULL || g_m.state == VRS_IDLE)
        return;
    memset(&cur, 0, sizeof cur);
    g_ops->binding(&cur);
    if (!voice_binding_same(&g_bind, &cur))
        voice_read_win_stop(VRX_CHAT);
}

int voice_read_win_active(void)
{
    return g_m.state != VRS_IDLE;
}

void voice_read_win_view_sync(HWND panel)
{
    push_view(panel);
}
