/* tests/test_voice_read_win.c -- the Read aloud controller against a fake ops
 * table that counts every call. Built only by `make wintest`. */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_framework.h"
#include "voice_read_win.h"
#include "voice_aispeech_win.h"
#include "voice_aispeech.h"
#include "voice_config.h"
#include "voice_conv_win.h"

#define HR_FAIL ((long)(int)0x80004005u)

typedef struct {
    int start, speak, stop, quit, note, view, turn_text;
    unsigned start_token; int start_engine;
    unsigned speak_token, speak_id; size_t speak_from; char speak_text[64];
    char note_text[200];
    int view_on, view_reading, view_disabled;
    int resolve_calls;
    int quit2, view2, stop2, view2_reading;
} Fk;

static Fk g;
static const char *g_text;
static int g_start_ok;
static VoiceBinding g_binding;
static unsigned g_next_gen;
static int g_gen_zero;

static int f_start(unsigned token, int engine)
{
    g.start++; g.start_token = token; g.start_engine = engine;
    if (g_start_ok) g_next_gen++;
    return g_start_ok;
}
static void f_speak(unsigned token, unsigned id, const char *text, size_t from)
{
    g.speak++; g.speak_token = token; g.speak_id = id; g.speak_from = from;
    snprintf(g.speak_text, sizeof g.speak_text, "%s", text);
}
static void f_stop(unsigned token) { (void)token; g.stop++; }
static void f_quit(unsigned token) { (void)token; g.quit++; }
static char *f_turn_text(int id)
{
    char buf[96];
    g.turn_text++;
    if (g_text == NULL)
        return NULL;
    if (g_text[0] == '\0')
        return _strdup("");
    snprintf(buf, sizeof buf, "%s %d", g_text, id);
    return _strdup(buf);
}
static void f_view(HWND panel, int on, int reading, int disabled)
{
    (void)panel; g.view++; g.view_on = on; g.view_reading = reading; g.view_disabled = disabled;
}
static void f_note(const char *t) { g.note++; snprintf(g.note_text, sizeof g.note_text, "%s", t); }
static void f_binding(VoiceBinding *out) { *out = g_binding; }
static int f_resolve(char *out, size_t cap)
{
    g.resolve_calls++;
    snprintf(out, cap, "%s", "Enter a speech API key");
    return 1;
}
static unsigned f_gen(unsigned token) { (void)token; return g_gen_zero ? 0u : g_next_gen; }
static void f_stop2(unsigned token) { (void)token; g.stop2++; }
static void f_quit2(unsigned token) { (void)token; g.quit2++; }
static void f_view2(HWND panel, int on, int reading, int disabled)
{
    (void)panel; (void)on; (void)disabled; g.view2++; g.view2_reading = reading;
}

static const VoiceReadOps FAKE = { f_start, f_speak, f_stop, f_quit, f_turn_text, f_view,
                                   f_note, f_binding, f_resolve, f_gen };
static const VoiceReadOps NOGEN = { f_start, f_speak, f_stop, f_quit, f_turn_text, f_view,
                                    f_note, f_binding, f_resolve, NULL };
static const VoiceReadOps OTHER = { f_start, f_speak, f_stop2, f_quit2, f_turn_text, f_view2,
                                    f_note, f_binding, f_resolve, f_gen };

static Settings S(int engine)
{
    Settings s;
    memset(&s, 0, sizeof s);
    s.ai_enabled = 1;
    s.voice_enabled = 1;
    s.voice_mode = VOICE_MODE_DICTATION;
    s.voice_speech_engine = engine;
    return s;
}

static void begin(const Settings *s)
{
    memset(&g, 0, sizeof g);
    g_text = "Reply";
    g_gen_zero = 0;
    g_start_ok = 1;
    g_next_gen = 9;
    memset(&g_binding, 0, sizeof g_binding);
    g_binding.session = &g_binding;
    g_binding.session_gen = 1;
    g_binding.conv_gen = 1;
    voice_read_win_set_ops(&FAKE);
    voice_read_win_apply(s, 1);
}

static void msg(VoiceTtsKind kind, unsigned token, unsigned gen, unsigned reply, long hr)
{
    VoiceTtsMsg m;
    memset(&m, 0, sizeof m);
    m.token = token; m.gen = gen; m.kind = kind; m.reply_id = reply; m.hr = hr;
    voice_read_win_on_tts(&m);
}

static void done(void) { voice_read_win_set_ops(NULL); }

