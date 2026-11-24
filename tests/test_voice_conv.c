#include "test_framework.h"
#include "voice_conv.h"
#include "voice_ledger.h"
#include <string.h>

/* Applies the actions to the real ledger the way the glue will. Releases are
 * strict: releasing what is not held sets the underflow flag. A batch result
 * releases the upload it ends (the glue joins it). */
static VoiceConvOut run(VoiceConv *c, VoiceConvEvent ev, int a, int b, unsigned long now)
{
    int had_batch = c->batch;
    VoiceConvOut o = voice_conv_step(c, ev, a, b, now);
    unsigned x = o.actions;
    if (x & VCA_ENGINE_START) voice_ledger_acquire(VRES_SPVOICE);
    if (x & VCA_ENGINE_STOP) voice_ledger_release(VRES_SPVOICE);
    if (x & VCA_LOCK_REG) voice_ledger_acquire(VRES_SESSION_LOCK);
    if (x & VCA_LOCK_UNREG) voice_ledger_release(VRES_SESSION_LOCK);
    if (x & VCA_OPEN_CAPTURE) voice_ledger_acquire(VRES_CAPTURE);
    if (x & VCA_CLOSE_CAPTURE) voice_ledger_release(VRES_CAPTURE);
    if (x & VCA_OPEN_LINK) voice_ledger_acquire(VRES_LIVE_SESSION);
    if (x & VCA_CLOSE_LINK) voice_ledger_release(VRES_LIVE_SESSION);
    if (x & VCA_SUBMIT_BATCH) voice_ledger_acquire(VRES_STT_UPLOAD);
    if (x & VCA_CANCEL_BATCH) voice_ledger_release(VRES_STT_UPLOAD);
    if (had_batch && !c->batch && !(x & VCA_CANCEL_BATCH)
        && (ev == VCE_BATCH_OK || ev == VCE_BATCH_FAIL))
        voice_ledger_release(VRES_STT_UPLOAD);
    return o;
}

static int has(VoiceConvOut o, unsigned bits) { return (o.actions & bits) == bits; }
static int clean(void) { return voice_ledger_all_zero() && voice_ledger_underflow() == 0; }

static void begin(VoiceConv *c, int live)
{
    voice_ledger_reset();
    voice_conv_init(c);
    run(c, VCE_START, live, 0, 0);
}

/* Drives the conversation to a state; returns the clock after it. */
static unsigned long goto_state(VoiceConv *c, VoiceConvState want, int live)
{
    unsigned long t = 0;
    begin(c, live);
    if (want == VCS_LISTENING) return t;
    run(c, VCE_TURN_START, 0, 0, t += 10);
    if (live) run(c, VCE_LINK_READY, 0, 0, t += 10);
    if (want == VCS_HEARING) return t;
    run(c, VCE_TURN_END, 0, 0, t += 100);
    if (want == VCS_ENDING) return t;
    if (live) run(c, VCE_LINK_FINAL, 0, 0, t += 100); else run(c, VCE_BATCH_OK, 0, 0, t += 100);
    run(c, VCE_TRANSCRIPT, VTC_TEXT, 0, t += 10);
    run(c, VCE_SENT, VSS_SENT, 0, t += 10);
    if (want == VCS_WORKING) return t;
    run(c, VCE_CHAT_IDLE, 1, 0, t += 1000);
    return t;   /* SPEAKING */
}

