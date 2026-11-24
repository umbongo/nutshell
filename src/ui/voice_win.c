#include "voice_win.h"

#include "ai_chat.h"
#include "opt_feature.h"
#include "secure_zero.h"
#include "settings_dlg.h"
#include "voice_audio_win.h"
#include "voice_config.h"
#include "voice_dict.h"
#include "voice_gate.h"
#include "voice_ledger.h"
#include "voice_live.h"
#include "voice_live_win.h"
#include "voice_ptt.h"
#include "voice_stt.h"
#include "voice_stt_win.h"
#include "voice_transcript.h"
#include "wav.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The recording's lifecycle is voice_dict's (pure, tested natively); voice_ptt
 * stays the key machine. This file carries out voice_dict's actions against the
 * audio thread, the live session, the upload worker and the AI panel's box.
 *
 * Every action handler is idempotent and safe with nothing held. Every capture,
 * live session, upload and live message carries the recording's token; anything
 * for another token is only freed.
 */

static HWND g_main;
static int g_audio_up;       /* the audio thread was started by voice_feature_apply */
static VoicePtt g_ptt;
static unsigned g_token_seq;
static unsigned g_token;     /* the current recording; 0 = none (IDLE) */
static VoiceBinding g_bind;  /* the panel state the recording started in */

static VoiceDict g_dict;
static int g_live;                 /* this recording is live */
static VoiceLiveTarget g_live_t;
static VoiceTranscript g_vt;       /* valid while g_vt_on */
static int g_vt_on;
static uint64_t g_digest;          /* voice_settings_digest at START */

static int g_capturing;            /* between capture_begin and VAM_CAPTURED / VAM_ERROR / discard */
static unsigned g_cap_token;       /* the open capture's token (it can outlive the recording) */
static VoiceCapture *g_kept;       /* audio kept for the final / the fallback upload */

static int g_link_open;            /* voice_live_start succeeded and voice_live_close was not called */
static unsigned g_link_token;
static int g_fail_reported;        /* the link's failure was reported (an ERROR then CLOSED is one failure) */

static int g_upload_open;          /* a VRES_STT_UPLOAD is held */
static unsigned g_upload_token;
static char *g_result_text;        /* the upload's transcript while DELIVER_BATCH runs (not owned) */

static int g_start_latched;        /* the recording was started by the mic button (cue text) */
static int g_suppress_dc;          /* the privacy text already told the user: no "Didn't catch that" */
static AiVoiceUi g_ui = AIV_IDLE;  /* what the panel's mic/cue shows */

int (*g_voice_filter)(const MSG *m);

#define MSG_DIDNT_CATCH "Didn't catch that"
#define VOICE_PRIVACY_TEXT \
    "Windows may be blocking the microphone: Settings > Privacy & security > Microphone"
#define CUE_TRANSCRIBING "Transcribing\xE2\x80\xA6"
#define LIVE_ERR_PREFIX "Live transcription error: "
#define WS_REFUSED_PREFIX "websocket upgrade refused, HTTP status: "

static unsigned long now_ms(void)
{
    return (unsigned long)GetTickCount();
}

static int tok_eq(unsigned a, unsigned b)
{
    return ((a ^ b) & VOICE_TOKEN_MASK) == 0;
}

static void dict_event(VoiceDictEvent ev, int a, int b);

static void ptt_reset(void)
{
    const Settings *s = ui_current_settings();
    voice_ptt_init(&g_ptt, s ? s->voice_ptt_mode : VOICE_MODE_TOGGLE,
                   s ? s->voice_mic_delay_ms : VOICE_MIC_DELAY_DEFAULT);
    if (g_main) KillTimer(g_main, VOICE_TIMER_ID);
}

