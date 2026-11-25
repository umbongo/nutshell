#include "voice_conv_win.h"

#include "secure_zero.h"
#include "voice_aispeech.h"
#include "voice_config.h"
#include "voice_echo.h"
#include "voice_level.h"
#include "voice_listen.h"
#include "voice_ptt.h"
#include "voice_speak.h"
#include "voice_stt.h"
#include "voice_transcript.h"
#include "voice_win.h"
#include "dlog.h"
#ifdef NS_VOICE_WAV_SEAM
#include "voice_audio_win.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The glue's own state is a handful of statics (one conversation at a time). Each
 * resource the machine opens has a flag here that says whether the module call was
 * really made, so every release is a no-op when nothing is held.
 */

#define CUE_DRAFT "Your text and what you say next will be sent after a pause."
#define FALLBACK_FAILED \
    "The recording could not be transcribed again: the text in the box is what arrived live"
#define QMAX 24
#define BUSY_FALLBACK_MS 10000ul
#define CONV_TOKEN_BASE 0x800000u

typedef struct { VoiceConvEvent ev; int a, b; } QEv;

static const VoiceConvOps *g_real;
static const VoiceConvOps *g_ops;
static HWND g_main;
static VoiceConv g_c;
static unsigned long g_adj;                 /* the test clock offset */

static unsigned g_seq;
static unsigned g_cap_token, g_live_token, g_stt_token, g_tts_token;
static int g_cap_open, g_link_open, g_stt_open, g_tts_alive;
static int g_tts_engine;                    /* VOICE_SPEECH_* of the live speech context */
static int g_engine_cfg;                    /* the configured engine, read at conversation start */
static int g_ai_off;                        /* AI speech cannot work this conversation (R37) */
static int g_aec = -1;                      /* the last VAM_CONV_OPENED value */
static VoiceListenMode g_want_mode = VLM_HOLD;
static int g_have_mode;

static VoiceTranscript g_vt;
static int g_vt_on;
static VoiceCapture *g_turn_audio;          /* the finished turn's audio (fallback or batch) */
static int g_batch_want;                    /* SUBMIT_BATCH is waiting for the turn's audio */
static int g_audio_lost;                    /* the turn's audio can no longer arrive (abandoned, no memory) */
static int g_turn_open_listener;            /* the listener's turn is open (TURN_START seen, no end yet) */
static int g_err_note_done;                 /* a provider error or failure text was shown this turn */

static unsigned g_since;                    /* the message count at the last send's baseline */
static int g_busy_seen;
static unsigned long g_sent_tick;

static char *g_reply;                       /* the last reply's speech text */
static char *g_spoken;                      /* what was last spoken (reply or phrase) */
static unsigned g_reply_seq, g_cur_id;
static int g_cur_is_reply;
static size_t g_cut;
static int g_playing, g_read_to_end;
static unsigned long g_spoken_end_at, g_reply_at;

static int g_notice_pending;                /* the first reply waits for the notice */
static int g_notice_checked;                /* asked this conversation */
static int g_notice_raised;                 /* raised this launch */
static char g_notice_voice[160];

static int g_paused;                        /* VoiceConvPause as last fed */
static int g_active_hint = -1;              /* WM_ACTIVATEAPP's last wParam, -1 unknown */
static char g_last_header[200];             /* the header text last shown */

static QEv g_q[QMAX];
static int g_qn;
static int g_in_step;
static int g_pend_end;                      /* a VCE_END that found the queue full: its own slot */
static int g_pend_end_a, g_pend_end_b;
static int g_keep_words;                    /* the VCA_DROP_TURN of an external send keeps the box's words */
static int g_ducked;                        /* the speech context's volume is ducked */
static unsigned long g_progress_at;         /* the last VCE_SPEECH_PROGRESS fed (0: none for this speech) */

static unsigned long now_ms(void)
{
    return (unsigned long)GetTickCount() + g_adj;
}

static int tok_eq(unsigned a, unsigned b)
{
    return ((a ^ b) & VOICE_TOKEN_MASK) == 0;
}

static unsigned next_token(void)
{
    g_seq = (g_seq + 1u) & 0x7FFFFFu;
    if (g_seq == 0) g_seq = 1;
    return CONV_TOKEN_BASE | g_seq;
}

static void wipe_free(char **p)
{
    if (*p) {
        secure_zero(*p, strlen(*p) + 1);
        free(*p);
        *p = NULL;
    }
}

