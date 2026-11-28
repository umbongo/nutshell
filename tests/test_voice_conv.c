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
    ASSERT_EQ(voice_conv_next_deadline(&c, 100), VOICE_CONV_ENDING_MAX_MS);
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
    int i;
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
    goto_state(&c, VCS_ENDING, 1);
    run(&c, VCE_LINK_FINAL, 0, 0, 300);
    run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 310);
    o = run(&c, VCE_SENT, VSS_EMPTY, 0, 320);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_EQ(o.header, VCH_DIDNT_CATCH);
    ASSERT_TRUE(has(o, VCA_UNLOCK_TYPING | VCA_MODE_LISTEN));
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
    ASSERT_EQ(voice_conv_next_deadline(&c, 4000), VOICE_CONV_ENDING_MAX_MS - 3000);
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
    /* a TEXT transcript from a batch turn sends */
    goto_state(&c, VCS_ENDING, 0);
    run(&c, VCE_BATCH_OK, 0, 0, 300);
    o = run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 310);
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

int test_vconv_silence_restarts_after_speaking(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_SPEAKING, 1);
    run(&c, VCE_SPEECH_DONE, 0, 0, 300000);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_EQ(run(&c, VCE_TICK, 0, 0, 300001).actions, 0u);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_EQ(voice_conv_next_deadline(&c, 300000), 180000);
    o = run(&c, VCE_TICK, 0, 0, 480000);
    ASSERT_EQ(c.state, VCS_IDLE);
    ASSERT_EQ(o.note, VCN_END_SILENCE);
    ASSERT_TRUE(clean());
    /* SPEECH_FAILED and a stop restart it too */
    goto_state(&c, VCS_SPEAKING, 0);
    run(&c, VCE_SPEECH_FAILED, 0, 0, 300000);
    ASSERT_EQ(voice_conv_next_deadline(&c, 300000), 180000);
    run(&c, VCE_PRESS, 0, 0, 300001);
    goto_state(&c, VCS_SPEAKING, 0);
    run(&c, VCE_ESCAPE, 0, 0, 300000);
    ASSERT_EQ(voice_conv_next_deadline(&c, 300000), 180000);
    run(&c, VCE_PRESS, 0, 0, 300001);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_noise_does_not_extend_silence(void)
{
    TEST_BEGIN();
    VoiceConv c; unsigned long t;
    begin(&c, 0);
    for (t = 10000; t < 100000; t += 20000) {
        run(&c, VCE_TURN_START, 0, 0, t);
        run(&c, VCE_TURN_ABANDON, 0, 0, t + 100);
    }
    run(&c, VCE_TURN_START, 0, 0, 100000);
    run(&c, VCE_TURN_END, 0, 0, 100100);
    run(&c, VCE_BATCH_OK, 0, 0, 100200);
    run(&c, VCE_TRANSCRIPT, VTC_EMPTY, 0, 100300);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_EQ(voice_conv_next_deadline(&c, 100300), 180000 - 100300);
    run(&c, VCE_TICK, 0, 0, 179999);
    ASSERT_EQ(c.state, VCS_LISTENING);
    run(&c, VCE_TICK, 0, 0, 180000);
    ASSERT_EQ(c.state, VCS_IDLE);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_link_failed_while_hearing(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_HEARING, 1);   /* link ready */
    o = run(&c, VCE_LINK_FAILED, 0, 0, 200);
    ASSERT_TRUE(has(o, VCA_CLOSE_LINK));
    ASSERT_EQ(o.header, VCH_LIVE_STOPPED);
    ASSERT_EQ(c.state, VCS_HEARING);
    o = run(&c, VCE_TURN_END, 0, 0, 300);
    ASSERT_TRUE(has(o, VCA_SUBMIT_BATCH | VCA_MODE_HOLD));
    ASSERT_FALSE(o.actions & VCA_END_TURN_AUDIO);
    ASSERT_EQ(voice_conv_next_deadline(&c, 300), VOICE_CONV_ENDING_MAX_MS);
    run(&c, VCE_BATCH_OK, 0, 0, 400);
    run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 410);
    run(&c, VCE_SENT, VSS_SENT, 0, 420);
    run(&c, VCE_PRESS, 0, 0, 430);
    ASSERT_TRUE(clean());
    /* never ready, then Send */
    begin(&c, 1);
    run(&c, VCE_TURN_START, 0, 0, 10);
    o = run(&c, VCE_LINK_FAILED, 1, 0, 20);
    ASSERT_EQ(o.header, VCH_LIVE_UNAVAILABLE);
    o = run(&c, VCE_SEND, 0, 0, 30);
    ASSERT_TRUE(has(o, VCA_SUBMIT_BATCH));
    run(&c, VCE_PRESS, 0, 0, 40);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_sending_guards(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_ENDING, 1);
    run(&c, VCE_LINK_FINAL, 0, 0, 300);
    /* SENT before a send was handed over: ignored */
    o = run(&c, VCE_SENT, VSS_SENT, 0, 305);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(c.state, VCS_ENDING);
    o = run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 310);
    ASSERT_TRUE(has(o, VCA_SEND_TURN));
    o = run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 311);
    ASSERT_EQ(o.actions, 0u);
    o = run(&c, VCE_TRANSCRIPT, VTC_EMPTY, 0, 312);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(c.state, VCS_ENDING);
    run(&c, VCE_SENT, VSS_SENT, 0, 320);
    ASSERT_EQ(c.state, VCS_WORKING);
    run(&c, VCE_PRESS, 0, 0, 330);
    ASSERT_TRUE(clean());
    TEST_END();
}

/* ---- Task 8: pause, held replies, cut-in, echo and repeat ---- */

