#include "test_framework.h"
#include "voice_read.h"
#include "voice_aispeech.h"
#include "voice_config.h"
#include <string.h>

static VoiceReadPress P(int id, int engine)
{
    VoiceReadPress p = { 0, 0, 1, 1 };
    p.item_id = id;
    p.engine = engine;
    return p;
}

/* A reader speaking item `id` on the AI engine; its serial is r->serial. */
static void speaking_ai(VoiceRead *r, int id)
{
    VoiceReadPress p = P(id, VOICE_SPEECH_AI);
    voice_read_init(r);
    voice_read_on_press(r, &p);
    voice_read_on_started(r, 1);
}

int test_vread_gate(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_read_shown(1, VOICE_MODE_DICTATION), 1);
    ASSERT_EQ(voice_read_shown(1, VOICE_MODE_CONVERSATION), 0);
    ASSERT_EQ(voice_read_shown(0, VOICE_MODE_DICTATION), 0);
    ASSERT_EQ(voice_read_gate(1, VOICE_MODE_DICTATION, 0), 1);
    ASSERT_EQ(voice_read_gate(1, VOICE_MODE_DICTATION, 1), 0);
    ASSERT_EQ(voice_read_gate(1, VOICE_MODE_CONVERSATION, 0), 0);
    ASSERT_EQ(voice_read_gate(0, VOICE_MODE_DICTATION, 0), 0);
    TEST_END();
}

int test_vread_press_refused_by_gate(void)
{
    TEST_BEGIN();
    VoiceRead r; voice_read_init(&r);
    VoiceReadPress p = P(7, VOICE_SPEECH_AI); p.gate = 0;
    ASSERT_EQ(voice_read_on_press(&r, &p).actions, 0u);
    ASSERT_EQ(r.state, VRS_IDLE);
    TEST_END();
}

int test_vread_ai_press_starts_then_speaks(void)
{
    TEST_BEGIN();
    VoiceRead r; voice_read_init(&r);
    VoiceReadPress p = P(7, VOICE_SPEECH_AI);
    VoiceReadOut o = voice_read_on_press(&r, &p);
    ASSERT_EQ(o.actions, VRA_START | VRA_VIEW);
    ASSERT_EQ(o.engine, VOICE_SPEECH_AI);
    ASSERT_EQ(r.state, VRS_STARTING); ASSERT_EQ(r.item_id, 7);
    o = voice_read_on_started(&r, 1);
    ASSERT_EQ(o.actions, VRA_SPEAK); ASSERT_EQ(o.serial, 1u); ASSERT_EQ(r.state, VRS_SPEAKING);
    o = voice_read_on_engine(&r, VRM_DONE, 1u, 0);
    ASSERT_EQ(o.actions, VRA_VIEW); ASSERT_EQ(r.state, VRS_IDLE);
    ASSERT_EQ(r.item_id, 0); ASSERT_EQ(r.ctx, 1);
    TEST_END();
}

int test_vread_windows_waits_ready(void)
{
    TEST_BEGIN();
    VoiceRead r; voice_read_init(&r);
    VoiceReadPress p = P(7, VOICE_SPEECH_WINDOWS);
    VoiceReadOut o = voice_read_on_press(&r, &p);
    ASSERT_EQ(o.actions, VRA_START | VRA_VIEW);
    ASSERT_EQ(o.engine, VOICE_SPEECH_WINDOWS);
    o = voice_read_on_started(&r, 1);
    ASSERT_EQ(o.actions, 0u); ASSERT_EQ(r.state, VRS_STARTING);
    o = voice_read_on_engine(&r, VRM_READY, 0, 0);
    ASSERT_EQ(o.actions, VRA_SPEAK); ASSERT_EQ(o.serial, 1u);
    ASSERT_EQ(r.state, VRS_SPEAKING);
    TEST_END();
}

int test_vread_second_press_reuses_context(void)
{
    TEST_BEGIN();
    VoiceRead r; speaking_ai(&r, 7);
    voice_read_on_engine(&r, VRM_DONE, 1u, 0);
    VoiceReadPress p = P(9, VOICE_SPEECH_AI);
    VoiceReadOut o = voice_read_on_press(&r, &p);
    ASSERT_EQ(o.actions, VRA_SPEAK | VRA_VIEW);
    ASSERT_EQ(o.serial, 2u);
    ASSERT_EQ(r.state, VRS_SPEAKING);
    TEST_END();
}