static void arm_timer(void)
{
    if (!g_main) return;
    const unsigned long t = now_ms();
    const long a = voice_ptt_next_deadline(&g_ptt, t);
    const long b = voice_dict_next_deadline(&g_dict, t);
    long ms = a < 0 ? b : (b < 0 ? a : (a < b ? a : b));
    if (ms < 0) KillTimer(g_main, VOICE_TIMER_ID);
    else SetTimer(g_main, VOICE_TIMER_ID, (UINT)(ms < 1 ? 1 : ms), NULL);
}

static unsigned next_token(void)
{
    /* Never 0 in the low 24 bits either: an audio error compares masked tokens. */
    if (((++g_token_seq) & VOICE_TOKEN_MASK) == 0) g_token_seq++;
    return g_token_seq;
}

/* No active session: the send path would drop the message, so no recording starts. */
static int no_session(HWND panel)
{
    VoiceBinding b;
    ai_chat_voice_binding(panel, &b);
    return b.session == NULL;
}

static void set_ui(AiVoiceUi st, const char *cue)
{
    g_ui = st;
    ai_chat_voice_set_ui(ui_ai_panel(), st, cue);
}

static void chat_error(const char *text)
{
    ai_chat_voice_status(ui_ai_panel(), text, 1);
}

/* ---- Held resources: each released exactly once ---- */

static void release_capture(void)
{
    if (!g_capturing) return;
    g_capturing = 0;
    voice_ledger_release(VRES_CAPTURE);
}

static void free_kept(void)
{
    if (!g_kept) return;
    voice_audio_free_capture(g_kept);   /* the PCM is wiped */
    g_kept = NULL;
}

static void release_upload(void)
{
    if (!g_upload_open) return;
    g_upload_open = 0;
    voice_ledger_release(VRES_STT_UPLOAD);
}

static void close_link(void)
{
    if (!g_link_open) return;
    g_link_open = 0;
    voice_live_close(g_link_token);
    voice_live_reap(0);
}

static void free_transcript(void)
{
    if (!g_vt_on) return;
    vt_free(&g_vt);
    g_vt_on = 0;
}