int test_vconv_full_cycle_live(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    begin(&c, 1);
    ASSERT_EQ(c.state, VCS_LISTENING);
    o = run(&c, VCE_TURN_START, 0, 0, 10);
    ASSERT_EQ(c.state, VCS_HEARING);
    ASSERT_TRUE(has(o, VCA_LOCK_TYPING | VCA_OPEN_LINK));
    run(&c, VCE_LINK_READY, 0, 0, 20);
    o = run(&c, VCE_TURN_END, 0, 0, 100);
    ASSERT_EQ(c.state, VCS_ENDING);
    ASSERT_TRUE(has(o, VCA_MODE_HOLD | VCA_END_TURN_AUDIO));
    ASSERT_EQ(o.header, VCH_SENDING);
    o = run(&c, VCE_LINK_FINAL, 0, 0, 200);
    ASSERT_EQ(o.actions, 0u);
    o = run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 210);
    ASSERT_TRUE(has(o, VCA_SEND_TURN));
    ASSERT_EQ(c.state, VCS_ENDING);
    o = run(&c, VCE_SENT, VSS_SENT, 0, 220);
    ASSERT_EQ(c.state, VCS_WORKING);
    ASSERT_TRUE(has(o, VCA_CLOSE_CAPTURE | VCA_CLOSE_LINK | VCA_UNLOCK_TYPING));
    ASSERT_EQ(o.header, VCH_WORKING);
    o = run(&c, VCE_CHAT_IDLE, 1, 0, 2000);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    ASSERT_TRUE(has(o, VCA_SPEAK_REPLY | VCA_OPEN_CAPTURE | VCA_MODE_SPEAK | VCA_STOP_CAPTION));
    ASSERT_EQ(o.header, VCH_SPEAKING);
    o = run(&c, VCE_SPEECH_DONE, 0, 0, 3000);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(has(o, VCA_MODE_LISTEN | VCA_SEND_CAPTION));
    ASSERT_EQ(o.header, VCH_LISTENING);
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 1);
    o = run(&c, VCE_PRESS, 0, 0, 3100);
    ASSERT_EQ(c.state, VCS_IDLE);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_full_cycle_batch(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    begin(&c, 0);
    o = run(&c, VCE_TURN_START, 0, 0, 10);
    ASSERT_FALSE(o.actions & VCA_OPEN_LINK);
    ASSERT_TRUE(has(o, VCA_LOCK_TYPING));
    o = run(&c, VCE_TURN_END, 0, 0, 100);
    ASSERT_TRUE(has(o, VCA_MODE_HOLD | VCA_SUBMIT_BATCH));
    ASSERT_FALSE(o.actions & VCA_END_TURN_AUDIO);
    ASSERT_EQ(o.header, VCH_TRANSCRIBING);
    ASSERT_EQ(voice_conv_next_deadline(&c, 100), -1);
    o = run(&c, VCE_BATCH_OK, 0, 0, 500);
    ASSERT_EQ(o.actions, 0u);
    o = run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 510);
    ASSERT_TRUE(has(o, VCA_SEND_TURN));
    run(&c, VCE_SENT, VSS_SENT, 0, 520);
    ASSERT_EQ(c.state, VCS_WORKING);
    run(&c, VCE_CHAT_IDLE, 1, 0, 2000);
    run(&c, VCE_SPEECH_DONE, 0, 0, 3000);
    ASSERT_EQ(c.state, VCS_LISTENING);
    run(&c, VCE_PRESS, 0, 0, 3100);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_capture_closed_in_working(void)
{
    TEST_BEGIN();
    VoiceConv c;
    goto_state(&c, VCS_WORKING, 1);
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 0);
    run(&c, VCE_CHAT_IDLE, 1, 0, 5000);
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 1);
    TEST_END();
}

