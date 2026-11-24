#include "test_framework.h"
#include "voice_dict.h"
#include "voice_ledger.h"
#include <string.h>

/* Applies the actions to the ledger the way the glue will. Strict: releasing
 * what is not held is a failure (voice_ledger_underflow()). CLOSE_LINK,
 * CANCEL_BATCH and END_CAPTURE_DISCARD are idempotent in the glue, so they
 * release only what is held; END_CAPTURE_KEEP and a batch result must find
 * their resource held. */
static VoiceDictOut run(VoiceDict *d, VoiceDictEvent ev, int a, int b, unsigned long now)
{
    int was_running = d->batch_running;
    VoiceDictOut o = voice_dict_step(d, ev, a, b, now);
    if (o.actions & VDA_OPEN_CAPTURE) voice_ledger_acquire(VRES_CAPTURE);
    if (o.actions & VDA_OPEN_LINK) voice_ledger_acquire(VRES_LIVE_SESSION);
    if (o.actions & VDA_END_CAPTURE_KEEP) voice_ledger_release(VRES_CAPTURE);
    if ((o.actions & VDA_END_CAPTURE_DISCARD) && voice_ledger_count(VRES_CAPTURE) > 0)
        voice_ledger_release(VRES_CAPTURE);
    if ((o.actions & VDA_CLOSE_LINK) && voice_ledger_count(VRES_LIVE_SESSION) > 0)
        voice_ledger_release(VRES_LIVE_SESSION);
    if (o.actions & VDA_SUBMIT_BATCH) voice_ledger_acquire(VRES_STT_UPLOAD);
    if (was_running && !d->batch_running && (ev == VDE_BATCH_OK || ev == VDE_BATCH_FAIL))
        voice_ledger_release(VRES_STT_UPLOAD);
    if ((o.actions & VDA_CANCEL_BATCH) && voice_ledger_count(VRES_STT_UPLOAD) > 0)
        voice_ledger_release(VRES_STT_UPLOAD);
    return o;
}

static int bits(VoiceDictOut o, unsigned want) { return (o.actions & want) == want; }

static void start(VoiceDict *d, int live, int auto_ok)
{
    voice_ledger_reset();
    voice_dict_init(d);
    run(d, VDE_START, live, auto_ok, 0);
}

static int ledger_clean(void)
{
    return voice_ledger_all_zero() && voice_ledger_underflow() == 0;
}