static int shown_has_words(void)
{
    if (!g_vt_on || !g_vt.shown) return 0;
    for (size_t i = 0; i < g_vt.slen; i++) {
        const char c = g_vt.shown[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return 1;
    }
    return 0;
}

/* Write what the transcript added or changed into the box. */
static void live_flush(void)
{
    size_t from = 0;
    char *txt = g_vt_on ? vt_render(&g_vt, &from) : NULL;
    if (!txt) return;
    ai_chat_voice_live_replace(ui_ai_panel(), from, txt);
    secure_zero(txt, strlen(txt) + 1);
    free(txt);
}

/* The box must still hold what this recording last wrote: something else
 * changed it otherwise, and the recording ends (never sends) rather than
 * overwrite it. */
static int box_intact(void)
{
    if (!g_vt_on) return 0;
    const size_t len = ai_chat_voice_live_begin(ui_ai_panel(), NULL);
    if (len < g_vt.draft_u16 + g_vt.shown_u16) {
        dict_event(VDE_END_SOFT, 0, 0);
        return 0;
    }
    return 1;
}

/* ---- The dict's actions ---- */

static void open_capture(const Settings *s)
{
    char cue[160];
    release_capture();   /* a capture still open here would leak its count */
    g_capturing = 1;
    g_cap_token = g_token;
    voice_ledger_acquire(VRES_CAPTURE);
    voice_audio_capture_begin(s ? s->voice_input_device : "", g_token, 0,
                              s ? s->voice_mic_gain_db : 0,
                              g_live ? VOICE_LIVE_CAP_MS : VOICE_CAP_MS, g_live);
    voice_cue_text(s ? s->voice_ptt_mode : VOICE_MODE_TOGGLE,
                   s ? s->voice_ptt_key : VOICE_PTT_RIGHT_SHIFT,
                   g_start_latched, s ? s->voice_auto_send : 0, g_live,
                   cue, sizeof(cue));
    set_ui(AIV_RECORDING, cue);
}

static void open_link(const Settings *s)
{
    char err[200] = "";
    const char *key = s ? (g_live_t.key_from_chat ? s->ai_api_key : s->stt_api_key) : "";
    g_fail_reported = 0;
    if (s && voice_live_start(g_main, &g_live_t, key, g_token, err, sizeof(err))) {
        g_link_open = 1;
        g_link_token = g_token;
        return;
    }
    dict_event(VDE_LINK_FAILED, 1, 0);   /* it never opened */
}

static void remove_words(void)
{
    if (g_live && g_vt_on)
        ai_chat_voice_live_replace(ui_ai_panel(), g_vt.draft_u16, "");
    free_transcript();
}

static void cancel_batch(void)
{
    if (!g_upload_open) return;
    voice_stt_cancel_all();   /* each cancelled upload still posts its failed result: stale, freed */
    release_upload();
}

static void submit_batch(const Settings *s)
{
    char err[300] = "";
    VoiceCapture *c = g_kept;
    unsigned char *wav = NULL;
    size_t n = 0;
    g_kept = NULL;
    if (c) wav = pcm_to_wav(&c->pcm, WAV_RATE, &n);
    if (c) voice_audio_free_capture(c);   /* the WAV is the only copy now */
    if (!wav) {
        snprintf(err, sizeof(err), "Not enough memory to send the recording");
    } else if (s && voice_stt_submit(g_main, s, wav, n, g_token, err, sizeof(err))) {
        /* Took ownership of the WAV. */
        voice_ledger_acquire(VRES_STT_UPLOAD);
        g_upload_open = 1;
        g_upload_token = g_token;
        return;
    } else if (!err[0]) {
        snprintf(err, sizeof(err), "The recording could not be sent");
    }
    /* A batch recording says why; a fallback's own text says the live words stay. */
    if (!g_live) chat_error(err);
    dict_event(VDE_BATCH_FAIL, 0, 0);
}

/* Send what the live recording left in the box. */
static void send_box(void)
{
    if (!shown_has_words()) {
        if (!g_suppress_dc) ai_chat_voice_status(ui_ai_panel(), MSG_DIDNT_CATCH, 0);
        return;
    }
    (void)ai_chat_voice_send_box(ui_ai_panel());
}

/* The upload's transcript arrived: a batch recording appends it, a fallback
 * replaces exactly this recording's live text. send: the finish rule said send. */
static void deliver_batch(int send)
{
    if (!g_result_text) return;
    if (!g_live) {
        ai_chat_voice_deliver(ui_ai_panel(), g_result_text, 0, send);
        return;
    }
    if (g_vt_on && vt_replace_all(&g_vt, g_result_text) == 0) live_flush();
    if (send) send_box();
}

static void show_text(VoiceDictText t)
{
    HWND panel = ui_ai_panel();
    switch (t) {
    case VDT_NONE:
    case VDT_TRANSCRIBING:       /* the panel's Transcribing look follows the state */
        break;
    case VDT_DIDNT_CATCH:
        if (!g_suppress_dc) ai_chat_voice_status(panel, voice_dict_text(t), 0);
        break;
    case VDT_LIVE_UNAVAILABLE:
    case VDT_LIVE_STOPPED:
        ai_chat_voice_status(panel, voice_dict_text(t), 0);
        break;
    case VDT_FALLBACK_FAILED:
    case VDT_FINAL_LATE:
        ai_chat_voice_status(panel, voice_dict_text(t), 1);
        break;
    }
}

static void run_out(VoiceDictOut o)
{
    const Settings *s = ui_current_settings();
    const unsigned a = o.actions;
    if (a & VDA_OPEN_CAPTURE) open_capture(s);
    if (a & VDA_OPEN_LINK) open_link(s);
    if ((a & VDA_LOCK_TYPING) && g_live) ai_chat_voice_lock_typing(ui_ai_panel(), 1);
    if (a & VDA_END_CAPTURE_KEEP) {
        if (g_capturing) voice_audio_capture_end(1);
    }
    if (a & VDA_END_CAPTURE_DISCARD) {
        if (g_capturing) {
            voice_audio_capture_end(0);
            release_capture();
        }
    }
    if (a & VDA_CLOSE_LINK) close_link();
    if (a & VDA_REMOVE_WORDS) remove_words();
    if (a & VDA_CANCEL_BATCH) cancel_batch();
    if (a & VDA_SUBMIT_BATCH) submit_batch(s);
    if (a & VDA_DROP_AUDIO) free_kept();
    /* Deliver first, then send: DELIVER_BATCH never sends on its own. */
    if (a & VDA_DELIVER_BATCH) deliver_batch((a & VDA_SEND_BOX) != 0);
    else if (a & VDA_SEND_BOX) send_box();
    show_text(o.text);
    if (a & VDA_UNLOCK_TYPING) ai_chat_voice_lock_typing(ui_ai_panel(), 0);
}

/* A recording the dict stopped without the key machine's say (Send, the cap, a
 * chat change, the final) takes the key machine out of recording too. */
static void ptt_sync(void)
{
    if (voice_ptt_recording(&g_ptt) && g_dict.state != VDS_RECORDING) {
        voice_ptt_capture_ended(&g_ptt, g_ptt.state == VS_REC_HOLD || g_ptt.state == VS_STOP_PRESS
                                       || g_ptt.state == VS_LATCHED_SPENT);
    }
}

static void post_step(void)
{
    if (g_dict.state == VDS_IDLE) {
        /* The recording is over: nothing it held stays open. */
        free_kept();
        close_link();
        free_transcript();
        g_token = 0;
        g_live = 0;
        if (g_ui != AIV_IDLE) set_ui(AIV_IDLE, NULL);
    } else if (g_dict.state == VDS_FINISHING && g_ui != AIV_TRANSCRIBING) {
        set_ui(AIV_TRANSCRIBING, CUE_TRANSCRIBING);
    }
    ptt_sync();
    arm_timer();
}

static void dict_event(VoiceDictEvent ev, int a, int b)
{
    run_out(voice_dict_step(&g_dict, ev, a, b, now_ms()));
    post_step();
}

static void start_recording(void)
{
    const Settings *s = ui_current_settings();
    HWND panel = ui_ai_panel();
    int need_sep = 0;
    voice_live_reap(0);
    g_token = next_token();
    ai_chat_voice_binding(panel, &g_bind);
    g_live = s && voice_live_resolve(s, &g_live_t);
    g_suppress_dc = 0;
    g_fail_reported = 0;
    g_result_text = NULL;
    g_digest = s ? voice_settings_digest(s) : 0;
    const size_t draft = ai_chat_voice_live_begin(panel, &need_sep);
    free_transcript();
    if (g_live) {
        vt_init(&g_vt, draft, need_sep);
        g_vt_on = 1;
    }
    voice_ptt_set_cap(&g_ptt, g_live ? VOICE_LIVE_CAP_MS : 0);
    dict_event(VDE_START, g_live, (s && s->voice_auto_send && draft == 0) ? 1 : 0);
}

static void act(VoiceStep st, VoiceEvent ev)
{
    HWND panel = ui_ai_panel();
    switch (st.action) {
    case VA_OPEN:
        g_start_latched = (ev == VE_MIC_CLICK);
        start_recording();
        break;
    case VA_FINISH:
        dict_event(VDE_STOP, 0, 0);   /* nothing open (the cap ended it): a no-op */
        break;
    case VA_DISCARD:
        dict_event(ev == VE_ESCAPE ? VDE_ESCAPE : VDE_END_SOFT, 0, 0);
        break;
    case VA_BUSY:
        ai_chat_voice_status(panel, voice_busy_text(g_dict.state != VDS_IDLE || g_capturing,
                                                    no_session(panel)), 0);
        break;
    case VA_NONE:
        break;
    }
}

static VoiceStep step_now(VoiceEvent ev, int in_panel)
{
    HWND panel = ui_ai_panel();
    VoiceStep st = voice_ptt_step(&g_ptt, ev, now_ms(), in_panel,
                                  ai_chat_voice_busy(panel) || g_dict.state != VDS_IDLE
                                  || g_capturing || no_session(panel));
    act(st, ev);
    arm_timer();
    return st;
}

/* The modifier keys physically held now, for the chord filter. */
static unsigned held_mods(void)
{
    unsigned mods = 0;
    if (GetKeyState(VK_LSHIFT) & 0x8000) mods |= VOICE_MOD_LSHIFT;
    if (GetKeyState(VK_RSHIFT) & 0x8000) mods |= VOICE_MOD_RSHIFT;
    if (GetKeyState(VK_LCONTROL) & 0x8000) mods |= VOICE_MOD_LCTRL;
    if (GetKeyState(VK_RCONTROL) & 0x8000) mods |= VOICE_MOD_RCTRL;
    if (GetKeyState(VK_MENU) & 0x8000) mods |= VOICE_MOD_ALT;
    if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) mods |= VOICE_MOD_WIN;
    return mods;
}