int test_vconv_pause_closes_capture_and_back(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    begin(&c, 1);
    o = run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 100);
    ASSERT_TRUE(has(o, VCA_CLOSE_CAPTURE));
    ASSERT_EQ(o.header, VCH_PAUSED_BG);
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 0);
    ASSERT_EQ(c.state, VCS_LISTENING);
    o = run(&c, VCE_RESUME, 0, 0, 200);
    ASSERT_TRUE(has(o, VCA_OPEN_CAPTURE | VCA_MODE_LISTEN));
    ASSERT_EQ(o.header, VCH_LISTENING);
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 1);
    ASSERT_EQ(run(&c, VCE_RESUME, 0, 0, 210).actions, 0u);   /* not paused: nothing */
    run(&c, VCE_PRESS, 0, 0, 300);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_pause_dialog_header(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    begin(&c, 0);
    o = run(&c, VCE_PAUSE, VCP_DIALOG, 0, 100);
    ASSERT_EQ(o.header, VCH_PAUSED_DIALOG);
    /* a second PAUSE only changes the reason */
    o = run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 110);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(o.header, VCH_PAUSED_BG);
    /* turn events and cut-in events are ignored while paused */
    ASSERT_EQ(run(&c, VCE_TURN_START, 0, 0, 120).actions, 0u);
    ASSERT_EQ(run(&c, VCE_DUCK, 0, 0, 120).actions, 0u);
    ASSERT_EQ(run(&c, VCE_CUT, 0, 0, 120).actions, 0u);
    ASSERT_EQ(c.state, VCS_LISTENING);
    run(&c, VCE_PRESS, 0, 0, 200);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_pause_drops_heard_turn(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_HEARING, 1);
    o = run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 500);
    ASSERT_TRUE(has(o, VCA_DROP_TURN | VCA_CLOSE_LINK | VCA_UNLOCK_TYPING | VCA_CLOSE_CAPTURE));
    ASSERT_EQ(voice_ledger_count(VRES_LIVE_SESSION), 0);
    ASSERT_FALSE(voice_conv_typing_locked(&c));
    o = run(&c, VCE_RESUME, 0, 0, 600);
    ASSERT_FALSE(o.actions & (VCA_SEND_TURN | VCA_SEND_TYPED | VCA_SUBMIT_BATCH));
    ASSERT_EQ(c.state, VCS_LISTENING);
    run(&c, VCE_PRESS, 0, 0, 700);
    ASSERT_TRUE(clean());
    /* ENDING with a batch upload running: cancelled */
    goto_state(&c, VCS_ENDING, 0);
    o = run(&c, VCE_PAUSE, VCP_DIALOG, 0, 500);
    ASSERT_TRUE(has(o, VCA_DROP_TURN | VCA_CANCEL_BATCH | VCA_UNLOCK_TYPING));
    ASSERT_EQ(voice_ledger_count(VRES_STT_UPLOAD), 0);
    /* its late result is ignored */
    ASSERT_EQ(run(&c, VCE_BATCH_OK, 0, 0, 510).actions, 0u);
    ASSERT_EQ(run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 520).actions, 0u);
    run(&c, VCE_PRESS, 0, 0, 700);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_held_reply_spoken_under_30s(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_WORKING, 1);
    o = run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 2000);
    ASSERT_EQ(o.actions, (unsigned)VCA_STOP_SPEECH);   /* an approval phrase being spoken stops */
    ASSERT_EQ(o.header, VCH_PAUSED_BG);
    o = run(&c, VCE_CHAT_IDLE, 1, 0, 5000);
    ASSERT_TRUE(has(o, VCA_HOLD_REPLY));
    ASSERT_FALSE(o.actions & (VCA_SPEAK_REPLY | VCA_OPEN_CAPTURE));
    ASSERT_EQ(c.state, VCS_LISTENING);
    o = run(&c, VCE_RESUME, 0, 0, 5000 + 29999);
    ASSERT_TRUE(has(o, VCA_SPEAK_REPLY | VCA_OPEN_CAPTURE | VCA_MODE_SPEAK | VCA_STOP_CAPTION));
    ASSERT_FALSE(o.actions & VCA_SPEAK_CONTINUE);
    ASSERT_EQ(o.header, VCH_SPEAKING);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    run(&c, VCE_SPEECH_DONE, 0, 0, 40000);
    run(&c, VCE_PRESS, 0, 0, 40100);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_held_reply_dropped_at_30s(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_WORKING, 1);
    run(&c, VCE_PAUSE, VCP_DIALOG, 0, 2000);
    run(&c, VCE_CHAT_IDLE, 1, 0, 5000);
    o = run(&c, VCE_RESUME, 0, 0, 5000 + 30000);
    ASSERT_TRUE(has(o, VCA_DROP_HELD | VCA_OPEN_CAPTURE | VCA_MODE_LISTEN));
    ASSERT_FALSE(o.actions & (VCA_SPEAK_REPLY | VCA_SPEAK_CONTINUE));
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_EQ(o.header, VCH_LISTENING);
    /* nothing is held a second time */
    run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 36000);
    o = run(&c, VCE_RESUME, 0, 0, 36100);
    ASSERT_FALSE(o.actions & (VCA_DROP_HELD | VCA_SPEAK_REPLY));
    run(&c, VCE_PRESS, 0, 0, 37000);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_held_error_phrase_and_no_prose(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_WORKING, 1);
    run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 2000);
    o = run(&c, VCE_CHAT_IDLE, 1, 1, 5000);
    ASSERT_TRUE(has(o, VCA_HOLD_REPLY));
    o = run(&c, VCE_RESUME, 0, 0, 6000);
    ASSERT_TRUE(has(o, VCA_SPEAK_PHRASE));
    ASSERT_EQ(o.phrase, VCPH_WENT_WRONG);
    run(&c, VCE_SPEECH_DONE, 0, 0, 7000);
    run(&c, VCE_PRESS, 0, 0, 7100);
    ASSERT_TRUE(clean());
    /* no prose: nothing held, resume listens */
    goto_state(&c, VCS_WORKING, 0);
    run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 2000);
    o = run(&c, VCE_CHAT_IDLE, 0, 0, 5000);
    ASSERT_FALSE(o.actions & VCA_HOLD_REPLY);
    o = run(&c, VCE_RESUME, 0, 0, 5100);
    ASSERT_TRUE(has(o, VCA_OPEN_CAPTURE | VCA_MODE_LISTEN));
    ASSERT_FALSE(o.actions & (VCA_SPEAK_REPLY | VCA_DROP_HELD));
    run(&c, VCE_PRESS, 0, 0, 5200);
    ASSERT_TRUE(clean());
    /* the safety poll holds the same way */
    goto_state(&c, VCS_WORKING, 0);
    run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 2000);
    run(&c, VCE_IDLE_POLL, 1, 1, 3000);
    o = run(&c, VCE_IDLE_POLL, 1, 1, 6000);
    ASSERT_TRUE(has(o, VCA_HOLD_REPLY));
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 0);
    run(&c, VCE_PRESS, 0, 0, 6100);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_speaking_paused_continues(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_SPEAKING, 1);
    o = run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 9000);
    ASSERT_TRUE(has(o, VCA_STOP_SPEECH | VCA_HOLD_REPLY | VCA_SEND_CAPTION | VCA_CLOSE_CAPTURE));
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 0);
    o = run(&c, VCE_RESUME, 0, 0, 9000 + 29999);
    ASSERT_TRUE(has(o, VCA_SPEAK_CONTINUE | VCA_OPEN_CAPTURE | VCA_STOP_CAPTION));
    ASSERT_FALSE(o.actions & VCA_SPEAK_REPLY);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    run(&c, VCE_SPEECH_DONE, 0, 0, 50000);
    ASSERT_EQ(c.state, VCS_LISTENING);
    run(&c, VCE_PRESS, 0, 0, 50100);
    ASSERT_TRUE(clean());
    /* a phrase being spoken is stopped, not held */
    goto_state(&c, VCS_WORKING, 1);
    run(&c, VCE_CHAT_IDLE, 1, 1, 4000);
    o = run(&c, VCE_PAUSE, VCP_DIALOG, 0, 4100);
    ASSERT_TRUE(has(o, VCA_STOP_SPEECH));
    ASSERT_FALSE(o.actions & VCA_HOLD_REPLY);
    o = run(&c, VCE_RESUME, 0, 0, 4200);
    ASSERT_FALSE(o.actions & (VCA_SPEAK_CONTINUE | VCA_SPEAK_PHRASE));
    run(&c, VCE_PRESS, 0, 0, 4300);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_working_continues_while_paused(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_WORKING, 1);
    o = run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 2000);
    ASSERT_EQ(o.actions, (unsigned)VCA_STOP_SPEECH);
    ASSERT_EQ(c.state, VCS_WORKING);
    run(&c, VCE_CHAT_IDLE, 1, 0, 3000);
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 0);
    ASSERT_EQ(c.state, VCS_LISTENING);
    run(&c, VCE_RESUME, 0, 0, 3100);
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 1);
    ASSERT_EQ(c.state, VCS_SPEAKING);   /* the held reply is spoken */
    run(&c, VCE_PRESS, 0, 0, 3200);
    run(&c, VCE_PRESS, 0, 0, 3210);
    ASSERT_TRUE(clean());
    /* RESUME while still working: header only */
    goto_state(&c, VCS_WORKING, 1);
    run(&c, VCE_PAUSE, VCP_DIALOG, 0, 2000);
    o = run(&c, VCE_RESUME, 0, 0, 2100);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(o.header, VCH_WORKING);
    ASSERT_EQ(c.state, VCS_WORKING);
    /* a paused WORKING never ends by the silence tick */
    run(&c, VCE_PAUSE, VCP_DIALOG, 0, 2200);
    ASSERT_EQ(run(&c, VCE_TICK, 0, 0, 900000).actions, 0u);
    ASSERT_EQ(c.state, VCS_WORKING);
    run(&c, VCE_ESCAPE, 0, 0, 900100);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_cutin_aec(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_SPEAKING, 1);
    o = run(&c, VCE_DUCK, 0, 0, 5000);
    ASSERT_EQ(o.actions, (unsigned)VCA_DUCK);
    o = run(&c, VCE_TURN_START, 0, 0, 5010);
    ASSERT_TRUE(has(o, VCA_LOCK_TYPING | VCA_OPEN_LINK));
    ASSERT_EQ(c.state, VCS_SPEAKING);
    ASSERT_TRUE(voice_conv_typing_locked(&c));
    ASSERT_EQ(voice_ledger_count(VRES_LIVE_SESSION), 1);
    /* a second TURN_START while a turn is open is nothing */
    ASSERT_EQ(run(&c, VCE_TURN_START, 0, 0, 5020).actions, 0u);
    o = run(&c, VCE_CUT, 0, 0, 5200);
    ASSERT_EQ(c.state, VCS_HEARING);
    ASSERT_TRUE(has(o, VCA_STOP_SPEECH | VCA_SEND_CAPTION));
    ASSERT_EQ(o.header, VCH_HEARING);
    ASSERT_FALSE(o.actions & VCA_OPEN_LINK);
    o = run(&c, VCE_TURN_END, 0, 0, 7000);
    ASSERT_EQ(c.state, VCS_ENDING);
    ASSERT_TRUE(has(o, VCA_END_TURN_AUDIO));
    run(&c, VCE_LINK_FINAL, 0, 0, 7100);
    run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 7110);
    run(&c, VCE_SENT, VSS_SENT, 0, 7120);
    ASSERT_EQ(c.state, VCS_WORKING);
    run(&c, VCE_PRESS, 0, 0, 7200);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_cutin_cough(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_SPEAKING, 1);
    run(&c, VCE_DUCK, 0, 0, 5000);
    run(&c, VCE_TURN_START, 0, 0, 5010);
    o = run(&c, VCE_UNDUCK, 0, 0, 5100);
    ASSERT_EQ(o.actions, (unsigned)VCA_UNDUCK);
    o = run(&c, VCE_TURN_ABANDON, 0, 0, 5110);
    ASSERT_TRUE(has(o, VCA_DROP_TURN | VCA_CLOSE_LINK | VCA_UNLOCK_TYPING));
    ASSERT_EQ(c.state, VCS_SPEAKING);
    ASSERT_EQ(voice_ledger_count(VRES_LIVE_SESSION), 0);
    ASSERT_FALSE(voice_conv_typing_locked(&c));
    /* the reply carries on and ends normally */
    o = run(&c, VCE_SPEECH_DONE, 0, 0, 9000);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_EQ(o.header, VCH_LISTENING);
    run(&c, VCE_PRESS, 0, 0, 9100);
    ASSERT_TRUE(clean());
    /* abandon with no turn open (half-duplex cough) is nothing */
    goto_state(&c, VCS_SPEAKING, 0);
    run(&c, VCE_DUCK, 0, 0, 5000);
    ASSERT_EQ(run(&c, VCE_TURN_ABANDON, 0, 0, 5100).actions, 0u);
    ASSERT_EQ(run(&c, VCE_UNDUCK, 0, 0, 5100).actions, (unsigned)VCA_UNDUCK);
    run(&c, VCE_PRESS, 0, 0, 5200);
    run(&c, VCE_PRESS, 0, 0, 5300);
    ASSERT_TRUE(clean());
    /* the speech ending while a cut-in turn is open: the turn is the one heard */
    goto_state(&c, VCS_SPEAKING, 1);
    run(&c, VCE_TURN_START, 0, 0, 5010);
    o = run(&c, VCE_SPEECH_DONE, 0, 0, 5020);
    ASSERT_EQ(c.state, VCS_HEARING);
    ASSERT_EQ(o.header, VCH_HEARING);
    run(&c, VCE_TURN_END, 0, 0, 6000);
    ASSERT_EQ(c.state, VCS_ENDING);
    run(&c, VCE_PRESS, 0, 0, 6100);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_cutin_hd(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_SPEAKING, 1);
    run(&c, VCE_DUCK, 0, 0, 5000);
    o = run(&c, VCE_CUT, 0, 0, 5200);
    ASSERT_EQ(c.state, VCS_HEARING);
    ASSERT_TRUE(has(o, VCA_STOP_SPEECH | VCA_SEND_CAPTION));
    ASSERT_EQ(voice_ledger_count(VRES_LIVE_SESSION), 0);   /* no link yet */
    ASSERT_FALSE(voice_conv_typing_locked(&c));
    o = run(&c, VCE_TURN_START, 0, 0, 5210);
    ASSERT_TRUE(has(o, VCA_LOCK_TYPING | VCA_OPEN_LINK));
    ASSERT_EQ(c.state, VCS_HEARING);
    ASSERT_EQ(voice_ledger_count(VRES_LIVE_SESSION), 1);
    run(&c, VCE_PRESS, 0, 0, 5300);
    ASSERT_TRUE(clean());
    /* batch: TURN_START after CUT opens no link */
    goto_state(&c, VCS_SPEAKING, 0);
    run(&c, VCE_CUT, 0, 0, 5200);
    o = run(&c, VCE_TURN_START, 0, 0, 5210);
    ASSERT_FALSE(o.actions & VCA_OPEN_LINK);
    ASSERT_TRUE(has(o, VCA_LOCK_TYPING));
    run(&c, VCE_PRESS, 0, 0, 5300);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_echo_dropped(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_ENDING, 1);
    run(&c, VCE_LINK_FINAL, 0, 0, 300);
    o = run(&c, VCE_TRANSCRIPT, VTC_ECHO, 0, 310);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(has(o, VCA_DROP_TURN | VCA_UNLOCK_TYPING | VCA_MODE_LISTEN | VCA_CLOSE_LINK));
    ASSERT_FALSE(o.actions & VCA_SEND_TURN);
    ASSERT_EQ(o.header, VCH_IGNORED_ECHO);
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 1);
    run(&c, VCE_PRESS, 0, 0, 400);
    ASSERT_TRUE(clean());
    TEST_END();
}

