/* tests/test_voice_conv_win.c -- the conversation controller's glue against a fake ops
 * table that records every call and does the ledger acquire/release each real op would.
 * Built only by `make wintest`. Events go through the glue's real entry points. */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_framework.h"
#include "voice_conv_win.h"
#include "voice_audio_win.h"
#include "voice_speak.h"
#include "voice_win.h"
#include "voice_ledger.h"

#define VC_REPLY_A "Here is the first answer, and it is long enough to have a cut-off inside it. "
#define VC_REPLY_B "This is the second answer to a second question, long enough to cut it in the middle. "

typedef struct {
    int cap_begin, cap_end, cap_mode, attach_live, cap_end_turn;
    unsigned stt_token; int stt_ok;
    unsigned cap_token; VoiceListenMode mode;
    int link_start, link_close, link_ok; unsigned live_token;
    int stt_submit, stt_cancel;
    int tts_start, tts_speak, tts_stop, tts_quit; unsigned tts_token, speak_id; size_t speak_from;
    char speak_text[600]; int volume;
    int lock_reg, lock_unreg, sess_lock;
    int send_turn, last_voice_turn; VoiceSendStatus send_status;
    int idle, reply_ok, reply_err; char reply_text[600];
    int cancel_work, notes; char note[300]; char header[300]; int headers;
    int live_begin, drop_words, typing, caption_stop, mic;
    char live_text[300]; int live_replace;
    int box_text;
    int notice_voice, notice_due_calls; char notice_name[160]; int notice_due_first;
    int app_active;
    int dialog_open;    /* a dialog owned by the undocked panel is up */
    int chat_differs;   /* the panel's binding is no longer the conversation's */
    HWND win;
} Fake;

static Fake F;

static int f_cap_begin(unsigned t) { F.cap_begin++; F.cap_token = t; voice_ledger_acquire(VRES_CAPTURE); return 1; }
static void f_cap_end(unsigned t) { (void)t; F.cap_end++; voice_ledger_release(VRES_CAPTURE); }
static void f_cap_mode(unsigned t, VoiceListenMode m) { (void)t; F.cap_mode++; F.mode = m; }
static void f_attach(unsigned t, unsigned l) { (void)t; (void)l; F.attach_live++; }
static void f_end_turn(unsigned t) { (void)t; F.cap_end_turn++; }
static int f_link_start(unsigned l)
{
    F.link_start++;
    F.live_token = l;
    if (!F.link_ok) return 0;
    voice_ledger_acquire(VRES_LIVE_SESSION);
    return 1;
}
static void f_link_close(unsigned l) { (void)l; F.link_close++; voice_ledger_release(VRES_LIVE_SESSION); }
static int f_stt_submit(VoiceCapture *c, unsigned t)
{
    (void)c;
    F.stt_submit++;
    F.stt_token = t;
    if (!F.stt_ok) return 0;
    voice_ledger_acquire(VRES_STT_UPLOAD);
    return 1;
}
static void f_stt_cancel(unsigned t) { (void)t; F.stt_cancel++; voice_ledger_release(VRES_STT_UPLOAD); }
static int f_tts_start(unsigned t)
{
    F.tts_start++;
    F.tts_token = t;
    voice_ledger_acquire(VRES_SPVOICE);
    return 1;
}
static void f_tts_speak(unsigned t, unsigned id, const char *text, size_t from)
{
    (void)t;
    F.tts_speak++;
    F.speak_id = id;
    F.speak_from = from;
    snprintf(F.speak_text, sizeof F.speak_text, "%s", text);
}
static void f_tts_stop(unsigned t) { (void)t; F.tts_stop++; }
static void f_tts_volume(unsigned t, int p) { (void)t; F.volume = p; }
static void f_tts_quit(unsigned t) { (void)t; F.tts_quit++; voice_ledger_release(VRES_SPVOICE); }
static int f_lock_reg(void)
{
    F.lock_reg++;
    if (!F.sess_lock) { F.sess_lock = 1; voice_ledger_acquire(VRES_SESSION_LOCK); }
    return 1;
}
static void f_lock_unreg(void)
{
    F.lock_unreg++;
    if (F.sess_lock) { F.sess_lock = 0; voice_ledger_release(VRES_SESSION_LOCK); }
}
static VoiceSendStatus f_send_turn(int v) { F.send_turn++; F.last_voice_turn = v; return F.send_status; }
static unsigned f_msg_count(void) { return 0; }
static int f_reply(unsigned since, char **text, int *err)
{
    (void)since;
    *text = NULL;
    *err = F.reply_err;
    if (!F.reply_ok) return 0;
    *text = (char *)malloc(strlen(F.reply_text) + 1);
    strcpy(*text, F.reply_text);
    return 1;
}
static int f_idle(void) { return F.idle; }
static int f_dialog_open(void) { return F.dialog_open; }
static int f_chat_same(void) { return !F.chat_differs; }
static void f_cancel_work(void) { F.cancel_work++; }
static void f_note(const char *t) { F.notes++; snprintf(F.note, sizeof F.note, "%s", t); }
static void f_header(const char *t) { F.headers++; snprintf(F.header, sizeof F.header, "%s", t); }
static void f_live_begin(void) { F.live_begin++; }
static void f_live_replace(const char *t) { F.live_replace++; snprintf(F.live_text, sizeof F.live_text, "%s", t); }
static void f_drop_words(void) { F.drop_words++; }
static void f_lock_typing(int on) { F.typing = on; }
static void f_caption(int stop) { F.caption_stop = stop; }
static void f_mic(int on) { F.mic = on; }
static int f_box_text(void) { return F.box_text; }
static int f_notice_due(char *name, size_t cap)
{
    F.notice_due_calls++;
    if (!F.notice_due_first) return 0;
    F.notice_due_first = 0;
    snprintf(name, cap, "%s", "Microsoft Zira");
    return 1;
}
/* The real dialog disables the main window while it is up and enables it on close. */
static int f_notice_voice(const char *name)
{
    F.notice_voice++;
    snprintf(F.notice_name, sizeof F.notice_name, "%s", name);
    EnableWindow(F.win, FALSE);
    EnableWindow(F.win, TRUE);
    return 0;
}
static int f_app_active(void) { return F.app_active; }