int test_vread_win_ai_press_speaks(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    ASSERT_EQ(g.start, 1);
    ASSERT_EQ(g.start_token, 0x7FFFFFF3u);
    ASSERT_EQ(g.start_engine, VOICE_SPEECH_AI);
    ASSERT_EQ(g.speak, 1);
    ASSERT_EQ(g.speak_id, 1u);
    ASSERT_EQ(g.speak_from, (size_t)0);
    ASSERT_STR_EQ(g.speak_text, "Reply 7");
    ASSERT_EQ(g.view_on, 1);
    ASSERT_EQ(g.view_reading, 7);
    ASSERT_EQ(g.view_disabled, 0);
    ASSERT_EQ(voice_read_win_active(), 1);
    done();
    TEST_END();
}

int test_vread_win_windows_waits_ready(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_WINDOWS);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    ASSERT_EQ(g.start, 1);
    ASSERT_EQ(g.speak, 0);
    msg(VTT_READY, VOICE_READ_TOKEN, 10, 0, 0);
    ASSERT_EQ(g.speak, 1);
    ASSERT_STR_EQ(g.speak_text, "Reply 7");
    done();
    TEST_END();
}

int test_vread_win_gate_refuses(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    voice_read_win_press(&s, 7, 1);                 /* recording */
    ASSERT_EQ(g.start, 0); ASSERT_EQ(g.turn_text, 0); ASSERT_EQ(g.speak, 0);
    Settings c = S(VOICE_SPEECH_AI);
    c.voice_mode = VOICE_MODE_CONVERSATION;
    voice_read_win_press(&c, 7, 0);
    ASSERT_EQ(g.start, 0); ASSERT_EQ(g.turn_text, 0);
    Settings o = S(VOICE_SPEECH_AI);
    o.voice_enabled = 0;
    voice_read_win_press(&o, 7, 0);
    ASSERT_EQ(g.start, 0); ASSERT_EQ(g.turn_text, 0);
    voice_read_win_apply(&s, 0);                    /* audio thread down */
    voice_read_win_press(&s, 7, 0);
    ASSERT_EQ(g.start, 0); ASSERT_EQ(g.turn_text, 0); ASSERT_EQ(g.speak, 0);
    done();
    TEST_END();
}

int test_vread_win_other_tokens_ignored(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    Fk before = g;
    unsigned tok[3] = { VOICE_TTS_SAMPLE_TOKEN, VOICE_AIS_SAMPLE_TOKEN, 5 };
    for (int i = 0; i < 3; i++) {
        msg(VTT_DONE, tok[i], 10, 1, 0);
        msg(VTT_ERROR, tok[i], 10, 0, 1);
    }
    ASSERT_EQ(g.start, before.start); ASSERT_EQ(g.stop, before.stop);
    ASSERT_EQ(g.quit, before.quit); ASSERT_EQ(g.note, before.note);
    ASSERT_EQ(g.view, before.view); ASSERT_EQ(g.speak, before.speak);
    ASSERT_EQ(voice_read_win_active(), 1);
    done();
    TEST_END();
}

int test_vread_win_error_releases_then_fresh_start(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_WINDOWS);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    msg(VTT_READY, VOICE_READ_TOKEN, 10, 0, 0);
    msg(VTT_ERROR, VOICE_READ_TOKEN, 10, 1, HR_FAIL);
    ASSERT_EQ(g.quit, 1);
    ASSERT_EQ(g.note, 1);
    ASSERT_STR_EQ(g.note_text, "Could not read the reply aloud: the Windows voice did not start.");
    ASSERT_EQ(g.view_reading, 0);
    ASSERT_EQ(voice_read_win_active(), 0);
    voice_read_win_press(&s, 9, 0);
    ASSERT_EQ(g.start, 2);
    done();
    TEST_END();
}

int test_vread_win_start_refused_shows_error(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    g_start_ok = 0;
    voice_read_win_press(&s, 7, 0);
    ASSERT_EQ(g.note, 1);
    ASSERT_STR_EQ(g.note_text, "Could not read the reply aloud: AI speech did not start.");
    ASSERT_EQ(g.speak, 0);
    ASSERT_EQ(g.view_reading, 0);
    done();
    TEST_END();
}