static void local_phrase_case(VoiceTurnClass cls, unsigned want, VoiceConvHeader hdr, int live)
{
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_ENDING, live);
    if (live) run(&c, VCE_LINK_FINAL, 0, 0, 300); else run(&c, VCE_BATCH_OK, 0, 0, 300);
    o = run(&c, VCE_TRANSCRIPT, (int)cls, 0, 310);
    if (c.state != VCS_SPEAKING || !has(o, want | VCA_DROP_TURN | VCA_UNLOCK_TYPING | VCA_MODE_SPEAK
                                           | VCA_STOP_CAPTION)
        || o.header != hdr || (o.actions & VCA_SEND_TURN) || !has(o, 0))
        voice_ledger_acquire(VRES_RENDER);   /* mark failure: the caller asserts the ledger */
    run(&c, VCE_SPEECH_DONE, 0, 0, 400);
    if (c.state != VCS_LISTENING) voice_ledger_acquire(VRES_RENDER);
    run(&c, VCE_PRESS, 0, 0, 500);
}

int test_vconv_repeat_local(void)
{
    TEST_BEGIN();
    local_phrase_case(VTC_REPEAT, VCA_SPEAK_REPEAT, VCH_REPEATING, 1);
    ASSERT_TRUE(clean());
    local_phrase_case(VTC_REPEAT, VCA_SPEAK_REPEAT, VCH_REPEATING, 0);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_continue_local(void)
{
    TEST_BEGIN();
    local_phrase_case(VTC_CONTINUE, VCA_SPEAK_CONTINUE, VCH_CARRYING_ON, 1);
    ASSERT_TRUE(clean());
    local_phrase_case(VTC_CONTINUE, VCA_SPEAK_CONTINUE, VCH_CARRYING_ON, 0);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_paused_ends_ledger_zero(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    struct { VoiceConvEvent ev; int a; VoiceConvNote note; } cases[] = {
        { VCE_PRESS, 0, VCN_NONE }, { VCE_ESCAPE, 0, VCN_NONE }, { VCE_STOP, 0, VCN_NONE },
        { VCE_END, VCX_PANEL_CLOSED, VCN_NONE }, { VCE_END, VCX_OFF, VCN_NONE },
        { VCE_END, VCX_SETTINGS, VCN_END_SETTINGS }, { VCE_END, VCX_LOCK, VCN_END_LOCK },
        { VCE_END, VCX_CHAT_CHANGED, VCN_END_CHAT }, { VCE_END, VCX_DEVICE_LOST, VCN_NONE },
        { VCE_TICK, 0, VCN_END_SILENCE },
    };
    VoiceConvState sts[5] = { VCS_LISTENING, VCS_HEARING, VCS_ENDING, VCS_SPEAKING, VCS_WORKING };
    int i, s, live, held;
    for (live = 0; live < 2; live++)
    for (s = 0; s < 5; s++)
    for (held = 0; held < 2; held++)
    for (i = 0; i < 10; i++) {
        unsigned long t = goto_state(&c, sts[s], live);
        run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, t + 1);
        if (held && sts[s] == VCS_WORKING) run(&c, VCE_CHAT_IDLE, 1, 0, t + 2);
        if (cases[i].ev == VCE_TICK) {
            if (c.state != VCS_LISTENING) continue;
            t += VOICE_CONV_SILENCE_END_MS;
        }
        o = run(&c, cases[i].ev, cases[i].a, 0, t + 10);
        ASSERT_EQ(c.state, VCS_IDLE);
        ASSERT_EQ(o.note, cases[i].note);
        ASSERT_TRUE(has(o, VCA_DROP_HELD));
        ASSERT_TRUE(clean());
    }
    /* a paused silence end counts from the later of send and idle */
    begin(&c, 0);
    run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 1000);
    ASSERT_EQ(run(&c, VCE_TICK, 0, 0, 179999).actions, 0u);
    ASSERT_EQ(c.state, VCS_LISTENING);
    o = run(&c, VCE_TICK, 0, 0, 180000);
    ASSERT_EQ(o.note, VCN_END_SILENCE);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_pause_while_send_pending(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_ENDING, 1);
    run(&c, VCE_LINK_FINAL, 0, 0, 300);
    run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 310);
    o = run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 320);   /* already handed to the chat */
    ASSERT_FALSE(o.actions & VCA_DROP_TURN);
    ASSERT_TRUE(has(o, VCA_CLOSE_CAPTURE));
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 0);
    o = run(&c, VCE_SENT, VSS_SENT, 0, 330);
    ASSERT_FALSE(o.actions & VCA_CLOSE_CAPTURE);
    ASSERT_EQ(c.state, VCS_WORKING);
    ASSERT_EQ(o.header, VCH_PAUSED_BG);
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 0);
    run(&c, VCE_CHAT_IDLE, 1, 0, 400);
    o = run(&c, VCE_RESUME, 0, 0, 500);
    ASSERT_TRUE(has(o, VCA_SPEAK_REPLY));
    run(&c, VCE_PRESS, 0, 0, 600);
    run(&c, VCE_PRESS, 0, 0, 700);
    ASSERT_TRUE(clean());
    /* a refused send while paused: note, no speech, capture closed */
    goto_state(&c, VCS_ENDING, 0);
    run(&c, VCE_BATCH_OK, 0, 0, 300);
    run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 310);
    run(&c, VCE_PAUSE, VCP_DIALOG, 0, 320);
    o = run(&c, VCE_SENT, VSS_BUSY, 0, 330);
    ASSERT_EQ(o.note, VCN_BUSY);
    ASSERT_FALSE(o.actions & VCA_SPEAK_PHRASE);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 0);
    ASSERT_EQ(o.header, VCH_PAUSED_DIALOG);
    run(&c, VCE_PRESS, 0, 0, 400);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_card_announced_on_resume(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_WORKING, 1);
    run(&c, VCE_PAUSE, VCP_DIALOG, 0, 2000);
    o = run(&c, VCE_CARD, VCARD_COMMAND, 0, 2500);
    ASSERT_EQ(o.actions, 0u);
    o = run(&c, VCE_RESUME, 0, 0, 3000);
    ASSERT_TRUE(has(o, VCA_SPEAK_PHRASE));
    ASSERT_EQ(o.phrase, VCPH_APPROVE_COMMAND);
    ASSERT_EQ(o.header, VCH_APPROVAL);
    ASSERT_EQ(c.state, VCS_WORKING);
    /* a second pause/resume cycle does not repeat it */
    run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 3100);
    o = run(&c, VCE_RESUME, 0, 0, 3200);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(o.header, VCH_WORKING);
    /* the latest card wins */
    run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 3300);
    run(&c, VCE_CARD, VCARD_COMMAND, 0, 3400);
    run(&c, VCE_CARD, VCARD_WRITE, 0, 3500);
    o = run(&c, VCE_RESUME, 0, 0, 3600);
    ASSERT_EQ(o.phrase, VCPH_APPROVE_WRITE);
    /* cleared when the turn finishes: nothing is announced for a finished turn */
    run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 3700);
    run(&c, VCE_CARD, VCARD_OTHER, 0, 3800);
    run(&c, VCE_CHAT_IDLE, 0, 0, 3900);
    o = run(&c, VCE_RESUME, 0, 0, 4000);
    ASSERT_FALSE(o.actions & VCA_SPEAK_PHRASE);
    run(&c, VCE_PRESS, 0, 0, 4100);
    ASSERT_TRUE(clean());
    /* cleared by an end: the next conversation starts clean */
    goto_state(&c, VCS_WORKING, 1);
    run(&c, VCE_PAUSE, VCP_DIALOG, 0, 2000);
    run(&c, VCE_CARD, VCARD_OTHER, 0, 2100);
    run(&c, VCE_PRESS, 0, 0, 2200);
    ASSERT_EQ(c.card, 0);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_typed_send_while_paused(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_WORKING, 1);
    run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 2000);
    run(&c, VCE_CHAT_IDLE, 1, 0, 3000);       /* a reply is held */
    o = run(&c, VCE_SEND, 0, 0, 4000);
    ASSERT_TRUE(has(o, VCA_SEND_TYPED | VCA_DROP_HELD));
    ASSERT_FALSE(o.actions & VCA_OPEN_CAPTURE);
    ASSERT_EQ(c.state, VCS_ENDING);
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 0);
    o = run(&c, VCE_SENT, VSS_SENT, 0, 4100);
    ASSERT_EQ(c.state, VCS_WORKING);
    ASSERT_EQ(o.header, VCH_PAUSED_BG);
    ASSERT_FALSE(o.actions & VCA_CLOSE_CAPTURE);
    /* the held reply is gone: the resume after the new reply speaks the new one only */
    run(&c, VCE_CHAT_IDLE, 1, 0, 5000);
    o = run(&c, VCE_RESUME, 0, 0, 5100);
    ASSERT_TRUE(has(o, VCA_SPEAK_REPLY));
    ASSERT_FALSE(o.actions & VCA_SPEAK_CONTINUE);
    run(&c, VCE_PRESS, 0, 0, 5200);
    run(&c, VCE_PRESS, 0, 0, 5300);
    ASSERT_TRUE(clean());
    /* no held reply: no DROP_HELD */
    begin(&c, 0);
    run(&c, VCE_PAUSE, VCP_DIALOG, 0, 100);
    o = run(&c, VCE_SEND, 0, 0, 200);
    ASSERT_TRUE(has(o, VCA_SEND_TYPED));
    ASSERT_FALSE(o.actions & VCA_DROP_HELD);
    /* refused while paused: back to paused LISTENING, capture still closed */
    o = run(&c, VCE_SENT, VSS_EMPTY, 0, 300);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 0);
    run(&c, VCE_PRESS, 0, 0, 400);
    ASSERT_TRUE(clean());
    TEST_END();
}