static VoiceConvOps fake_ops(void)
{
    VoiceConvOps o;
    memset(&o, 0, sizeof o);
    o.cap_begin = f_cap_begin; o.cap_end = f_cap_end; o.cap_mode = f_cap_mode;
    o.cap_attach_live = f_attach; o.cap_end_turn = f_end_turn;
    o.link_start = f_link_start; o.link_close = f_link_close;
    o.stt_submit = f_stt_submit; o.stt_cancel = f_stt_cancel;
    o.tts_start = f_tts_start; o.tts_speak = f_tts_speak; o.tts_stop = f_tts_stop;
    o.tts_volume = f_tts_volume; o.tts_quit = f_tts_quit;
    o.lock_reg = f_lock_reg; o.lock_unreg = f_lock_unreg;
    o.send_turn = f_send_turn; o.msg_count = f_msg_count; o.reply = f_reply;
    o.chat_idle = f_idle; o.cancel_work = f_cancel_work;
    o.note = f_note; o.header = f_header;
    o.live_begin = f_live_begin; o.live_replace = f_live_replace; o.drop_words = f_drop_words;
    o.lock_typing = f_lock_typing; o.send_caption = f_caption; o.mic_ui = f_mic;
    o.box_has_text = f_box_text; o.notice_voice = f_notice_voice;
    o.notice_due = f_notice_due; o.app_active = f_app_active; o.dialog_open = f_dialog_open; o.chat_same = f_chat_same;
    return o;
}

static VoiceConvOps g_ops_store;

static LRESULT CALLBACK vc_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_ACTIVATEAPP:
        voice_conv_note_activateapp(w != 0);
        voice_conv_on_activation();
        break;
    case WM_ENABLE:
        voice_conv_on_activation();
        break;
    case WM_VOICE_NOTICE:
        voice_conv_on_notice();
        return 0;
    case WM_TIMER:
        if (w == VOICE_CONV_TIMER_ID) { voice_conv_on_timer(); return 0; }
        break;
    default:
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

static void pump(void)
{
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
        if (m.message == WM_TIMER && m.wParam == VOICE_CONV_TIMER_ID) continue;   /* real time is not under test */
        DispatchMessageW(&m);
    }
}

static HWND vc_begin(void)
{
    static int registered;
    if (!registered) {
        WNDCLASSW wc;
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = vc_proc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"VConvTestWin";
        RegisterClassW(&wc);
        registered = 1;
    }
    memset(&F, 0, sizeof F);
    F.link_ok = 1;
    F.idle = 1;
    F.send_status = VSS_SENT;
    F.app_active = 1;
    F.stt_ok = 1;
    F.notice_due_first = 0;
    voice_ledger_reset();
    F.win = CreateWindowExW(0, L"VConvTestWin", L"vconv", WS_POPUP, 0, 0, 10, 10, NULL, NULL,
                            GetModuleHandleW(NULL), NULL);
    g_ops_store = fake_ops();
    voice_conv_set_ops(&g_ops_store);
    voice_conv_attach(F.win, NULL);
    return F.win;
}

static void vc_end(HWND w)
{
    voice_conv_set_ops(NULL);
    KillTimer(w, VOICE_CONV_TIMER_ID);
    DestroyWindow(w);
}

/* A turn the machine has been told about: the listener's events through the audio entry. */
static void aud_event(VoiceListenEvent e)
{
    voice_conv_on_audio(VAM_CONV_EVENT, VOICE_CONV_PACK(e, voice_conv_capture_token()));
}

static void live_msg(VoiceLiveEvKind kind, const char *item, const char *text)
{
    VoiceLiveMsg m;
    memset(&m, 0, sizeof m);
    m.token = F.live_token;
    m.ev.kind = kind;
    snprintf(m.ev.item, sizeof m.ev.item, "%s", item ? item : "");
    m.ev.text = (char *)text;
    voice_conv_on_live(&m);
}

static void to_working(void)
{
    F.idle = 0;                       /* the chat is busy as the send goes out */
    voice_conv_event(VCE_SEND, 0, 0); /* a typed send from Listening */
}

/* The chat finishes with `reply`: the glue posts nothing here, the test plays WM_VOICE_TURN_IDLE. */
static void chat_finishes(const char *reply)
{
    F.idle = 1;
    F.reply_ok = reply != NULL;
    F.reply_err = 0;
    snprintf(F.reply_text, sizeof F.reply_text, "%s", reply ? reply : "");
    voice_conv_on_turn_idle();
}

static void to_speaking(void)
{
    to_working();
    chat_finishes(VC_REPLY_A);
}

typedef enum { END_KEY, END_ESCAPE, END_STOP, END_PANEL, END_OFF, END_SETTINGS, END_LOCK,
               END_SILENCE, END_CHAT, END_DEVICE, END_COUNT } EndKind;

static const char *const k_end_note[END_COUNT] = {
    "", "", "", "", "",
    "Conversation ended: the voice settings changed",
    "Conversation ended: Windows was locked",
    "Conversation ended: nothing said for 3 minutes",
    "Conversation ended: the chat changed",
    "The microphone stopped responding (was it unplugged?). Nothing was sent."
};

static void do_end(EndKind k)
{
    switch (k) {
    case END_KEY:      voice_conv_event(VCE_PRESS, 0, 0); break;
    case END_ESCAPE:   voice_conv_event(VCE_ESCAPE, 0, 0); break;
    case END_STOP:     voice_conv_event(VCE_STOP, 0, 0); break;
    case END_PANEL:    voice_conv_event(VCE_END, VCX_PANEL_CLOSED, 0); break;
    case END_OFF:      voice_conv_event(VCE_END, VCX_OFF, 0); break;
    case END_SETTINGS: voice_conv_event(VCE_END, VCX_SETTINGS, 0); break;
    case END_LOCK:     voice_conv_event(VCE_END, VCX_LOCK, 0); break;
    case END_CHAT:     voice_conv_event(VCE_END, VCX_CHAT_CHANGED, 0); break;
    case END_SILENCE:
        voice_conv_test_advance(181000ul);
        voice_conv_on_timer();
        break;
    case END_DEVICE:
        voice_conv_on_audio(VAM_ERROR, VOICE_ERR_PACK(VAE_DEVICE_LOST, voice_conv_capture_token()));
        break;
    default: break;
    }
}