int voice_pump_filter(const MSG *m)
{
    const Settings *s = ui_current_settings();
    if (!s || !g_audio_up) return 0;
    VoiceInput in = voice_ptt_classify(m->message, m->wParam, m->lParam, s->voice_ptt_key);
    if (in == VI_NONE) return 0;
    if (in == VI_PTT_DOWN) in = voice_ptt_chord_filter(in, s->voice_ptt_key, held_mods());
    /* Modifier keys (Right Shift, Right Ctrl) are never consumed: a press must
     * still reach the terminal or edit (capitals, chords). */
    const int f_key = !voice_ptt_key_is_modifier(s->voice_ptt_key);
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
    dict_event(VDE_TICK, 0, 0);   /* the wait for a live final runs out here */
}

void voice_on_deactivate(void)
{
    if (!g_audio_up) return;
    step_now(VE_DEACTIVATE, 0);
    /* A live recording already stopped and waiting for its final also ends (never
     * sends); a batch upload in flight keeps today's rule. */
    if (g_live && g_dict.state != VDS_IDLE) dict_event(VDE_END_SOFT, 0, 0);
}

void voice_mic_clicked(void)
{
    if (!g_audio_up) return;
    step_now(VE_MIC_CLICK, 1);
}

int voice_send_pressed(void)
{
    if (!g_audio_up || g_dict.state == VDS_IDLE) return 0;
    dict_event(VDE_SEND, 0, 0);
    return 1;
}