int test_vread_win_late_error_of_first_keeps_second(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    voice_read_win_press(&s, 9, 0);
    ASSERT_EQ(g.speak, 2);
    ASSERT_EQ(g.speak_id, 2u);
    msg(VTT_ERROR, VOICE_READ_TOKEN, 10, 1, HR_FAIL);
    ASSERT_EQ(g.quit, 0);
    ASSERT_EQ(g.note, 0);
    ASSERT_EQ(voice_read_win_active(), 1);
    msg(VTT_DONE, VOICE_READ_TOKEN, 10, 2, 0);
    ASSERT_EQ(g.view_reading, 0);
    ASSERT_EQ(voice_read_win_active(), 0);
    done();
    TEST_END();
}

int test_vread_win_resolve_reason(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    msg(VTT_ERROR, VOICE_READ_TOKEN, 10, 1, VOICE_AIS_HR_RESOLVE);
    ASSERT_EQ(g.note, 1);
    ASSERT_STR_EQ(g.note_text, "Could not read the reply aloud: Enter a speech API key.");
    done();
    TEST_END();
}

int test_vread_win_empty_text_does_nothing(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    g_text = "";
    voice_read_win_press(&s, 7, 0);
    ASSERT_EQ(g.start, 0); ASSERT_EQ(g.speak, 0);
    g_text = NULL;
    voice_read_win_press(&s, 7, 0);
    ASSERT_EQ(g.start, 0);
    done();
    TEST_END();
}

int test_vread_win_settings_change_releases(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    Settings t = s;
    snprintf(t.voice_ai_voice, sizeof t.voice_ai_voice, "%s", "alloy");
    voice_read_win_apply(&t, 1);
    ASSERT_EQ(g.stop, 1);
    ASSERT_EQ(g.quit, 1);
    ASSERT_EQ(g.note, 0);
    voice_read_win_apply(&t, 1);
    ASSERT_EQ(g.quit, 1);
    done();
    TEST_END();
}

int test_vread_win_mode_switch_releases(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    Settings c = s;
    c.voice_mode = VOICE_MODE_CONVERSATION;
    voice_read_win_apply(&c, 1);
    ASSERT_EQ(g.quit, 1);
    ASSERT_EQ(g.view_on, 0);
    done();
    TEST_END();
}

int test_vread_win_sample_stop_is_not_error(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    voice_read_win_stop(VRX_SAMPLE);
    ASSERT_EQ(g.stop, 1);
    ASSERT_EQ(g.quit, 1);
    ASSERT_EQ(g.note, 0);
    done();
    TEST_END();
}

int test_vread_win_chat_change_stops(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    voice_read_win_chat_check();
    ASSERT_EQ(g.stop, 0);
    ASSERT_EQ(voice_read_win_active(), 1);
    g_binding.conv_gen++;
    voice_read_win_chat_check();
    ASSERT_EQ(g.stop, 1);
    ASSERT_EQ(g.quit, 0);
    ASSERT_EQ(voice_read_win_active(), 0);
    done();
    TEST_END();
}

int test_vread_win_recording_disables_view(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    voice_read_win_set_recording(1);
    ASSERT_EQ(g.view_disabled, 1);
    voice_read_win_set_recording(0);
    ASSERT_EQ(g.view_disabled, 0);
    done();
    TEST_END();
}

/* A released context's late messages never reach the next one: its VTT_ERROR
 * (reply id 0) and its VTT_READY carry the old generation. */
int test_vread_win_old_context_messages_dropped(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_WINDOWS);
    begin(&s);
    voice_read_win_press(&s, 7, 0);                 /* context gen 10 */
    msg(VTT_READY, VOICE_READ_TOKEN, 10, 0, 0);
    voice_read_win_stop(VRX_SETTINGS);              /* release */
    ASSERT_EQ(g.quit, 1);
    voice_read_win_press(&s, 9, 0);                 /* fresh context, gen 11 */
    ASSERT_EQ(g.start, 2);
    ASSERT_EQ(g.speak, 1);
    msg(VTT_ERROR, VOICE_READ_TOKEN, 10, 0, HR_FAIL);   /* the old one's late error */
    ASSERT_EQ(g.quit, 1);
    ASSERT_EQ(g.note, 0);
    ASSERT_EQ(voice_read_win_active(), 1);
    msg(VTT_READY, VOICE_READ_TOKEN, 10, 0, 0);     /* the old one's late ready */
    ASSERT_EQ(g.speak, 1);
    msg(VTT_READY, VOICE_READ_TOKEN, 11, 0, 0);     /* the new one's own */
    ASSERT_EQ(g.speak, 2);
    ASSERT_EQ(g.speak_id > 0, 1);
    msg(VTT_ERROR, VOICE_READ_TOKEN, 11, 0, HR_FAIL);
    ASSERT_EQ(g.quit, 2);
    ASSERT_EQ(g.note, 1);
    done();
    TEST_END();
}