int test_vconv_win_ends_ledger_zero(void)
{
    TEST_BEGIN();
    for (int from = 0; from < 3; from++) {          /* 0 Listening, 1 Working, 2 Speaking */
        for (int k = 0; k < END_COUNT; k++) {
            /* a silence end is judged only while Listening; a lost device only while a capture is open */
            if (k == END_SILENCE && from != 0) continue;
            if (k == END_DEVICE && from == 1) continue;
            HWND w = vc_begin();
            voice_conv_start(0);
            ASSERT_TRUE(voice_conv_running());
            ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 1);
            ASSERT_EQ(voice_ledger_count(VRES_SPVOICE), 1);
            ASSERT_EQ(voice_ledger_count(VRES_SESSION_LOCK), 1);
            if (from == 1) to_working();
            if (from == 2) to_speaking();
            if (from == 1) ASSERT_EQ(voice_conv_state(), VCS_WORKING);
            if (from == 2) ASSERT_EQ(voice_conv_state(), VCS_SPEAKING);
            F.notes = 0;
            F.note[0] = '\0';
            do_end((EndKind)k);
            /* From Speaking the key, Escape and Stop only stop the speech first */
            if (from == 2 && (k == END_KEY || k == END_ESCAPE || k == END_STOP)) {
                ASSERT_TRUE(voice_conv_running());
                ASSERT_EQ(voice_conv_state(), VCS_LISTENING);
                ASSERT_TRUE(F.tts_stop >= 1);
                do_end((EndKind)k);
            }
            if (voice_conv_running() || !voice_ledger_all_zero() || voice_ledger_underflow()
                || strcmp(F.note, k_end_note[k]) != 0)
                printf("  from=%d end=%d running=%d zero=%d note='%s'\n", from, k,
                       voice_conv_running(), voice_ledger_all_zero(), F.note);
            ASSERT_TRUE(!voice_conv_running());
            ASSERT_TRUE(voice_ledger_all_zero());
            ASSERT_EQ(voice_ledger_underflow(), 0);
            ASSERT_STR_EQ(F.note, k_end_note[k]);
            ASSERT_EQ(F.mic, 0);
            ASSERT_EQ(F.typing, 0);
            ASSERT_EQ(F.cap_begin, F.cap_end);
            ASSERT_EQ(F.tts_quit, 1);
            if (from == 1 && k == END_ESCAPE) ASSERT_EQ(F.cancel_work, 1);
            vc_end(w);
        }
    }
    TEST_END();
}