int test_vconv_each_send_status(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    static const VoiceSendStatus st[4] = { VSS_BUSY, VSS_NO_SESSION, VSS_FULL, VSS_LAUNCH_FAILED };
    static const VoiceConvNote nt[4] = { VCN_BUSY, VCN_NO_SESSION, VCN_FULL, VCN_LAUNCH };
    int i, ended;
    for (i = 0; i < 4; i++) {
        goto_state(&c, VCS_ENDING, 1);
        run(&c, VCE_LINK_FINAL, 0, 0, 300);
        run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 310);
        o = run(&c, VCE_SENT, (int)st[i], 0, 320);
        ASSERT_EQ(c.state, VCS_SPEAKING);
        ASSERT_TRUE(has(o, VCA_SPEAK_PHRASE | VCA_UNLOCK_TYPING | VCA_MODE_SPEAK));
        ASSERT_EQ(o.phrase, VCPH_NOT_SENT);
        ASSERT_EQ(o.note, nt[i]);
        o = run(&c, VCE_SPEECH_DONE, 0, 0, 400);
        ASSERT_EQ(c.state, VCS_LISTENING);
        ASSERT_EQ(o.note, VCN_NONE);
        run(&c, VCE_PRESS, 0, 0, 500);
        ASSERT_TRUE(clean());
    }
    /* a failed phrase is not a "no speech" note */
    goto_state(&c, VCS_ENDING, 0);
    run(&c, VCE_BATCH_OK, 0, 0, 300);
    run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 310);
    run(&c, VCE_SENT, VSS_BUSY, 0, 320);
    o = run(&c ,VCE_SPEECH_FAILED, 0, 0, 330);
    ASSERT_EQ(o.note, VCN_NONE);
    ASSERT_EQ(c.state, VCS_LISTENING);
    run(&c, VCE_PRESS, 0, 0, 400);
    ASSERT_TRUE(clean());
    ended = 0;
    goto_state(&c, VCS_ENDING, 1);
    run(&c, VCE_LINK_FINAL, 0, 0, 300);
    run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 310);
    o = run(&c, VCE_SENT, VSS_EMPTY, 0, 320);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_EQ(o.header, VCH_DIDNT_CATCH);
    ASSERT_TRUE(has(o, VCA_UNLOCK_TYPING | VCA_MODE_LISTEN));
    (void)ended;
    run(&c, VCE_PRESS, 0, 0, 400);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_send_in_listening_and_hearing(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    begin(&c, 1);
    o = run(&c, VCE_SEND, 0, 0, 10);
    ASSERT_TRUE(has(o, VCA_SEND_TYPED));
    ASSERT_EQ(c.state, VCS_ENDING);
    o = run(&c, VCE_SENT, VSS_SENT, 0, 20);
    ASSERT_EQ(c.state, VCS_WORKING);
    ASSERT_TRUE(has(o, VCA_CLOSE_CAPTURE));
    run(&c, VCE_PRESS, 0, 0, 30);
    ASSERT_TRUE(clean());
    goto_state(&c, VCS_HEARING, 1);
    o = run(&c, VCE_SEND, 0, 0, 500);
    ASSERT_TRUE(has(o, VCA_END_TURN_AUDIO | VCA_MODE_HOLD));
    ASSERT_EQ(c.state, VCS_ENDING);
    run(&c, VCE_PRESS, 0, 0, 600);
    ASSERT_TRUE(clean());
    goto_state(&c, VCS_HEARING, 0);
    o = run(&c, VCE_SEND, 0, 0, 500);
    ASSERT_TRUE(has(o, VCA_SUBMIT_BATCH));
    run(&c, VCE_PRESS, 0, 0, 600);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_final_wait_falls_back(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    begin(&c, 1);
    run(&c, VCE_TURN_START, 0, 0, 10);
    run(&c, VCE_LINK_READY, 0, 0, 20);
    run(&c, VCE_TURN_END, 0, 0, 1000);
    ASSERT_EQ(voice_conv_next_deadline(&c, 1000), 3000);
    o = run(&c, VCE_TICK, 0, 0, 3999);
    ASSERT_EQ(o.actions, 0u);
    o = run(&c, VCE_TICK, 0, 0, 4000);
    ASSERT_TRUE(has(o, VCA_SUBMIT_BATCH | VCA_CLOSE_LINK));
    ASSERT_EQ(o.header, VCH_LIVE_STOPPED);
    ASSERT_EQ(c.state, VCS_ENDING);
    ASSERT_EQ(voice_conv_next_deadline(&c, 4000), -1);
    o = run(&c, VCE_TICK, 0, 0, 9000);
    ASSERT_EQ(o.actions, 0u);
    run(&c, VCE_BATCH_OK, 0, 0, 5000);
    o = run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 5010);
    ASSERT_TRUE(has(o, VCA_SEND_TURN));
    run(&c, VCE_PRESS, 0, 0, 5020);
    ASSERT_TRUE(clean());
    /* never became ready: LIVE_UNAVAILABLE on LINK_FAILED */
    begin(&c, 1);
    run(&c, VCE_TURN_START, 0, 0, 10);
    run(&c, VCE_TURN_END, 0, 0, 100);
    o = run(&c, VCE_LINK_FAILED, 1, 0, 150);
    ASSERT_TRUE(has(o, VCA_SUBMIT_BATCH | VCA_CLOSE_LINK));
    ASSERT_EQ(o.header, VCH_LIVE_UNAVAILABLE);
    run(&c, VCE_BATCH_FAIL, 0, 0, 200);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(voice_conv_typing_locked(&c) == 0);
    run(&c, VCE_PRESS, 0, 0, 300);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_working_ends_on_idle_not_busy(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o; unsigned long t, t0;
    t0 = goto_state(&c, VCS_WORKING, 1);
    ASSERT_EQ(voice_conv_next_deadline(&c, t0), VOICE_CONV_IDLE_POLL_MS);
    for (t = t0; t < t0 + 60000; t += 1000) {
        o = run(&c, VCE_IDLE_POLL, 0, 0, t);
        ASSERT_EQ(o.actions, 0u);
    }
    ASSERT_EQ(c.state, VCS_WORKING);
    t0 = t;
    ASSERT_EQ(run(&c, VCE_IDLE_POLL, 1, 1, t0).actions, 0u);
    ASSERT_EQ(run(&c, VCE_IDLE_POLL, 1, 1, t0 + 1000).actions, 0u);
    ASSERT_EQ(run(&c, VCE_IDLE_POLL, 1, 1, t0 + 2000).actions, 0u);
    o = run(&c, VCE_IDLE_POLL, 1, 1, t0 + 3000);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    ASSERT_TRUE(has(o, VCA_SPEAK_REPLY));
    run(&c, VCE_PRESS, 0, 0, t0 + 3100);   /* SPEAKING: stops speech, keeps going */
    run(&c, VCE_PRESS, 0, 0, t0 + 3200);
    ASSERT_TRUE(clean());
    /* a busy poll in between restarts the three seconds */
    goto_state(&c, VCS_WORKING, 0);
    run(&c, VCE_IDLE_POLL, 1, 1, 10000);
    run(&c, VCE_IDLE_POLL, 1, 1, 12000);
    run(&c, VCE_IDLE_POLL, 0, 0, 12500);
    ASSERT_EQ(run(&c, VCE_IDLE_POLL, 1, 1, 13000).actions, 0u);
    ASSERT_EQ(run(&c, VCE_IDLE_POLL, 1, 1, 15999).actions, 0u);
    ASSERT_EQ(c.state, VCS_WORKING);
    run(&c, VCE_IDLE_POLL, 1, 1, 16000);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    run(&c, VCE_PRESS, 0, 0, 16100);
    run(&c, VCE_PRESS, 0, 0, 16200);
    ASSERT_TRUE(clean());
    /* CHAT_IDLE ends it at once */
    goto_state(&c, VCS_WORKING, 0);
    run(&c, VCE_CHAT_IDLE, 1, 0, 9000);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    run(&c, VCE_PRESS, 0, 0, 9100);
    run(&c, VCE_PRESS, 0, 0, 9200);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_error_reply_phrase(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_WORKING, 1);
    o = run(&c, VCE_CHAT_IDLE, 1, 1, 5000);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    ASSERT_TRUE(has(o, VCA_SPEAK_PHRASE));
    ASSERT_FALSE(o.actions & VCA_SPEAK_REPLY);
    ASSERT_EQ(o.phrase, VCPH_WENT_WRONG);
    o = run(&c, VCE_SPEECH_FAILED, 0, 0, 5100);   /* a phrase: no note */
    ASSERT_EQ(o.note, VCN_NONE);
    run(&c, VCE_PRESS, 0, 0, 5200);
    ASSERT_TRUE(clean());
    /* a reply whose speech failed: the note */
    goto_state(&c, VCS_SPEAKING, 1);
    o = run(&c, VCE_SPEECH_FAILED, 0, 0, 6000);
    ASSERT_EQ(o.note, VCN_NO_SPEECH);
    ASSERT_EQ(c.state, VCS_LISTENING);
    run(&c, VCE_PRESS, 0, 0, 6100);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_no_prose_resumes_listening(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_WORKING, 1);
    o = run(&c, VCE_CHAT_IDLE, 0, 0, 5000);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(has(o, VCA_OPEN_CAPTURE | VCA_MODE_LISTEN));
    ASSERT_EQ(o.header, VCH_LISTENING);
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 1);
    run(&c, VCE_PRESS, 0, 0, 5100);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_card_announced(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    static const VoiceCardKind k[3] = { VCARD_COMMAND, VCARD_WRITE, VCARD_OTHER };
    static const VoiceConvPhrase p[3] = { VCPH_APPROVE_COMMAND, VCPH_APPROVE_WRITE, VCPH_APPROVE_OTHER };
    int i;
    for (i = 0; i < 3; i++) {
        goto_state(&c, VCS_WORKING, 1);
        o = run(&c, VCE_CARD, (int)k[i], 0, 3000);
        ASSERT_TRUE(has(o, VCA_SPEAK_PHRASE));
        ASSERT_EQ(o.phrase, p[i]);
        ASSERT_EQ(o.header, VCH_APPROVAL);
        ASSERT_EQ(c.state, VCS_WORKING);
        ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 0);
        o = run(&c, VCE_SPEECH_DONE, 0, 0, 3500);
        ASSERT_EQ(o.actions, 0u);
        ASSERT_EQ(c.state, VCS_WORKING);
        run(&c, VCE_PRESS, 0, 0, 3600);
        ASSERT_TRUE(clean());
    }
    TEST_END();
}