int test_vread_ready_after_stop_is_silent(void)
{
    TEST_BEGIN();
    for (int variant = 0; variant < 3; variant++) {
        VoiceRead r; voice_read_init(&r);
        VoiceReadPress p = P(7, VOICE_SPEECH_WINDOWS);
        VoiceReadOut o;
        voice_read_on_press(&r, &p);
        voice_read_on_started(&r, 1);
        if (variant == 0) o = voice_read_on_press(&r, &p);
        else if (variant == 1) o = voice_read_on_stop(&r, VRX_DICTATION);
        else o = voice_read_on_stop(&r, VRX_CHAT);
        ASSERT_EQ(o.actions, VRA_STOP | VRA_VIEW);
        ASSERT_EQ(r.state, VRS_IDLE);
        o = voice_read_on_engine(&r, VRM_READY, 0, 0);
        ASSERT_EQ(o.actions, 0u);
        ASSERT_EQ(r.ready, 1);
        ASSERT_EQ(r.state, VRS_IDLE);
    }
    TEST_END();
}

int test_vread_press_reading_reply_stops(void)
{
    TEST_BEGIN();
    VoiceRead r; speaking_ai(&r, 7);
    VoiceReadPress p = P(7, VOICE_SPEECH_AI);
    VoiceReadOut o = voice_read_on_press(&r, &p);
    ASSERT_EQ(o.actions, VRA_STOP | VRA_VIEW);
    ASSERT_EQ(r.state, VRS_IDLE); ASSERT_EQ(r.ctx, 1);
    TEST_END();
}

int test_vread_press_other_reply_switches(void)
{
    TEST_BEGIN();
    VoiceRead r; speaking_ai(&r, 7);
    VoiceReadPress p = P(9, VOICE_SPEECH_AI);
    VoiceReadOut o = voice_read_on_press(&r, &p);
    ASSERT_EQ(o.actions, VRA_STOP | VRA_SPEAK | VRA_VIEW);
    ASSERT_EQ(o.serial, 2u);
    ASSERT_EQ(r.item_id, 9);
    TEST_END();
}

int test_vread_late_reply_of_first_ignored(void)
{
    TEST_BEGIN();
    VoiceRead r; speaking_ai(&r, 7);
    VoiceReadPress p = P(9, VOICE_SPEECH_AI);
    voice_read_on_press(&r, &p);
    ASSERT_EQ(voice_read_on_engine(&r, VRM_DONE, 1u, 0).actions, 0u);
    ASSERT_EQ(r.state, VRS_SPEAKING); ASSERT_EQ(r.item_id, 9);
    ASSERT_EQ(voice_read_on_engine(&r, VRM_ERROR, 1u, 401).actions, 0u);
    ASSERT_EQ(r.ctx, 1);
    TEST_END();
}

int test_vread_error_while_speaking_releases(void)
{
    TEST_BEGIN();
    VoiceRead r; speaking_ai(&r, 7);
    VoiceReadOut o = voice_read_on_engine(&r, VRM_ERROR, r.serial, 401);
    ASSERT_EQ(o.actions, VRA_RELEASE | VRA_ERROR | VRA_VIEW);
    ASSERT_EQ(o.hr, 401L);
    ASSERT_EQ(o.err_engine, VOICE_SPEECH_AI);
    ASSERT_EQ(r.ctx, 0); ASSERT_EQ(r.state, VRS_IDLE);
    VoiceReadPress p = P(7, VOICE_SPEECH_AI);
    o = voice_read_on_press(&r, &p);
    ASSERT_EQ(o.actions, VRA_START | VRA_VIEW);
    TEST_END();
}

int test_vread_error_while_starting(void)
{
    TEST_BEGIN();
    VoiceRead r; voice_read_init(&r);
    VoiceReadPress p = P(7, VOICE_SPEECH_WINDOWS);
    voice_read_on_press(&r, &p);
    voice_read_on_started(&r, 1);
    VoiceReadOut o = voice_read_on_engine(&r, VRM_ERROR, 0, (long)0x80004005);
    ASSERT_EQ(o.actions, VRA_RELEASE | VRA_ERROR | VRA_VIEW);
    ASSERT_EQ(o.err_engine, VOICE_SPEECH_WINDOWS);
    TEST_END();
}

int test_vread_start_refused(void)
{
    TEST_BEGIN();
    VoiceRead r; voice_read_init(&r);
    VoiceReadPress p = P(7, VOICE_SPEECH_AI);
    voice_read_on_press(&r, &p);
    VoiceReadOut o = voice_read_on_started(&r, 0);
    ASSERT_EQ(o.actions, VRA_ERROR | VRA_VIEW);
    ASSERT_EQ(o.hr, 0L);
    ASSERT_EQ(r.state, VRS_IDLE); ASSERT_EQ(r.ctx, 0);
    TEST_END();
}

int test_vread_error_after_stop_silent_release(void)
{
    TEST_BEGIN();
    VoiceRead r; speaking_ai(&r, 7);
    unsigned s = r.serial;
    VoiceReadOut o = voice_read_on_stop(&r, VRX_ESCAPE);
    ASSERT_EQ(o.actions, VRA_STOP | VRA_VIEW);
    o = voice_read_on_engine(&r, VRM_ERROR, s, -1);
    ASSERT_EQ(o.actions, VRA_RELEASE);
    TEST_END();
}