/* The cut-in's mode change is the listener's own (it moves itself to LISTEN with the turn
 * carrying on); the machine must not send a mode command that would wipe that turn. */
int test_vconv_cut_sends_no_mode(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_SPEAKING, 1);
    run(&c, VCE_DUCK, 0, 0, 5000);
    o = run(&c, VCE_CUT, 0, 0, 5200);
    ASSERT_EQ(c.state, VCS_HEARING);
    ASSERT_FALSE(o.actions & (VCA_MODE_LISTEN | VCA_MODE_SPEAK | VCA_MODE_HOLD));
    run(&c, VCE_PRESS, 0, 0, 5300);
    ASSERT_TRUE(clean());
    TEST_END();
}

/* A Hearing with no turn event ever coming (a lost end) does not hold the mic for ever. */
int test_vconv_hearing_watchdog(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t0;
    long left;
    t0 = goto_state(&c, VCS_HEARING, 1);
    left = voice_conv_next_deadline(&c, t0);
    ASSERT_TRUE(left > 0 && left <= VOICE_CONV_HEARING_MAX_MS);
    ASSERT_EQ(run(&c, VCE_TICK, 0, 0, t0 + VOICE_CONV_HEARING_MAX_MS - 100).actions, 0u);
    ASSERT_EQ(c.state, VCS_HEARING);
    o = run(&c, VCE_TICK, 0, 0, t0 + VOICE_CONV_HEARING_MAX_MS + 100);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(has(o, VCA_DROP_TURN | VCA_UNLOCK_TYPING | VCA_CLOSE_LINK | VCA_MODE_LISTEN));
    ASSERT_EQ(o.header, VCH_LISTENING);
    ASSERT_FALSE(voice_conv_typing_locked(&c));
    run(&c, VCE_PRESS, 0, 0, t0 + VOICE_CONV_HEARING_MAX_MS + 200);
    ASSERT_TRUE(clean());
    /* a cut-in Hearing with no turn (half-duplex, TURN_START lost) has the same limit */
    t0 = goto_state(&c, VCS_SPEAKING, 0);
    run(&c, VCE_CUT, 0, 0, t0 + 100);
    ASSERT_EQ(c.state, VCS_HEARING);
    ASSERT_TRUE(voice_conv_next_deadline(&c, t0 + 100) > 0);
    o = run(&c, VCE_TICK, 0, 0, t0 + 100 + VOICE_CONV_HEARING_MAX_MS + 1);
    ASSERT_EQ(c.state, VCS_LISTENING);
    run(&c, VCE_PRESS, 0, 0, t0 + 200 + VOICE_CONV_HEARING_MAX_MS);
    ASSERT_TRUE(clean());
    TEST_END();
}

