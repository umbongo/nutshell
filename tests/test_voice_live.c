#include "test_framework.h"
#include "config.h"
#include "voice_live.h"
#include "voice_lang.h"
#include "json_validate.h"
#include "json_parser.h"
#include <stdlib.h>
#include <string.h>

static void ls(Settings *s, const char *prov)
{
    memset(s, 0, sizeof *s);
    snprintf(s->voice_stt_provider, sizeof s->voice_stt_provider, "%s", prov);
}

static int msg_is(char *m, size_t len, const char *want)
{
    int ok = m && strcmp(m, want) == 0 && len == strlen(want);
    if (!ok) printf("  got:  %s\n  want: %s\n", m ? m : "(null)", want);
    free(m);
    return ok;
}

int test_voice_live_resolve(void)
{
    TEST_BEGIN();
    static Settings s;
    VoiceLiveTarget t;
    ls(&s, "openai");
    ASSERT_EQ(voice_live_provider(&s), VLP_OPENAI);
    ASSERT_EQ(voice_live_resolve(&s, &t), 1);
    ASSERT_EQ(t.prov, VLP_OPENAI);
    ASSERT_STR_EQ(t.host, "api.openai.com");
    ASSERT_EQ(t.port, 443);
    ASSERT_EQ(t.secure, 1);
    ASSERT_STR_EQ(t.path, "/v1/realtime?intent=transcription");
    ASSERT_STR_EQ(t.model, "gpt-live-transcribe");
    ASSERT_EQ(t.rate, 24000);
    ASSERT_EQ(t.hangover_ms, VOICE_LIVE_HANGOVER_OPENAI_MS);
    ASSERT_EQ(t.key_from_chat, 0);

    ls(&s, "mistral");
    ASSERT_EQ(voice_live_provider(&s), VLP_MISTRAL);
    ASSERT_EQ(voice_live_resolve(&s, &t), 1);
    ASSERT_STR_EQ(t.host, "api.mistral.ai");
    ASSERT_STR_EQ(t.path, "/v1/audio/transcriptions/realtime?model=voxtral-mini-transcribe-realtime-3");
    ASSERT_EQ(t.rate, 16000);
    ASSERT_EQ(t.hangover_ms, VOICE_LIVE_HANGOVER_MISTRAL_MS);

    const char *no[] = {"groq", "gemini", "custom"};
    for (int i = 0; i < 3; i++) {
        ls(&s, no[i]);
        strcpy(s.voice_stt_url, "https://h/v1");
        ASSERT_EQ(voice_live_provider(&s), VLP_NONE);
        ASSERT_EQ(voice_live_resolve(&s, &t), 0);
    }
    ls(&s, "bogus");
    ASSERT_EQ(voice_live_resolve(&s, &t), 0);

    ls(&s, "groq");
    s.voice_stt_use_ai = 1;
    strcpy(s.ai_provider, "openai");
    ASSERT_EQ(voice_live_provider(&s), VLP_OPENAI);
    ASSERT_EQ(voice_live_resolve(&s, &t), 1);
    ASSERT_EQ(t.key_from_chat, 1);
    strcpy(s.ai_provider, "anthropic");
    ASSERT_EQ(voice_live_resolve(&s, &t), 0);

    ls(&s, "openai");
    strcpy(s.voice_stt_live_model, "gpt-realtime-whisper");
    ASSERT_EQ(voice_live_resolve(&s, &t), 1);
    ASSERT_STR_EQ(t.model, "gpt-realtime-whisper");
    ls(&s, "mistral");
    strcpy(s.voice_stt_live_model, "my.model_2-x");
    ASSERT_EQ(voice_live_resolve(&s, &t), 1);
    ASSERT_STR_EQ(t.path, "/v1/audio/transcriptions/realtime?model=my.model_2-x");
    const char *bad[] = {"a b", "a&b=c", "a/b", "m\"x", "a%20b", "é"};
    for (int i = 0; i < 6; i++) {
        strcpy(s.voice_stt_live_model, bad[i]);
        ASSERT_EQ(voice_live_resolve(&s, &t), 0);
    }
    memset(s.voice_stt_live_model, 'a', sizeof s.voice_stt_live_model - 1);
    ASSERT_EQ(voice_live_resolve(&s, &t), 0);

    ASSERT_NOT_NULL(voice_live_models(VLP_OPENAI));
    ASSERT_STR_EQ(voice_live_models(VLP_OPENAI)[0], "gpt-live-transcribe");
    ASSERT_STR_EQ(voice_live_models(VLP_OPENAI)[1], "gpt-realtime-whisper");
    ASSERT_NULL(voice_live_models(VLP_OPENAI)[2]);
    ASSERT_STR_EQ(voice_live_models(VLP_MISTRAL)[0], "voxtral-mini-transcribe-realtime-3");
    ASSERT_NULL(voice_live_models(VLP_MISTRAL)[1]);
    ASSERT_NULL(voice_live_models(VLP_NONE));
    TEST_END();
}