void voice_on_chat_changed(void)
{
    /* Called from inside ai_chat: only a post, never a synchronous re-entry. */
    if (g_main && g_audio_up && g_dict.state != VDS_IDLE)
        PostMessage(g_main, WM_VOICE_CHAT, 0, 0);
}

void voice_on_chat_changed_msg(void)
{
    /* A batch recording keeps today's rule: dropped at delivery when the
     * binding changed. A live one writes into the box as it goes, so it ends. */
    if (!g_audio_up || g_dict.state == VDS_IDLE || !g_live) return;
    VoiceBinding now;
    ai_chat_voice_binding(ui_ai_panel(), &now);
    if (!voice_binding_same(&g_bind, &now)) dict_event(VDE_END_SOFT, 0, 0);
}

/* Back to nothing recording and nothing uploading, whatever state it was in. */
static void stop_everything(void)
{
    if (g_dict.state != VDS_IDLE) dict_event(VDE_END_HARD, 0, 0);
    if (g_capturing) {
        if (g_audio_up) voice_audio_capture_end(0);
        release_capture();
    }
    free_kept();
    close_link();
    free_transcript();
    release_upload();
    g_token = 0;
    g_live = 0;
    ai_chat_voice_lock_typing(ui_ai_panel(), 0);
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
            /* A voice setting changed under a running recording: end it, whatever it was. */
            if (g_dict.state != VDS_IDLE && s && voice_settings_digest(s) != g_digest)
                dict_event(VDE_END_HARD, 0, 0);
            if (g_ptt.state == VS_IDLE && !g_capturing) ptt_reset();   /* new key/mode/delay */
            g_voice_filter = voice_pump_filter;
            ai_chat_voice_attach(ui_ai_panel(), 1);
        }
    } else {
        g_voice_filter = NULL;
        stop_everything();
        voice_stt_cancel_all();   /* an upload in flight is cancelled and joined */
        voice_live_reap(3000);    /* closes and joins every live session */
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
    voice_live_reap(3000);
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
    if (!g_audio_up || !g_capturing || c->token != g_cap_token) {
        voice_audio_free_capture(c);   /* a stale or discarded recording */
        return;
    }
    release_capture();
    if (!g_token || c->token != g_token || g_dict.state == VDS_IDLE) {
        voice_audio_free_capture(c);   /* the recording already ended (the live final came first) */
        ptt_sync();
        arm_timer();
        return;
    }
    int has_audio = 0;
    switch (c->verdict) {
    case PCM_EMPTY:
    case PCM_SILENT:
        break;
    case PCM_ALL_ZERO:
        g_suppress_dc = 1;
        chat_error(VOICE_PRIVACY_TEXT);
        break;
    default:
        has_audio = 1;
        break;
    }
    if (has_audio) {
        free_kept();
        g_kept = c;   /* freed exactly once: submit, drop, or the recording's end */
    } else {
        voice_audio_free_capture(c);   /* the PCM is wiped either way */
    }
    dict_event(VDE_CAPTURED, has_audio, 0);
}