/* A batch Ending whose audio or transcript never arrives is bounded: it sets its deadline
 * (the ending_* tests below pin what happens when it passes). */
int test_vconv_ending_watchdog(void)
{
    TEST_BEGIN();
    VoiceConv c;
    unsigned long t0 = goto_state(&c, VCS_ENDING, 0);
    long left = voice_conv_next_deadline(&c, t0);
    ASSERT_TRUE(left > 0 && left <= VOICE_CONV_ENDING_MAX_MS);
    ASSERT_EQ(run(&c, VCE_TICK, 0, 0, t0 + VOICE_CONV_ENDING_MAX_MS - 100).actions, 0u);
    ASSERT_EQ(c.state, VCS_ENDING);
    /* live: the 3 s final wait comes first, the deadline still bounds the whole Ending */
    t0 = goto_state(&c, VCS_ENDING, 1);
    left = voice_conv_next_deadline(&c, t0);
    ASSERT_TRUE(left > 0 && left <= VOICE_CONV_FINAL_WAIT_MS);
    run(&c, VCE_TICK, 0, 0, t0 + VOICE_CONV_FINAL_WAIT_MS + 10);   /* falls back to batch */
    ASSERT_EQ(c.state, VCS_ENDING);
    left = voice_conv_next_deadline(&c, t0 + VOICE_CONV_FINAL_WAIT_MS + 10);
    ASSERT_TRUE(left > 0);
    run(&c, VCE_END, VCX_OFF, 0, t0 + 5000);
    ASSERT_TRUE(clean());
    TEST_END();
}

/* A lost VTT_DONE does not leave the capture in speak mode for ever. */
int test_vconv_speaking_watchdog(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t0 = goto_state(&c, VCS_SPEAKING, 1);
    long left = voice_conv_next_deadline(&c, t0);
    ASSERT_TRUE(left > 0 && left <= VOICE_CONV_SPEAKING_MAX_MS);
    ASSERT_EQ(run(&c, VCE_TICK, 0, 0, t0 + VOICE_CONV_SPEAKING_MAX_MS - 100).actions, 0u);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    o = run(&c, VCE_TICK, 0, 0, t0 + VOICE_CONV_SPEAKING_MAX_MS + 100);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(has(o, VCA_STOP_SPEECH | VCA_MODE_LISTEN | VCA_SEND_CAPTION));
    run(&c, VCE_PRESS, 0, 0, t0 + VOICE_CONV_SPEAKING_MAX_MS + 200);
    ASSERT_TRUE(clean());
    TEST_END();
}

/* A local "say that again" or "carry on" is not a turn sent to the assistant: it must not
 * restart the 3-minute silence clock (spec 5.6: it counts from the last turn sent). */
int test_vconv_local_phrase_keeps_silence_clock(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_ENDING, 0);
    run(&c, VCE_BATCH_OK, 0, 0, 200);
    o = run(&c, VCE_TRANSCRIPT, VTC_REPEAT, 0, 300);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    ASSERT_TRUE(has(o, VCA_SPEAK_REPEAT));
    run(&c, VCE_SPEECH_DONE, 0, 0, 170000);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_EQ(voice_conv_next_deadline(&c, 170000), 10000);   /* still counted from the start */
    o = run(&c, VCE_TICK, 0, 0, 180001);
    ASSERT_EQ(c.state, VCS_IDLE);
    ASSERT_TRUE(clean());
    /* a reply that came from the chat does restart it */
    goto_state(&c, VCS_SPEAKING, 0);
    run(&c, VCE_SPEECH_DONE, 0, 0, 170000);
    ASSERT_EQ(voice_conv_next_deadline(&c, 170000), 180000);
    run(&c, VCE_PRESS, 0, 0, 170100);
    ASSERT_TRUE(clean());
    TEST_END();
}

/* The final-text deadline survives the tick counter wrapping (the Windows count is 32 bits). */
int test_vconv_final_deadline_survives_tick_wrap(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    const unsigned long t0 = (unsigned long)-5000;
    voice_ledger_reset();
    voice_conv_init(&c);
    run(&c, VCE_START, 1, 0, t0);
    run(&c, VCE_TURN_START, 0, 0, t0 + 10);
    run(&c, VCE_LINK_READY, 0, 0, t0 + 20);
    run(&c, VCE_TURN_END, 0, 0, t0 + 4000);       /* the 3 s wait ends 2000 after the wrap */
    ASSERT_EQ(c.state, VCS_ENDING);
    o = run(&c, VCE_TICK, 0, 0, t0 + 4500);
    ASSERT_EQ(o.actions, 0u);                      /* not yet: 500 ms into the wait */
    ASSERT_EQ(voice_conv_next_deadline(&c, t0 + 4500), 2500);
    o = run(&c, VCE_TICK, 0, 0, t0 + 7100);
    ASSERT_TRUE(has(o, VCA_SUBMIT_BATCH | VCA_CLOSE_LINK));
    run(&c, VCE_PRESS, 0, 0, t0 + 7200);
    ASSERT_TRUE(clean());
    TEST_END();
}

/* ---- Task 4: external sends, speech progress, the silence clock ---- */

/* The box was sent from outside the conversation (a typed message, a slash command): the
 * conversation moves to Working at once, whatever it was doing. */