int test_vdict_live_key_stop(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    voice_ledger_reset();
    voice_dict_init(&d);
    o = run(&d, VDE_START, 1, 0, 0);
    ASSERT_EQ(o.actions, VDA_OPEN_CAPTURE | VDA_OPEN_LINK | VDA_LOCK_TYPING);
    ASSERT_EQ(d.link, VDL_CONNECTING);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    ASSERT_EQ(d.link, VDL_READY);
    o = run(&d, VDE_STOP, 0, 0, 100);
    ASSERT_EQ(o.actions, VDA_END_CAPTURE_KEEP);
    ASSERT_EQ(d.state, VDS_FINISHING);
    o = run(&d, VDE_CAPTURED, 1, 0, 110);
    ASSERT_EQ(o.actions, 0u);
    o = run(&d, VDE_LINK_FINAL, 0, 0, 200);
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(o.actions & VDA_DROP_AUDIO);
    ASSERT_TRUE(o.actions & VDA_UNLOCK_TYPING);
    ASSERT_TRUE(o.actions & VDA_CLOSE_LINK);
    ASSERT_FALSE(o.actions & VDA_SEND_BOX);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

int test_vdict_live_auto_send(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    start(&d, 1, 1);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    run(&d, VDE_STOP, 0, 0, 100);
    run(&d, VDE_CAPTURED, 1, 0, 110);
    o = run(&d, VDE_LINK_FINAL, 0, 0, 200);
    ASSERT_TRUE(o.actions & VDA_SEND_BOX);
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(ledger_clean());
    /* the final first, the capture after: nothing, the glue frees it */
    start(&d, 1, 1);
    run(&d, VDE_STOP, 0, 0, 100);
    o = run(&d, VDE_LINK_FINAL, 0, 0, 120);
    ASSERT_TRUE(o.actions & VDA_SEND_BOX);
    o = run(&d, VDE_CAPTURED, 1, 0, 130);
    ASSERT_EQ(o.actions, VDA_DROP_AUDIO);
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

int test_vdict_live_send_while_recording(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    start(&d, 1, 0);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    o = run(&d, VDE_SEND, 0, 0, 100);
    ASSERT_EQ(o.actions, VDA_END_CAPTURE_KEEP);
    ASSERT_EQ(d.send_after, 1);
    run(&d, VDE_CAPTURED, 1, 0, 110);
    o = run(&d, VDE_LINK_FINAL, 0, 0, 500);
    ASSERT_TRUE(o.actions & VDA_SEND_BOX);
    ASSERT_TRUE(ledger_clean());

    /* no final: the wait runs out at +3000 */
    start(&d, 1, 0);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    run(&d, VDE_SEND, 0, 0, 100);
    run(&d, VDE_CAPTURED, 1, 0, 110);
    o = run(&d, VDE_TICK, 0, 0, 3099);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(d.state, VDS_FINISHING);
    /* C1: the deadline with audio kept is a link failure: fallback upload,
     * Send still pending (no send of the live text at the deadline) */
    o = run(&d, VDE_TICK, 0, 0, 3100);
    ASSERT_EQ(o.text, VDT_LIVE_STOPPED);
    ASSERT_TRUE(bits(o, VDA_CLOSE_LINK | VDA_SUBMIT_BATCH));
    ASSERT_FALSE(o.actions & VDA_SEND_BOX);
    ASSERT_FALSE(o.actions & VDA_DROP_AUDIO);
    ASSERT_EQ(d.state, VDS_FINISHING);
    ASSERT_EQ(d.send_after, 1);
    ASSERT_EQ(d.fallback, 1);
    o = run(&d, VDE_BATCH_OK, 0, 0, 4000);
    ASSERT_TRUE(bits(o, VDA_DELIVER_BATCH | VDA_SEND_BOX | VDA_UNLOCK_TYPING));
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(ledger_clean());

    /* capture still due at the deadline: fallback, the upload follows the capture */
    start(&d, 1, 0);
    run(&d, VDE_STOP, 0, 0, 100);
    o = run(&d, VDE_TICK, 0, 0, 3100);
    ASSERT_EQ(o.text, VDT_LIVE_STOPPED);
    ASSERT_TRUE(o.actions & VDA_CLOSE_LINK);
    ASSERT_FALSE(o.actions & VDA_SUBMIT_BATCH);
    ASSERT_EQ(d.state, VDS_FINISHING);
    ASSERT_EQ(voice_dict_next_deadline(&d, 3200), -1L);
    o = run(&d, VDE_CAPTURED, 1, 0, 3300);
    ASSERT_TRUE(o.actions & VDA_SUBMIT_BATCH);
    o = run(&d, VDE_BATCH_OK, 0, 0, 3400);
    ASSERT_TRUE(o.actions & VDA_DELIVER_BATCH);
    ASSERT_TRUE(ledger_clean());

    /* Send pressed again while finishing only sets send_after */
    start(&d, 1, 0);
    run(&d, VDE_STOP, 0, 0, 100);
    o = run(&d, VDE_SEND, 0, 0, 200);
    ASSERT_EQ(o.actions, 0u);
    o = run(&d, VDE_LINK_FINAL, 0, 0, 300);
    ASSERT_TRUE(o.actions & VDA_SEND_BOX);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

int test_vdict_live_escape(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    start(&d, 1, 1);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    o = run(&d, VDE_ESCAPE, 0, 0, 50);
    ASSERT_TRUE(o.actions & VDA_REMOVE_WORDS);
    ASSERT_TRUE(o.actions & VDA_END_CAPTURE_DISCARD);
    ASSERT_TRUE(o.actions & VDA_CLOSE_LINK);
    ASSERT_TRUE(o.actions & VDA_UNLOCK_TYPING);
    ASSERT_EQ(d.state, VDS_IDLE);
    o = run(&d, VDE_LINK_FINAL, 0, 0, 100);
    ASSERT_EQ(o.actions, 0u);
    o = run(&d, VDE_CAPTURED, 1, 0, 110);
    ASSERT_EQ(o.actions, VDA_DROP_AUDIO);
    ASSERT_TRUE(ledger_clean());
    /* Escape means nothing while finishing */
    start(&d, 1, 0);
    run(&d, VDE_STOP, 0, 0, 100);
    o = run(&d, VDE_ESCAPE, 0, 0, 110);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(d.state, VDS_FINISHING);
    run(&d, VDE_END_HARD, 0, 0, 120);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

int test_vdict_live_soft_end_never_sends(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    start(&d, 1, 1);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    o = run(&d, VDE_END_SOFT, 0, 0, 100);
    ASSERT_EQ(o.actions, VDA_END_CAPTURE_KEEP);
    ASSERT_EQ(d.state, VDS_FINISHING);
    run(&d, VDE_CAPTURED, 1, 0, 110);
    o = run(&d, VDE_LINK_FINAL, 0, 0, 200);
    ASSERT_FALSE(o.actions & VDA_SEND_BOX);
    ASSERT_TRUE(ledger_clean());
    /* a Send after the soft end still does not send */
    start(&d, 1, 0);
    run(&d, VDE_END_SOFT, 0, 0, 100);
    run(&d, VDE_SEND, 0, 0, 110);
    o = run(&d, VDE_LINK_FINAL, 0, 0, 200);
    ASSERT_FALSE(o.actions & VDA_SEND_BOX);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

int test_vdict_hard_end(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    start(&d, 1, 1);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    o = run(&d, VDE_END_HARD, 0, 0, 100);
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(o.actions & VDA_END_CAPTURE_DISCARD);
    ASSERT_TRUE(o.actions & VDA_CLOSE_LINK);
    ASSERT_TRUE(o.actions & VDA_CANCEL_BATCH);
    ASSERT_TRUE(o.actions & VDA_DROP_AUDIO);
    ASSERT_TRUE(o.actions & VDA_UNLOCK_TYPING);
    ASSERT_FALSE(o.actions & VDA_SEND_BOX);
    ASSERT_FALSE(o.actions & VDA_REMOVE_WORDS);
    ASSERT_TRUE(ledger_clean());

    /* finishing with a fallback upload running */
    start(&d, 1, 1);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    run(&d, VDE_STOP, 0, 0, 100);
    run(&d, VDE_CAPTURED, 1, 0, 110);
    run(&d, VDE_LINK_FAILED, 0, 0, 120);
    ASSERT_EQ(d.batch_running, 1);
    ASSERT_EQ(voice_ledger_count(VRES_STT_UPLOAD), 1);
    o = run(&d, VDE_END_HARD, 0, 0, 130);
    ASSERT_TRUE(o.actions & VDA_CANCEL_BATCH);
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_EQ(d.batch_running, 0);
    ASSERT_TRUE(ledger_clean());
    /* idle: nothing */
    o = run(&d, VDE_END_HARD, 0, 0, 140);
    ASSERT_EQ(o.actions, 0u);
    TEST_END();
}

int test_vdict_link_never_opened(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    start(&d, 1, 0);
    o = run(&d, VDE_LINK_FAILED, 1, 0, 50);
    ASSERT_EQ(o.text, VDT_LIVE_UNAVAILABLE);
    ASSERT_TRUE(o.actions & VDA_CLOSE_LINK);
    ASSERT_EQ(d.link, VDL_FAILED);
    ASSERT_EQ(d.fallback, 1);
    o = run(&d, VDE_STOP, 0, 0, 100);
    ASSERT_EQ(o.actions, VDA_END_CAPTURE_KEEP);
    ASSERT_EQ(voice_dict_next_deadline(&d, 100), -1L);
    o = run(&d, VDE_CAPTURED, 1, 0, 110);
    ASSERT_TRUE(o.actions & VDA_SUBMIT_BATCH);
    ASSERT_EQ(o.text, VDT_TRANSCRIBING);
    o = run(&d, VDE_BATCH_OK, 0, 0, 900);
    ASSERT_TRUE(o.actions & VDA_DELIVER_BATCH);
    ASSERT_TRUE(o.actions & VDA_DROP_AUDIO);
    ASSERT_FALSE(o.actions & VDA_SEND_BOX);
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(ledger_clean());
    /* a mid-recording drop says stopped, not unavailable */
    start(&d, 1, 0);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    o = run(&d, VDE_LINK_FAILED, 0, 0, 50);
    ASSERT_EQ(o.text, VDT_LIVE_STOPPED);
    run(&d, VDE_END_HARD, 0, 0, 60);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

int test_vdict_link_drops_after_stop_either_order(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    start(&d, 1, 0);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    run(&d, VDE_STOP, 0, 0, 100);
    ASSERT_EQ(voice_dict_next_deadline(&d, 100), 3000L);
    o = run(&d, VDE_CAPTURED, 1, 0, 110);
    ASSERT_EQ(o.actions, 0u);
    o = run(&d, VDE_LINK_FAILED, 0, 0, 120);
    ASSERT_TRUE(o.actions & VDA_SUBMIT_BATCH);
    ASSERT_TRUE(o.actions & VDA_CLOSE_LINK);
    o = run(&d, VDE_BATCH_OK, 0, 0, 800);
    ASSERT_TRUE(o.actions & VDA_DELIVER_BATCH);
    ASSERT_TRUE(ledger_clean());

    start(&d, 1, 0);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    run(&d, VDE_STOP, 0, 0, 100);
    o = run(&d, VDE_LINK_FAILED, 0, 0, 110);
    ASSERT_FALSE(o.actions & VDA_SUBMIT_BATCH);
    ASSERT_EQ(d.state, VDS_FINISHING);
    o = run(&d, VDE_CAPTURED, 1, 0, 120);
    ASSERT_TRUE(o.actions & VDA_SUBMIT_BATCH);
    run(&d, VDE_BATCH_OK, 0, 0, 800);
    ASSERT_TRUE(ledger_clean());

    /* the drop first, then a silent capture: nothing to transcribe, no hang */
    start(&d, 1, 0);
    run(&d, VDE_STOP, 0, 0, 100);
    run(&d, VDE_LINK_FAILED, 0, 0, 110);
    o = run(&d, VDE_CAPTURED, 0, 0, 120);
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_FALSE(o.actions & VDA_SUBMIT_BATCH);
    ASSERT_TRUE(o.actions & VDA_UNLOCK_TYPING);
    ASSERT_TRUE(ledger_clean());

    /* a late LINK_FAILED after the final is ignored */
    start(&d, 1, 0);
    run(&d, VDE_STOP, 0, 0, 100);
    run(&d, VDE_LINK_FINAL, 0, 0, 110);
    o = run(&d, VDE_LINK_FAILED, 0, 0, 120);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

int test_vdict_fallback_fails(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    start(&d, 1, 1);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    run(&d, VDE_SEND, 0, 0, 100);
    run(&d, VDE_CAPTURED, 1, 0, 110);
    run(&d, VDE_LINK_FAILED, 0, 0, 120);
    o = run(&d, VDE_BATCH_FAIL, 0, 0, 900);
    ASSERT_EQ(o.text, VDT_FALLBACK_FAILED);
    ASSERT_TRUE(o.actions & VDA_DROP_AUDIO);
    ASSERT_FALSE(o.actions & VDA_SEND_BOX);
    ASSERT_FALSE(o.actions & VDA_DELIVER_BATCH);
    ASSERT_TRUE(o.actions & VDA_UNLOCK_TYPING);
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

int test_vdict_send_during_fallback(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    start(&d, 1, 0);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    run(&d, VDE_STOP, 0, 0, 100);
    run(&d, VDE_CAPTURED, 1, 0, 110);
    run(&d, VDE_LINK_FAILED, 0, 0, 120);
    o = run(&d, VDE_SEND, 0, 0, 200);
    ASSERT_EQ(o.actions, 0u);
    /* no 3 s limit during the fallback */
    o = run(&d, VDE_TICK, 0, 0, 60000);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(d.state, VDS_FINISHING);
    o = run(&d, VDE_BATCH_OK, 0, 0, 61000);
    ASSERT_TRUE(o.actions & VDA_DELIVER_BATCH);
    ASSERT_TRUE(o.actions & VDA_SEND_BOX);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

int test_vdict_batch_recording(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    start(&d, 0, 0);
    ASSERT_EQ(d.link, VDL_NONE);
    o = run(&d, VDE_STOP, 0, 0, 100);
    ASSERT_EQ(o.actions, VDA_END_CAPTURE_KEEP);
    ASSERT_EQ(voice_dict_next_deadline(&d, 100), -1L);
    o = run(&d, VDE_CAPTURED, 1, 0, 110);
    ASSERT_TRUE(o.actions & VDA_SUBMIT_BATCH);
    ASSERT_EQ(o.text, VDT_TRANSCRIBING);
    o = run(&d, VDE_BATCH_OK, 0, 0, 900);
    ASSERT_TRUE(o.actions & VDA_DELIVER_BATCH);
    ASSERT_FALSE(o.actions & VDA_SEND_BOX);
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(ledger_clean());

    /* auto-send: delivery and send together */
    start(&d, 0, 1);
    run(&d, VDE_STOP, 0, 0, 100);
    run(&d, VDE_CAPTURED, 1, 0, 110);
    o = run(&d, VDE_BATCH_OK, 0, 0, 900);
    ASSERT_TRUE((o.actions & (VDA_DELIVER_BATCH | VDA_SEND_BOX)) == (VDA_DELIVER_BATCH | VDA_SEND_BOX));
    ASSERT_TRUE(ledger_clean());

    /* Send while recording forces the send */
    start(&d, 0, 0);
    o = run(&d, VDE_SEND, 0, 0, 100);
    ASSERT_EQ(o.actions, VDA_END_CAPTURE_KEEP);
    run(&d, VDE_CAPTURED, 1, 0, 110);
    o = run(&d, VDE_BATCH_OK, 0, 0, 900);
    ASSERT_TRUE((o.actions & (VDA_DELIVER_BATCH | VDA_SEND_BOX)) == (VDA_DELIVER_BATCH | VDA_SEND_BOX));
    ASSERT_TRUE(ledger_clean());

    /* silent capture */
    start(&d, 0, 1);
    run(&d, VDE_STOP, 0, 0, 100);
    o = run(&d, VDE_CAPTURED, 0, 0, 110);
    ASSERT_EQ(o.text, VDT_DIDNT_CATCH);
    ASSERT_TRUE(o.actions & VDA_UNLOCK_TYPING);
    ASSERT_FALSE(o.actions & VDA_SEND_BOX);
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(ledger_clean());

    /* batch failure: finish, nothing sent */
    start(&d, 0, 1);
    run(&d, VDE_STOP, 0, 0, 100);
    run(&d, VDE_CAPTURED, 1, 0, 110);
    o = run(&d, VDE_BATCH_FAIL, 0, 0, 900);
    ASSERT_TRUE(o.actions & VDA_DROP_AUDIO);
    ASSERT_FALSE(o.actions & VDA_SEND_BOX);
    ASSERT_TRUE(o.actions & VDA_UNLOCK_TYPING);
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(ledger_clean());

    /* soft end while recording: today's discard */
    start(&d, 0, 1);
    o = run(&d, VDE_END_SOFT, 0, 0, 100);
    ASSERT_EQ(o.actions, VDA_END_CAPTURE_DISCARD | VDA_UNLOCK_TYPING);
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(ledger_clean());

    /* hard end while the batch upload runs */
    start(&d, 0, 1);
    run(&d, VDE_STOP, 0, 0, 100);
    run(&d, VDE_CAPTURED, 1, 0, 110);
    o = run(&d, VDE_END_HARD, 0, 0, 120);
    ASSERT_TRUE(o.actions & VDA_CANCEL_BATCH);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

int test_vdict_deadline(void)
{
    TEST_BEGIN();
    VoiceDict d;
    voice_ledger_reset();
    voice_dict_init(&d);
    ASSERT_EQ(voice_dict_next_deadline(&d, 0), -1L);
    run(&d, VDE_START, 1, 0, 0);
    ASSERT_EQ(voice_dict_next_deadline(&d, 50), -1L);
    run(&d, VDE_STOP, 0, 0, 1000);
    ASSERT_EQ(voice_dict_next_deadline(&d, 1000), 3000L);
    ASSERT_EQ(voice_dict_next_deadline(&d, 3500), 500L);
    ASSERT_EQ(voice_dict_next_deadline(&d, 9000), 0L);
    run(&d, VDE_LINK_FAILED, 0, 0, 1500);
    ASSERT_EQ(voice_dict_next_deadline(&d, 1500), -1L);
    run(&d, VDE_END_HARD, 0, 0, 1600);
    ASSERT_EQ(voice_dict_next_deadline(&d, 1600), -1L);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

int test_vdict_clean_restart_and_noops(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    start(&d, 1, 1);
    run(&d, VDE_SEND, 0, 0, 100);
    run(&d, VDE_END_SOFT, 0, 0, 110);
    run(&d, VDE_CAPTURED, 1, 0, 120);
    run(&d, VDE_LINK_FINAL, 0, 0, 130);
    ASSERT_EQ(d.send_after, 0);
    ASSERT_EQ(d.no_send, 0);
    ASSERT_EQ(d.have_audio, 0);
    /* a fresh batch recording starts with nothing left over */
    o = run(&d, VDE_START, 0, 0, 200);
    ASSERT_EQ(o.actions, VDA_OPEN_CAPTURE | VDA_LOCK_TYPING);
    /* START while recording is ignored */
    o = run(&d, VDE_START, 1, 1, 210);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(d.live, 0);
    run(&d, VDE_END_SOFT, 0, 0, 220);
    ASSERT_TRUE(ledger_clean());
    /* idle: every event but START is nothing (CAPTURED with a=1 has its own test) */
    {
        static const VoiceDictEvent evs[] = {
            VDE_STOP, VDE_SEND, VDE_ESCAPE, VDE_END_SOFT, VDE_END_HARD, VDE_LINK_READY,
            VDE_LINK_FAILED, VDE_LINK_FINAL, VDE_CAPTURED, VDE_BATCH_OK, VDE_BATCH_FAIL, VDE_TICK
        };
        size_t i;
        for (i = 0; i < sizeof evs / sizeof evs[0]; i++) {
            o = voice_dict_step(&d, evs[i], 0, 1, 300);
            ASSERT_EQ(o.actions, 0u);
            ASSERT_EQ(o.text, VDT_NONE);
            ASSERT_EQ(d.state, VDS_IDLE);
            ASSERT_EQ(d.have_audio, 0);
            ASSERT_EQ(d.captured, 0);
            ASSERT_EQ(d.link, VDL_NONE);
            ASSERT_EQ(d.final_deadline, 0ul);
        }
    }
    /* a stray batch result while recording changes nothing */
    start(&d, 0, 0);
    o = run(&d, VDE_BATCH_OK, 0, 0, 10);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(d.state, VDS_RECORDING);
    run(&d, VDE_END_SOFT, 0, 0, 20);
    ASSERT_TRUE(ledger_clean());
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

int test_vdict_text(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(voice_dict_text(VDT_NONE), "");
    ASSERT_STR_EQ(voice_dict_text(VDT_LIVE_UNAVAILABLE), "Live text is unavailable: the words will appear when you stop");
    ASSERT_STR_EQ(voice_dict_text(VDT_LIVE_STOPPED), "Live text stopped: the rest will appear when you stop");
    ASSERT_STR_EQ(voice_dict_text(VDT_FALLBACK_FAILED), "The recording could not be transcribed again: the text in the box is what arrived live");
    ASSERT_STR_EQ(voice_dict_text(VDT_FINAL_LATE), "The last words did not arrive. Check the text and press Send.");
    ASSERT_STR_EQ(voice_dict_text(VDT_DIDNT_CATCH), "Didn't catch that");
    ASSERT_STR_EQ(voice_dict_text(VDT_TRANSCRIBING), "Transcribing\xE2\x80\xA6");
    TEST_END();
}

int test_voice_ledger_counts(void)
{
    TEST_BEGIN();
    int i;
    voice_ledger_reset();
    ASSERT_TRUE(voice_ledger_all_zero());
    voice_ledger_acquire(VRES_CAPTURE);
    voice_ledger_acquire(VRES_CAPTURE);
    voice_ledger_release(VRES_CAPTURE);
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 1);
    ASSERT_FALSE(voice_ledger_all_zero());
    ASSERT_EQ(voice_ledger_underflow(), 0);
    voice_ledger_release(VRES_CAPTURE);
    ASSERT_TRUE(voice_ledger_all_zero());
    voice_ledger_release(VRES_CAPTURE);
    ASSERT_EQ(voice_ledger_count(VRES_CAPTURE), 0);
    ASSERT_EQ(voice_ledger_underflow(), 1);
    voice_ledger_reset();
    ASSERT_EQ(voice_ledger_underflow(), 0);
    /* kinds are independent */
    for (i = 0; i < (int)VRES_COUNT; i++) voice_ledger_acquire((VoiceRes)i);
    for (i = 0; i < (int)VRES_COUNT; i++) ASSERT_EQ(voice_ledger_count((VoiceRes)i), 1);
    voice_ledger_release(VRES_STT_UPLOAD);
    ASSERT_EQ(voice_ledger_count(VRES_STT_UPLOAD), 0);
    ASSERT_EQ(voice_ledger_count(VRES_LIVE_SESSION), 1);
    /* out of range is ignored */
    voice_ledger_acquire(VRES_COUNT);
    voice_ledger_release((VoiceRes)-1);
    ASSERT_EQ(voice_ledger_count(VRES_COUNT), 0);
    ASSERT_EQ(voice_ledger_underflow(), 0);
    voice_ledger_reset();
    TEST_END();
}


int test_vdict_capture_ends_recording(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    /* live: the audio thread ended the capture itself (the cap or a fault) */
    start(&d, 1, 0);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    o = run(&d, VDE_CAPTURED, 1, 0, 1000);
    ASSERT_TRUE(bits(o, VDA_END_CAPTURE_KEEP));
    ASSERT_FALSE(o.actions & VDA_SUBMIT_BATCH);
    ASSERT_EQ(d.state, VDS_FINISHING);
    ASSERT_EQ(d.captured, 1);
    ASSERT_EQ(voice_dict_next_deadline(&d, 1000), 3000L);
    o = run(&d, VDE_LINK_FINAL, 0, 0, 1200);
    ASSERT_TRUE(bits(o, VDA_DROP_AUDIO | VDA_UNLOCK_TYPING));
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(ledger_clean());
    /* batch: stop and capture in one step */
    start(&d, 0, 0);
    o = run(&d, VDE_CAPTURED, 1, 0, 1000);
    ASSERT_TRUE(bits(o, VDA_END_CAPTURE_KEEP | VDA_SUBMIT_BATCH));
    ASSERT_EQ(o.text, VDT_TRANSCRIBING);
    o = run(&d, VDE_BATCH_OK, 0, 0, 2000);
    ASSERT_TRUE(bits(o, VDA_DELIVER_BATCH));
    ASSERT_TRUE(ledger_clean());
    /* batch, silent */
    start(&d, 0, 0);
    o = run(&d, VDE_CAPTURED, 0, 0, 1000);
    ASSERT_TRUE(bits(o, VDA_END_CAPTURE_KEEP | VDA_UNLOCK_TYPING));
    ASSERT_EQ(o.text, VDT_DIDNT_CATCH);
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(ledger_clean());
    /* a link that already failed: one submit, from the combined step */
    start(&d, 1, 0);
    run(&d, VDE_LINK_FAILED, 0, 0, 10);
    o = run(&d, VDE_CAPTURED, 1, 0, 1000);
    ASSERT_TRUE(bits(o, VDA_END_CAPTURE_KEEP | VDA_SUBMIT_BATCH));
    ASSERT_EQ(voice_ledger_count(VRES_STT_UPLOAD), 1);
    run(&d, VDE_BATCH_OK, 0, 0, 2000);
    ASSERT_TRUE(ledger_clean());
    /* a late CAPTURED(1) in RECORDING, then the link drops: one submit only */
    start(&d, 1, 0);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    o = run(&d, VDE_CAPTURED, 1, 0, 1000);
    ASSERT_FALSE(o.actions & VDA_SUBMIT_BATCH);
    o = run(&d, VDE_LINK_FAILED, 0, 0, 1100);
    ASSERT_TRUE(bits(o, VDA_SUBMIT_BATCH | VDA_CLOSE_LINK));
    ASSERT_EQ(voice_ledger_count(VRES_STT_UPLOAD), 1);
    o = run(&d, VDE_CAPTURED, 1, 0, 1200);
    ASSERT_FALSE(o.actions & VDA_SUBMIT_BATCH);
    run(&d, VDE_BATCH_OK, 0, 0, 2000);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

int test_vdict_captured_while_idle(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    voice_ledger_reset();
    voice_dict_init(&d);
    o = run(&d, VDE_CAPTURED, 1, 0, 100);
    ASSERT_EQ(o.actions, VDA_DROP_AUDIO);
    ASSERT_EQ(o.text, VDT_NONE);
    ASSERT_EQ(d.captured, 0);
    ASSERT_EQ(d.have_audio, 0);
    o = run(&d, VDE_CAPTURED, 0, 0, 100);
    ASSERT_EQ(o.actions, 0u);
    /* the next recording is not poisoned by the stray capture */
    run(&d, VDE_START, 1, 0, 200);
    run(&d, VDE_LINK_READY, 0, 0, 210);
    run(&d, VDE_STOP, 0, 0, 300);
    ASSERT_EQ(d.captured, 0);
    o = run(&d, VDE_LINK_FAILED, 0, 0, 310);
    ASSERT_FALSE(o.actions & VDA_SUBMIT_BATCH);
    ASSERT_EQ(d.state, VDS_FINISHING);
    run(&d, VDE_END_HARD, 0, 0, 320);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

int test_vdict_final_before_stop(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    start(&d, 1, 0);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    o = run(&d, VDE_LINK_FINAL, 0, 0, 50);
    ASSERT_EQ(o.actions, 0u);
    run(&d, VDE_STOP, 0, 0, 100);
    ASSERT_EQ(voice_dict_next_deadline(&d, 100), 3000L);
    o = run(&d, VDE_CAPTURED, 1, 0, 110);
    ASSERT_TRUE(bits(o, VDA_DROP_AUDIO | VDA_UNLOCK_TYPING | VDA_CLOSE_LINK));
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(ledger_clean());
    /* the capture is lost: the deadline is the exit, and the final did arrive */
    start(&d, 1, 1);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    run(&d, VDE_LINK_FINAL, 0, 0, 50);
    run(&d, VDE_STOP, 0, 0, 100);
    o = run(&d, VDE_TICK, 0, 0, 3099);
    ASSERT_EQ(o.actions, 0u);
    o = run(&d, VDE_TICK, 0, 0, 3100);
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(bits(o, VDA_SEND_BOX | VDA_UNLOCK_TYPING | VDA_CLOSE_LINK));
    ASSERT_EQ(o.text, VDT_NONE);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

int test_vdict_soft_end_during_upload(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    start(&d, 1, 1);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    run(&d, VDE_STOP, 0, 0, 100);
    run(&d, VDE_CAPTURED, 1, 0, 110);
    run(&d, VDE_LINK_FAILED, 0, 0, 120);
    o = run(&d, VDE_END_SOFT, 0, 0, 130);
    ASSERT_EQ(o.actions, 0u);
    o = run(&d, VDE_BATCH_OK, 0, 0, 900);
    ASSERT_TRUE(bits(o, VDA_DELIVER_BATCH));
    ASSERT_FALSE(o.actions & VDA_SEND_BOX);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

int test_vdict_escape_batch_and_silent_live(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    start(&d, 0, 1);
    o = run(&d, VDE_ESCAPE, 0, 0, 100);
    ASSERT_TRUE(bits(o, VDA_END_CAPTURE_DISCARD | VDA_REMOVE_WORDS | VDA_UNLOCK_TYPING));
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(ledger_clean());
    /* a silent live capture waits for the final; the final finishes it */
    start(&d, 1, 0);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    run(&d, VDE_STOP, 0, 0, 100);
    o = run(&d, VDE_CAPTURED, 0, 0, 110);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(d.state, VDS_FINISHING);
    o = run(&d, VDE_LINK_FINAL, 0, 0, 200);
    ASSERT_FALSE(o.actions & VDA_SUBMIT_BATCH);
    ASSERT_FALSE(o.actions & VDA_DROP_AUDIO);
    ASSERT_TRUE(bits(o, VDA_UNLOCK_TYPING));
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

int test_vdict_timeout_without_send_keeps_text(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    start(&d, 1, 1);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    run(&d, VDE_STOP, 0, 0, 100);
    run(&d, VDE_CAPTURED, 0, 0, 110);
    o = run(&d, VDE_TICK, 0, 0, 3100);
    ASSERT_FALSE(o.actions & VDA_SEND_BOX);
    ASSERT_EQ(o.text, VDT_NONE);
    ASSERT_TRUE(bits(o, VDA_CLOSE_LINK | VDA_UNLOCK_TYPING));
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

/* C1 with no audio at all: finish as before (the glue says "Didn't catch that"
 * when the box gained nothing) */
int test_vdict_deadline_no_audio_finishes(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    start(&d, 1, 0);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    run(&d, VDE_STOP, 0, 0, 100);
    run(&d, VDE_CAPTURED, 0, 0, 110);
    o = run(&d, VDE_TICK, 0, 0, 3100);
    ASSERT_FALSE(o.actions & VDA_SUBMIT_BATCH);
    ASSERT_TRUE(bits(o, VDA_CLOSE_LINK | VDA_UNLOCK_TYPING));
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

/* I2: a real discard (Hold short tap, chord) is Escape's actions */
int test_vdict_discard_like_escape(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    start(&d, 1, 0);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    o = run(&d, VDE_DISCARD, 0, 0, 50);
    ASSERT_TRUE(bits(o, VDA_END_CAPTURE_DISCARD | VDA_CLOSE_LINK | VDA_REMOVE_WORDS |
                        VDA_UNLOCK_TYPING));
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(ledger_clean());
    start(&d, 0, 0);
    o = run(&d, VDE_DISCARD, 0, 0, 50);
    ASSERT_TRUE(bits(o, VDA_END_CAPTURE_DISCARD | VDA_UNLOCK_TYPING));
    ASSERT_EQ(d.state, VDS_IDLE);
    /* idle and finishing: nothing */
    o = run(&d, VDE_DISCARD, 0, 0, 60);
    ASSERT_EQ(o.actions, 0u);
    start(&d, 1, 0);
    run(&d, VDE_STOP, 0, 0, 100);
    o = run(&d, VDE_DISCARD, 0, 0, 110);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(d.state, VDS_FINISHING);
    run(&d, VDE_END_HARD, 0, 0, 120);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}

/* I4: Escape while an upload runs cancels it; Send says it will wait */
int test_vdict_escape_cancels_upload(void)
{
    TEST_BEGIN();
    VoiceDict d; VoiceDictOut o;
    /* live fallback */
    start(&d, 1, 1);
    run(&d, VDE_LINK_READY, 0, 0, 10);
    run(&d, VDE_STOP, 0, 0, 100);
    run(&d, VDE_CAPTURED, 1, 0, 110);
    run(&d, VDE_LINK_FAILED, 0, 0, 120);
    ASSERT_EQ(d.batch_running, 1);
    o = run(&d, VDE_SEND, 0, 0, 130);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(o.text, VDT_WILL_SEND);
    o = run(&d, VDE_ESCAPE, 0, 0, 140);
    ASSERT_TRUE(bits(o, VDA_CANCEL_BATCH | VDA_UNLOCK_TYPING | VDA_DROP_AUDIO));
    ASSERT_FALSE(o.actions & VDA_SEND_BOX);
    ASSERT_FALSE(o.actions & VDA_REMOVE_WORDS);
    ASSERT_FALSE(o.actions & VDA_DELIVER_BATCH);
    ASSERT_EQ(o.text, VDT_CANCELLED);
    ASSERT_EQ(d.state, VDS_IDLE);
    ASSERT_TRUE(ledger_clean());
    /* batch recording */
    start(&d, 0, 1);
    run(&d, VDE_STOP, 0, 0, 100);
    run(&d, VDE_CAPTURED, 1, 0, 110);
    o = run(&d, VDE_ESCAPE, 0, 0, 120);
    ASSERT_TRUE(bits(o, VDA_CANCEL_BATCH | VDA_UNLOCK_TYPING));
    ASSERT_FALSE(o.actions & (VDA_DELIVER_BATCH | VDA_SEND_BOX));
    ASSERT_EQ(d.state, VDS_IDLE);
    o = run(&d, VDE_BATCH_OK, 0, 0, 130);        /* a late result changes nothing */
    ASSERT_EQ(o.actions, 0u);
    ASSERT_TRUE(ledger_clean());
    /* FINISHING without an upload: Escape does nothing */
    start(&d, 1, 0);
    run(&d, VDE_STOP, 0, 0, 100);
    o = run(&d, VDE_ESCAPE, 0, 0, 110);
    ASSERT_EQ(o.actions, 0u);
    ASSERT_EQ(d.state, VDS_FINISHING);
    run(&d, VDE_END_HARD, 0, 0, 120);
    ASSERT_TRUE(ledger_clean());
    TEST_END();
}
