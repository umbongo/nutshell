#include "voice_win.h"

#include "ai_chat.h"
#include "opt_feature.h"
#include "secure_zero.h"
#include "settings_dlg.h"
#include "voice_audio_win.h"
#include "voice_config.h"
#include "voice_gate.h"
#include "voice_ptt.h"
#include "voice_stt_win.h"
#include "wav.h"
#include <stdio.h>
#include <stdlib.h>

static HWND g_main;
static int g_audio_up;       /* the audio thread was started by voice_feature_apply */
static VoicePtt g_ptt;
static unsigned g_token_seq;
static unsigned g_token;     /* the current recording / upload; 0 = none */
static int g_pending;        /* finished recording, transcript not yet delivered */
static int g_capturing;      /* between capture_begin and VAM_CAPTURED / VAM_ERROR */
static VoiceBinding g_bind;  /* the panel state the recording started in */

int (*g_voice_filter)(const MSG *m);

#define MSG_DIDNT_CATCH "Didn't catch that"
#define CUE_TRANSCRIBING "Transcribing\xE2\x80\xA6"

static void ptt_reset(void)
{
    const Settings *s = ui_current_settings();
    voice_ptt_init(&g_ptt, s ? s->voice_ptt_mode : VOICE_MODE_HOLD,
                   s ? s->voice_mic_delay_ms : VOICE_MIC_DELAY_DEFAULT);
    if (g_main) KillTimer(g_main, VOICE_TIMER_ID);
}

static void arm_timer(void)
{
    if (!g_main) return;
    long ms = voice_ptt_next_deadline(&g_ptt, (unsigned long)GetTickCount());
    if (ms < 0) KillTimer(g_main, VOICE_TIMER_ID);
    else SetTimer(g_main, VOICE_TIMER_ID, (UINT)(ms < 1 ? 1 : ms), NULL);
}

static unsigned next_token(void)
{
    if (++g_token_seq == 0) g_token_seq = 1;
    return g_token_seq;
}

static void act(VoiceStep st, VoiceEvent ev)
{
    const Settings *s = ui_current_settings();
    HWND panel = ui_ai_panel();
    switch (st.action) {
    case VA_OPEN: {
        char cue[128];
        g_token = next_token();
        g_pending = 0;
        ai_chat_voice_binding(panel, &g_bind);
        g_capturing = 1;
        voice_audio_capture_begin(s ? s->voice_input_device : "", g_token, 0);
        voice_cue_text(s ? s->voice_ptt_mode : VOICE_MODE_HOLD,
                       s ? s->voice_ptt_key : VOICE_PTT_RIGHT_CTRL,
                       ev == VE_MIC_CLICK, cue, sizeof(cue));
        ai_chat_voice_set_ui(panel, AIV_RECORDING, cue);
        break;
    }
    case VA_FINISH:
        voice_audio_capture_end(1);
        g_pending = 1;
        ai_chat_voice_set_ui(panel, AIV_TRANSCRIBING, CUE_TRANSCRIBING);
        break;
    case VA_DISCARD:
        voice_audio_capture_end(0);
        g_capturing = 0;
        g_token = 0;
        g_pending = 0;
        ai_chat_voice_set_ui(panel, AIV_IDLE, NULL);
        break;
    case VA_BUSY:
        ai_chat_voice_status(panel, g_pending ? "Still transcribing the last recording"
                                              : "Assistant is busy", 0);
        break;
    case VA_NONE:
        break;
    }
}

static VoiceStep step_now(VoiceEvent ev, int in_panel)
{
    HWND panel = ui_ai_panel();
    VoiceStep st = voice_ptt_step(&g_ptt, ev, (unsigned long)GetTickCount(), in_panel,
                                  ai_chat_voice_busy(panel) || g_pending);
    act(st, ev);
    arm_timer();
    return st;
}