int test_voice_live_session_openai(void)
{
    TEST_BEGIN();
    static Settings s;
    VoiceLiveTarget t;
    size_t len = 0;
    ls(&s, "openai");
    strcpy(s.voice_stt_language, "en-GB");
    ASSERT_EQ(voice_live_resolve(&s, &t), 1);
    ASSERT_STR_EQ(t.iso, "en");
    ASSERT_NOT_NULL(t.prompt);
    char want[2048], esc[1024];
    const char *hint = voice_lang_spelling_hint("en-GB");
    size_t e = json_escape_string(hint, strlen(hint), esc, sizeof esc, 0, 0);
    ASSERT_TRUE(e > 0);
    esc[e] = '\0';
    snprintf(want, sizeof want,
        "{\"type\":\"session.update\",\"session\":{\"type\":\"transcription\",\"audio\":{\"input\":"
        "{\"format\":{\"type\":\"audio/pcm\",\"rate\":24000},\"transcription\":{\"model\":"
        "\"gpt-live-transcribe\",\"languages\":[\"en\"],\"prompt\":\"%s\"},\"turn_detection\":null}}}}", esc);
    char *m = voice_live_msg_session(&t, &len);
    ASSERT_TRUE(msg_is(m, len, want));

    s.voice_stt_language[0] = '\0';
    ASSERT_EQ(voice_live_resolve(&s, &t), 1);
    m = voice_live_msg_session(&t, &len);
    ASSERT_TRUE(msg_is(m, len,
        "{\"type\":\"session.update\",\"session\":{\"type\":\"transcription\",\"audio\":{\"input\":"
        "{\"format\":{\"type\":\"audio/pcm\",\"rate\":24000},\"transcription\":{\"model\":"
        "\"gpt-live-transcribe\"},\"turn_detection\":null}}}}"));

    strcpy(s.voice_stt_language, "fr");
    ASSERT_EQ(voice_live_resolve(&s, &t), 1);
    m = voice_live_msg_session(&t, &len);
    ASSERT_NOT_NULL(m);
    ASSERT_NOT_NULL(strstr(m, "\"languages\":[\"fr\"]"));
    ASSERT_NULL(strstr(m, "\"prompt\""));
    ASSERT_NULL(strstr(m, "\"language\""));
    ASSERT_NULL(strstr(m, "\"delay\""));
    free(m);
    TEST_END();
}

int test_voice_live_session_mistral(void)
{
    TEST_BEGIN();
    static Settings s;
    VoiceLiveTarget t;
    size_t len = 0;
    ls(&s, "mistral");
    strcpy(s.voice_stt_language, "en-GB");
    ASSERT_EQ(voice_live_resolve(&s, &t), 1);
    char *m = voice_live_msg_session(&t, &len);
    ASSERT_TRUE(msg_is(m, len,
        "{\"type\":\"session.update\",\"session\":{\"audio_format\":{\"encoding\":\"pcm_s16le\","
        "\"sample_rate\":16000},\"target_streaming_delay_ms\":480,\"language\":\"en\"}}"));
    s.voice_stt_language[0] = '\0';
    ASSERT_EQ(voice_live_resolve(&s, &t), 1);
    m = voice_live_msg_session(&t, &len);
    ASSERT_TRUE(msg_is(m, len,
        "{\"type\":\"session.update\",\"session\":{\"audio_format\":{\"encoding\":\"pcm_s16le\","
        "\"sample_rate\":16000},\"target_streaming_delay_ms\":480}}"));
    TEST_END();
}

int test_voice_live_audio_msg(void)
{
    TEST_BEGIN();
    static Settings s;
    VoiceLiveTarget t;
    size_t len = 0;
    const int16_t pcm[3] = {1, -2, 0x1234};
    ls(&s, "openai");
    ASSERT_EQ(voice_live_resolve(&s, &t), 1);
    char *m = voice_live_msg_audio(&t, pcm, 3, &len);
    ASSERT_TRUE(msg_is(m, len, "{\"type\":\"input_audio_buffer.append\",\"audio\":\"AQD+/zQS\"}"));
    ASSERT_NULL(voice_live_msg_audio(&t, pcm, 0, &len));
    ASSERT_NULL(voice_live_msg_audio(&t, NULL, 3, &len));
    ls(&s, "mistral");
    ASSERT_EQ(voice_live_resolve(&s, &t), 1);
    m = voice_live_msg_audio(&t, pcm, 3, &len);
    ASSERT_TRUE(msg_is(m, len, "{\"type\":\"input_audio.append\",\"audio\":\"AQD+/zQS\"}"));
    /* a full 100 ms chunk */
    static int16_t big[1600];
    m = voice_live_msg_audio(&t, big, 1600, &len);
    ASSERT_NOT_NULL(m);
    ASSERT_EQ(len, strlen(m));
    free(m);
    TEST_END();
}