static const char *NOTE_WIN = "Could not read the reply aloud: the Windows voice did not start.";
static const char *NOTE_AI = "Could not read the reply aloud: AI speech did not start.";

int test_vread_win_no_context_gen_refuses(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    voice_read_win_set_ops(&NOGEN);
    voice_read_win_apply(&s, 1);
    voice_read_win_press(&s, 7, 0);
    ASSERT_EQ(g.start, 0);
    ASSERT_EQ(g.speak, 0);
    ASSERT_EQ(voice_read_win_active(), 0);
    ASSERT_EQ(g.note, 1);
    ASSERT_STR_EQ(g.note_text, NOTE_AI);
    done();
    TEST_END();
}

int test_vread_win_zero_context_gen_quits(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_WINDOWS);
    begin(&s);
    g_gen_zero = 1;
    voice_read_win_press(&s, 7, 0);
    ASSERT_EQ(g.start, 1);
    ASSERT_EQ(g.quit, 1);
    ASSERT_EQ(g.speak, 0);
    ASSERT_EQ(g.note, 1);
    ASSERT_STR_EQ(g.note_text, NOTE_WIN);
    ASSERT_EQ(voice_read_win_active(), 0);
    ASSERT_EQ(g.view_reading, 0);
    done();
    TEST_END();
}

/* A is released with its DONE and ERROR still queued; B starts; A's messages
 * arrive first and must neither end nor release B. */
int test_vread_win_stale_messages_before_new_context(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    voice_read_win_press(&s, 7, 0);                 /* A, gen 10, reply 1 */
    voice_read_win_stop(VRX_SETTINGS);
    ASSERT_EQ(g.quit, 1);
    voice_read_win_press(&s, 9, 0);                 /* B, gen 11, reply 2 */
    ASSERT_STR_EQ(g.speak_text, "Reply 9");
    msg(VTT_DONE, VOICE_READ_TOKEN, 10, 1, 0);
    msg(VTT_ERROR, VOICE_READ_TOKEN, 10, 0, HR_FAIL);
    ASSERT_EQ(g.quit, 1);
    ASSERT_EQ(g.note, 0);
    ASSERT_EQ(voice_read_win_active(), 1);
    ASSERT_EQ(g.speak_id > 1, 1);
    msg(VTT_DONE, VOICE_READ_TOKEN, 11, g.speak_id, 0);
    ASSERT_EQ(voice_read_win_active(), 0);
    ASSERT_EQ(g.view_reading, 0);
    voice_read_win_press(&s, 9, 0);                 /* kept context reused */
    msg(VTT_ERROR, VOICE_READ_TOKEN, 11, 0, HR_FAIL);
    ASSERT_EQ(g.quit, 2);
    ASSERT_EQ(g.note, 1);
    done();
    TEST_END();
}

int test_vread_win_set_ops_releases_held_context(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    Fk before = g;
    voice_read_win_set_ops(NULL);
    ASSERT_EQ(g.quit, before.quit + 1);
    ASSERT_EQ(g.stop, before.stop + 1);
    ASSERT_EQ(g.view_reading, 0);
    ASSERT_EQ(voice_read_win_active(), 0);
    /* idle with a context held, replaced by another table */
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    msg(VTT_DONE, VOICE_READ_TOKEN, 10, 1, 0);
    ASSERT_EQ(g.quit, 0);
    before = g;
    voice_read_win_set_ops(&OTHER);
    ASSERT_EQ(g.quit, before.quit + 1);
    ASSERT_EQ(g.quit2, 0);
    ASSERT_EQ(g.view, before.view + 1);
    ASSERT_EQ(g.view_reading, 0);
    ASSERT_EQ(g.view2, 0);
    /* a fresh press on the new table starts again */
    voice_read_win_apply(&s, 1);
    voice_read_win_press(&s, 7, 0);
    ASSERT_EQ(g.start, 2);
    done();
    TEST_END();
}

int test_vread_win_set_ops_while_reading_other_table(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    voice_read_win_set_ops(&OTHER);
    ASSERT_EQ(g.quit, 1);
    ASSERT_EQ(g.quit2, 0);
    ASSERT_EQ(g.view_reading, 0);
    ASSERT_EQ(g.view_on, 1);
    done();
    TEST_END();
}