int test_vconv_card_kind(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_card_kind(NULL), VCARD_COMMAND);
    ASSERT_EQ(voice_card_kind("run_command"), VCARD_COMMAND);
    ASSERT_EQ(voice_card_kind("write_file"), VCARD_WRITE);
    ASSERT_EQ(voice_card_kind("edit_file"), VCARD_WRITE);
    ASSERT_EQ(voice_card_kind("web_fetch"), VCARD_OTHER);
    ASSERT_EQ(voice_card_kind("web_search"), VCARD_OTHER);
    ASSERT_EQ(voice_card_kind("something_else"), VCARD_OTHER);
    ASSERT_EQ(voice_card_kind(""), VCARD_OTHER);
    TEST_END();
}

int test_vconv_speaking_press_escape_stop(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    VoiceConvEvent evs[3] = { VCE_PRESS, VCE_ESCAPE, VCE_STOP };
    int i;
    for (i = 0; i < 3; i++) {
        goto_state(&c, VCS_SPEAKING, 1);
        o = run(&c, evs[i], 0, 0, 9000);
        ASSERT_EQ(c.state, VCS_LISTENING);
        ASSERT_TRUE(has(o, VCA_STOP_SPEECH | VCA_MODE_LISTEN | VCA_SEND_CAPTION));
        ASSERT_FALSE(o.actions & (VCA_ENGINE_STOP | VCA_CLOSE_CAPTURE | VCA_LOCK_UNREG));
        ASSERT_EQ(voice_ledger_count(VRES_SPVOICE), 1);
        run(&c, VCE_PRESS, 0, 0, 9100);
        ASSERT_EQ(c.state, VCS_IDLE);
        ASSERT_TRUE(clean());
    }
    TEST_END();
}