static char *dup_str(const char *s)
{
    const size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* ---- ops: the table in use, and the optional members ---- */

static int ops_ready(void)
{
    return g_ops != NULL;
}

static void note(const char *text)
{
    if (text && text[0] && g_ops->note) g_ops->note(text);
}

static int default_app_active(void)
{
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (!fg) return 0;
    (void)GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static int app_active(void)
{
    return g_ops->app_active ? g_ops->app_active() : default_app_active();
}

/* ---- the turn's held memory ---- */

static void wipe_turn_audio(void)
{
    if (g_turn_audio) {
        voice_audio_free_capture(g_turn_audio);   /* the PCM is wiped */
        g_turn_audio = NULL;
    }
}

static void free_transcript(void)
{
    if (g_vt_on) {
        vt_free(&g_vt);
        g_vt_on = 0;
    }
}

static void live_flush(void)
{
    if (!g_vt_on) return;
    char *txt = vt_render(&g_vt, NULL);   /* brings the transcript's own state up to date */
    if (!txt) return;
    secure_zero(txt, strlen(txt) + 1);
    free(txt);
    g_ops->live_replace(vt_rendered(&g_vt));
}

static void turn_begin(void)
{
    wipe_turn_audio();
    free_transcript();
    g_batch_want = 0;
    g_err_note_done = 0;
    g_audio_lost = 0;
    g_ops->live_begin();
    vt_init(&g_vt, 0, 0);   /* the real live_replace puts the separator in front itself */
    g_vt_on = 1;
}

static void drop_turn(void)
{
    if (g_vt_on && !g_keep_words) g_ops->drop_words();
    free_transcript();
    wipe_turn_audio();
    g_batch_want = 0;
}

/* ---- the event queue ---- */

static void enqueue(VoiceConvEvent ev, int a, int b)
{
    if (ev == VCE_END && g_pend_end) return;   /* an end is parked: it is the first, and it wins */
    if (g_qn < QMAX) {
        g_q[g_qn].ev = ev;
        g_q[g_qn].a = a;
        g_q[g_qn].b = b;
        g_qn++;
        return;
    }
    if (ev == VCE_END) {   /* an end is never dropped: the first one waits in its own slot */
        if (!g_pend_end) {
            g_pend_end = 1;
            g_pend_end_a = a;
            g_pend_end_b = b;
        }
        return;
    }
    DLOG(DLOG_AI, DLOG_WARN, 0, "voice.conv_queue_full", "event=%d dropped", (int)ev);
}

static void run_event(VoiceConvEvent ev, int a, int b);

/* Runs what is queued, one event at a time; an event raised inside a step waits its turn. */
static void pump(void)
{
    if (g_in_step) return;
    g_in_step = 1;
    while (g_qn > 0 || g_pend_end) {
        QEv e;
        if (g_qn > 0) {
            e = g_q[0];
            g_qn--;
            memmove(&g_q[0], &g_q[1], (size_t)g_qn * sizeof g_q[0]);
        } else {   /* the queue drained: the pending end runs now */
            e.ev = VCE_END;
            e.a = g_pend_end_a;
            e.b = g_pend_end_b;
            g_pend_end = 0;
        }
        run_event(e.ev, e.a, e.b);
    }
    g_in_step = 0;
}

void voice_conv_event(VoiceConvEvent ev, int a, int b)
{
    if (!ops_ready()) return;
    enqueue(ev, a, b);
    pump();
}

/* ---- machine actions ---- */

const char *voice_conv_device_text(VoiceAudioErr e)
{
    switch (e) {
    case VAE_NO_DEVICE:
        return "No microphone was found. Connect one, or choose another in Settings > Voice.";
    case VAE_DEVICE_LOST:
        return "The microphone stopped responding (was it unplugged?). Nothing was sent.";
    case VAE_ACCESS_DENIED:
        return "Windows may be blocking the microphone: Settings > Privacy & security > Microphone";
    default:
        return "The microphone could not be opened.";
    }
}

static void apply_mode(void)
{
    if (!g_cap_open || !g_have_mode) return;
    g_ops->cap_mode(g_cap_token, g_want_mode);
}

static VoiceListenMode speak_mode(void)
{
    return (g_aec == 1 || g_aec == 2) ? VLM_SPEAK_AEC : VLM_SPEAK_HD;
}

static void open_capture(void)
{
    g_cap_token = next_token();
    if (g_ops->cap_begin(g_cap_token)) {
        g_cap_open = 1;
        g_have_mode = 0;
        return;
    }
    g_cap_open = 0;
    note(voice_conv_device_text(VAE_OPEN_FAILED));
    enqueue(VCE_END, VCX_DEVICE_LOST, 0);
}

static void close_capture(void)
{
    if (!g_cap_open) return;
    g_cap_open = 0;
    g_have_mode = 0;
    g_turn_open_listener = 0;   /* a closed capture has no turn */
    g_ops->cap_end(g_cap_token);
}

static void close_link(void)
{
    if (!g_link_open) return;
    g_link_open = 0;
    g_ops->link_close(g_live_token);   /* the session's threads wake the UI when they exit */
}

static void open_link(void)
{
    g_live_token = next_token();
    if (g_ops->link_start(g_live_token)) {
        g_link_open = 1;
        if (g_cap_open) g_ops->cap_attach_live(g_cap_token, g_live_token);
        return;
    }
    enqueue(VCE_LINK_FAILED, 1, 0);   /* it never opened */
}

static void release_upload(void)
{
    if (!g_stt_open) return;
    g_stt_open = 0;
    g_ops->stt_cancel(g_stt_token);
}

/* The Ending deadline follows this turn's upload: twice its send and receive timeouts (the one
 * retry). The WAV is the PCM at 16 kHz 16-bit mono plus its 44-byte header. */
static void set_ending_budget(void)
{
    int send_ms = 0, recv_ms = 0;
    voice_stt_upload_timeouts(44u + g_turn_audio->pcm.n * sizeof(int16_t), &send_ms, &recv_ms);
    voice_conv_set_ending_budget(&g_c, 2ul * ((unsigned long)send_ms + (unsigned long)recv_ms));
}

static void do_submit(void)
{
    g_batch_want = 0;
    if (g_stt_open) release_upload();
    g_stt_token = next_token();
    set_ending_budget();
    if (g_ops->stt_submit(g_turn_audio, g_stt_token)) {
        g_stt_open = 1;
    } else {
        if (g_c.live) note(FALLBACK_FAILED);
        enqueue(VCE_BATCH_FAIL, 0, 0);   /* a batch turn: the op says why */
    }
    wipe_turn_audio();   /* the upload holds its own copy */
}

static void submit_batch(void)
{
    if (g_turn_audio) {
        do_submit();
        return;
    }
    if (g_audio_lost) {   /* nothing to transcribe: the same as a failed transcription */
        if (g_c.live) note(FALLBACK_FAILED);
        enqueue(VCE_BATCH_FAIL, 0, 0);
        return;
    }
    g_batch_want = 1;     /* the audio arrives as VAM_CONV_TURN */
}

/* The turn's audio will never come (the listener abandoned the turn, or the audio thread ran out of
 * memory for it): stop waiting for it, as a failed transcription does. A late VAM_CONV_TURN is dropped. */
static void audio_lost(void)
{
    if (g_turn_audio) return;   /* it already arrived */
    g_audio_lost = 1;
    if (g_batch_want) {
        g_batch_want = 0;
        if (g_c.live) note(FALLBACK_FAILED);
        enqueue(VCE_BATCH_FAIL, 0, 0);
        pump();
    }
}

static void cancel_batch(void)
{
    g_batch_want = 0;
    release_upload();
    wipe_turn_audio();
}

/* The turn's text for classification: what the transcript holds, no separator. */
static VoiceTurnClass classify_text(const char *text)
{
    VoiceReplyCtx ctx;
    ctx.spoken = g_spoken;
    ctx.playing = g_playing;
    ctx.spoken_end_at = g_spoken_end_at;
    ctx.reply = g_reply;
    ctx.reply_at = g_reply_at;
    ctx.read_to_end = g_read_to_end;
    return voice_turn_classify(text, &ctx, now_ms());
}

static void transcript_final(const char *text)
{
    wipe_turn_audio();   /* the transcript is final: the fallback audio is not needed */
    enqueue(VCE_TRANSCRIPT, (int)classify_text(text ? text : ""), 0);
}

/* 1 when the panel's chat is no longer the one the conversation started in. */
static int chat_moved(void)
{
    return g_ops->chat_same && !g_ops->chat_same();
}

static void send_now(int voice_turn)
{
    VoiceSendStatus st;
    if (chat_moved()) {   /* a posted chat-changed notice may not have run yet */
        enqueue(VCE_END, VCX_CHAT_CHANGED, 0);
        return;
    }
    g_since = g_ops->msg_count();
    st = g_ops->send_turn(voice_turn);
    if (st == VSS_SENT) {
        g_busy_seen = 0;
        g_sent_tick = now_ms();
        if (!g_ops->chat_idle()) g_busy_seen = 1;
    }
    enqueue(VCE_SENT, (int)st, 0);
}

/* ---- speech ---- */

/* Who speaks the next text: AI speech unless Windows speech is configured or AI speech cannot
 * work in this conversation. */
static int want_engine(void)
{
    return (g_engine_cfg == VOICE_SPEECH_AI && !g_ai_off) ? VOICE_SPEECH_AI : VOICE_SPEECH_WINDOWS;
}

static int start_context(int engine)
{
    g_tts_token = next_token();
    if (!g_ops->tts_start(g_tts_token, engine)) return 0;
    g_tts_alive = 1;
    g_tts_engine = engine;
    return 1;
}

static void header_raw(const char *t)
{
    if (!t || !t[0] || !g_ops->header) return;
    snprintf(g_last_header, sizeof g_last_header, "%s", t);
#ifdef NS_VOICE_WAV_SEAM
    voice_audio_seam_note_header(t);
#endif
    g_ops->header(t);
}

#define HDR_AI_FAILED "AI speech failed: using Windows speech"

/* Starts the wanted engine; AI speech that cannot start reads with Windows speech, with the header. */
static int start_best(void)
{
    const int want = want_engine();
    g_tts_alive = 0;
    if (start_context(want)) return 1;
    if (want != VOICE_SPEECH_AI) return 0;
    header_raw(HDR_AI_FAILED);
    return start_context(VOICE_SPEECH_WINDOWS);
}

/* The context a text is spoken with: the live one when it is the wanted engine's, else it is
 * quit and the wanted one started (a reply after a fallback returns to AI speech). */
static int ensure_engine(void)
{
    if (g_tts_alive && g_tts_engine == want_engine()) return 1;
    if (g_tts_alive) {
        g_ops->tts_quit(g_tts_token);
        g_tts_alive = 0;
    }
    return start_best();
}

static void mark_not_playing(int read_to_end)
{
    if (!g_playing) return;
    g_playing = 0;
    g_spoken_end_at = now_ms();
    if (g_cur_is_reply) {
        g_reply_at = g_spoken_end_at;
        g_read_to_end = read_to_end;
    }
}

static void stop_speech(void)
{
    if (g_playing && g_tts_alive) g_ops->tts_stop(g_tts_token);
    if (g_ducked && g_tts_alive && g_ops->tts_volume) g_ops->tts_volume(g_tts_token, 100);
    g_ducked = 0;   /* no VCA_UNDUCK comes with every stop (an external send): the duck ends here */
    mark_not_playing(0);
}

static void speak_with_context(const char *text, size_t from, int is_reply)
{
    g_notice_pending = 0;
    /* a cut-in's duck is over once the speech it ducked is gone: every reply starts at full volume */
    if (g_ops->tts_volume) g_ops->tts_volume(g_tts_token, 100);
    g_ducked = 0;
    g_progress_at = 0;
    g_cur_id = ++g_reply_seq;
    g_cur_is_reply = is_reply;
    if (is_reply) {
        g_cut = from;
        g_read_to_end = 0;
    }
    wipe_free(&g_spoken);
    g_spoken = dup_str(text);
    g_playing = 1;
    g_ops->tts_speak(g_tts_token, g_cur_id, text, from);
}

static void begin_speak(const char *text, size_t from, int is_reply)
{
    if (!text || !ensure_engine()) {
        enqueue(VCE_SPEECH_FAILED, 0, 0);
        return;
    }
    speak_with_context(text, from, is_reply);
}

/* The first reply of a launch, with Automatic landing on a voice that is not an English (UK)
 * woman's: not spoken now; WM_VOICE_NOTICE shows the notice and the conversation pauses
 * while it is up, then speaks the held reply from 0 on resume. */
static int notice_wanted(void)
{
    if (g_notice_raised || g_notice_checked || !g_ops->notice_due) return 0;
    if (g_engine_cfg == VOICE_SPEECH_AI) return 0;   /* the notice is for Windows speech only; a fallback never raises it */
    g_notice_checked = 1;
    g_notice_voice[0] = '\0';
    return g_ops->notice_due(g_notice_voice, sizeof g_notice_voice);
}

static void speak_reply(void)
{
    char *text = NULL;
    int err = 0;
    char *sp = NULL;
    if (chat_moved()) {   /* the reply belongs to another chat now: say nothing */
        enqueue(VCE_END, VCX_CHAT_CHANGED, 0);
        return;
    }
    if (g_ops->reply(g_since, &text, &err) && text) sp = voice_speak_text(text);
    if (text) {
        secure_zero(text, strlen(text) + 1);
        free(text);
    }
    wipe_free(&g_reply);
    g_cut = 0;
    /* A new reply has its own times: the last one's must not age a "carry on" out of the window or
     * into it. Stamped now rather than zero: a zero is "long ago" to the classifier's age test. */
    g_read_to_end = 0;   /* the last reply's "read to the end" is not this one's */
    g_reply_at = now_ms();
    g_spoken_end_at = g_reply_at - VOICE_ECHO_AFTER_MS - 1ul;   /* outside the echo window */
    if (sp && sp[0]) {
        g_reply = sp;
    } else {
        if (sp) {
            secure_zero(sp, strlen(sp) + 1);
            free(sp);
        }
        enqueue(VCE_SPEECH_FAILED, 0, 0);
        return;
    }
    if (notice_wanted()) {
        g_notice_pending = 1;
        if (g_main) PostMessage(g_main, WM_VOICE_NOTICE, 0, 0);
        return;
    }
    begin_speak(g_reply, 0, 1);
}

static void speak_continue(void)
{
    if (!g_reply) {
        enqueue(VCE_SPEECH_FAILED, 0, 0);
        return;
    }
    begin_speak(g_reply, g_cut < strlen(g_reply) ? g_cut : 0, 1);
}

static void speak_repeat(void)
{
    if (!g_reply) {
        enqueue(VCE_SPEECH_FAILED, 0, 0);
        return;
    }
    begin_speak(g_reply, 0, 1);
}

static void engine_stop(void)
{
    if (g_tts_alive) g_ops->tts_quit(g_tts_token);
    g_tts_alive = 0;
    g_playing = 0;
}

/* ---- header, notes, and the end of everything ---- */

static void show_header(VoiceConvHeader h)
{
    const char *t;
    if (h == VCH_NONE) return;
    t = voice_conv_header_text(h);
    if (h == VCH_LISTENING && g_c.state == VCS_LISTENING && g_ops->box_has_text
        && g_ops->box_has_text())
        t = CUE_DRAFT;
    header_raw(t);
}

/* The draft cue follows the box while Listening: a draft in the box shows the cue, an empty box
 * the plain Listening text. Any other header (a pause, "Didn't catch that") is left alone. */
void voice_conv_on_box_changed(void)
{
    const char *want;
    if (!ops_ready() || !voice_conv_running() || g_c.state != VCS_LISTENING || g_c.pause != VCP_NONE)
        return;
    if (strcmp(g_last_header, CUE_DRAFT) != 0 && strcmp(g_last_header, voice_conv_header_text(VCH_LISTENING)) != 0)
        return;
    want = (g_ops->box_has_text && g_ops->box_has_text()) ? CUE_DRAFT : voice_conv_header_text(VCH_LISTENING);
    if (strcmp(want, g_last_header) == 0) return;
    snprintf(g_last_header, sizeof g_last_header, "%s", want);
#ifdef NS_VOICE_WAV_SEAM
    voice_audio_seam_note_header(want);
#endif
    if (g_ops->header) g_ops->header(want);
}

static void arm_timer(void)
{
    long ms;
    if (!g_main) return;
    ms = voice_conv_next_deadline(&g_c, now_ms());
    if (ms < 0) {
        KillTimer(g_main, VOICE_CONV_TIMER_ID);
        return;
    }
    SetTimer(g_main, VOICE_CONV_TIMER_ID, (UINT)(ms < 1 ? 1 : ms), NULL);
}

static void reset_resources(void)
{
    free_transcript();
    wipe_turn_audio();
    wipe_free(&g_reply);
    wipe_free(&g_spoken);
    g_batch_want = g_turn_open_listener = g_audio_lost = 0;
    g_notice_pending = 0;
    g_cut = 0;
    g_playing = g_read_to_end = 0;
    g_paused = 0;
    g_busy_seen = 0;
}

static void apply(VoiceConvOut o)
{
    const unsigned a = o.actions;

    if (a & VCA_CANCEL_WORK) g_ops->cancel_work();
    if (a & VCA_STOP_SPEECH) stop_speech();
    if (a & VCA_DUCK) {
        if (g_tts_alive) {
            g_ops->tts_volume(g_tts_token, VOICE_TTS_DUCK_PERCENT);
            g_ducked = 1;
        }
    }
    if (a & VCA_UNDUCK) {
        if (g_tts_alive) g_ops->tts_volume(g_tts_token, 100);
        g_ducked = 0;
    }
    if (a & VCA_DROP_TURN) drop_turn();
    if (a & VCA_CANCEL_BATCH) cancel_batch();
    if (a & VCA_CLOSE_LINK) close_link();
    if (a & VCA_LOCK_REG) (void)g_ops->lock_reg();
    if (a & VCA_ENGINE_START) {
        (void)start_best();
    }
    if (a & VCA_CLOSE_CAPTURE) close_capture();
    if (a & VCA_OPEN_CAPTURE) open_capture();

    if (a & (VCA_MODE_LISTEN | VCA_MODE_SPEAK | VCA_MODE_HOLD)) {
        const VoiceListenMode m = (a & VCA_MODE_SPEAK) ? speak_mode()
                                : (a & VCA_MODE_LISTEN) ? VLM_LISTEN : VLM_HOLD;
        if (m == VLM_HOLD && g_turn_open_listener && g_cap_open) {
            /* Send pressed while hearing: the turn ends now, exactly as the pause would end it
             * (the live session is ended and the audio kept on the audio thread's side). The
             * TURN_END and the audio follow as messages. */
            g_ops->cap_end_turn(g_cap_token);
            g_turn_open_listener = 0;
        }
        g_want_mode = m;
        g_have_mode = 1;
        apply_mode();
    }
    if (a & VCA_LOCK_TYPING) {
        turn_begin();
        g_ops->lock_typing(1);
    }
    if (a & VCA_OPEN_LINK) open_link();
    /* VCA_END_TURN_AUDIO: nothing to do here; the audio thread ends the live session's audio
     * when the turn ends (by the pause, or by cap_end_turn above). */
    if (a & VCA_SUBMIT_BATCH) submit_batch();
    if (a & VCA_UNLOCK_TYPING) g_ops->lock_typing(0);
    if (a & VCA_SEND_TURN) send_now(1);
    if (a & VCA_SEND_TYPED) send_now(0);

    if (a & VCA_SPEAK_REPLY) speak_reply();
    if (a & VCA_SPEAK_REPEAT) speak_repeat();
    if (a & VCA_SPEAK_CONTINUE) speak_continue();
    if (a & VCA_SPEAK_PHRASE) {
        const char *p = voice_conv_phrase_text(o.phrase);
        if (p[0]) begin_speak(p, 0, 0);
        else enqueue(VCE_SPEECH_FAILED, 0, 0);
    }
    if (a & VCA_STOP_CAPTION) g_ops->send_caption(1);
    if (a & VCA_SEND_CAPTION) g_ops->send_caption(0);
    if (a & VCA_LOCK_UNREG) g_ops->lock_unreg();
    if (a & VCA_ENGINE_STOP) engine_stop();

    note(voice_conv_note_text(o.note));
    show_header(o.header);
}

static void run_event(VoiceConvEvent ev, int a, int b)
{
    const VoiceConvState before = g_c.state;
    const VoiceConvOut o = voice_conv_step(&g_c, ev, a, b, now_ms());
    if (before == VCS_IDLE && ev == VCE_START) g_ops->mic_ui(1);
    g_keep_words = (ev == VCE_EXTERNAL_SEND);   /* the words in the box were just sent */
    apply(o);
    g_keep_words = 0;
    if (g_c.state == VCS_IDLE) {
        if (before != VCS_IDLE) {
            g_ops->mic_ui(0);
            reset_resources();
            g_cap_open = g_link_open = g_stt_open = 0;
        }
    } else if (!g_c.turn && g_c.state != VCS_ENDING && g_c.state != VCS_HEARING) {
        /* No turn is open: whatever the words and the audio were for is over (the words stay
         * in the box as a draft when the turn was not sent). */
        free_transcript();
        wipe_turn_audio();
        g_batch_want = 0;
    }
    arm_timer();
}

/* ---- entry points ---- */

void voice_conv_attach(HWND main, const VoiceConvOps *real)
{
    if (main) g_main = main;
    if (real) g_real = real;
    if (!g_ops) g_ops = g_real;
}

void voice_conv_set_ops(const VoiceConvOps *ops)
{
    if (g_main) KillTimer(g_main, VOICE_CONV_TIMER_ID);
    reset_resources();
    voice_conv_init(&g_c);
    g_ops = ops ? ops : g_real;
    g_qn = 0;
    g_in_step = 0;
    g_pend_end = g_keep_words = g_ducked = 0;
    g_progress_at = 0;
    g_engine_cfg = VOICE_SPEECH_WINDOWS;
    g_ai_off = 0;
    g_tts_engine = VOICE_SPEECH_WINDOWS;
    g_cap_open = g_link_open = g_stt_open = g_tts_alive = 0;
    g_cap_token = g_live_token = g_stt_token = g_tts_token = 0;
    g_aec = -1;
    g_have_mode = 0;
    g_active_hint = -1;
    g_last_header[0] = '\0';
    g_notice_checked = g_notice_raised = 0;
    g_adj = 0;
    g_since = 0;
}

int voice_conv_running(void)
{
    return voice_conv_active(&g_c);
}

VoiceConvState voice_conv_state(void)
{
    return g_c.state;
}

int voice_conv_is_typing_locked(void)
{
    return voice_conv_typing_locked(&g_c);
}

int voice_conv_last_aec(void)
{
    return g_aec;
}

unsigned voice_conv_capture_token(void)
{
    return g_cap_open ? g_cap_token : 0;
}

void voice_conv_test_advance(unsigned long ms)
{
    g_adj += ms;
}

size_t voice_conv_test_turn_audio_bytes(void)
{
    return g_turn_audio ? g_turn_audio->pcm.n * sizeof(int16_t) : 0;
}

/* The box was sent from outside the conversation. The reply baseline is taken here, as send_now
 * does for the conversation's own send, then the machine moves to Working. The conversation's own
 * send in flight (Ending, sending) is left alone. */
void voice_conv_external_send(void)
{
    if (!ops_ready() || !voice_conv_running()) return;
    if (g_c.state == VCS_ENDING && g_c.sending) return;
    g_since = g_ops->msg_count();
    g_sent_tick = now_ms();
    g_busy_seen = g_ops->chat_idle() ? 0 : 1;
    voice_conv_event(VCE_EXTERNAL_SEND, 0, 0);
}

/* TEST-ONLY seams (declared in the header): fill the queue without running it, then run it. */
void voice_conv_test_queue(VoiceConvEvent ev, int a, int b)
{
    enqueue(ev, a, b);
}

void voice_conv_test_pump(void)
{
    pump();
}

void voice_conv_start(int live)
{
    if (!ops_ready() || voice_conv_running()) return;
    reset_resources();
    g_last_header[0] = '\0';
    g_notice_checked = 0;   /* g_aec keeps the last report for the Settings page */
    g_ai_off = 0;           /* R37: a new conversation tries AI speech again */
    g_engine_cfg = g_ops->speech_engine ? g_ops->speech_engine() : VOICE_SPEECH_WINDOWS;
    g_since = g_ops->msg_count();
    voice_conv_event(VCE_START, live ? 1 : 0, 0);
}

void voice_conv_on_audio(UINT kind, LPARAM lp)
{
    if (kind == VAM_CONV_TURN) {
        VoiceCapture *c = (VoiceCapture *)lp;
        if (!c) return;
        if (!ops_ready() || !voice_conv_running() || !tok_eq(c->token, g_cap_token)
            || !g_cap_open || g_c.state != VCS_ENDING || g_audio_lost) {
            voice_audio_free_capture(c);   /* a stale or abandoned turn */
            return;
        }
        wipe_turn_audio();
        g_turn_audio = c;
        if (g_batch_want) {
            do_submit();
            pump();
        }
        return;
    }
    if (!ops_ready() || !voice_conv_running()) return;
    if (kind == VAM_LEVEL) {
        if (g_ops->level && g_cap_open && g_paused == 0)
            g_ops->level(voice_meter_frac(voice_dbfs((int)(intptr_t)lp)));
        return;
    }
    if (kind == VAM_ERROR) {
        const unsigned tok = VOICE_ERR_TOKEN(lp);
        const VoiceAudioErr e = VOICE_ERR_CODE(lp);
        if (!g_cap_open || !tok_eq(tok, g_cap_token)) return;   /* another capture's */
        if (e == VAE_NO_MEMORY) {   /* one turn was lost; the capture keeps running */
            if (g_c.state == VCS_ENDING) audio_lost();
            return;
        }
        /* The capture is gone (ended by another command, or the device failed). */
        close_capture();
        if (e != VAE_CONV_ENDED) note(voice_conv_device_text(e));
        voice_conv_event(VCE_END, VCX_DEVICE_LOST, 0);
        return;
    }
    {
        const unsigned tok = (unsigned)((uintptr_t)lp & VOICE_TOKEN_MASK);
        const unsigned val = (unsigned)(((uintptr_t)lp >> 24) & 0xFFu);
        if (!g_cap_open || !tok_eq(tok, g_cap_token)) return;
        if (kind == VAM_CONV_OPENED) {
            g_aec = (int)val;
            if (g_want_mode == VLM_SPEAK_AEC || g_want_mode == VLM_SPEAK_HD)
                g_want_mode = speak_mode();   /* this device's report, not the previous capture's */
            apply_mode();   /* the speak mode depends on what the device reported */
            return;
        }
        if (kind == VAM_CONV_EVENT) {
            switch ((VoiceListenEvent)val) {
            case VLN_TURN_START:
                g_turn_open_listener = 1;
                voice_conv_event(VCE_TURN_START, 0, 0);
                break;
            case VLN_TURN_END:
            case VLN_TURN_CAP:
                g_turn_open_listener = 0;
                voice_conv_event(VCE_TURN_END, 0, 0);
                break;
            case VLN_TURN_ABANDON:
                g_turn_open_listener = 0;
                if (g_c.state == VCS_ENDING) audio_lost();   /* the end-turn command lost the race */
                voice_conv_event(VCE_TURN_ABANDON, 0, 0);
                break;
            case VLN_DUCK:
                voice_conv_event(VCE_DUCK, 0, 0);
                break;
            case VLN_UNDUCK:
                voice_conv_event(VCE_UNDUCK, 0, 0);
                break;
            case VLN_CUT:
                g_want_mode = VLM_LISTEN;   /* the listener moved itself; no command is sent */
                voice_conv_event(VCE_CUT, 0, 0);
                break;
            default:
                break;
            }
        }
    }
}

void voice_conv_on_live(const VoiceLiveMsg *m)
{
    const VoiceLiveEvent *ev;
    if (!m || !ops_ready() || !voice_conv_running() || !g_link_open || !g_vt_on
        || !tok_eq(m->token, g_live_token))
        return;
    ev = &m->ev;
    switch (ev->kind) {
    case VLE_READY:
        voice_conv_event(VCE_LINK_READY, 0, 0);
        break;
    case VLE_DELTA:
        if (ev->text && vt_delta(&g_vt, ev->item, ev->text, ev->item[0] == '\0') == 0) live_flush();
        break;
    case VLE_COMPLETED:
        if (vt_complete(&g_vt, ev->item, ev->text) == 0) live_flush();
        break;
    case VLE_COMMITTED:
        if (ev->item[0]) (void)vt_register(&g_vt, ev->item);
        break;
    case VLE_ITEM_FAILED:
        if (vt_complete(&g_vt, ev->item, NULL) == 0) live_flush();
        break;
    case VLE_DONE:
        if (g_c.state != VCS_ENDING) break;   /* not the end of this turn: the 3 s wait covers it */
        if (ev->text && ev->text[0] && vt_replace_all(&g_vt, ev->text) == 0) live_flush();
        {
            char *text = vt_text(&g_vt);
            enqueue(VCE_LINK_FINAL, 0, 0);
            transcript_final(text);
            if (text) {
                secure_zero(text, strlen(text) + 1);
                free(text);
            }
        }
        close_link();   /* the session is over */
        pump();
        break;
    case VLE_ERROR:
        if (!g_err_note_done) {
            char text[400];
            g_err_note_done = 1;
            snprintf(text, sizeof text, "Live transcription error: %s",
                     ev->text && ev->text[0] ? ev->text : "unknown error");
            note(text);
        }
        break;
    case VLE_CLOSED:
        if (!g_err_note_done && m->http_status != 0) {
            char text[400] = "";
            g_err_note_done = 1;
            voice_stt_http_error(m->http_status, ev->text ? ev->text : "", text, sizeof text);
            note(text);
        }
        close_link();
        voice_conv_event(VCE_LINK_FAILED, 0, 0);
        break;
    default:
        break;
    }
}

void voice_conv_on_stt(VoiceSttResult *r)
{
    if (!r) return;
    if (!ops_ready() || !voice_conv_running() || !g_stt_open || r->token != g_stt_token) {
        voice_stt_result_free(r);   /* stale (cancelled), or the conversation is over */
        return;
    }
    release_upload();   /* the result is in: the upload is over */
    if (g_c.state != VCS_ENDING || !g_vt_on) {
        voice_stt_result_free(r);
        return;
    }
    if (r->ok && r->text) {
        /* A batch turn's transcript (or the fallback's) replaces any live text of this turn. */
        if (vt_replace_all(&g_vt, r->text) == 0) live_flush();
        enqueue(VCE_BATCH_OK, 0, 0);
        transcript_final(r->text);
    } else {
        if (g_c.live) note(FALLBACK_FAILED);
        else note(r->err[0] ? r->err : "Transcription failed");
        enqueue(VCE_BATCH_FAIL, 0, 0);   /* the live words stay: its own header says so */
    }
    voice_stt_result_free(r);
    pump();
}

/* An AI speech failure for the current reply (R10, R15, R37): quits the AI context and reads the
 * rest of the reply, from the failure's offset, with Windows speech. A setting that cannot work
 * writes the note once and keeps this conversation on Windows speech; a passing failure shows the
 * header and the next reply tries AI speech again. */
static void ai_fallback(const VoiceTtsMsg *m)
{
    char reason[200], resolve_err[160], text[320];
    char *rest = g_spoken ? dup_str(g_spoken) : NULL;
    const int was_ducked = g_ducked;
    const int is_reply = g_cur_is_reply;
    size_t from = m->offset;
    if (voice_ais_failure_fatal(m->hr)) {
        resolve_err[0] = '\0';
        if (m->hr == VOICE_AIS_HR_RESOLVE && g_ops->ai_resolve_error
            && !g_ops->ai_resolve_error(resolve_err, sizeof resolve_err))
            resolve_err[0] = '\0';
        voice_ais_failure_reason(m->hr, resolve_err, reason, sizeof reason);
        snprintf(text, sizeof text, "AI speech is off for this conversation: %s. "
                 "Replies are read with Windows speech.", reason);
        g_ai_off = 1;
        note(text);
    } else {
        header_raw(HDR_AI_FAILED);
    }
    g_ops->tts_quit(g_tts_token);
    g_tts_alive = 0;
    if (!rest || !start_context(VOICE_SPEECH_WINDOWS)) {
        if (rest) {
            secure_zero(rest, strlen(rest) + 1);
            free(rest);
        }
        mark_not_playing(0);
        voice_conv_event(VCE_SPEECH_FAILED, 0, 0);
        return;
    }
    if (from > strlen(rest)) from = strlen(rest);
    speak_with_context(rest, from, is_reply);
    if (was_ducked && g_ops->tts_volume) {   /* a cut-in's duck outlives the engine change */
        g_ops->tts_volume(g_tts_token, VOICE_TTS_DUCK_PERCENT);
        g_ducked = 1;
    }
    secure_zero(rest, strlen(rest) + 1);
    free(rest);
}

void voice_conv_on_tts(const VoiceTtsMsg *m)
{
    if (!m) return;
    if (m->kind == VTT_EXITED) {
        voice_tts_reap(m->token, m->gen);   /* a no-op for an unknown or stale one */
        if (ops_ready() && tok_eq(m->token, g_tts_token)) g_tts_alive = 0;
        return;
    }
    if (!ops_ready() || !voice_conv_running() || !tok_eq(m->token, g_tts_token)) return;
    switch (m->kind) {
    case VTT_READY:
        break;
    case VTT_WORD:
        if (m->reply_id == g_cur_id && g_playing) {
            const unsigned long t = now_ms();
            if (g_cur_is_reply) g_cut = m->offset;
            /* The speech is advancing: the Speaking watchdog restarts (at most once a second). */
            if (g_progress_at == 0 || t - g_progress_at >= 1000ul) {
                g_progress_at = t ? t : 1ul;
                voice_conv_event(VCE_SPEECH_PROGRESS, 0, 0);
            }
        }
        break;
    case VTT_DONE:
        if (m->reply_id != g_cur_id || !g_playing) break;
        mark_not_playing(g_cur_is_reply);
        voice_conv_event(VCE_SPEECH_DONE, 0, 0);
        break;
    case VTT_ERROR:
        /* An error for an older reply is nothing. The thread stays in its loop after an error
         * (no VTT_EXITED follows a failed Speak), so it is quit here, not just forgotten. */
        if (m->reply_id != 0 && m->reply_id != g_cur_id) break;
        if (g_tts_alive && g_tts_engine == VOICE_SPEECH_AI && g_playing) {
            ai_fallback(m);
            break;
        }
        if (g_tts_alive) g_ops->tts_quit(g_tts_token);
        g_tts_alive = 0;
        mark_not_playing(0);
        voice_conv_event(VCE_SPEECH_FAILED, 0, 0);
        break;
    default:
        break;
    }
}

static void poll_working(void)
{
    int idle, prose = 0, err = 0;
    char *text = NULL;
    if (g_c.state != VCS_WORKING) return;
    idle = g_ops->chat_idle();
    if (!idle) g_busy_seen = 1;
    if (!g_busy_seen && now_ms() - g_sent_tick < BUSY_FALLBACK_MS) return;
    if (idle) {
        prose = g_ops->reply(g_since, &text, &err) ? 1 : 0;
        if (text) {
            secure_zero(text, strlen(text) + 1);
            free(text);
        }
    }
    voice_conv_event(VCE_IDLE_POLL, idle, (prose ? 1 : 0) | (err ? 2 : 0));
}

void voice_conv_on_timer(void)
{
    if (g_main) KillTimer(g_main, VOICE_CONV_TIMER_ID);
    if (!ops_ready() || !voice_conv_running()) return;
    voice_conv_event(VCE_TICK, 0, 0);
    poll_working();
    if (voice_conv_running()) arm_timer();
}

void voice_conv_on_turn_idle(void)
{
    int prose, err = 0;
    char *text = NULL;
    /* The chat posted this earlier; re-check it now. An idle from before the send, or one
     * posted twice, or one outside Working, is dropped. */
    if (!ops_ready() || !voice_conv_running() || g_c.state != VCS_WORKING) return;
    if (!g_ops->chat_idle() || !g_busy_seen) return;
    prose = g_ops->reply(g_since, &text, &err) ? 1 : 0;
    if (text) {
        secure_zero(text, strlen(text) + 1);
        free(text);
    }
    voice_conv_event(VCE_CHAT_IDLE, prose, err ? 1 : 0);
}

void voice_conv_on_card(int kind)
{
    if (!ops_ready() || !voice_conv_running() || g_c.state != VCS_WORKING) return;
    g_busy_seen = 1;   /* a card means the chat was busy for this turn */
    voice_conv_event(VCE_CARD, kind, 0);
}

void voice_conv_on_notice(void)
{
    if (!ops_ready() || !voice_conv_running() || !g_notice_pending) return;
    g_notice_raised = 1;   /* shown this launch from here: a conversation that ended first owes it */
    (void)g_ops->notice_voice(g_notice_voice[0] ? g_notice_voice : "the Windows default voice");
    /* The dialog pauses the conversation (the main window is disabled) and resume speaks the
     * held reply. If nothing paused (the dialog did not open), speak it now. */
    if (g_notice_pending && voice_conv_running() && g_c.state == VCS_SPEAKING && g_reply
        && !g_playing)
        begin_speak(g_reply, 0, 1);
}

void voice_conv_note_activateapp(int active)
{
    g_active_hint = active ? 1 : 0;   /* the probe is unreliable during the transition itself */
}

void voice_conv_on_activation(void)
{
    int want;
    if (!ops_ready() || !voice_conv_running()) return;
    if (g_main && !IsWindowEnabled(g_main)) want = VCP_DIALOG;
    else if (g_ops->dialog_open && g_ops->dialog_open()) want = VCP_DIALOG;
    else if (!(g_active_hint >= 0 ? g_active_hint : app_active())) want = VCP_BACKGROUND;
    else want = VCP_NONE;
    if (want == g_paused) return;
    g_paused = want;
    if (want == VCP_NONE) voice_conv_event(VCE_RESUME, 0, 0);
    else voice_conv_event(VCE_PAUSE, want, 0);
}