int test_vread_win_exited_while_reading(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_WINDOWS);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    msg(VTT_READY, VOICE_READ_TOKEN, 10, 0, 0);
    msg(VTT_EXITED, VOICE_READ_TOKEN, 99, 0, 0);    /* another generation: ignored */
    ASSERT_EQ(g.quit, 0);
    ASSERT_EQ(voice_read_win_active(), 1);
    msg(VTT_EXITED, VOICE_READ_TOKEN, 10, 0, 0);
    ASSERT_EQ(g.quit, 1);
    ASSERT_EQ(g.note, 1);
    ASSERT_STR_EQ(g.note_text, NOTE_WIN);
    ASSERT_EQ(voice_read_win_active(), 0);
    voice_read_win_press(&s, 7, 0);                 /* a fresh start, not a lost press */
    ASSERT_EQ(g.start, 2);
    done();
    TEST_END();
}

static int g_conv_quits;
static void f_conv_quit(unsigned token) { (void)token; g_conv_quits++; }

/* One exit message goes through both handlers, in voice_on_tts's order: the reader
 * releases its context once and notes the failure; the conversation, not running and
 * not owning the token, quits nothing. */
int test_vread_win_exited_through_both_handlers(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_WINDOWS);
    begin(&s);
    VoiceConvOps co;
    memset(&co, 0, sizeof co);
    co.tts_quit = f_conv_quit;
    voice_conv_set_ops(&co);
    g_conv_quits = 0;
    voice_read_win_press(&s, 7, 0);
    msg(VTT_READY, VOICE_READ_TOKEN, 10, 0, 0);
    ASSERT_EQ(voice_read_win_active(), 1);
    VoiceTtsMsg m;
    memset(&m, 0, sizeof m);
    m.token = VOICE_READ_TOKEN; m.gen = 10; m.kind = VTT_EXITED;
    voice_read_win_on_tts(&m);
    voice_conv_on_tts(&m);
    ASSERT_EQ(g.quit, 1);
    ASSERT_EQ(g_conv_quits, 0);
    ASSERT_EQ(voice_read_win_active(), 0);
    ASSERT_EQ(g.note, 1);
    ASSERT_STR_EQ(g.note_text, NOTE_WIN);
    voice_read_win_on_tts(&m);               /* a repeat finds no context: nothing again */
    voice_conv_on_tts(&m);
    ASSERT_EQ(g.quit, 1);
    ASSERT_EQ(g_conv_quits, 0);
    voice_conv_set_ops(NULL);
    done();
    TEST_END();
}

int test_vread_win_exited_while_idle(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_WINDOWS);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    msg(VTT_READY, VOICE_READ_TOKEN, 10, 0, 0);
    msg(VTT_DONE, VOICE_READ_TOKEN, 10, 1, 0);
    msg(VTT_EXITED, VOICE_READ_TOKEN, 8, 0, 0);     /* stale generation */
    ASSERT_EQ(g.quit, 0);
    msg(VTT_EXITED, VOICE_READ_TOKEN, 10, 0, 0);
    ASSERT_EQ(g.quit, 1);
    ASSERT_EQ(g.note, 0);
    voice_read_win_press(&s, 7, 0);
    ASSERT_EQ(g.start, 2);
    done();
    TEST_END();
}

int test_vread_win_set_recording_stops_reading(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    voice_read_win_set_recording(1);
    ASSERT_EQ(g.stop, 1);
    ASSERT_EQ(g.quit, 0);
    ASSERT_EQ(g.view_reading, 0);
    ASSERT_EQ(g.view_disabled, 1);
    ASSERT_EQ(voice_read_win_active(), 0);
    /* the stored flag gates a press whose own argument says not recording */
    voice_read_win_press(&s, 7, 0);
    ASSERT_EQ(g.start, 1);
    ASSERT_EQ(g.speak, 1);
    voice_read_win_set_recording(0);
    voice_read_win_press(&s, 7, 0);
    ASSERT_EQ(g.speak, 2);                          /* kept context reused */
    done();
    TEST_END();
}