int voice_pump_filter(const MSG *m)
{
    const Settings *s = ui_current_settings();
    if (!s || !g_audio_up) return 0;
    VoiceInput in = voice_ptt_classify(m->message, m->wParam, m->lParam, s->voice_ptt_key);
    if (in == VI_NONE) return 0;
    const int f_key = s->voice_ptt_key != VOICE_PTT_RIGHT_CTRL;   /* Right Ctrl is never consumed */
    const VoiceState before = g_ptt.state;
    if (in == VI_PTT_REPEAT) return f_key && before != VS_IDLE;

    VoiceEvent ev;
    switch (in) {
    case VI_PTT_DOWN: ev = VE_KEY_DOWN; break;
    case VI_PTT_UP:   ev = VE_KEY_UP; break;
    case VI_ESCAPE:   ev = VE_ESCAPE; break;
    default:          ev = VE_OTHER; break;
    }
    HWND panel = ui_ai_panel();
    int in_panel = panel && ai_chat_focus_role(panel, GetFocus()) != NULL;
    VoiceStep st = step_now(ev, in_panel);

    if (in == VI_ESCAPE) return st.consume;
    if (in == VI_PTT_DOWN || in == VI_PTT_UP)
        return f_key && (before != g_ptt.state || g_ptt.state != VS_IDLE);
    return 0;
}

void voice_on_timer(void)
{
    if (g_main) KillTimer(g_main, VOICE_TIMER_ID);
    if (!g_audio_up) return;
    HWND panel = ui_ai_panel();
    step_now(VE_TICK, panel && ai_chat_focus_role(panel, GetFocus()) != NULL);
}

void voice_on_deactivate(void)
{
    if (!g_audio_up) return;
    step_now(VE_DEACTIVATE, 0);
}

void voice_mic_clicked(void)
{
    if (!g_audio_up) return;
    step_now(VE_MIC_CLICK, 1);
}

/* Back to nothing recording and nothing uploading, whatever state it was in. */
static void stop_everything(void)
{
    if (g_capturing && g_audio_up) voice_audio_capture_end(0);
    g_capturing = 0;
    g_token = 0;
    g_pending = 0;
    ptt_reset();
}

void voice_feature_apply(HWND main)
{
    const Settings *s = ui_current_settings();
    int on = (s != NULL) ? opt_feature_active(s, OPT_VOICE) : 0;
    voice_gate_set_enabled(on);
    if (main) g_main = main;
    if (on) {
        /* Idempotent: it is called more than once at startup. */
        if (!g_audio_up && voice_audio_start(main)) g_audio_up = 1;
        if (g_audio_up) {
            if (g_ptt.state == VS_IDLE && !g_capturing) ptt_reset();   /* new key/mode/delay */
            g_voice_filter = voice_pump_filter;
            ai_chat_voice_attach(ui_ai_panel(), 1);
        }
    } else {
        g_voice_filter = NULL;
        stop_everything();
        voice_stt_cancel_all();   /* an upload in flight is cancelled and joined */
        ai_chat_voice_attach(ui_ai_panel(), 0);
        if (g_audio_up) {
            voice_audio_stop();
            g_audio_up = 0;
        }
    }
}

void voice_feature_shutdown(void)
{
    voice_gate_set_enabled(0);
    g_voice_filter = NULL;
    stop_everything();
    voice_stt_cancel_all();
    ai_chat_voice_attach(ui_ai_panel(), 0);
    if (g_audio_up) {
        voice_audio_stop();
        g_audio_up = 0;
    }
}

int voice_feature_running(void)
{
    return g_audio_up;
}