int test_vconv_external_send_from_listening(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    begin(&c, 0);
    o = run(&c, VCE_EXTERNAL_SEND, 0, 0, 100);
    ASSERT_EQ(c.state, VCS_WORKING);
    ASSERT_TRUE(has(o, VCA_CLOSE_CAPTURE));
    ASSERT_EQ(o.header, VCH_WORKING);
    ASSERT_EQ(c.sent_at, 100ul);
    o = run(&c, VCE_CHAT_IDLE, 1, 0, 2000);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    ASSERT_TRUE(has(o, VCA_SPEAK_REPLY));
    run(&c, VCE_PRESS, 0, 0, 2100);
    ASSERT_EQ(c.state, VCS_LISTENING);
    run(&c, VCE_PRESS, 0, 0, 2200);
    ASSERT_EQ(c.state, VCS_IDLE);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_external_send_from_hearing_and_speaking(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t;
    t = goto_state(&c, VCS_HEARING, 1);
    o = run(&c, VCE_EXTERNAL_SEND, 0, 0, t + 10);
    ASSERT_EQ(c.state, VCS_WORKING);
    ASSERT_TRUE(has(o, VCA_DROP_TURN | VCA_CLOSE_LINK | VCA_UNLOCK_TYPING | VCA_CLOSE_CAPTURE));
    ASSERT_EQ(c.turn, 0);
    run(&c, VCE_PRESS, 0, 0, t + 20);
    ASSERT_TRUE(clean());
    /* ENDING that has not handed anything over: the upload is cancelled */
    t = goto_state(&c, VCS_ENDING, 0);
    o = run(&c, VCE_EXTERNAL_SEND, 0, 0, t + 10);
    ASSERT_EQ(c.state, VCS_WORKING);
    ASSERT_TRUE(has(o, VCA_DROP_TURN | VCA_CANCEL_BATCH | VCA_CLOSE_CAPTURE));
    run(&c, VCE_PRESS, 0, 0, t + 20);
    ASSERT_TRUE(clean());
    t = goto_state(&c, VCS_SPEAKING, 0);
    o = run(&c, VCE_EXTERNAL_SEND, 0, 0, t + 10);
    ASSERT_EQ(c.state, VCS_WORKING);
    ASSERT_TRUE(has(o, VCA_STOP_SPEECH | VCA_SEND_CAPTION | VCA_CLOSE_CAPTURE));
    ASSERT_FALSE(o.actions & VCA_DROP_TURN);
    ASSERT_EQ(o.header, VCH_WORKING);
    run(&c, VCE_PRESS, 0, 0, t + 20);
    ASSERT_TRUE(clean());
    /* a cut-in turn open while Speaking is dropped too */
    t = goto_state(&c, VCS_SPEAKING, 1);
    run(&c, VCE_TURN_START, 0, 0, t + 5);
    o = run(&c, VCE_EXTERNAL_SEND, 0, 0, t + 10);
    ASSERT_EQ(c.state, VCS_WORKING);
    ASSERT_TRUE(has(o, VCA_STOP_SPEECH | VCA_DROP_TURN | VCA_CLOSE_LINK | VCA_UNLOCK_TYPING));
    run(&c, VCE_PRESS, 0, 0, t + 20);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_external_send_ignored(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_state(&c, VCS_WORKING, 0);
    o = run(&c, VCE_EXTERNAL_SEND, 0, 0, t + 10);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(o.header, VCH_NONE);
    ASSERT_EQ(c.state, VCS_WORKING);
    ASSERT_TRUE(c.sent_at != t + 10);
    run(&c, VCE_PRESS, 0, 0, t + 20);
    ASSERT_TRUE(clean());
    /* ENDING with the turn already handed over */
    goto_state(&c, VCS_ENDING, 0);
    run(&c, VCE_BATCH_OK, 0, 0, 200);
    run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 210);
    ASSERT_EQ(c.sending, 1);
    o = run(&c, VCE_EXTERNAL_SEND, 0, 0, 220);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(c.state, VCS_ENDING);
    run(&c, VCE_PRESS, 0, 0, 230);
    ASSERT_TRUE(clean());
    /* IDLE */
    voice_ledger_reset();
    voice_conv_init(&c);
    o = run(&c, VCE_EXTERNAL_SEND, 0, 0, 10);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(c.state, VCS_IDLE);
    TEST_END();
}

int test_vconv_external_send_paused(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_state(&c, VCS_WORKING, 0);
    run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, t + 10);
    o = run(&c, VCE_CHAT_IDLE, 1, 0, t + 20);
    ASSERT_TRUE(has(o, VCA_HOLD_REPLY));
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_EQ(c.held, 1);
    o = run(&c, VCE_EXTERNAL_SEND, 0, 0, t + 30);
    ASSERT_EQ(c.state, VCS_WORKING);
    ASSERT_EQ(o.header, VCH_PAUSED_BG);
    ASSERT_TRUE(has(o, VCA_DROP_HELD));
    ASSERT_EQ(c.held, 0);
    run(&c, VCE_PRESS, 0, 0, t + 40);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_progress_refreshes_speaking_watchdog(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_state(&c, VCS_SPEAKING, 0), at;
    int i;
    for (i = 1; i <= 15; i++) {
        at = t + (unsigned long)i * 60000ul;
        o = run(&c, VCE_SPEECH_PROGRESS, 0, 0, at);
        ASSERT_EQ(o.actions, 0u);
        ASSERT_EQ(o.header, VCH_NONE);
        ASSERT_EQ(c.state, VCS_SPEAKING);
        ASSERT_EQ(run(&c, VCE_TICK, 0, 0, at + 1000).actions, 0u);
        ASSERT_EQ(c.state, VCS_SPEAKING);
    }
    run(&c, VCE_PRESS, 0, 0, at + 2000);
    run(&c, VCE_PRESS, 0, 0, at + 2100);
    ASSERT_TRUE(clean());
    /* without progress the watchdog fires at 600 s */
    t = goto_state(&c, VCS_SPEAKING, 0);
    ASSERT_EQ(run(&c, VCE_TICK, 0, 0, t + 599000).actions, 0u);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    run(&c, VCE_TICK, 0, 0, t + 600000);
    ASSERT_EQ(c.state, VCS_LISTENING);
    /* progress in any other state means nothing */
    ASSERT_EQ(run(&c, VCE_SPEECH_PROGRESS, 0, 0, t + 600100).actions, 0u);
    run(&c, VCE_PRESS, 0, 0, t + 600200);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_refusal_phrase_keeps_silence_clock(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_state(&c, VCS_ENDING, 0);
    run(&c, VCE_BATCH_OK, 0, 0, 200);
    run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, 210);
    o = run(&c, VCE_SENT, VSS_FULL, 0, 220);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    ASSERT_TRUE(has(o, VCA_SPEAK_PHRASE));
    run(&c, VCE_SPEECH_DONE, 0, 0, 170000);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_EQ(voice_conv_next_deadline(&c, 170000), 10000);
    run(&c, VCE_TICK, 0, 0, 180001);
    ASSERT_EQ(c.state, VCS_IDLE);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_paused_local_repeat_keeps_silence_clock(void)
{
    TEST_BEGIN();
    VoiceConv c;
    goto_state(&c, VCS_ENDING, 0);
    run(&c, VCE_BATCH_OK, 0, 0, 200);
    run(&c, VCE_TRANSCRIPT, VTC_REPEAT, 0, 300);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 1000);
    ASSERT_EQ(c.held, 1);
    run(&c, VCE_RESUME, 0, 0, 2000);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    run(&c, VCE_SPEECH_DONE, 0, 0, 170000);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_EQ(voice_conv_next_deadline(&c, 170000), 10000);
    run(&c, VCE_TICK, 0, 0, 180001);
    ASSERT_EQ(c.state, VCS_IDLE);
    ASSERT_TRUE(clean());
    /* a chat reply held and resumed does restart it */
    goto_state(&c, VCS_WORKING, 0);
    run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, 1000);
    run(&c, VCE_CHAT_IDLE, 1, 0, 1100);
    run(&c, VCE_RESUME, 0, 0, 1200);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    run(&c, VCE_SPEECH_DONE, 0, 0, 170000);
    ASSERT_EQ(voice_conv_next_deadline(&c, 170000), 180000);
    run(&c, VCE_PRESS, 0, 0, 170100);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_hearing_watchdog_keeps_silence_clock(void)
{
    TEST_BEGIN();
    VoiceConv c;
    unsigned long t = goto_state(&c, VCS_HEARING, 0);
    run(&c, VCE_TICK, 0, 0, t + VOICE_CONV_HEARING_MAX_MS + 100);
    ASSERT_EQ(c.state, VCS_LISTENING);
    run(&c, VCE_TICK, 0, 0, VOICE_CONV_SILENCE_END_MS);
    ASSERT_EQ(c.state, VCS_IDLE);
    ASSERT_TRUE(clean());
    TEST_END();
}

/* Task 13: a turn stuck in Ending ends the conversation, with a note, at its deadline. */
int test_vconv_ending_deadline_default(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t0 = goto_state(&c, VCS_ENDING, 0);
    ASSERT_EQ(c.ending_max, (unsigned long)VOICE_CONV_ENDING_MAX_MS);
    ASSERT_EQ(run(&c, VCE_TICK, 0, 0, t0 + 179000).actions, 0u);
    ASSERT_EQ(c.state, VCS_ENDING);
    o = run(&c, VCE_TICK, 0, 0, t0 + 180000);
    ASSERT_EQ(c.state, VCS_IDLE);
    ASSERT_EQ(o.note, VCN_END_STUCK);
    ASSERT_TRUE(has(o, VCA_CANCEL_BATCH | VCA_UNLOCK_TYPING));
    ASSERT_EQ(o.actions & VCA_DROP_TURN, 0u);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_ending_deadline_budget(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t0 = goto_state(&c, VCS_ENDING, 0);
    voice_conv_set_ending_budget(&c, 324000ul);
    ASSERT_EQ(c.ending_max, 334000ul);
    ASSERT_EQ(run(&c, VCE_TICK, 0, 0, t0 + 180000).actions, 0u);
    ASSERT_EQ(c.state, VCS_ENDING);
    ASSERT_EQ(run(&c, VCE_TICK, 0, 0, t0 + 333000).actions, 0u);
    ASSERT_TRUE(voice_conv_next_deadline(&c, t0 + 180000) > 100000);
    o = run(&c, VCE_TICK, 0, 0, t0 + 334000);
    ASSERT_EQ(c.state, VCS_IDLE);
    ASSERT_EQ(o.note, VCN_END_STUCK);
    ASSERT_TRUE(clean());
    /* a small budget never shortens the default */
    t0 = goto_state(&c, VCS_ENDING, 0);
    voice_conv_set_ending_budget(&c, 1000ul);
    ASSERT_EQ(c.ending_max, (unsigned long)VOICE_CONV_ENDING_MAX_MS);
    /* every entry to Ending starts from the default again */
    voice_conv_set_ending_budget(&c, 324000ul);
    run(&c, VCE_END, VCX_OFF, 0, t0 + 10);
    t0 = goto_state(&c, VCS_ENDING, 0);
    ASSERT_EQ(c.ending_max, (unsigned long)VOICE_CONV_ENDING_MAX_MS);
    TEST_END();
}

int test_vconv_ending_deadline_live(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t0 = goto_state(&c, VCS_ENDING, 1);
    run(&c, VCE_TICK, 0, 0, t0 + VOICE_CONV_FINAL_WAIT_MS + 10);   /* the fallback batch starts */
    ASSERT_EQ(c.state, VCS_ENDING);
    ASSERT_EQ(run(&c, VCE_TICK, 0, 0, t0 + 179000).actions, 0u);
    o = run(&c, VCE_TICK, 0, 0, t0 + 180000);
    ASSERT_EQ(c.state, VCS_IDLE);
    ASSERT_EQ(o.note, VCN_END_STUCK);
    ASSERT_TRUE(has(o, VCA_CANCEL_BATCH));
    ASSERT_EQ(o.actions & VCA_DROP_TURN, 0u);
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_ending_note_text(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(voice_conv_note_text(VCN_END_STUCK), "Conversation ended: the last turn took too long");
    TEST_END();
}

int test_vconv_ai_failed_header_text(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(voice_conv_header_text(VCH_AI_FAILED), "AI speech failed: using Windows speech");
    TEST_END();
}

/* ---- Mute the microphone while speech plays (the setting on) ---- */

/* Like goto_state(..., SPEAKING), with the mute setting on at start. */
static unsigned long goto_speaking_muted(VoiceConv *c, int live, VoiceConvOut *at_speaking)
{
    unsigned long t = 0;
    voice_ledger_reset();
    voice_conv_init(c);
    run(c, VCE_START, live, 1, 0);
    run(c, VCE_TURN_START, 0, 0, t += 10);
    if (live) run(c, VCE_LINK_READY, 0, 0, t += 10);
    run(c, VCE_TURN_END, 0, 0, t += 100);
    if (live) run(c, VCE_LINK_FINAL, 0, 0, t += 100); else run(c, VCE_BATCH_OK, 0, 0, t += 100);
    run(c, VCE_TRANSCRIPT, VTC_TEXT, 0, t += 10);
    run(c, VCE_SENT, VSS_SENT, 0, t += 10);
    *at_speaking = run(c, VCE_CHAT_IDLE, 1, 0, t += 1000);
    return t;
}

int test_vconv_mute_speaking_holds_the_listener(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    goto_speaking_muted(&c, 1, &o);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    ASSERT_TRUE(has(o, VCA_SPEAK_REPLY | VCA_MODE_HOLD));
    ASSERT_EQ(o.actions & VCA_MODE_SPEAK, 0u);
    ASSERT_EQ(o.header, VCH_SPEAKING_MUTED);
    ASSERT_STR_EQ(voice_conv_header_text(VCH_SPEAKING_MUTED),
        "Microphone off while the assistant speaks. Press the voice key to interrupt.");
    TEST_END();
}

int test_vconv_mute_off_sends_the_speak_mode(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_state(&c, VCS_WORKING, 1);
    o = run(&c, VCE_CHAT_IDLE, 1, 0, t + 1000);
    ASSERT_TRUE(has(o, VCA_SPEAK_REPLY | VCA_MODE_SPEAK));
    ASSERT_EQ(o.actions & VCA_MODE_HOLD, 0u);
    ASSERT_EQ(o.header, VCH_SPEAKING);
    TEST_END();
}

int test_vconv_mute_speaking_ignores_the_listener(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_speaking_muted(&c, 1, &o);
    o = run(&c, VCE_DUCK, 0, 0, t += 10);
    ASSERT_EQ(o.actions, 0u); ASSERT_EQ(o.header, VCH_NONE);
    o = run(&c, VCE_TURN_START, 0, 0, t += 10);
    ASSERT_EQ(o.actions, 0u); ASSERT_EQ(o.header, VCH_NONE);
    ASSERT_EQ(c.turn, 0);
    ASSERT_EQ(c.typing, 0);
    o = run(&c, VCE_CUT, 0, 0, t += 10);
    ASSERT_EQ(o.actions, 0u); ASSERT_EQ(o.header, VCH_NONE);
    o = run(&c, VCE_UNDUCK, 0, 0, t += 10);
    ASSERT_EQ(o.actions, 0u);
    o = run(&c, VCE_TURN_ABANDON, 0, 0, t += 10);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    ASSERT_EQ(c.turn, 0);
    TEST_END();
}

int test_vconv_mute_off_still_cuts_in(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_state(&c, VCS_SPEAKING, 1);
    o = run(&c, VCE_DUCK, 0, 0, t += 10);
    ASSERT_TRUE(has(o, VCA_DUCK));
    o = run(&c, VCE_CUT, 0, 0, t += 10);
    ASSERT_EQ(c.state, VCS_HEARING);
    ASSERT_TRUE(has(o, VCA_STOP_SPEECH));
    TEST_END();
}

int test_vconv_mute_speech_done_listens_after_the_guard(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_speaking_muted(&c, 0, &o);
    t += 5000;
    o = run(&c, VCE_SPEECH_DONE, 0, 0, t);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(o.header, VCH_NONE);
    ASSERT_EQ(voice_conv_next_deadline(&c, t), (long)VOICE_CONV_MUTE_GUARD_MS);
    o = run(&c, VCE_TICK, 0, 0, t + VOICE_CONV_MUTE_GUARD_MS - 1);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    ASSERT_EQ(o.actions, 0u);
    o = run(&c, VCE_TICK, 0, 0, t + VOICE_CONV_MUTE_GUARD_MS);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(has(o, VCA_MODE_LISTEN));
    ASSERT_EQ(o.header, VCH_LISTENING);
    ASSERT_EQ(c.muted, 0);
    TEST_END();
}

int test_vconv_mute_guard_ignores_the_microphone(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_speaking_muted(&c, 0, &o);
    run(&c, VCE_SPEECH_DONE, 0, 0, t += 3000);
    o = run(&c, VCE_TURN_START, 0, 0, t + 100);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    o = run(&c, VCE_SPEECH_DONE, 0, 0, t + 200);   /* a repeated report does not move the guard */
    ASSERT_EQ(o.actions, 0u);
    o = run(&c, VCE_TICK, 0, 0, t + VOICE_CONV_MUTE_GUARD_MS);
    ASSERT_EQ(c.state, VCS_LISTENING);
    TEST_END();
}

int test_vconv_mute_off_speech_done_listens_at_once(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_state(&c, VCS_SPEAKING, 0);
    o = run(&c, VCE_SPEECH_DONE, 0, 0, t + 100);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(has(o, VCA_MODE_LISTEN));
    TEST_END();
}

int test_vconv_mute_speech_failed_mid_reply_starts_the_guard(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_speaking_muted(&c, 0, &o);
    t += 100;
    o = run(&c, VCE_SPEECH_FAILED, 0, 0, t);
    ASSERT_EQ(c.state, VCS_SPEAKING);          /* the microphone stays held */
    ASSERT_EQ(o.note, VCN_NO_SPEECH);          /* the note shows as before */
    ASSERT_EQ(o.actions & VCA_MODE_LISTEN, 0u);
    ASSERT_EQ(c.muted, 1);
    ASSERT_EQ(voice_conv_next_deadline(&c, t), (long)VOICE_CONV_MUTE_GUARD_MS);
    o = run(&c, VCE_TICK, 0, 0, t + VOICE_CONV_MUTE_GUARD_MS - 1);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    ASSERT_EQ(o.actions, 0u);
    o = run(&c, VCE_TICK, 0, 0, t + VOICE_CONV_MUTE_GUARD_MS);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(has(o, VCA_MODE_LISTEN));
    ASSERT_EQ(o.note, VCN_NONE);
    TEST_END();
}

int test_vconv_mute_speech_failed_in_the_guard_changes_nothing(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_speaking_muted(&c, 0, &o);
    run(&c, VCE_SPEECH_DONE, 0, 0, t += 3000);
    o = run(&c, VCE_SPEECH_FAILED, 0, 0, t + 100);   /* a late engine error */
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(o.note, VCN_NONE);
    ASSERT_EQ(o.header, VCH_NONE);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    ASSERT_EQ(voice_conv_next_deadline(&c, t + 100), (long)VOICE_CONV_MUTE_GUARD_MS - 100);
    o = run(&c, VCE_TICK, 0, 0, t + VOICE_CONV_MUTE_GUARD_MS - 1);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    o = run(&c, VCE_TICK, 0, 0, t + VOICE_CONV_MUTE_GUARD_MS);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(has(o, VCA_MODE_LISTEN));
    ASSERT_EQ(o.note, VCN_NONE);
    TEST_END();
}

int test_vconv_mute_off_speech_failed_listens_at_once(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_state(&c, VCS_SPEAKING, 0);
    o = run(&c, VCE_SPEECH_FAILED, 0, 0, t + 100);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(has(o, VCA_MODE_LISTEN));
    ASSERT_EQ(o.note, VCN_NO_SPEECH);
    TEST_END();
}

int test_vconv_mute_resumed_held_reply_is_muted(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_speaking_muted(&c, 0, &o);
    o = run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, t += 500);   /* cut off mid-reply: held */
    ASSERT_TRUE(has(o, VCA_HOLD_REPLY));
    ASSERT_EQ(c.muted, 0);
    o = run(&c, VCE_RESUME, 0, 0, t += 500);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    ASSERT_TRUE(has(o, VCA_SPEAK_CONTINUE | VCA_MODE_HOLD));
    ASSERT_EQ(o.actions & VCA_MODE_SPEAK, 0u);
    ASSERT_EQ(o.header, VCH_SPEAKING_MUTED);
    ASSERT_EQ(c.muted, 1);
    TEST_END();
}

int test_vconv_mute_spoken_phrases_are_muted(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = 0;
    /* the "something went wrong" phrase */
    voice_ledger_reset();
    voice_conv_init(&c);
    run(&c, VCE_START, 0, 1, 0);
    run(&c, VCE_TURN_START, 0, 0, t += 10);
    run(&c, VCE_TURN_END, 0, 0, t += 100);
    run(&c, VCE_BATCH_OK, 0, 0, t += 100);
    run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, t += 10);
    run(&c, VCE_SENT, VSS_SENT, 0, t += 10);
    o = run(&c, VCE_CHAT_IDLE, 1, 1, t += 1000);
    ASSERT_TRUE(has(o, VCA_SPEAK_PHRASE | VCA_MODE_HOLD));
    ASSERT_EQ(o.phrase, VCPH_WENT_WRONG);
    ASSERT_EQ(o.header, VCH_SPEAKING_MUTED);
    /* the refusal's phrase */
    t = 0;
    voice_ledger_reset();
    voice_conv_init(&c);
    run(&c, VCE_START, 0, 1, 0);
    run(&c, VCE_TURN_START, 0, 0, t += 10);
    run(&c, VCE_TURN_END, 0, 0, t += 100);
    run(&c, VCE_BATCH_OK, 0, 0, t += 100);
    run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, t += 10);
    o = run(&c, VCE_SENT, VSS_BUSY, 0, t += 10);
    ASSERT_TRUE(has(o, VCA_SPEAK_PHRASE | VCA_MODE_HOLD));
    ASSERT_EQ(o.phrase, VCPH_NOT_SENT);
    ASSERT_EQ(o.header, VCH_SPEAKING_MUTED);
    TEST_END();
}