int test_vread_stop_reasons(void)
{
    TEST_BEGIN();
    VoiceReadStop soft[] = { VRX_DICTATION, VRX_ESCAPE, VRX_CHAT };
    VoiceReadStop hard[] = { VRX_PANEL, VRX_SETTINGS, VRX_SAMPLE };
    for (int i = 0; i < 3; i++) {
        VoiceRead r; speaking_ai(&r, 7);
        VoiceReadOut o = voice_read_on_stop(&r, soft[i]);
        ASSERT_EQ(o.actions, VRA_STOP | VRA_VIEW);
        ASSERT_EQ(r.ctx, 1);
    }
    for (int i = 0; i < 3; i++) {
        VoiceRead r; speaking_ai(&r, 7);
        VoiceReadOut o = voice_read_on_stop(&r, hard[i]);
        ASSERT_EQ(o.actions, VRA_STOP | VRA_RELEASE | VRA_VIEW);
        ASSERT_EQ(r.ctx, 0);
    }
    {
        VoiceRead r; speaking_ai(&r, 7);
        voice_read_on_engine(&r, VRM_DONE, r.serial, 0);
        ASSERT_EQ(voice_read_on_stop(&r, VRX_SETTINGS).actions, VRA_RELEASE);
        speaking_ai(&r, 7);
        voice_read_on_engine(&r, VRM_DONE, r.serial, 0);
        ASSERT_EQ(voice_read_on_stop(&r, VRX_ESCAPE).actions, 0u);
    }
    {
        VoiceReadStop all[] = { VRX_DICTATION, VRX_ESCAPE, VRX_CHAT, VRX_PANEL, VRX_SETTINGS, VRX_SAMPLE };
        for (int i = 0; i < 6; i++) {
            VoiceRead r; voice_read_init(&r);
            ASSERT_EQ(voice_read_on_stop(&r, all[i]).actions, 0u);
        }
    }
    TEST_END();
}

int test_vread_empty_reply(void)
{
    TEST_BEGIN();
    VoiceRead r; voice_read_init(&r);
    VoiceReadPress p = P(7, VOICE_SPEECH_AI); p.has_text = 0;
    ASSERT_EQ(voice_read_on_press(&r, &p).actions, 0u);
    speaking_ai(&r, 9);
    VoiceReadOut o = voice_read_on_press(&r, &p);
    ASSERT_EQ(o.actions, VRA_STOP | VRA_VIEW);
    ASSERT_EQ(r.state, VRS_IDLE);
    TEST_END();
}

int test_vread_engine_change_restarts(void)
{
    TEST_BEGIN();
    VoiceRead r; speaking_ai(&r, 9);
    voice_read_on_engine(&r, VRM_DONE, r.serial, 0);
    VoiceReadPress p = P(7, VOICE_SPEECH_WINDOWS);
    VoiceReadOut o = voice_read_on_press(&r, &p);
    ASSERT_EQ(o.actions, VRA_RELEASE | VRA_START | VRA_VIEW);
    ASSERT_EQ(o.engine, VOICE_SPEECH_WINDOWS);
    TEST_END();
}

int test_vread_unexpected_messages(void)
{
    TEST_BEGIN();
    VoiceRead r; voice_read_init(&r);
    ASSERT_EQ(voice_read_on_started(&r, 1).actions, 0u);
    ASSERT_EQ(voice_read_on_engine(&r, VRM_READY, 0, 0).actions, 0u);
    ASSERT_EQ(voice_read_on_engine(&r, VRM_DONE, 1u, 0).actions, 0u);
    ASSERT_EQ(voice_read_on_engine(&r, VRM_ERROR, 0, -1).actions, 0u);
    TEST_END();
}

int test_vread_error_text(void)
{
    TEST_BEGIN();
    char b[200];
    voice_read_error_text(VOICE_SPEECH_WINDOWS, (long)0x80004005, NULL, b, sizeof b);
    ASSERT_STR_EQ(b, "Could not read the reply aloud: the Windows voice did not start.");
    voice_read_error_text(VOICE_SPEECH_AI, 0, NULL, b, sizeof b);
    ASSERT_STR_EQ(b, "Could not read the reply aloud: AI speech did not start.");
    voice_read_error_text(VOICE_SPEECH_AI, 401, NULL, b, sizeof b);
    ASSERT_STR_EQ(b, "Could not read the reply aloud: the speech provider refused the key (HTTP 401).");
    voice_read_error_text(VOICE_SPEECH_AI, VOICE_AIS_HR_RESOLVE, "Enter a speech API key", b, sizeof b);
    ASSERT_STR_EQ(b, "Could not read the reply aloud: Enter a speech API key.");
    voice_read_error_text(VOICE_SPEECH_AI, 401, NULL, b, 10);
    ASSERT_EQ((int)strlen(b), 9);
    TEST_END();
}