static void ends_ledger_zero_from(VoiceConvState st, int live, int *fails)
{
    VoiceConv c; VoiceConvOut o;
    unsigned long t;
    int i;
    struct { VoiceConvEvent ev; int a; VoiceConvNote note; } cases[] = {
        { VCE_PRESS, 0, VCN_NONE }, { VCE_ESCAPE, 0, VCN_NONE }, { VCE_STOP, 0, VCN_NONE },
        { VCE_END, VCX_PANEL_CLOSED, VCN_NONE }, { VCE_END, VCX_OFF, VCN_NONE },
        { VCE_END, VCX_SETTINGS, VCN_END_SETTINGS }, { VCE_END, VCX_LOCK, VCN_END_LOCK },
        { VCE_END, VCX_CHAT_CHANGED, VCN_END_CHAT }, { VCE_END, VCX_DEVICE_LOST, VCN_NONE },
        { VCE_TICK, 0, VCN_END_SILENCE },
    };
    for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
        t = goto_state(&c, st, live);
        if (cases[i].ev == VCE_TICK) {
            if (st != VCS_LISTENING) continue;
            t += VOICE_CONV_SILENCE_END_MS;
        } else t += 5;
        o = run(&c, cases[i].ev, cases[i].a, 0, t);
        if (st == VCS_SPEAKING && cases[i].ev >= VCE_PRESS && cases[i].ev <= VCE_STOP) {
            if (c.state != VCS_LISTENING) (*fails)++;
            run(&c, VCE_PRESS, 0, 0, t + 1);
            o.note = cases[i].note;
        }
        if (c.state != VCS_IDLE) (*fails)++;
        if (o.note != cases[i].note && !(st == VCS_SPEAKING && cases[i].note == VCN_NONE)) (*fails)++;
        if (!clean()) (*fails)++;
        if (st == VCS_WORKING && cases[i].ev == VCE_ESCAPE && !(o.actions & VCA_CANCEL_WORK)) (*fails)++;
        if (cases[i].ev != VCE_ESCAPE && (o.actions & VCA_CANCEL_WORK)) (*fails)++;
        if (o.actions & VCA_DROP_TURN) (*fails)++;
    }
}

int test_vconv_ends_ledger_zero(void)
{
    TEST_BEGIN();
    int fails = 0, live;
    VoiceConvState sts[5] = { VCS_LISTENING, VCS_HEARING, VCS_ENDING, VCS_WORKING, VCS_SPEAKING };
    int i;
    for (live = 0; live < 2; live++)
        for (i = 0; i < 5; i++)
            ends_ledger_zero_from(sts[i], live, &fails);
    ASSERT_EQ(fails, 0);
    TEST_END();
}

int test_vconv_idle_move_carries_releases(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_HEARING, 1);
    o = run(&c, VCE_PRESS, 0, 0, 500);
    ASSERT_TRUE(has(o, VCA_CLOSE_CAPTURE | VCA_CLOSE_LINK | VCA_UNLOCK_TYPING | VCA_LOCK_UNREG
                       | VCA_ENGINE_STOP | VCA_STOP_SPEECH | VCA_DROP_HELD));
    ASSERT_FALSE(o.actions & (VCA_CANCEL_BATCH | VCA_SEND_CAPTION));
    ASSERT_EQ(o.header, VCH_NONE);
    ASSERT_TRUE(!voice_conv_active(&c));
    goto_state(&c, VCS_ENDING, 0);
    o = run(&c, VCE_ESCAPE, 0, 0, 500);
    ASSERT_TRUE(has(o, VCA_CANCEL_BATCH));
    TEST_END();
}