int test_vconv_mute_external_send_in_the_guard(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_speaking_muted(&c, 0, &o);
    run(&c, VCE_SPEECH_DONE, 0, 0, t += 3000);
    o = run(&c, VCE_EXTERNAL_SEND, 0, 0, t + 100);   /* the user types and sends */
    ASSERT_EQ(c.state, VCS_WORKING);
    ASSERT_EQ(o.header, VCH_WORKING);
    ASSERT_EQ(c.muted, 0);
    ASSERT_EQ(c.guard, 0);
    o = run(&c, VCE_TICK, 0, 0, t + VOICE_CONV_MUTE_GUARD_MS);   /* the guard does not fire */
    ASSERT_EQ(c.state, VCS_WORKING);
    ASSERT_EQ(o.actions, 0u);
    o = run(&c, VCE_TICK, 0, 0, t + 5000);
    ASSERT_EQ(c.state, VCS_WORKING);
    ASSERT_EQ(o.actions & VCA_MODE_LISTEN, 0u);
    TEST_END();
}

int test_vconv_mute_the_key_interrupts_the_reply(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_speaking_muted(&c, 1, &o);
    o = run(&c, VCE_PRESS, 0, 0, t + 500);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(has(o, VCA_STOP_SPEECH | VCA_MODE_LISTEN));
    ASSERT_EQ(o.header, VCH_LISTENING);
    ASSERT_EQ(c.muted, 0);
    ASSERT_TRUE(voice_conv_active(&c));
    /* a later reply is muted again */
    TEST_END();
}