int test_vconv_win_activate_and_enable_pause(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(0);
    ASSERT_EQ(F.cap_begin, 1);
    ASSERT_EQ(F.cap_end, 0);

    /* the probe still says active (as it does mid-transition): the message's wParam rules */
    PostMessage(w, WM_ACTIVATEAPP, FALSE, 0);
    pump();
    ASSERT_EQ(F.cap_end, 1);
    ASSERT_STR_EQ(F.header, "Paused while Nutshell is in the background");
    PostMessage(w, WM_ACTIVATEAPP, TRUE, 0);
    pump();
    ASSERT_EQ(F.cap_begin, 2);
    ASSERT_STR_EQ(F.header, "Listening\xE2\x80\xA6 speak, then pause to send");

    EnableWindow(w, FALSE);
    ASSERT_EQ(F.cap_end, 2);
    ASSERT_STR_EQ(F.header, "Paused while a dialog is open");
    EnableWindow(w, TRUE);
    ASSERT_EQ(F.cap_begin, 3);
    ASSERT_STR_EQ(F.header, "Listening\xE2\x80\xA6 speak, then pause to send");
    ASSERT_TRUE(voice_conv_running());

    /* a repeated notification with no change feeds nothing */
    PostMessage(w, WM_ACTIVATEAPP, TRUE, 0);
    pump();
    ASSERT_EQ(F.cap_begin, 3);

    voice_conv_event(VCE_END, VCX_OFF, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

int test_vconv_win_full_turn(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(1);
    const unsigned tok = voice_conv_capture_token();
    ASSERT_TRUE(tok != 0);
    voice_conv_on_audio(VAM_CONV_OPENED, VOICE_CONV_PACK(1, tok));
    ASSERT_EQ(voice_conv_last_aec(), 1);

    aud_event(VLN_TURN_START);
    ASSERT_EQ(voice_conv_state(), VCS_HEARING);
    ASSERT_EQ(F.link_start, 1);
    ASSERT_EQ(F.attach_live, 1);
    ASSERT_EQ(F.live_begin, 1);
    ASSERT_EQ(F.typing, 1);

    live_msg(VLE_READY, "", NULL);
    live_msg(VLE_COMMITTED, "i1", NULL);
    live_msg(VLE_DELTA, "i1", "hello wor");
    live_msg(VLE_DELTA, "i1", "ld");
    ASSERT_STR_EQ(F.live_text, "hello world");

    aud_event(VLN_TURN_END);
    ASSERT_EQ(voice_conv_state(), VCS_ENDING);
    ASSERT_EQ(F.mode, VLM_HOLD);
    live_msg(VLE_COMPLETED, "i1", "hello world");
    F.idle = 0;                              /* the chat is busy as the send goes out */
    live_msg(VLE_DONE, "", NULL);
    ASSERT_EQ(F.send_turn, 1);
    ASSERT_EQ(F.last_voice_turn, 1);
    ASSERT_EQ(voice_conv_state(), VCS_WORKING);
    ASSERT_EQ(F.typing, 0);
    ASSERT_EQ(F.link_close, 1);
    ASSERT_EQ(voice_ledger_count(VRES_LIVE_SESSION), 0);

    /* a link message for the old turn's token is dropped */
    live_msg(VLE_DELTA, "i2", "stale words");
    ASSERT_STR_EQ(F.live_text, "hello world");

    const char *reply = "Sure thing. Here is the answer to your question.";
    chat_finishes(reply);
    ASSERT_EQ(voice_conv_state(), VCS_SPEAKING);
    ASSERT_EQ(F.tts_speak, 1);
    char *want = voice_speak_text(reply);
    ASSERT_NOT_NULL(want);
    ASSERT_STR_EQ(F.speak_text, want);
    ASSERT_EQ((int)F.speak_from, 0);
    free(want);
    ASSERT_EQ(F.mode, VLM_SPEAK_AEC);

    VoiceTtsMsg m;
    memset(&m, 0, sizeof m);
    m.token = F.tts_token;
    m.kind = VTT_DONE;
    m.reply_id = F.speak_id;
    voice_conv_on_tts(&m);
    ASSERT_EQ(voice_conv_state(), VCS_LISTENING);
    ASSERT_EQ(F.mode, VLM_LISTEN);

    voice_conv_event(VCE_PRESS, 0, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    ASSERT_EQ(voice_ledger_underflow(), 0);
    vc_end(w);
    TEST_END();
}

static void tts_msg(VoiceTtsKind kind, unsigned id, size_t off)
{
    VoiceTtsMsg m;
    memset(&m, 0, sizeof m);
    m.token = F.tts_token;
    m.kind = kind;
    m.reply_id = id;
    m.offset = off;
    voice_conv_on_tts(&m);
}

int test_vconv_win_stale_reply_events_ignored(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(1);
    const unsigned tok = voice_conv_capture_token();
    voice_conv_on_audio(VAM_CONV_OPENED, VOICE_CONV_PACK(2, tok));

    to_working();
    chat_finishes(VC_REPLY_A);
    const unsigned id_a = F.speak_id;
    tts_msg(VTT_WORD, id_a, 12);
    /* the user stops it, asks again, and a second reply starts */
    voice_conv_event(VCE_PRESS, 0, 0);
    ASSERT_EQ(voice_conv_state(), VCS_LISTENING);
    to_working();
    chat_finishes(VC_REPLY_B);
    const unsigned id_b = F.speak_id;
    ASSERT_TRUE(id_b != id_a);
    ASSERT_EQ(voice_conv_state(), VCS_SPEAKING);

    tts_msg(VTT_WORD, id_a, 70);     /* the old reply's boundary arrives late: ignored */
    tts_msg(VTT_WORD, id_b, 40);
    tts_msg(VTT_WORD, id_a, 71);     /* and again after the new one moved: still ignored */

    /* the user talks over it: cut in, then asks to carry on */
    aud_event(VLN_CUT);
    ASSERT_EQ(voice_conv_state(), VCS_HEARING);
    aud_event(VLN_TURN_START);
    live_msg(VLE_READY, "", NULL);
    aud_event(VLN_TURN_END);
    ASSERT_EQ(voice_conv_state(), VCS_ENDING);
    const int speaks = F.tts_speak;
    live_msg(VLE_DONE, "", "carry on");
    ASSERT_EQ(voice_conv_state(), VCS_SPEAKING);
    ASSERT_EQ(F.tts_speak, speaks + 1);
    ASSERT_EQ((int)F.speak_from, 40);
    ASSERT_TRUE(strstr(F.speak_text, "second answer") != NULL);
    ASSERT_EQ(F.send_turn, 2);       /* only the two typed sends: "carry on" is not sent */

    voice_conv_event(VCE_END, VCX_OFF, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

int test_vconv_win_notice_once(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    F.notice_due_first = 1;
    voice_conv_start(0);
    to_working();
    chat_finishes(VC_REPLY_A);
    ASSERT_EQ(F.tts_speak, 0);               /* held for the notice */
    ASSERT_EQ(F.notice_voice, 0);
    pump();                                  /* WM_VOICE_NOTICE: the dialog opens and closes */
    ASSERT_EQ(F.notice_voice, 1);
    ASSERT_STR_EQ(F.notice_name, "Microsoft Zira");
    ASSERT_EQ(F.tts_speak, 1);               /* spoken on resume, from the start */
    ASSERT_EQ((int)F.speak_from, 0);
    ASSERT_TRUE(strstr(F.speak_text, "first answer") != NULL);
    ASSERT_EQ(voice_conv_state(), VCS_SPEAKING);

    voice_conv_event(VCE_PRESS, 0, 0);       /* stop the speech */
    to_working();
    chat_finishes(VC_REPLY_B);
    ASSERT_EQ(F.tts_speak, 2);               /* the second reply is spoken at once */
    pump();
    ASSERT_EQ(F.notice_voice, 1);

    voice_conv_event(VCE_END, VCX_OFF, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

int test_vconv_win_off_mid_reply(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(0);
    to_speaking();
    ASSERT_EQ(voice_conv_state(), VCS_SPEAKING);
    ASSERT_EQ(F.caption_stop, 1);
    const int stops = F.tts_stop;
    voice_conv_event(VCE_END, VCX_OFF, 0);
    ASSERT_TRUE(F.tts_stop > stops);
    ASSERT_EQ(F.tts_quit, 1);
    ASSERT_EQ(F.cap_end, F.cap_begin);
    ASSERT_EQ(F.caption_stop, 0);
    ASSERT_TRUE(!voice_conv_running());
    ASSERT_TRUE(voice_ledger_all_zero());
    ASSERT_EQ(voice_ledger_underflow(), 0);
    vc_end(w);
    TEST_END();
}

int test_vconv_win_events_after_their_moment_dropped(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(1);
    const unsigned tok = voice_conv_capture_token();
    /* an idle that arrives while Listening (a typed send's idle from before) changes nothing */
    F.reply_ok = 1;
    snprintf(F.reply_text, sizeof F.reply_text, "%s", VC_REPLY_A);
    voice_conv_on_turn_idle();
    ASSERT_EQ(voice_conv_state(), VCS_LISTENING);
    ASSERT_EQ(F.tts_speak, 0);
    /* a live message with no turn open, an upload result, a speech event for no reply */
    live_msg(VLE_DELTA, "x", "ghost");
    ASSERT_EQ(F.live_replace, 0);
    tts_msg(VTT_DONE, 99, 0);
    ASSERT_EQ(voice_conv_state(), VCS_LISTENING);
    /* an error under another capture's token is not ours */
    voice_conv_on_audio(VAM_ERROR, VOICE_ERR_PACK(VAE_DEVICE_LOST, tok + 1));
    ASSERT_TRUE(voice_conv_running());

    /* Working: an idle before the chat was ever busy is not the turn's end */
    F.idle = 1;
    voice_conv_event(VCE_SEND, 0, 0);
    ASSERT_EQ(voice_conv_state(), VCS_WORKING);
    voice_conv_on_turn_idle();
    ASSERT_EQ(voice_conv_state(), VCS_WORKING);

    voice_conv_event(VCE_END, VCX_OFF, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

/* ---- batch, fallback, Send while Hearing, the draft cue ---- */

#define FALLBACK_FAILED_NOTE \
    "The recording could not be transcribed again: the text in the box is what arrived live"

static VoiceCapture *make_turn(unsigned tok)
{
    VoiceCapture *c = (VoiceCapture *)calloc(1, sizeof *c);
    int16_t pcm[320];
    for (int i = 0; i < 320; i++) pcm[i] = (int16_t)(i & 1 ? -3000 : 3000);
    pcm_init(&c->pcm, 4000);
    (void)pcm_append(&c->pcm, pcm, 320);
    c->token = tok;
    c->verdict = PCM_SPEECH;
    return c;
}

static void stt_result(int ok, const char *text, const char *err)
{
    VoiceSttResult *r = (VoiceSttResult *)calloc(1, sizeof *r);
    r->token = F.stt_token;
    r->ok = ok;
    r->text_cap = 256;
    r->text = (char *)calloc(1, r->text_cap);
    if (text) snprintf(r->text, r->text_cap, "%s", text);
    if (err) snprintf(r->err, sizeof r->err, "%s", err);
    voice_conv_on_stt(r);   /* the glue frees it */
}

/* Hearing; then Send. Returns with the listener's turn ended by the capture command. */
static unsigned hear_then_send(int live)
{
    voice_conv_start(live);
    const unsigned tok = voice_conv_capture_token();
    voice_conv_on_audio(VAM_CONV_OPENED, VOICE_CONV_PACK(0, tok));
    aud_event(VLN_TURN_START);
    return tok;
}

int test_vconv_win_send_while_hearing_batch(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    const unsigned tok = hear_then_send(0);
    ASSERT_EQ(voice_conv_state(), VCS_HEARING);
    voice_conv_event(VCE_SEND, 0, 0);
    ASSERT_EQ(voice_conv_state(), VCS_ENDING);
    ASSERT_EQ(F.cap_end_turn, 1);            /* the turn ends now, as a pause would end it */
    ASSERT_EQ(F.stt_submit, 0);              /* the audio is still on its way */
    aud_event(VLN_TURN_END);                 /* what the audio thread posts, then the audio */
    voice_conv_on_audio(VAM_CONV_TURN, (LPARAM)make_turn(tok));
    ASSERT_EQ(F.stt_submit, 1);
    ASSERT_EQ(voice_ledger_count(VRES_STT_UPLOAD), 1);
    stt_result(1, "hello there friend", NULL);
    ASSERT_EQ(voice_ledger_count(VRES_STT_UPLOAD), 0);
    ASSERT_STR_EQ(F.live_text, "hello there friend");
    ASSERT_EQ(F.send_turn, 1);
    ASSERT_EQ(F.last_voice_turn, 1);
    ASSERT_EQ(voice_conv_state(), VCS_WORKING);
    voice_conv_event(VCE_END, VCX_OFF, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

/* Task 13 (R29): a turn stuck in Ending ends the conversation with a note; its audio is wiped
 * and its words stay in the box. */
int test_vconv_win_ending_wipes_audio_keeps_words(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    const unsigned tok = hear_then_send(1);
    live_msg(VLE_READY, "", NULL);
    live_msg(VLE_DELTA, "i1", "hello wor");
    voice_conv_event(VCE_SEND, 0, 0);
    ASSERT_EQ(voice_conv_state(), VCS_ENDING);
    aud_event(VLN_TURN_END);
    voice_conv_on_audio(VAM_CONV_TURN, (LPARAM)make_turn(tok));   /* kept as the fallback */
    ASSERT_TRUE(voice_conv_test_turn_audio_bytes() > 0);
    ASSERT_EQ(F.drop_words, 0);
    F.notes = 0;
    voice_conv_test_advance(181000ul);
    voice_conv_on_timer();   /* the live final never came: the fallback batch starts */
    voice_conv_on_timer();
    /* the upload's own budget (2 x (30 s + 60 s) + 10 s for this short turn) holds the Ending open
     * past the 180 s default: the glue set it when it submitted */
    ASSERT_TRUE(voice_conv_running());
    ASSERT_EQ(voice_conv_state(), VCS_ENDING);
    voice_conv_test_advance(10000ul);
    voice_conv_on_timer();   /* the upload never answers: the deadline */
    ASSERT_TRUE(!voice_conv_running());
    ASSERT_EQ((int)voice_conv_test_turn_audio_bytes(), 0);
    ASSERT_EQ(F.drop_words, 0);
    ASSERT_EQ(F.notes, 1);
    ASSERT_STR_EQ(F.note, "Conversation ended: the last turn took too long");
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

int test_vconv_win_send_while_hearing_live_link_fails_to_fallback(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    const unsigned tok = hear_then_send(1);
    ASSERT_EQ(F.link_start, 1);
    live_msg(VLE_READY, "", NULL);
    live_msg(VLE_DELTA, "i1", "hello wor");
    voice_conv_event(VCE_SEND, 0, 0);
    ASSERT_EQ(voice_conv_state(), VCS_ENDING);
    ASSERT_EQ(F.cap_end_turn, 1);
    aud_event(VLN_TURN_END);
    voice_conv_on_audio(VAM_CONV_TURN, (LPARAM)make_turn(tok));   /* kept as the fallback */
    ASSERT_EQ(F.stt_submit, 0);
    live_msg(VLE_CLOSED, "", NULL);          /* the link fails after Send */
    ASSERT_EQ(F.stt_submit, 1);              /* the kept audio goes to the batch recogniser */
    ASSERT_EQ(voice_conv_state(), VCS_ENDING);
    stt_result(1, "hello world again", NULL);
    ASSERT_STR_EQ(F.live_text, "hello world again");   /* replaces this turn's live text */
    ASSERT_EQ(F.send_turn, 1);
    ASSERT_EQ(voice_ledger_count(VRES_STT_UPLOAD), 0);
    ASSERT_EQ(voice_ledger_count(VRES_LIVE_SESSION), 0);
    voice_conv_event(VCE_END, VCX_OFF, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

int test_vconv_win_fallback_failed_note(void)
{
    TEST_BEGIN();
    for (int mode = 0; mode < 3; mode++) {   /* 0 result failed, 1 submit refused, 2 batch turn failed */
        HWND w = vc_begin();
        const unsigned tok = hear_then_send(mode != 2);
        if (mode != 2) live_msg(VLE_DELTA, "i1", "partial words");
        if (mode == 1) F.stt_ok = 0;
        voice_conv_event(VCE_SEND, 0, 0);
        aud_event(VLN_TURN_END);
        voice_conv_on_audio(VAM_CONV_TURN, (LPARAM)make_turn(tok));
        F.note[0] = '\0';
        if (mode != 2) live_msg(VLE_CLOSED, "", NULL);
        if (mode == 0) stt_result(0, NULL, "HTTP 500");
        if (mode == 2) stt_result(0, NULL, "HTTP 401");
        if (mode == 1) ASSERT_EQ(F.stt_submit, 1);
        if (mode == 2) {
            ASSERT_STR_EQ(F.note, "HTTP 401");                /* a batch turn says why */
        } else {
            ASSERT_STR_EQ(F.note, FALLBACK_FAILED_NOTE);      /* a live turn says the words stay */
        }
        ASSERT_EQ(voice_conv_state(), VCS_LISTENING);
        ASSERT_EQ(F.send_turn, 0);
        ASSERT_EQ(voice_ledger_count(VRES_STT_UPLOAD), 0);
        voice_conv_event(VCE_END, VCX_OFF, 0);
        ASSERT_TRUE(voice_ledger_all_zero());
        vc_end(w);
    }
    TEST_END();
}

int test_vconv_win_draft_cue_follows_the_box(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(0);
    ASSERT_STR_EQ(F.header, "Listening\xE2\x80\xA6 speak, then pause to send");
    F.box_text = 1;
    voice_conv_on_box_changed();
    ASSERT_STR_EQ(F.header, "Your text and what you say next will be sent after a pause.");
    const int h = F.headers;
    voice_conv_on_box_changed();             /* no change: nothing is rewritten */
    ASSERT_EQ(F.headers, h);
    F.box_text = 0;
    voice_conv_on_box_changed();
    ASSERT_STR_EQ(F.header, "Listening\xE2\x80\xA6 speak, then pause to send");
    voice_conv_event(VCE_END, VCX_OFF, 0);
    vc_end(w);
    TEST_END();
}

/* A turn's audio that can no longer arrive must not leave the conversation waiting in Ending. */
int test_vconv_win_audio_lost_leaves_ending(void)
{
    TEST_BEGIN();
    for (int how = 0; how < 3; how++) {   /* 0 abandon (batch), 1 no memory (batch), 2 no memory then link fails (live) */
        HWND w = vc_begin();
        const unsigned tok = hear_then_send(how == 2);
        voice_conv_event(VCE_SEND, 0, 0);
        ASSERT_EQ(voice_conv_state(), VCS_ENDING);
        F.note[0] = '\0';
        if (how == 0) aud_event(VLN_TURN_ABANDON);          /* the listener's abandon beat the end-turn command */
        else voice_conv_on_audio(VAM_ERROR, VOICE_ERR_PACK(VAE_NO_MEMORY, tok));
        if (how == 2) {
            ASSERT_EQ(voice_conv_state(), VCS_ENDING);       /* the link may still deliver the words */
            live_msg(VLE_CLOSED, "", NULL);                  /* no link, no audio: nothing to fall back to */
        }
        ASSERT_EQ(voice_conv_state(), VCS_LISTENING);
        if (how == 2) ASSERT_STR_EQ(F.note, "The recording could not be transcribed again: the text in the box is what arrived live");
        voice_conv_on_audio(VAM_CONV_TURN, (LPARAM)make_turn(tok));   /* a late turn is a no-op */
        ASSERT_EQ(F.stt_submit, 0);
        ASSERT_EQ(voice_conv_state(), VCS_LISTENING);
        voice_conv_event(VCE_END, VCX_OFF, 0);
        ASSERT_TRUE(voice_ledger_all_zero());
        vc_end(w);
    }
    TEST_END();
}

/* A turn or a reply for a chat that is no longer the conversation's is not sent or spoken:
 * the conversation ends "the chat changed" instead (the posted notice may arrive after the
 * message that would have acted). */
int test_vconv_win_changed_chat_sends_and_speaks_nothing(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(0);
    F.chat_differs = 1;
    voice_conv_event(VCE_SEND, 0, 0);          /* a typed send: refused, the conversation ends */
    ASSERT_EQ(F.send_turn, 0);
    ASSERT_FALSE(voice_conv_running());
    ASSERT_STR_EQ(F.note, "Conversation ended: the chat changed");
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);

    w = vc_begin();
    voice_conv_start(0);
    to_working();                              /* same chat: sent */
    ASSERT_EQ(F.send_turn, 1);
    F.chat_differs = 1;                        /* the chat changes before its reply is read */
    chat_finishes(VC_REPLY_A);
    ASSERT_EQ(F.tts_speak, 0);
    ASSERT_FALSE(voice_conv_running());
    ASSERT_STR_EQ(F.note, "Conversation ended: the chat changed");
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

/* A cut-in's duck must not leak into the next reply: it is spoken at full volume. */
int test_vconv_win_reply_after_duck_is_full_volume(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(0);
    to_speaking();
    ASSERT_EQ(F.tts_speak, 1);
    aud_event(VLN_DUCK);
    ASSERT_EQ(F.volume, 18);
    aud_event(VLN_CUT);                        /* the cut ends the duck */
    ASSERT_EQ(F.volume, 100);
    voice_conv_event(VCE_PRESS, 0, 0);         /* the cut left it Hearing: a press ends it */
    ASSERT_FALSE(voice_conv_running());
    vc_end(w);

    w = vc_begin();                            /* a duck whose un-duck was lost */
    voice_conv_start(0);
    F.volume = 18;
    to_speaking();
    ASSERT_EQ(F.tts_speak, 1);
    ASSERT_EQ(F.volume, 100);                  /* the reply starts at full volume */
    voice_conv_event(VCE_PRESS, 0, 0);
    voice_conv_event(VCE_PRESS, 0, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

/* A speech thread that reports an error stays in its loop (no VTT_EXITED): the glue quits it
 * itself, so no SpVoice outlives the conversation. An error for an older reply is ignored. */
int test_vconv_win_tts_error_quits_the_context(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(0);
    to_speaking();
    ASSERT_EQ(F.tts_start, 1);
    tts_msg(VTT_ERROR, F.speak_id + 50, 0);    /* a reply that is not the current one */
    ASSERT_EQ(F.tts_quit, 0);
    ASSERT_EQ(voice_conv_state(), VCS_SPEAKING);
    tts_msg(VTT_ERROR, F.speak_id, 0);         /* Speak failed for the current reply */
    ASSERT_EQ(F.tts_quit, 1);
    ASSERT_EQ(voice_ledger_count(VRES_SPVOICE), 0);
    ASSERT_EQ(voice_conv_state(), VCS_LISTENING);
    voice_conv_event(VCE_PRESS, 0, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    ASSERT_EQ(voice_ledger_underflow(), 0);
    vc_end(w);
    TEST_END();
}

/* A dialog that disables only the undocked AI panel pauses the conversation too: the main
 * window stays enabled, so the pause decision must also ask the panel. */
int test_vconv_win_panel_dialog_pauses(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(0);
    ASSERT_EQ(F.cap_begin, 1);
    F.dialog_open = 1;
    voice_conv_on_activation();                /* the panel's WM_ENABLE(FALSE) */
    ASSERT_EQ(F.cap_end, 1);
    ASSERT_STR_EQ(F.header, "Paused while a dialog is open");
    F.dialog_open = 0;
    voice_conv_on_activation();                /* and its WM_ENABLE(TRUE) */
    ASSERT_EQ(F.cap_begin, 2);
    ASSERT_STR_EQ(F.header, "Listening\xE2\x80\xA6 speak, then pause to send");
    voice_conv_event(VCE_PRESS, 0, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

/* The speak mode follows the echo-cancellation report of the capture that just opened, not the
 * previous one's: the default microphone may have changed while the conversation was paused. */
int test_vconv_win_speak_mode_follows_new_capture(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(0);
    voice_conv_on_audio(VAM_CONV_OPENED, VOICE_CONV_PACK(2, voice_conv_capture_token()));
    to_speaking();
    ASSERT_EQ(F.mode, VLM_SPEAK_AEC);
    PostMessage(w, WM_ACTIVATEAPP, FALSE, 0);   /* paused: the reply is held from its cut-off */
    pump();
    PostMessage(w, WM_ACTIVATEAPP, TRUE, 0);    /* resumed: a new capture, the held reply speaks */
    pump();
    ASSERT_EQ(voice_conv_state(), VCS_SPEAKING);
    voice_conv_on_audio(VAM_CONV_OPENED, VOICE_CONV_PACK(0, voice_conv_capture_token()));
    ASSERT_EQ(F.mode, VLM_SPEAK_HD);            /* the new device has no echo cancellation */
    voice_conv_event(VCE_PRESS, 0, 0);
    voice_conv_event(VCE_PRESS, 0, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

/* ---- Task 8b: Retry as an external send, unducking, the reply's times, progress, the notice
 * flag and a pending end event ---- */

int test_vconv_win_external_send(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(0);
    ASSERT_EQ(voice_conv_state(), VCS_LISTENING);
    F.idle = 0;                          /* the chat is busy as the external send goes out */
    voice_conv_external_send();
    ASSERT_EQ(voice_conv_state(), VCS_WORKING);
    ASSERT_EQ(F.cap_end, 1);             /* the capture is closed */
    ASSERT_EQ(F.send_turn, 0);           /* the glue sends nothing itself */
    chat_finishes(VC_REPLY_A);           /* a reply after the new baseline */
    ASSERT_EQ(voice_conv_state(), VCS_SPEAKING);
    ASSERT_EQ(F.tts_speak, 1);
    ASSERT_TRUE(strstr(F.speak_text, "first answer") != NULL);
    voice_conv_event(VCE_END, VCX_OFF, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

int test_vconv_win_external_send_keeps_words(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(1);
    voice_conv_on_audio(VAM_CONV_OPENED, VOICE_CONV_PACK(2, voice_conv_capture_token()));
    aud_event(VLN_TURN_START);
    live_msg(VLE_READY, "", NULL);
    live_msg(VLE_DELTA, "i1", "hello there");
    ASSERT_EQ(voice_conv_state(), VCS_HEARING);
    ASSERT_TRUE(F.live_replace >= 1);
    ASSERT_EQ(F.link_close, 0);
    F.idle = 0;
    voice_conv_external_send();
    ASSERT_EQ(voice_conv_state(), VCS_WORKING);
    ASSERT_EQ(F.drop_words, 0);          /* the words in the box were just sent: not stripped */
    ASSERT_EQ(F.link_close, 1);          /* the turn's link is closed */
    ASSERT_EQ(F.cap_end, 1);
    ASSERT_EQ(F.typing, 0);
    voice_conv_event(VCE_END, VCX_OFF, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

int test_vconv_win_external_send_unducks(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(1);
    voice_conv_on_audio(VAM_CONV_OPENED, VOICE_CONV_PACK(2, voice_conv_capture_token()));
    to_speaking();
    ASSERT_EQ(voice_conv_state(), VCS_SPEAKING);
    aud_event(VLN_DUCK);
    ASSERT_EQ(F.volume, 18);
    F.idle = 0;
    voice_conv_external_send();
    ASSERT_EQ(voice_conv_state(), VCS_WORKING);
    ASSERT_TRUE(F.tts_stop >= 1);
    ASSERT_EQ(F.volume, 100);            /* the duck is over once the speech it ducked is gone */
    voice_conv_event(VCE_END, VCX_OFF, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

/* A reply's times belong to that reply. Conversation 1 speaks reply A to the end (its time is stamped),
 * 6 minutes pass, and conversation 2's first reply B is held for the notice: B is loaded but not playing,
 * so a cut-in stops nothing and nothing restamps the times. "carry on" is then judged against B's own
 * times (CONTINUE); against A's stale ones it would be plain text. */
int test_vconv_win_new_reply_resets_times(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(1);
    voice_conv_on_audio(VAM_CONV_OPENED, VOICE_CONV_PACK(2, voice_conv_capture_token()));
    to_speaking();
    tts_msg(VTT_DONE, F.speak_id, 0);    /* reply A read to the end: its time is stamped */
    voice_conv_event(VCE_END, VCX_OFF, 0);
    ASSERT_FALSE(voice_conv_running());
    voice_conv_test_advance(360000ul);   /* 6 minutes: past the phrase window */

    F.notice_due_first = 1;
    voice_conv_start(1);
    voice_conv_on_audio(VAM_CONV_OPENED, VOICE_CONV_PACK(2, voice_conv_capture_token()));
    to_working();
    const int speaks = F.tts_speak;
    chat_finishes(VC_REPLY_B);           /* held for the notice: loaded, not playing */
    ASSERT_EQ(voice_conv_state(), VCS_SPEAKING);
    ASSERT_EQ(F.tts_speak, speaks);
    aud_event(VLN_CUT);                  /* the user talks over the held reply */
    ASSERT_EQ(voice_conv_state(), VCS_HEARING);
    aud_event(VLN_TURN_START);
    live_msg(VLE_READY, "", NULL);
    aud_event(VLN_TURN_END);
    ASSERT_EQ(voice_conv_state(), VCS_ENDING);
    live_msg(VLE_DONE, "", "carry on");  /* judged against B's times only */
    ASSERT_EQ(voice_conv_state(), VCS_SPEAKING);
    ASSERT_EQ(F.tts_speak, speaks + 1);  /* CONTINUE: B is spoken */
    ASSERT_TRUE(strstr(F.speak_text, "second answer") != NULL);
    ASSERT_EQ(F.send_turn, 2);           /* "carry on" was not sent as a turn */
    voice_conv_event(VCE_END, VCX_OFF, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

int test_vconv_win_progress_keeps_long_reply(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(0);
    to_speaking();
    const unsigned id = F.speak_id;
    for (int i = 0; i < 22; i++) {       /* 11 minutes in 30-second steps */
        voice_conv_test_advance(30000ul);
        tts_msg(VTT_WORD, id, 5);
        voice_conv_on_timer();
        ASSERT_EQ(voice_conv_state(), VCS_SPEAKING);
    }
    /* a word of an older reply is no progress: 10 minutes after the last real one, Speaking ends */
    voice_conv_test_advance(590000ul);
    tts_msg(VTT_WORD, id + 50, 5);
    voice_conv_test_advance(11000ul);
    voice_conv_on_timer();
    ASSERT_EQ(voice_conv_state(), VCS_LISTENING);
    voice_conv_event(VCE_END, VCX_OFF, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

/* Progress is fed at most once a second: a second word 0.5 s after the first does not move the
 * watchdog, so Speaking still ends 10 minutes after the first. */
int test_vconv_win_progress_throttled(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(0);
    to_speaking();
    const unsigned id = F.speak_id;
    tts_msg(VTT_WORD, id, 5);
    voice_conv_test_advance(500ul);
    tts_msg(VTT_WORD, id, 9);            /* inside the second: not fed */
    voice_conv_test_advance(599600ul);
    voice_conv_on_timer();
    ASSERT_EQ(voice_conv_state(), VCS_LISTENING);
    voice_conv_event(VCE_END, VCX_OFF, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

int test_vconv_win_notice_kept_if_ended_first(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    F.notice_due_first = 1;
    voice_conv_start(0);
    to_working();
    chat_finishes(VC_REPLY_A);           /* the notice is posted, the reply held */
    ASSERT_EQ(F.tts_speak, 0);
    ASSERT_EQ(F.notice_voice, 0);
    voice_conv_event(VCE_END, VCX_OFF, 0);   /* the conversation ends before WM_VOICE_NOTICE runs */
    pump();
    ASSERT_EQ(F.notice_voice, 0);        /* nothing was shown: the notice is still owed */
    ASSERT_TRUE(voice_ledger_all_zero());
    F.notice_due_first = 1;
    voice_conv_start(0);
    to_working();
    chat_finishes(VC_REPLY_A);           /* the next conversation's first reply raises it */
    ASSERT_EQ(F.tts_speak, 0);
    pump();
    ASSERT_EQ(F.notice_voice, 1);
    ASSERT_EQ(F.tts_speak, 1);
    voice_conv_event(VCE_END, VCX_OFF, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

int test_vconv_win_end_never_dropped(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(0);
    to_speaking();
    F.note[0] = '\0';
    for (int i = 0; i < 30; i++) voice_conv_test_queue(VCE_TICK, 0, 0);   /* the queue is full */
    voice_conv_test_queue(VCE_END, VCX_OFF, 0);                           /* the end must survive */
    voice_conv_test_queue(VCE_END, VCX_SETTINGS, 0);                      /* the first end wins */
    voice_conv_test_pump();
    ASSERT_EQ(voice_conv_state(), VCS_IDLE);
    ASSERT_FALSE(voice_conv_running());
    ASSERT_TRUE(strstr(F.note, "settings changed") == NULL);   /* it was the OFF end that ran */
    ASSERT_TRUE(voice_ledger_all_zero());
    ASSERT_EQ(voice_ledger_underflow(), 0);
    vc_end(w);
    TEST_END();
}

/* While an end is parked, a later end (here the one a send raises when the chat moved, which finds
 * room in the queue the pump has begun to drain) does not jump ahead of it: the first end wins. */
int test_vconv_win_parked_end_wins(void)
{
    TEST_BEGIN();
    HWND w = vc_begin();
    voice_conv_start(0);
    F.chat_differs = 1;
    F.note[0] = '\0';
    voice_conv_test_queue(VCE_SEND, 0, 0);                    /* its send raises END(CHAT_CHANGED) */
    for (int i = 0; i < 23; i++) voice_conv_test_queue(VCE_TICK, 0, 0);   /* the queue is full */
    voice_conv_test_queue(VCE_END, VCX_OFF, 0);               /* parked */
    voice_conv_test_pump();
    ASSERT_EQ(voice_conv_state(), VCS_IDLE);
    ASSERT_TRUE(strstr(F.note, "chat changed") == NULL);      /* the parked OFF end ran first */
    ASSERT_EQ(F.send_turn, 0);
    ASSERT_TRUE(voice_ledger_all_zero());
    vc_end(w);
    TEST_END();
}

#endif /* _WIN32 */