int test_vconv_escape_cancels_work(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_WORKING, 1);
    o = run(&c, VCE_ESCAPE, 0, 0, 5000);
    ASSERT_TRUE(has(o, VCA_CANCEL_WORK));
    ASSERT_EQ(c.state, VCS_IDLE);
    goto_state(&c, VCS_WORKING, 1);
    o = run(&c, VCE_PRESS, 0, 0, 5000);
    ASSERT_FALSE(o.actions & VCA_CANCEL_WORK);
    ASSERT_EQ(c.state, VCS_IDLE);
    goto_state(&c, VCS_WORKING, 1);
    o = run(&c, VCE_STOP, 0, 0, 5000);
    ASSERT_FALSE(o.actions & VCA_CANCEL_WORK);
    ASSERT_EQ(c.state, VCS_IDLE);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_silence_end(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    begin(&c, 1);
    ASSERT_EQ(voice_conv_next_deadline(&c, 0), 180000);
    ASSERT_EQ(voice_conv_next_deadline(&c, 100000), 80000);
    o = run(&c, VCE_TICK, 0, 0, 179999);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(c.state, VCS_LISTENING);
    o = run(&c, VCE_TICK, 0, 0, 180000);
    ASSERT_EQ(c.state, VCS_IDLE);
    ASSERT_EQ(o.note, VCN_END_SILENCE);
    ASSERT_TRUE(clean());
    /* a CHAT_IDLE at t restarts the count from t */
    goto_state(&c, VCS_WORKING, 1);   /* sent at 40 */
    run(&c, VCE_CHAT_IDLE, 0, 0, 50000);
    ASSERT_EQ(c.state, VCS_LISTENING);
    o = run(&c, VCE_TICK, 0, 0, 50000 + 179999);
    ASSERT_EQ(o.actions, 0u);
    o = run(&c, VCE_TICK, 0, 0, 50000 + 180000);
    ASSERT_EQ(o.note, VCN_END_SILENCE);
    ASSERT_EQ(c.state, VCS_IDLE);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_typing_lock(void)
{
    TEST_BEGIN();
    VoiceConv c;
    VoiceConvState sts[5] = { VCS_LISTENING, VCS_HEARING, VCS_ENDING, VCS_WORKING, VCS_SPEAKING };
    int want[5] = { 0, 1, 1, 0, 0 };
    int i;
    for (i = 0; i < 5; i++) {
        goto_state(&c, sts[i], 1);
        ASSERT_EQ(c.state, sts[i]);
        ASSERT_EQ(voice_conv_typing_locked(&c), want[i]);
    }
    TEST_END();
}

int test_vconv_stale_events_ignored(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_ENDING, 1);
    o = run(&c, VCE_CHAT_IDLE, 1, 0, 300);
    ASSERT_EQ(o.actions, 0u); ASSERT_EQ(c.state, VCS_ENDING);
    o = run(&c, VCE_IDLE_POLL, 1, 1, 300);
    ASSERT_EQ(o.actions, 0u); ASSERT_EQ(c.state, VCS_ENDING);
    o = run(&c, VCE_SPEECH_DONE, 0, 0, 300);
    ASSERT_EQ(o.actions, 0u); ASSERT_EQ(c.state, VCS_ENDING);
    run(&c, VCE_PRESS, 0, 0, 400);
    ASSERT_TRUE(clean());
    begin(&c, 1);
    o = run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 10);
    ASSERT_EQ(o.actions, 0u); ASSERT_EQ(c.state, VCS_LISTENING);
    o = run(&c, VCE_SENT, VSS_SENT, 0, 10);
    ASSERT_EQ(o.actions, 0u); ASSERT_EQ(c.state, VCS_LISTENING);
    o = run(&c, VCE_LINK_READY, 0, 0, 10);
    ASSERT_EQ(o.actions, 0u);
    o = run(&c, VCE_TURN_END, 0, 0, 10);
    ASSERT_EQ(o.actions, 0u); ASSERT_EQ(c.state, VCS_LISTENING);
    o = run(&c, VCE_BATCH_OK, 0, 0, 10);
    ASSERT_EQ(o.actions, 0u);
    run(&c, VCE_PRESS, 0, 0, 20);
    ASSERT_TRUE(clean());
    /* IDLE */
    voice_conv_init(&c);
    o = run(&c, VCE_SENT, VSS_SENT, 0, 10);
    ASSERT_EQ(o.actions, 0u); ASSERT_EQ(c.state, VCS_IDLE);
    o = run(&c, VCE_PRESS, 0, 0, 10);
    ASSERT_EQ(o.actions, 0u);
    o = run(&c, VCE_END, VCX_OFF, 0, 10);
    ASSERT_EQ(o.actions, 0u); ASSERT_EQ(o.note, VCN_NONE);
    o = run(&c, VCE_TICK, 0, 0, 10000000);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(voice_conv_next_deadline(&c, 5), -1);
    /* WORKING */
    goto_state(&c, VCS_WORKING, 1);
    o = run(&c, VCE_LINK_FINAL, 0, 0, 3000);
    ASSERT_EQ(o.actions, 0u); ASSERT_EQ(c.state, VCS_WORKING);
    o = run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 3000);
    ASSERT_EQ(o.actions, 0u);
    o = run(&c, VCE_SENT, VSS_SENT, 0, 3000);
    ASSERT_EQ(o.actions, 0u);
    o = run(&c, VCE_TURN_START, 0, 0, 3000);
    ASSERT_EQ(o.actions, 0u);
    run(&c, VCE_PRESS, 0, 0, 3100);
    ASSERT_TRUE(clean());
    /* a second START while active changes nothing */
    begin(&c, 1);
    o = run(&c, VCE_START, 1, 0, 5);
    ASSERT_EQ(o.actions, 0u);
    run(&c, VCE_PRESS, 0, 0, 6);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_press_press_ledger_zero(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    begin(&c, 1);
    ASSERT_EQ(voice_ledger_count(VRES_SPVOICE), 1);
    ASSERT_EQ(voice_ledger_count(VRES_SESSION_LOCK), 1);
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 1);
    ASSERT_TRUE(voice_conv_active(&c));
    o = run(&c, VCE_PRESS, 0, 0, 0);
    ASSERT_EQ(c.state, VCS_IDLE);
    ASSERT_TRUE(clean());
    o = run(&c, VCE_PRESS, 0, 0, 0);   /* the second press is nothing */
    ASSERT_EQ(o.actions, 0u);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_start_actions(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    voice_ledger_reset();
    voice_conv_init(&c);
    ASSERT_EQ(c.state, VCS_IDLE);
    o = run(&c, VCE_START, 1, 0, 777);
    ASSERT_EQ(o.actions, VCA_ENGINE_START | VCA_LOCK_REG | VCA_OPEN_CAPTURE | VCA_MODE_LISTEN);
    ASSERT_EQ(o.header, VCH_LISTENING);
    ASSERT_EQ(c.sent_at, 777ul);
    ASSERT_EQ(c.idle_at, 777ul);
    run(&c, VCE_PRESS, 0, 0, 800);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_hearing_abandon_and_empty_and_fail(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_HEARING, 1);
    o = run(&c, VCE_TURN_ABANDON, 0, 0, 200);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(has(o, VCA_DROP_TURN | VCA_CLOSE_LINK | VCA_UNLOCK_TYPING));
    ASSERT_EQ(voice_ledger_count(VRES_LIVE_SESSION), 0);
    run(&c, VCE_PRESS, 0, 0, 300);
    ASSERT_TRUE(clean());
    /* empty transcript */
    goto_state(&c, VCS_ENDING, 1);
    run(&c, VCE_LINK_FINAL, 0, 0, 300);
    o = run(&c, VCE_TRANSCRIPT, VTC_EMPTY, 0, 310);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(has(o, VCA_DROP_TURN | VCA_UNLOCK_TYPING | VCA_MODE_LISTEN | VCA_CLOSE_LINK));
    ASSERT_EQ(o.header, VCH_DIDNT_CATCH);
    run(&c, VCE_PRESS, 0, 0, 400);
    ASSERT_TRUE(clean());
    /* ECHO/REPEAT/CONTINUE behave as TEXT until Task 8 */
    goto_state(&c, VCS_ENDING, 0);
    run(&c, VCE_BATCH_OK, 0, 0, 300);
    o = run(&c, VCE_TRANSCRIPT, VTC_ECHO, 0, 310);
    ASSERT_TRUE(has(o, VCA_SEND_TURN));
    run(&c, VCE_PRESS, 0, 0, 400);
    ASSERT_TRUE(clean());
    /* batch failure */
    goto_state(&c, VCS_ENDING, 0);
    o = run(&c, VCE_BATCH_FAIL, 0, 0, 300);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(has(o, VCA_UNLOCK_TYPING | VCA_MODE_LISTEN));
    ASSERT_FALSE(o.actions & VCA_DROP_TURN);
    run(&c, VCE_PRESS, 0, 0, 400);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_texts(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(voice_conv_header_text(VCH_NONE), "");
    ASSERT_STR_EQ(voice_conv_header_text(VCH_LISTENING), "Listening\xE2\x80\xA6 speak, then pause to send");
    ASSERT_STR_EQ(voice_conv_header_text(VCH_HEARING), "Hearing you\xE2\x80\xA6");
    ASSERT_STR_EQ(voice_conv_header_text(VCH_SENDING), "Sending\xE2\x80\xA6");
    ASSERT_STR_EQ(voice_conv_header_text(VCH_TRANSCRIBING), "Transcribing\xE2\x80\xA6");
    ASSERT_STR_EQ(voice_conv_header_text(VCH_WORKING), "Working\xE2\x80\xA6 press Escape to stop");
    ASSERT_STR_EQ(voice_conv_header_text(VCH_SPEAKING), "Speaking\xE2\x80\xA6 talk to cut in");
    ASSERT_STR_EQ(voice_conv_header_text(VCH_PAUSED_BG), "Paused while Nutshell is in the background");
    ASSERT_STR_EQ(voice_conv_header_text(VCH_PAUSED_DIALOG), "Paused while a dialog is open");
    ASSERT_STR_EQ(voice_conv_header_text(VCH_APPROVAL), "Waiting for your approval");
    ASSERT_STR_EQ(voice_conv_header_text(VCH_DIDNT_CATCH), "Didn't catch that");
    ASSERT_STR_EQ(voice_conv_header_text(VCH_IGNORED_ECHO), "Ignored the assistant's own voice");
    ASSERT_STR_EQ(voice_conv_header_text(VCH_REPEATING), "Repeating");
    ASSERT_STR_EQ(voice_conv_header_text(VCH_CARRYING_ON), "Carrying on");
    ASSERT_STR_EQ(voice_conv_header_text(VCH_LIVE_UNAVAILABLE),
                  "Live text is unavailable: the words will appear when you stop");
    ASSERT_STR_EQ(voice_conv_header_text(VCH_LIVE_STOPPED),
                  "Live text stopped: the rest will appear when you stop");
    ASSERT_STR_EQ(voice_conv_header_text((VoiceConvHeader)99), "");
    ASSERT_STR_EQ(voice_conv_note_text(VCN_NONE), "");
    ASSERT_STR_EQ(voice_conv_note_text(VCN_END_SETTINGS), "Conversation ended: the voice settings changed");
    ASSERT_STR_EQ(voice_conv_note_text(VCN_END_LOCK), "Conversation ended: Windows was locked");
    ASSERT_STR_EQ(voice_conv_note_text(VCN_END_SILENCE), "Conversation ended: nothing said for 3 minutes");
    ASSERT_STR_EQ(voice_conv_note_text(VCN_END_CHAT), "Conversation ended: the chat changed");
    ASSERT_STR_EQ(voice_conv_note_text(VCN_BUSY), "The assistant is busy");
    ASSERT_STR_EQ(voice_conv_note_text(VCN_NO_SESSION), "Connect a session first");
    ASSERT_STR_EQ(voice_conv_note_text(VCN_FULL), "The conversation is full: start a New Chat");
    ASSERT_STR_EQ(voice_conv_note_text(VCN_LAUNCH), "The request could not start");
    ASSERT_STR_EQ(voice_conv_note_text(VCN_NO_SPEECH), "Could not speak the reply: it is on screen");
    ASSERT_STR_EQ(voice_conv_phrase_text(VCPH_NONE), "");
    ASSERT_STR_EQ(voice_conv_phrase_text(VCPH_APPROVE_COMMAND), "I need your approval to run a command.");
    ASSERT_STR_EQ(voice_conv_phrase_text(VCPH_APPROVE_WRITE), "I need your approval to write a file.");
    ASSERT_STR_EQ(voice_conv_phrase_text(VCPH_APPROVE_OTHER), "I need your approval for the next step.");
    ASSERT_STR_EQ(voice_conv_phrase_text(VCPH_NOT_SENT), "I couldn't send that. The details are on screen.");
    ASSERT_STR_EQ(voice_conv_phrase_text(VCPH_WENT_WRONG), "Something went wrong. The details are on screen.");
    TEST_END();
}