int test_vconv_mute_the_key_during_the_guard(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_speaking_muted(&c, 0, &o);
    run(&c, VCE_SPEECH_DONE, 0, 0, t += 3000);
    o = run(&c, VCE_PRESS, 0, 0, t + 100);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(has(o, VCA_MODE_LISTEN));
    ASSERT_EQ(c.guard, 0);
    ASSERT_EQ(voice_conv_next_deadline(&c, t + 100) > 300, 1);
    TEST_END();
}

int test_vconv_mute_stop_escape_and_end_still_work(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_speaking_muted(&c, 0, &o);
    o = run(&c, VCE_STOP, 0, 0, t + 10);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(has(o, VCA_STOP_SPEECH | VCA_MODE_LISTEN));
    t = goto_speaking_muted(&c, 0, &o);
    o = run(&c, VCE_ESCAPE, 0, 0, t + 10);
    ASSERT_EQ(c.state, VCS_LISTENING);
    ASSERT_TRUE(has(o, VCA_STOP_SPEECH | VCA_MODE_LISTEN));
    t = goto_speaking_muted(&c, 0, &o);
    o = run(&c, VCE_END, VCX_PANEL_CLOSED, 0, t + 10);
    ASSERT_EQ(c.state, VCS_IDLE);
    ASSERT_TRUE(has(o, VCA_STOP_SPEECH | VCA_CLOSE_CAPTURE | VCA_ENGINE_STOP));
    ASSERT_TRUE(clean());
    TEST_END();
}

int test_vconv_mute_next_reply_is_muted_again(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_speaking_muted(&c, 0, &o);
    run(&c, VCE_PRESS, 0, 0, t += 10);
    run(&c, VCE_TURN_START, 0, 0, t += 10);
    run(&c, VCE_TURN_END, 0, 0, t += 100);
    run(&c, VCE_BATCH_OK, 0, 0, t += 100);
    run(&c, VCE_TRANSCRIPT, VTC_TEXT, 0, t += 10);
    run(&c, VCE_SENT, VSS_SENT, 0, t += 10);
    o = run(&c, VCE_CHAT_IDLE, 1, 0, t += 1000);
    ASSERT_TRUE(has(o, VCA_MODE_HOLD));
    ASSERT_EQ(o.header, VCH_SPEAKING_MUTED);
    TEST_END();
}

int test_vconv_mute_pause_in_the_guard_does_not_hold_the_reply(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = goto_speaking_muted(&c, 0, &o);
    run(&c, VCE_SPEECH_DONE, 0, 0, t += 3000);
    o = run(&c, VCE_PAUSE, VCP_BACKGROUND, 0, t + 50);
    ASSERT_EQ(o.actions & VCA_HOLD_REPLY, 0u);
    ASSERT_EQ(c.held, 0);
    ASSERT_EQ(c.muted, 0);
    TEST_END();
}

int test_vconv_mute_repeat_phrase_keeps_the_muted_header(void)
{
    TEST_BEGIN();
    VoiceConv c; VoiceConvOut o;
    unsigned long t = 0;
    voice_ledger_reset();
    voice_conv_init(&c);
    run(&c, VCE_START, 0, 1, 0);
    run(&c, VCE_TURN_START, 0, 0, t += 10);
    run(&c, VCE_TURN_END, 0, 0, t += 100);
    run(&c, VCE_BATCH_OK, 0, 0, t += 100);
    o = run(&c, VCE_TRANSCRIPT, VTC_REPEAT, 0, t += 10);
    ASSERT_EQ(c.state, VCS_SPEAKING);
    ASSERT_TRUE(has(o, VCA_SPEAK_REPEAT | VCA_MODE_HOLD));
    ASSERT_EQ(o.header, VCH_SPEAKING_MUTED);
    TEST_END();
}