int test_vread_win_apply_unchanged_keeps_reading(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    voice_read_win_apply(&s, 1);
    ASSERT_EQ(g.stop, 0); ASSERT_EQ(g.quit, 0);
    ASSERT_EQ(voice_read_win_active(), 1);
    msg(VTT_DONE, VOICE_READ_TOKEN, 10, 1, 0);
    voice_read_win_apply(&s, 1);                    /* idle, context held */
    ASSERT_EQ(g.stop, 0); ASSERT_EQ(g.quit, 0);
    done();
    TEST_END();
}

int test_vread_win_apply_switches_off_release(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    Settings off = s;
    off.voice_enabled = 0;
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    voice_read_win_apply(&off, 1);
    ASSERT_EQ(g.stop, 1); ASSERT_EQ(g.quit, 1);
    ASSERT_EQ(voice_read_win_active(), 0);
    begin(&s);
    off = s;
    off.ai_enabled = 0;
    voice_read_win_press(&s, 7, 0);
    voice_read_win_apply(&off, 1);
    ASSERT_EQ(g.stop, 1); ASSERT_EQ(g.quit, 1);
    ASSERT_EQ(voice_read_win_active(), 0);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    voice_read_win_apply(&s, 0);
    ASSERT_EQ(g.stop, 1); ASSERT_EQ(g.quit, 1);
    ASSERT_EQ(voice_read_win_active(), 0);
    done();
    TEST_END();
}

int test_vread_win_engine_change_between_presses(void)
{
    TEST_BEGIN();
    Settings w = S(VOICE_SPEECH_WINDOWS);
    Settings a = S(VOICE_SPEECH_AI);
    begin(&w);
    /* no context held: the refused start leaves none */
    g_start_ok = 0;
    voice_read_win_press(&w, 7, 0);
    ASSERT_EQ(g.start_engine, VOICE_SPEECH_WINDOWS);
    g_start_ok = 1;
    voice_read_win_press(&a, 7, 0);
    ASSERT_EQ(g.start, 2);
    ASSERT_EQ(g.start_engine, VOICE_SPEECH_AI);
    ASSERT_EQ(g.quit, 0);
    /* context held: the old one is quit first */
    done();
    begin(&w);
    voice_read_win_press(&w, 7, 0);
    msg(VTT_READY, VOICE_READ_TOKEN, 10, 0, 0);
    msg(VTT_DONE, VOICE_READ_TOKEN, 10, 1, 0);
    voice_read_win_press(&a, 7, 0);
    ASSERT_EQ(g.quit, 1);
    ASSERT_EQ(g.start, 2);
    ASSERT_EQ(g.start_engine, VOICE_SPEECH_AI);
    done();
    TEST_END();
}

int test_vread_win_escape_then_ready_speaks_nothing(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_WINDOWS);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    voice_read_win_stop(VRX_ESCAPE);
    ASSERT_EQ(g.quit, 0);
    msg(VTT_READY, VOICE_READ_TOKEN, 10, 0, 0);
    ASSERT_EQ(g.speak, 0);
    ASSERT_EQ(voice_read_win_active(), 0);
    done();
    TEST_END();
}

int test_vread_win_second_press_before_ready_speaks_second(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_WINDOWS);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    voice_read_win_press(&s, 9, 0);
    ASSERT_EQ(g.start, 1);
    ASSERT_EQ(g.speak, 0);
    msg(VTT_READY, VOICE_READ_TOKEN, 10, 0, 0);
    ASSERT_EQ(g.speak, 1);
    ASSERT_STR_EQ(g.speak_text, "Reply 9");
    ASSERT_EQ(g.view_reading, 9);
    done();
    TEST_END();
}

int test_vread_win_kept_context_reused(void)
{
    TEST_BEGIN();
    Settings s = S(VOICE_SPEECH_AI);
    begin(&s);
    voice_read_win_press(&s, 7, 0);
    msg(VTT_DONE, VOICE_READ_TOKEN, 10, 1, 0);
    voice_read_win_press(&s, 9, 0);
    ASSERT_EQ(g.start, 1);
    ASSERT_EQ(g.speak, 2);
    ASSERT_STR_EQ(g.speak_text, "Reply 9");
    voice_read_win_stop(VRX_DICTATION);
    ASSERT_EQ(g.quit, 0);
    voice_read_win_press(&s, 7, 0);
    voice_read_win_stop(VRX_ESCAPE);
    ASSERT_EQ(g.quit, 0);
    voice_read_win_press(&s, 7, 0);
    ASSERT_EQ(g.start, 1);
    ASSERT_EQ(g.speak, 4);
    done();
    TEST_END();
}

#endif /* _WIN32 */