int test_voice_live_gate_and_end_msgs(void)
{
    TEST_BEGIN();
    static Settings s;
    VoiceLiveTarget t;
    size_t len = 99;
    ls(&s, "openai");
    ASSERT_EQ(voice_live_resolve(&s, &t), 1);
    char *m = voice_live_msg_gate_closed(&t, &len);
    ASSERT_TRUE(msg_is(m, len, "{\"type\":\"input_audio_buffer.commit\"}"));
    ASSERT_NULL(voice_live_msg_end(&t, &len));
    ASSERT_EQ(len, 0);
    ls(&s, "mistral");
    ASSERT_EQ(voice_live_resolve(&s, &t), 1);
    m = voice_live_msg_gate_closed(&t, &len);
    ASSERT_TRUE(msg_is(m, len, "{\"type\":\"input_audio.flush\"}"));
    m = voice_live_msg_end(&t, &len);
    ASSERT_TRUE(msg_is(m, len, "{\"type\":\"input_audio.end\"}"));
    TEST_END();
}

int test_voice_live_escape(void)
{
    TEST_BEGIN();
    VoiceLiveTarget t;
    size_t len = 0;
    memset(&t, 0, sizeof t);
    t.prov = VLP_OPENAI;
    t.rate = 24000;
    snprintf(t.model, sizeof t.model, "m\"x\\y\r\nz");
    t.prompt = "p\"q\\r\r\ns";
    snprintf(t.iso, sizeof t.iso, "en");
    char *m = voice_live_msg_session(&t, &len);
    ASSERT_NOT_NULL(m);
    ASSERT_NULL(strchr(m, '\r'));
    ASSERT_NULL(strchr(m, '\n'));
    JsonNode *root = json_parse(m);
    ASSERT_NOT_NULL(root);
    JsonNode *sess = json_obj_get(root, "session");
    JsonNode *au = sess ? json_obj_get(sess, "audio") : NULL;
    JsonNode *in = au ? json_obj_get(au, "input") : NULL;
    JsonNode *tr = in ? json_obj_get(in, "transcription") : NULL;
    ASSERT_NOT_NULL(tr);
    ASSERT_STR_EQ(json_obj_str(tr, "model"), "m\"x\\y\r\nz");
    ASSERT_STR_EQ(json_obj_str(tr, "prompt"), "p\"q\\r\r\ns");
    json_free(root);
    free(m);
    TEST_END();
}

static int parse1(VoiceLiveProvider p, const char *j, VoiceLiveEvent *ev)
{
    return voice_live_parse(p, j, strlen(j), ev);
}