static void on_capture_error(VoiceAudioErr e)
{
    /* The thread has ended this capture already; this also covers any path
     * where the device could still be open. A no-op when nothing is active. */
    if (g_audio_up) voice_audio_capture_end(0);
    release_capture();
    const char *text;
    switch (e) {
    case VAE_NO_DEVICE:
        text = "No microphone was found. Connect one, or choose another in Settings > Voice.";
        break;
    case VAE_DEVICE_LOST:
        text = "The microphone stopped responding (was it unplugged?). Nothing was sent.";
        break;
    case VAE_ACCESS_DENIED:
        text = VOICE_PRIVACY_TEXT;
        break;
    default:
        text = "The microphone could not be opened.";
        break;
    }
    if (g_dict.state != VDS_IDLE) dict_event(VDE_END_HARD, 0, 0);
    ptt_reset();
    set_ui(AIV_IDLE, NULL);
    chat_error(text);
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
    if (wp == VAM_ERROR) {
        const VoiceAudioErr err = VOICE_ERR_CODE(lp);
        const VoiceErrRoute route = voice_err_route(VOICE_ERR_TOKEN(lp), g_cap_token,
                                                    g_capturing && g_audio_up);
        if (route == VOICE_ERR_CAPTURE) on_capture_error(err);
        else if (route == VOICE_ERR_SETTINGS) (void)settings_voice_on_audio(wp, (LPARAM)err);
        return;   /* a stale recording's error changes nothing */
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
    if (!g_audio_up || !g_upload_open || r->token != g_upload_token) {
        voice_stt_result_free(r);   /* stale (cancelled), or voice went off meanwhile */
        return;
    }
    release_upload();
    if (!g_token || r->token != g_token || g_dict.state != VDS_FINISHING) {
        voice_stt_result_free(r);
        return;
    }
    if (!g_live) {
        /* A batch recording: today's rules. The chat changed: dropped, whatever
         * came back; a failure says why; the words are appended otherwise. */
        VoiceBinding now;
        HWND panel = ui_ai_panel();
        ai_chat_voice_binding(panel, &now);
        if (!panel || !voice_binding_same(&g_bind, &now)) {
            ai_chat_voice_status(panel, "Voice message dropped: the chat changed", 0);
            dict_event(VDE_BATCH_FAIL, 0, 0);
        } else if (!r->ok) {
            chat_error(r->err[0] ? r->err : "Transcription failed");
            dict_event(VDE_BATCH_FAIL, 0, 0);
        } else {
            g_result_text = r->text;
            dict_event(VDE_BATCH_OK, 0, 0);
            g_result_text = NULL;
        }
    } else if (r->ok) {
        g_result_text = r->text;
        dict_event(VDE_BATCH_OK, 0, 0);
        g_result_text = NULL;
    } else {
        dict_event(VDE_BATCH_FAIL, 0, 0);   /* the live words stay: its own text says so */
    }
    voice_stt_result_free(r);
}

/* A failed link's reason for the chat, once. */
static void report_link_failure(const VoiceLiveEvent *ev)
{
    if (g_fail_reported) return;
    g_fail_reported = 1;
    char text[400] = "";
    if (ev->kind == VLE_ERROR) {
        snprintf(text, sizeof(text), LIVE_ERR_PREFIX "%s",
                 ev->text && ev->text[0] ? ev->text : "unknown error");
    } else if (ev->text && ev->text[0]) {
        const size_t pl = strlen(WS_REFUSED_PREFIX);
        if (strncmp(ev->text, WS_REFUSED_PREFIX, pl) == 0)
            voice_stt_http_error(atoi(ev->text + pl), "", text, sizeof(text));
        else
            voice_stt_http_error(0, ev->text, text, sizeof(text));
    }
    if (text[0]) chat_error(text);
}

void voice_on_live(LPARAM lp)
{
    VoiceLiveMsg *m = (VoiceLiveMsg *)lp;
    voice_live_reap(0);   /* join the sessions that have ended */
    if (!m) return;
    if (!g_audio_up || !g_live || !g_link_open || g_dict.state == VDS_IDLE
        || !tok_eq(m->token, g_link_token) || !tok_eq(m->token, g_token)) {
        voice_live_msg_free(m);   /* another recording's, or one already closed */
        return;
    }
    const VoiceLiveEvent *ev = &m->ev;
    const int append_only = g_live_t.prov == VLP_MISTRAL;
    switch (ev->kind) {
    case VLE_READY:
        dict_event(VDE_LINK_READY, 0, 0);
        break;
    case VLE_DELTA:
        if (box_intact() && ev->text && vt_delta(&g_vt, ev->item, ev->text, append_only) == 0)
            live_flush();
        break;
    case VLE_COMPLETED:
        if (box_intact() && vt_complete(&g_vt, ev->item, ev->text) == 0) live_flush();
        break;
    case VLE_ITEM_FAILED:
        /* The item will never complete: keep the words that arrived for it. */
        if (box_intact() && vt_complete(&g_vt, ev->item, NULL) == 0) live_flush();
        break;
    case VLE_DONE:
        if (ev->text && ev->text[0]) {
            if (box_intact() && vt_replace_all(&g_vt, ev->text) == 0) live_flush();
        }
        if (g_dict.state != VDS_IDLE) dict_event(VDE_LINK_FINAL, 0, 0);
        close_link();   /* the session is over */
        break;
    case VLE_ERROR:
    case VLE_CLOSED: {
        const int never_opened = g_dict.link == VDL_CONNECTING;
        report_link_failure(ev);
        dict_event(VDE_LINK_FAILED, never_opened, 0);   /* an ERROR then CLOSED is one failure */
        close_link();
        break;
    }
    default:
        break;   /* created, committed: nothing to do */
    }
    voice_live_msg_free(m);
}