static void on_captured(VoiceCapture *c)
{
    HWND panel = ui_ai_panel();
    const Settings *s = ui_current_settings();
    if (!g_audio_up || !g_capturing || !g_token || c->token != g_token) {
        voice_audio_free_capture(c);   /* a stale or discarded recording */
        return;
    }
    g_capturing = 0;
    const char *problem = NULL;
    int problem_is_error = 0;
    switch (c->verdict) {
    case PCM_EMPTY:
    case PCM_SILENT:
        problem = MSG_DIDNT_CATCH;
        break;
    case PCM_ALL_ZERO:
        problem = "Windows may be blocking the microphone: Settings > Privacy & security > Microphone";
        problem_is_error = 1;
        break;
    default:
        break;
    }
    if (!problem) {
        size_t n = 0;
        unsigned char *wav = pcm_to_wav(&c->pcm, WAV_RATE, &n);
        if (!wav) {
            problem = "Not enough memory to send the recording";
            problem_is_error = 1;
        } else {
            char err[300] = "";
            /* Takes ownership of the WAV, success or not. */
            if (!s || !voice_stt_submit(g_main, s, wav, n, g_token, err, sizeof(err))) {
                problem = err[0] ? err : "The recording could not be sent";
                problem_is_error = 1;
                ai_chat_voice_set_ui(panel, AIV_IDLE, NULL);
                ai_chat_voice_status(panel, problem, 1);
                g_token = 0;
                g_pending = 0;
                voice_audio_free_capture(c);
                return;
            }
        }
    }
    voice_audio_free_capture(c);   /* the PCM is wiped either way */
    if (problem) {
        g_token = 0;
        g_pending = 0;
        ai_chat_voice_set_ui(panel, AIV_IDLE, NULL);
        ai_chat_voice_status(panel, problem, problem_is_error);
    }
    /* else: the upload runs; the UI stays on "Transcribing" until WM_VOICE_STT. */
}

static void on_capture_error(VoiceAudioErr e)
{
    HWND panel = ui_ai_panel();
    const char *text;
    switch (e) {
    case VAE_NO_DEVICE:
        text = "No microphone was found. Connect one, or choose another in Settings > Voice.";
        break;
    case VAE_DEVICE_LOST:
        text = "The microphone stopped responding (was it unplugged?). Nothing was sent.";
        break;
    default:
        text = "The microphone could not be opened.";
        break;
    }
    g_capturing = 0;
    g_token = 0;
    g_pending = 0;
    ptt_reset();
    ai_chat_voice_set_ui(panel, AIV_IDLE, NULL);
    ai_chat_voice_status(panel, text, 1);
}

void voice_on_audio_msg(WPARAM wp, LPARAM lp)
{
    /* A push-to-talk capture's messages are ours whoever has Settings open
     * (the Test uses token 0 and never sends a capture). */
    if (wp == VAM_CAPTURED) {
        VoiceCapture *c = (VoiceCapture *)lp;
        if (c) on_captured(c);
        return;
    }
    if (wp == VAM_ERROR && g_capturing && g_audio_up) {
        on_capture_error((VoiceAudioErr)lp);
        return;
    }
    /* An open Settings page takes the device list, levels and errors; what
     * nobody wants is freed here. */
    if (settings_voice_on_audio(wp, lp)) return;
    if (wp == VAM_DEVICES) voice_audio_free_devices((VoiceDeviceList *)lp);
}

void voice_on_stt(LPARAM lp)
{
    VoiceSttResult *r = (VoiceSttResult *)lp;
    if (!r) return;
    if (!g_audio_up || !g_token || r->token != g_token) {
        secure_zero(r, sizeof(*r));   /* stale, or voice went off meanwhile */
        free(r);
        return;
    }
    g_token = 0;
    g_pending = 0;
    HWND panel = ui_ai_panel();
    const Settings *s = ui_current_settings();
    ai_chat_voice_set_ui(panel, AIV_IDLE, NULL);
    VoiceBinding now;
    ai_chat_voice_binding(panel, &now);
    if (!panel || !voice_binding_same(&g_bind, &now))
        ai_chat_voice_status(panel, "Voice message dropped: the chat changed", 0);
    else if (!r->ok)
        ai_chat_voice_status(panel, r->err[0] ? r->err : "Transcription failed", 1);
    else
        ai_chat_voice_deliver(panel, r->text, s ? s->voice_review : 0);
    secure_zero(r, sizeof(*r));
    free(r);
}