int test_voice_live_parse_openai(void)
{
    TEST_BEGIN();
    VoiceLiveEvent ev;
    ASSERT_EQ(parse1(VLP_OPENAI, "{\"type\":\"session.created\"}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_CREATED);
    ASSERT_NULL(ev.text);
    voice_live_event_free(&ev);
    ASSERT_EQ(parse1(VLP_OPENAI, "{\"type\":\"session.updated\",\"session\":{}}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_READY);
    voice_live_event_free(&ev);
    ASSERT_EQ(parse1(VLP_OPENAI, "{\"type\":\"input_audio_buffer.committed\",\"item_id\":\"item_2\"}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_COMMITTED);
    ASSERT_STR_EQ(ev.item, "item_2");
    voice_live_event_free(&ev);
    ASSERT_EQ(parse1(VLP_OPENAI, "{\"type\":\"conversation.item.input_audio_transcription.delta\","
                     "\"item_id\":\"item_3\",\"delta\":\"Hello,\"}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_DELTA);
    ASSERT_STR_EQ(ev.item, "item_3");
    ASSERT_STR_EQ(ev.text, "Hello,");
    voice_live_event_free(&ev);
    ASSERT_NULL(ev.text);
    ASSERT_EQ(parse1(VLP_OPENAI, "{\"type\":\"conversation.item.input_audio_transcription.completed\","
                     "\"item_id\":\"item_3\",\"transcript\":\"Hello, world.\"}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_COMPLETED);
    ASSERT_STR_EQ(ev.item, "item_3");
    ASSERT_STR_EQ(ev.text, "Hello, world.");
    voice_live_event_free(&ev);
    ASSERT_EQ(parse1(VLP_OPENAI, "{\"type\":\"conversation.item.input_audio_transcription.failed\","
                     "\"item_id\":\"item_4\",\"error\":{\"message\":\"no speech\"}}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_ITEM_FAILED);
    ASSERT_STR_EQ(ev.item, "item_4");
    ASSERT_STR_EQ(ev.text, "no speech");
    voice_live_event_free(&ev);
    ASSERT_EQ(parse1(VLP_OPENAI, "{\"type\":\"error\",\"error\":{\"message\":\"bad key\"}}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_ERROR);
    ASSERT_STR_EQ(ev.text, "bad key");
    voice_live_event_free(&ev);
    ASSERT_EQ(parse1(VLP_OPENAI, "{\"type\":\"error\"}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_ERROR);
    ASSERT_NOT_NULL(ev.text);
    ASSERT_TRUE(ev.text[0] != '\0');
    voice_live_event_free(&ev);
    ASSERT_EQ(parse1(VLP_OPENAI, "{\"type\":\"conversation.item.created\"}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_IGNORE);
    voice_live_event_free(&ev);
    ASSERT_EQ(parse1(VLP_OPENAI, "{not json", &ev), 0);
    ASSERT_EQ(ev.kind, VLE_IGNORE);
    ASSERT_NULL(ev.text);
    ASSERT_EQ(parse1(VLP_OPENAI, "{\"type\":\"conversation.item.input_audio_transcription.delta\","
                     "\"delta\":\"x\"}", &ev), 0);
    ASSERT_EQ(ev.kind, VLE_IGNORE);
    ASSERT_EQ(parse1(VLP_OPENAI, "{\"type\":\"conversation.item.input_audio_transcription.delta\","
                     "\"item_id\":\"i\"}", &ev), 0);
    ASSERT_EQ(parse1(VLP_OPENAI, "{}", &ev), 0);
    ASSERT_EQ(parse1(VLP_OPENAI, "[1]", &ev), 0);
    ASSERT_EQ(voice_live_parse(VLP_OPENAI, NULL, 0, &ev), 0);
    voice_live_event_free(&ev);
    voice_live_event_free(NULL);
    TEST_END();
}

int test_voice_live_parse_mistral(void)
{
    TEST_BEGIN();
    VoiceLiveEvent ev;
    ASSERT_EQ(parse1(VLP_MISTRAL, "{\"type\":\"session.created\"}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_CREATED);
    voice_live_event_free(&ev);
    ASSERT_EQ(parse1(VLP_MISTRAL, "{\"type\":\"session.updated\"}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_READY);
    voice_live_event_free(&ev);
    ASSERT_EQ(parse1(VLP_MISTRAL, "{\"type\":\"transcription.text.delta\",\"text\":\" world\"}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_DELTA);
    ASSERT_STR_EQ(ev.text, " world");
    ASSERT_STR_EQ(ev.item, "");
    voice_live_event_free(&ev);
    ASSERT_EQ(parse1(VLP_MISTRAL, "{\"type\":\"transcription.done\",\"text\":\"Hello world\"}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_DONE);
    ASSERT_STR_EQ(ev.text, "Hello world");
    voice_live_event_free(&ev);
    ASSERT_EQ(parse1(VLP_MISTRAL, "{\"type\":\"error\",\"error\":{\"message\":\"bad\"}}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_ERROR);
    ASSERT_STR_EQ(ev.text, "bad");
    voice_live_event_free(&ev);
    ASSERT_EQ(parse1(VLP_MISTRAL, "{\"type\":\"error\",\"error\":{\"message\":{\"detail\":\"x\"}}}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_ERROR);
    ASSERT_STR_EQ(ev.text, "x");
    voice_live_event_free(&ev);
    ASSERT_EQ(parse1(VLP_MISTRAL, "{\"type\":\"error\",\"error\":{\"message\":{\"code\":3}}}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_ERROR);
    ASSERT_TRUE(ev.text && ev.text[0]);
    voice_live_event_free(&ev);
    ASSERT_EQ(parse1(VLP_MISTRAL, "{\"type\":\"error\",\"error\":\"plain\"}", &ev), 1);
    ASSERT_STR_EQ(ev.text, "plain");
    voice_live_event_free(&ev);
    ASSERT_EQ(parse1(VLP_MISTRAL, "{\"type\":\"transcription.language\",\"language\":\"en\"}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_IGNORE);
    voice_live_event_free(&ev);
    ASSERT_EQ(parse1(VLP_MISTRAL, "{\"type\":\"transcription.text.delta\"}", &ev), 0);
    ASSERT_EQ(ev.kind, VLE_IGNORE);
    ASSERT_EQ(parse1(VLP_MISTRAL, "nope", &ev), 0);
    /* an OpenAI-only type is unknown to Mistral */
    ASSERT_EQ(parse1(VLP_MISTRAL, "{\"type\":\"input_audio_buffer.committed\",\"item_id\":\"a\"}", &ev), 1);
    ASSERT_EQ(ev.kind, VLE_IGNORE);
    voice_live_event_free(&ev);
    TEST_END();
}
