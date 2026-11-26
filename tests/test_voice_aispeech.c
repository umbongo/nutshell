/* tests/test_voice_aispeech.c -- AI speech in core: providers, targets, bound keys,
 * request bodies, Mistral's event parser, failure classes and the offset map. */
#include "test_framework.h"
#include "config.h"
#include "voice_aispeech.h"
#include "voice_config.h"
#include "sse_line.h"
#include "base64.h"
#include "json_parser.h"
#include "secure_zero.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIX_WAV  "tests/fixtures/voice_tts/provisional_mistral_wav.sse"
#define FIX_ERR  "tests/fixtures/voice_tts/provisional_mistral_error.sse"
#define CAP_WAV  "tests/fixtures/voice_tts/mistral_stream_wav.sse"
#define CAP_PCM  "tests/fixtures/voice_tts/mistral_stream_pcm.sse"
#define CAP_ERR  "tests/fixtures/voice_tts/mistral_error_bad_voice.sse"

static void base_settings(Settings *s)
{
    memset(s, 0, sizeof *s);
    snprintf(s->voice_tts_provider, sizeof s->voice_tts_provider, "mistral");
    snprintf(s->voice_stt_provider, sizeof s->voice_stt_provider, "openai");
}

static void set_custom(Settings *s, const char *url, const char *model, const char *voice)
{
    snprintf(s->voice_tts_provider, sizeof s->voice_tts_provider, "custom");
    snprintf(s->voice_tts_url, sizeof s->voice_tts_url, "%s", url);
    snprintf(s->voice_ai_tts_model, sizeof s->voice_ai_tts_model, "%s", model);
    snprintf(s->voice_ai_voice, sizeof s->voice_ai_voice, "%s", voice);
}

/* ---- providers, resolution, keys ---- */

int test_vais_provider_table(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const VoiceAisProvider *p = voice_ais_providers(&n);
    ASSERT_EQ(n, (size_t)2);
    ASSERT_STR_EQ(p[0].id, "mistral");
    ASSERT_STR_EQ(p[1].id, "custom");
    ASSERT_TRUE(voice_ais_provider_find("mistral") != NULL);
    ASSERT_TRUE(voice_ais_provider_find("custom") != NULL);
    ASSERT_NULL(voice_ais_provider_find("openai"));
    ASSERT_NULL(voice_ais_provider_find("groq"));
    ASSERT_NULL(voice_ais_provider_find(""));
    ASSERT_NULL(voice_ais_provider_find(NULL));
    const VoiceAisProvider *m = voice_ais_provider_find("mistral");
    ASSERT_STR_EQ(m->models[0], "voxtral-mini-tts-3-0");
    ASSERT_NULL(m->models[1]);
    ASSERT_STR_EQ(m->voices[0], "voice_1787");
    ASSERT_STR_EQ(m->voices[1], "voice_1786");
    ASSERT_STR_EQ(m->voices[2], "voice_590");
    ASSERT_STR_EQ(m->voices[3], "gb_jane_neutral");
    ASSERT_EQ(m->shape, VAIS_SHAPE_MISTRAL);
    ASSERT_EQ(m->needs_key, 1);
    const VoiceAisProvider *c = voice_ais_provider_find("custom");
    ASSERT_STR_EQ(c->models[0], "kokoro");
    ASSERT_STR_EQ(c->voices[0], "bf_emma");
    ASSERT_STR_EQ(c->voices[1], "af_heart");
    ASSERT_EQ(c->shape, VAIS_SHAPE_OPENAI);
    ASSERT_EQ(c->needs_key, 0);
    TEST_END();
}

int test_vais_resolve_mistral_defaults(void)
{
    TEST_BEGIN();
    static Settings s;
    VoiceAisTarget t;
    char err[200] = "";
    base_settings(&s);
    snprintf(s.tts_api_key, sizeof s.tts_api_key, "sk-m");
    snprintf(s.tts_api_key_for, sizeof s.tts_api_key_for, "mistral");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(t.url, "https://api.mistral.ai/v1/audio/speech");
    ASSERT_STR_EQ(t.model, "voxtral-mini-tts-3-0");
    ASSERT_STR_EQ(t.voice, "voice_1787");
    ASSERT_STR_EQ(t.provider, "mistral");
    ASSERT_STR_EQ(t.host, "api.mistral.ai");
    ASSERT_EQ(t.shape, VAIS_SHAPE_MISTRAL);
    ASSERT_EQ(t.send_instructions, 0);
    ASSERT_EQ(t.key_from, VAIS_KEY_OWN);
    ASSERT_STR_EQ(voice_ais_key(&s, &t), "sk-m");
    /* Typed values win; a typed style is never sent to Mistral. */
    snprintf(s.voice_ai_voice, sizeof s.voice_ai_voice, "voice_590");
    snprintf(s.voice_ai_tts_model, sizeof s.voice_ai_tts_model, "m-x");
    snprintf(s.voice_ai_style, sizeof s.voice_ai_style, "warm");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(t.voice, "voice_590");
    ASSERT_STR_EQ(t.model, "m-x");
    ASSERT_EQ(t.send_instructions, 0);
    TEST_END();
}

int test_vais_resolve_custom(void)
{
    TEST_BEGIN();
    static Settings s;
    VoiceAisTarget t;
    char err[200] = "";
    base_settings(&s);
    set_custom(&s, "  http://localhost:8880/v1/audio/speech/ ", "kokoro", "bf_emma");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 1);   /* an empty key is allowed */
    ASSERT_STR_EQ(t.url, "http://localhost:8880/v1/audio/speech");
    ASSERT_STR_EQ(t.host, "localhost:8880");
    ASSERT_STR_EQ(t.provider, "custom");
    ASSERT_EQ(t.shape, VAIS_SHAPE_OPENAI);
    ASSERT_EQ(t.send_instructions, 0);
    ASSERT_STR_EQ(voice_ais_key(&s, &t), "");
    snprintf(s.voice_ai_style, sizeof s.voice_ai_style, "British, calm");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_EQ(t.send_instructions, 1);
    ASSERT_STR_EQ(t.style, "British, calm");
    snprintf(s.voice_ai_style, sizeof s.voice_ai_style, "   ");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_EQ(t.send_instructions, 0);
    set_custom(&s, "https://h.example/v1", "kokoro", "bf_emma");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(t.url, "https://h.example/v1/audio/speech");
    ASSERT_STR_EQ(t.host, "h.example");

    set_custom(&s, "", "kokoro", "bf_emma");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 0);
    ASSERT_STR_EQ(err, "Enter the speech Base URL");
    set_custom(&s, "   ", "kokoro", "bf_emma");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 0);
    ASSERT_STR_EQ(err, "Enter the speech Base URL");
    set_custom(&s, "ftp://nope", "kokoro", "bf_emma");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 0);
    ASSERT_STR_EQ(err, "The speech Base URL is not valid");
    set_custom(&s, "http://h/v1", "", "bf_emma");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 0);
    ASSERT_STR_EQ(err, "Enter the speech model your server uses");
    set_custom(&s, "http://h/v1", "kokoro", "");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 0);
    ASSERT_STR_EQ(err, "Enter the voice your server uses");
    TEST_END();
}

int test_vais_unknown_provider(void)
{
    TEST_BEGIN();
    static Settings s;
    VoiceAisTarget t;
    char err[200];
    static const char *const ids[] = {"openai", "gemini", "groq", ""};
    for (size_t i = 0; i < 4; i++) {
        base_settings(&s);
        snprintf(s.voice_tts_provider, sizeof s.voice_tts_provider, "%s", ids[i]);
        snprintf(s.tts_api_key, sizeof s.tts_api_key, "k");
        snprintf(s.tts_api_key_for, sizeof s.tts_api_key_for, "mistral");
        err[0] = '\0';
        ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 0);
        ASSERT_STR_EQ(err, "The speech provider is not known");
        ASSERT_EQ(voice_ais_key_shared(&s), 0);
        char b[64];
        voice_ais_binding(&s, b, sizeof b);
        ASSERT_STR_EQ(b, "");
    }
    TEST_END();
}

/* R43: a key typed for one binding is not kept once the page's binding differs, and the
 * saved key is then not offered for the new binding. */
int test_vais_key_rebind(void)
{
    TEST_BEGIN();
    static Settings s;
    VoiceAisTarget t;
    char typed[CFG_STR_MAX], now[CFG_STR_MAX], err[200];
    base_settings(&s);
    voice_ais_binding(&s, typed, sizeof typed);
    ASSERT_STR_EQ(typed, "mistral");
    /* unchanged binding: kept */
    voice_ais_binding(&s, now, sizeof now);
    ASSERT_EQ(voice_ais_key_still_bound(typed, now), 1);
    /* the exact scenario: key typed under Mistral, provider then Custom with a URL */
    snprintf(s.tts_api_key, sizeof s.tts_api_key, "KEY");
    snprintf(s.tts_api_key_for, sizeof s.tts_api_key_for, "%s", typed);
    set_custom(&s, "http://some-host:8880/v1", "kokoro", "bf_emma");
    voice_ais_binding(&s, now, sizeof now);
    ASSERT_TRUE(strcmp(now, typed) != 0);
    ASSERT_EQ(voice_ais_key_still_bound(typed, now), 0);
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 1);   /* custom needs no key */
    ASSERT_STR_EQ(voice_ais_key(&s, &t), "");                   /* not offered to the new host */
    /* typed for custom host A, URL now host B: not kept */
    char a[CFG_STR_MAX];
    set_custom(&s, "http://host-a/v1", "kokoro", "bf_emma");
    voice_ais_binding(&s, a, sizeof a);
    set_custom(&s, "http://host-b/v1", "kokoro", "bf_emma");
    voice_ais_binding(&s, now, sizeof now);
    ASSERT_EQ(voice_ais_key_still_bound(a, now), 0);
    ASSERT_EQ(voice_ais_key_still_bound(NULL, now), 0);
    ASSERT_EQ(voice_ais_key_still_bound(a, NULL), 0);
    TEST_END();
}

/* voice_ais_resolve fills t->host before a later error (the Settings privacy line reads it). */
int test_vais_resolve_host_on_error(void)
{
    TEST_BEGIN();
    static Settings s;
    VoiceAisTarget t;
    char err[200];
    base_settings(&s);
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 0);   /* Mistral without a key */
    ASSERT_STR_EQ(err, "Enter a speech API key");
    ASSERT_STR_EQ(t.host, "api.mistral.ai");
    set_custom(&s, "http://some-host:8880/v1", "", "bf_emma");   /* Custom without a model */
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 0);
    ASSERT_STR_EQ(err, "Enter the speech model your server uses");
    ASSERT_STR_EQ(t.host, "some-host:8880");
    TEST_END();
}

int test_vais_key_shared(void)
{
    TEST_BEGIN();
    static Settings s;
    VoiceAisTarget t;
    char err[200];
    base_settings(&s);
    snprintf(s.voice_stt_provider, sizeof s.voice_stt_provider, "mistral");
    snprintf(s.stt_api_key, sizeof s.stt_api_key, "stt-key");
    ASSERT_EQ(voice_ais_key_shared(&s), (int)VAIS_KEY_STT);
    snprintf(s.tts_api_key, sizeof s.tts_api_key, "own");
    snprintf(s.tts_api_key_for, sizeof s.tts_api_key_for, "mistral");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_EQ(t.key_from, VAIS_KEY_STT);
    ASSERT_STR_EQ(voice_ais_key(&s, &t), "stt-key");   /* the saved key is kept, unused */

    /* "Use current AI assistant" with a Custom chat at the same base. */
    base_settings(&s);
    s.voice_stt_use_ai = 1;
    snprintf(s.ai_provider, sizeof s.ai_provider, "custom");
    snprintf(s.ai_custom_url, sizeof s.ai_custom_url, "http://h/v1/chat/completions");
    snprintf(s.ai_api_key, sizeof s.ai_api_key, "chat-key");
    set_custom(&s, "http://h/v1", "kokoro", "bf_emma");
    ASSERT_EQ(voice_ais_key_shared(&s), (int)VAIS_KEY_CHAT);
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_EQ(t.key_from, VAIS_KEY_CHAT);
    ASSERT_STR_EQ(voice_ais_key(&s, &t), "chat-key");
    /* A Mistral chat cannot lend its key: "Use current AI assistant" is not offered for it. */
    snprintf(s.ai_provider, sizeof s.ai_provider, "mistral");
    snprintf(s.voice_tts_provider, sizeof s.voice_tts_provider, "mistral");
    ASSERT_EQ(voice_ais_key_shared(&s), (int)VAIS_KEY_OWN);
    TEST_END();
}

int test_vais_key_not_shared(void)
{
    TEST_BEGIN();
    static Settings s;
    base_settings(&s);                                   /* STT openai, TTS mistral */
    ASSERT_EQ(voice_ais_key_shared(&s), (int)VAIS_KEY_OWN);

    /* Custom with different bases. */
    base_settings(&s);
    snprintf(s.voice_stt_provider, sizeof s.voice_stt_provider, "custom");
    snprintf(s.voice_stt_url, sizeof s.voice_stt_url, "http://a/v1");
    set_custom(&s, "http://b/v1", "kokoro", "bf_emma");
    ASSERT_EQ(voice_ais_key_shared(&s), (int)VAIS_KEY_OWN);
    /* The same base, spelled differently, is shared. */
    snprintf(s.voice_stt_url, sizeof s.voice_stt_url, "http://b/v1/audio/transcriptions/");
    ASSERT_EQ(voice_ais_key_shared(&s), (int)VAIS_KEY_STT);

    /* An unresolved STT target means no sharing, even with the same provider id. */
    base_settings(&s);
    snprintf(s.voice_stt_provider, sizeof s.voice_stt_provider, "custom");
    s.voice_stt_url[0] = '\0';
    set_custom(&s, "http://b/v1", "kokoro", "bf_emma");
    ASSERT_EQ(voice_ais_key_shared(&s), (int)VAIS_KEY_OWN);
    base_settings(&s);
    snprintf(s.voice_stt_provider, sizeof s.voice_stt_provider, "nonesuch");
    ASSERT_EQ(voice_ais_key_shared(&s), (int)VAIS_KEY_OWN);
    /* Chat provider that cannot do STT. */
    base_settings(&s);
    s.voice_stt_use_ai = 1;
    snprintf(s.ai_provider, sizeof s.ai_provider, "anthropic");
    ASSERT_EQ(voice_ais_key_shared(&s), (int)VAIS_KEY_OWN);
    TEST_END();
}

int test_vais_key_binding(void)
{
    TEST_BEGIN();
    static Settings s;
    VoiceAisTarget t;
    char err[200], b[CFG_STR_MAX];
    base_settings(&s);
    snprintf(s.tts_api_key, sizeof s.tts_api_key, "the-key");
    snprintf(s.tts_api_key_for, sizeof s.tts_api_key_for, "mistral");
    voice_ais_binding(&s, b, sizeof b);
    ASSERT_STR_EQ(b, "mistral");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(voice_ais_key(&s, &t), "the-key");

    /* The same key under Custom is not used. */
    set_custom(&s, "http://a/v1", "kokoro", "bf_emma");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(voice_ais_key(&s, &t), "");

    /* A key bound to custom http://a/v1 follows only that base. */
    snprintf(s.tts_api_key_for, sizeof s.tts_api_key_for, "custom http://a/v1");
    voice_ais_binding(&s, b, sizeof b);
    ASSERT_STR_EQ(b, "custom http://a/v1");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(voice_ais_key(&s, &t), "the-key");
    snprintf(s.voice_tts_url, sizeof s.voice_tts_url, "http://b/v1");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(voice_ais_key(&s, &t), "");
    snprintf(s.voice_tts_url, sizeof s.voice_tts_url, "http://a/v1/");   /* same base */
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(voice_ais_key(&s, &t), "the-key");

    /* Back to Mistral: the custom-bound key is not its key. */
    snprintf(s.voice_tts_provider, sizeof s.voice_tts_provider, "mistral");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 0);
    ASSERT_STR_EQ(err, "Enter a speech API key");
    ASSERT_STR_EQ(voice_ais_key(&s, &t), "");
    /* No binding at all is no key. */
    s.tts_api_key_for[0] = '\0';
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 0);
    ASSERT_STR_EQ(err, "Enter a speech API key");
    TEST_END();
}

int test_vais_binding_too_long_never_matches(void)
{
    TEST_BEGIN();
    static Settings s;
    VoiceAisTarget t;
    char err[200], b[CFG_STR_MAX];
    base_settings(&s);
    char url[CFG_STR_MAX];
    memcpy(url, "http://", 7);
    memset(url + 7, 'h', 245);          /* base of 252 bytes: "custom " + it is 259 */
    url[252] = '\0';
    set_custom(&s, url, "kokoro", "bf_emma");
    memset(b, 'x', sizeof b - 1);
    b[sizeof b - 1] = '\0';
    voice_ais_binding(&s, b, sizeof b);
    ASSERT_STR_EQ(b, "");               /* it does not fit: nothing, never a prefix */
    /* The stored runtime field can only hold the cut-off prefix; it must not match. */
    char full[CFG_STR_MAX + 16];
    snprintf(full, sizeof full, "custom %s", url);
    memcpy(s.tts_api_key_for, full, sizeof s.tts_api_key_for - 1);
    s.tts_api_key_for[sizeof s.tts_api_key_for - 1] = '\0';
    ASSERT_EQ(strlen(s.tts_api_key_for), sizeof s.tts_api_key_for - 1);
    snprintf(s.tts_api_key, sizeof s.tts_api_key, "secret");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(voice_ais_key(&s, &t), "");
    /* A small buffer is also "" rather than a cut string. */
    set_custom(&s, "http://a/v1", "kokoro", "bf_emma");
    voice_ais_binding(&s, b, 8);
    ASSERT_STR_EQ(b, "");
    voice_ais_binding(&s, b, 0);        /* no write, no crash */
    TEST_END();
}

/* ---- bodies ---- */

static size_t json_member_count(const JsonNode *o)
{
    return o->as.obj.keys.size;
}

static VoiceAisTarget mk_target(VoiceAisShape shape, int instr)
{
    VoiceAisTarget t;
    memset(&t, 0, sizeof t);
    snprintf(t.model, sizeof t.model, "mdl");
    snprintf(t.voice, sizeof t.voice, "vox");
    snprintf(t.style, sizeof t.style, "calm");
    t.shape = shape;
    t.send_instructions = instr;
    return t;
}

int test_vais_body_mistral(void)
{
    TEST_BEGIN();
    VoiceAisTarget t = mk_target(VAIS_SHAPE_MISTRAL, 1);   /* never sends instructions */
    size_t n = 0;
    char *b = voice_ais_body(&t, "Hello", 5, &n);
    ASSERT_NOT_NULL(b);
    if (b) {
        ASSERT_EQ(n, strlen(b));
        JsonNode *r = json_parse(b);
        ASSERT_NOT_NULL(r);
        if (r) {
            ASSERT_EQ(json_member_count(r), (size_t)5);
            ASSERT_STR_EQ(json_obj_str(r, "model"), "mdl");
            ASSERT_STR_EQ(json_obj_str(r, "input"), "Hello");
            ASSERT_STR_EQ(json_obj_str(r, "voice_id"), "vox");
                    ASSERT_STR_EQ(json_obj_str(r, "response_format"), "wav");
            JsonNode *st = json_obj_get(r, "stream");
            ASSERT_NOT_NULL(st);
            if (st) { ASSERT_EQ(st->type, JSON_BOOL); ASSERT_TRUE(strstr(b, "\"stream\":true") != NULL); }
            ASSERT_NULL(json_obj_get(r, "instructions"));
            ASSERT_NULL(json_obj_get(r, "voice"));
            json_free(r);
        }
        secure_zero(b, n);
        free(b);
    }
    TEST_END();
}

int test_vais_body_openai_shape(void)
{
    TEST_BEGIN();
    VoiceAisTarget t = mk_target(VAIS_SHAPE_OPENAI, 0);
    size_t n = 0;
    char *b = voice_ais_body(&t, "Hi there", 8, &n);
    ASSERT_NOT_NULL(b);
    if (b) {
        JsonNode *r = json_parse(b);
        ASSERT_NOT_NULL(r);
        if (r) {
            ASSERT_EQ(json_member_count(r), (size_t)4);
            ASSERT_STR_EQ(json_obj_str(r, "model"), "mdl");
            ASSERT_STR_EQ(json_obj_str(r, "voice"), "vox");
            ASSERT_STR_EQ(json_obj_str(r, "input"), "Hi there");
            ASSERT_STR_EQ(json_obj_str(r, "response_format"), "wav");
            ASSERT_NULL(json_obj_get(r, "instructions"));
            ASSERT_NULL(json_obj_get(r, "stream"));
            ASSERT_NULL(json_obj_get(r, "voice_id"));
            json_free(r);
        }
        free(b);
    }
    t.send_instructions = 1;
    b = voice_ais_body(&t, "Hi", 2, &n);
    ASSERT_NOT_NULL(b);
    if (b) {
        JsonNode *r = json_parse(b);
        ASSERT_NOT_NULL(r);
        if (r) {
            ASSERT_EQ(json_member_count(r), (size_t)5);
            ASSERT_STR_EQ(json_obj_str(r, "instructions"), "calm");
            json_free(r);
        }
        free(b);
    }
    /* send_instructions with an empty style adds nothing */
    t.style[0] = '\0';
    b = voice_ais_body(&t, "Hi", 2, &n);
    ASSERT_NOT_NULL(b);
    if (b) {
        JsonNode *r = json_parse(b);
        ASSERT_NOT_NULL(r);
        if (r) { ASSERT_NULL(json_obj_get(r, "instructions")); json_free(r); }
        free(b);
    }
    TEST_END();
}

int test_vais_body_escapes(void)
{
    TEST_BEGIN();
    static const char text[] = "say \"hi\" \\ back\nnew\tTab caf\xC3\xA9 ok \xE2\x9C\x93 end";
    for (int shape = 0; shape < 2; shape++) {
        VoiceAisTarget t = mk_target((VoiceAisShape)shape, 0);
        size_t n = 0;
        char *b = voice_ais_body(&t, text, sizeof text - 1, &n);
        ASSERT_NOT_NULL(b);
        if (!b) continue;
        ASSERT_EQ(n, strlen(b));
        JsonNode *r = json_parse(b);
        ASSERT_NOT_NULL(r);
        if (r) {
            JsonNode *in = json_obj_get(r, "input");
            ASSERT_NOT_NULL(in);
            if (in) ASSERT_STR_EQ(in->as.str_val, text);
            json_free(r);
        }
        free(b);
    }
    /* A model or voice with a quote still makes valid JSON. */
    VoiceAisTarget t = mk_target(VAIS_SHAPE_OPENAI, 0);
    snprintf(t.voice, sizeof t.voice, "a\"b");
    size_t n = 0;
    char *b = voice_ais_body(&t, "x", 1, &n);
    ASSERT_NOT_NULL(b);
    if (b) {
        JsonNode *r = json_parse(b);
        ASSERT_NOT_NULL(r);
        if (r) { ASSERT_STR_EQ(json_obj_str(r, "voice"), "a\"b"); json_free(r); }
        free(b);
    }
    TEST_END();
}

int test_vais_brackets_neutralised(void)
{
    TEST_BEGIN();
    char text[] = "[whisper] go [now]";
    const char orig[] = "[whisper] go [now]";
    for (int shape = 0; shape < 2; shape++) {
        VoiceAisTarget t = mk_target((VoiceAisShape)shape, 0);
        size_t n = 0;
        char *b = voice_ais_body(&t, text, strlen(text), &n);
        ASSERT_NOT_NULL(b);
        if (!b) continue;
        JsonNode *r = json_parse(b);
        ASSERT_NOT_NULL(r);
        if (r) {
            JsonNode *in = json_obj_get(r, "input");
            ASSERT_NOT_NULL(in);
            if (in) {
                ASSERT_STR_EQ(in->as.str_val, "(whisper) go (now)");
                ASSERT_EQ(strlen(in->as.str_val), strlen(orig));
            }
            json_free(r);
        }
        free(b);
        ASSERT_STR_EQ(text, orig);              /* the caller's text is unchanged */
    }
    TEST_END();
}

int test_vais_body_rejects_crlf(void)
{
    TEST_BEGIN();
    static const char *const bad[] = {"a\rb", "a\nb", "\r", "x\r\n"};
    for (size_t i = 0; i < 4; i++) {
        VoiceAisTarget t = mk_target(VAIS_SHAPE_MISTRAL, 0);
        size_t n = 99;
        snprintf(t.model, sizeof t.model, "%s", bad[i]);
        ASSERT_NULL(voice_ais_body(&t, "x", 1, &n));
        t = mk_target(VAIS_SHAPE_OPENAI, 0);
        snprintf(t.voice, sizeof t.voice, "%s", bad[i]);
        ASSERT_NULL(voice_ais_body(&t, "x", 1, &n));
    }
    VoiceAisTarget t = mk_target(VAIS_SHAPE_OPENAI, 0);
    ASSERT_NULL(voice_ais_body(NULL, "x", 1, NULL));
    ASSERT_NULL(voice_ais_body(&t, NULL, 1, NULL));
    TEST_END();
}

/* ---- failures ---- */

int test_vais_failure_classes(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_ais_failure_fatal(VOICE_AIS_HR_RESOLVE), 1);
    ASSERT_EQ(voice_ais_failure_fatal(VOICE_AIS_HR_FORMAT), 1);
    ASSERT_EQ(voice_ais_failure_fatal(401), 1);
    ASSERT_EQ(voice_ais_failure_fatal(403), 1);
    ASSERT_EQ(voice_ais_failure_fatal(404), 1);
    ASSERT_EQ(voice_ais_failure_fatal(422), 1);
    ASSERT_EQ(voice_ais_failure_fatal(VOICE_AIS_HR_NET), 0);
    ASSERT_EQ(voice_ais_failure_fatal(VOICE_AIS_HR_DEVICE), 0);
    ASSERT_EQ(voice_ais_failure_fatal(VOICE_AIS_HR_STREAM), 0);
    ASSERT_EQ(voice_ais_failure_fatal(429), 0);
    ASSERT_EQ(voice_ais_failure_fatal(500), 0);
    ASSERT_EQ(voice_ais_failure_fatal(503), 0);
    ASSERT_EQ(voice_ais_failure_fatal(0), 0);
    ASSERT_EQ(voice_ais_failure_fatal(400), 0);
    TEST_END();
}

int test_vais_failure_reasons(void)
{
    TEST_BEGIN();
    char o[200];
    voice_ais_failure_reason(401, NULL, o, sizeof o);
    ASSERT_STR_EQ(o, "the speech provider refused the key (HTTP 401)");
    voice_ais_failure_reason(403, NULL, o, sizeof o);
    ASSERT_STR_EQ(o, "the speech provider refused the key (HTTP 403)");
    voice_ais_failure_reason(404, NULL, o, sizeof o);
    ASSERT_STR_EQ(o, "the speech model or voice was not found (HTTP 404)");
    voice_ais_failure_reason(422, NULL, o, sizeof o);
    ASSERT_STR_EQ(o, "the speech provider refused the request (HTTP 422)");
    voice_ais_failure_reason(VOICE_AIS_HR_FORMAT, NULL, o, sizeof o);
    ASSERT_STR_EQ(o, "unsupported audio format");
    voice_ais_failure_reason(VOICE_AIS_HR_RESOLVE, "Enter a speech API key", o, sizeof o);
    ASSERT_STR_EQ(o, "Enter a speech API key");
    voice_ais_failure_reason(VOICE_AIS_HR_RESOLVE, NULL, o, sizeof o);
    ASSERT_TRUE(o[0] != '\0');
    voice_ais_failure_reason(VOICE_AIS_HR_NET, NULL, o, sizeof o);
    ASSERT_TRUE(o[0] != '\0');
    voice_ais_failure_reason(503, NULL, o, sizeof o);
    ASSERT_TRUE(strstr(o, "503") != NULL);
    char small[8];
    voice_ais_failure_reason(404, NULL, small, sizeof small);   /* cut, terminated */
    ASSERT_EQ(strlen(small), (size_t)7);
    voice_ais_failure_reason(404, NULL, small, 0);              /* no write */
    TEST_END();
}

/* The cap on the audio one request may deliver: 5 minutes at 24 kHz. */
int test_vais_audio_cap(void)
{
    TEST_BEGIN();
    ASSERT_EQ((unsigned long)VOICE_AIS_MAX_FRAMES, 7200000UL);
    ASSERT_EQ(voice_ais_audio_over_cap(0, 0), 0);
    ASSERT_EQ(voice_ais_audio_over_cap(0, VOICE_AIS_MAX_FRAMES), 0);       /* exactly the cap */
    ASSERT_EQ(voice_ais_audio_over_cap(0, (uint64_t)VOICE_AIS_MAX_FRAMES + 1), 1);
    ASSERT_EQ(voice_ais_audio_over_cap(VOICE_AIS_MAX_FRAMES - 1, 1), 0);
    ASSERT_EQ(voice_ais_audio_over_cap(VOICE_AIS_MAX_FRAMES - 1, 2), 1);
    ASSERT_EQ(voice_ais_audio_over_cap(VOICE_AIS_MAX_FRAMES, 0), 0);
    ASSERT_EQ(voice_ais_audio_over_cap(VOICE_AIS_MAX_FRAMES, 1), 1);
    ASSERT_EQ(voice_ais_audio_over_cap(3600000, 3600000), 0);
    ASSERT_EQ(voice_ais_audio_over_cap(3600000, 3600001), 1);
    /* The addition must not wrap around to a small number. */
    ASSERT_EQ(voice_ais_audio_over_cap(UINT64_MAX, 1), 1);
    ASSERT_EQ(voice_ais_audio_over_cap(1, UINT64_MAX), 1);
    ASSERT_EQ(voice_ais_audio_over_cap(UINT64_MAX, UINT64_MAX), 1);
    ASSERT_EQ(voice_ais_audio_over_cap(UINT64_MAX - 5, 10), 1);
    /* the general form, for the wintest's lowered cap */
    ASSERT_EQ(voice_ais_frames_over(0, 100, 100), 0);
    ASSERT_EQ(voice_ais_frames_over(0, 101, 100), 1);
    ASSERT_EQ(voice_ais_frames_over(101, 0, 100), 1);
    ASSERT_EQ(voice_ais_frames_over(UINT64_MAX, 1, 100), 1);
    TEST_END();
}

/* ---- Mistral's events ---- */

#define MAX_EV 64
typedef struct {
    VoiceAisSse st;
    int kinds[MAX_EV];
    int n;
    unsigned char *audio;
    size_t audio_len;
    size_t max_delta;
    int stop_on_terminal;
} EvRec;

static int rec_line(const char *line, size_t len, void *ctx)
{
    EvRec *r = (EvRec *)ctx;
    VoiceAisEv ev;
    int rc = voice_ais_sse_line(&r->st, line, len, &ev);
    if (ev.kind != VAIS_EV_NONE && r->n < MAX_EV) r->kinds[r->n++] = (int)ev.kind;
    if (ev.kind == VAIS_EV_AUDIO) {
        if (ev.audio_len > r->max_delta) r->max_delta = ev.audio_len;
        unsigned char *g = (unsigned char *)realloc(r->audio, r->audio_len + ev.audio_len + 1);
        if (g) {
            r->audio = g;
            memcpy(r->audio + r->audio_len, ev.audio, ev.audio_len);
            r->audio_len += ev.audio_len;
        }
    }
    if (ev.audio) { secure_zero(ev.audio, ev.audio_len); free(ev.audio); }
    return r->stop_on_terminal ? rc : 0;
}

static void rec_init(EvRec *r)
{
    memset(r, 0, sizeof *r);
    voice_ais_sse_init(&r->st);
}

static void rec_free(EvRec *r)
{
    if (r->audio) { secure_zero(r->audio, r->audio_len); free(r->audio); }
    r->audio = NULL;
}

static char *read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (buf && fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); buf = NULL; }
    fclose(f);
    if (buf) { buf[sz] = '\0'; *len = (size_t)sz; }
    return buf;
}

/* Feed a file in `step`-byte pieces through sse_line_feed into the recorder. */
static int feed_file(const char *path, size_t step, EvRec *r)
{
    size_t len = 0;
    char *buf = read_all(path, &len);
    if (!buf) return 0;
    SseLine sl;
    sse_line_init(&sl);
    for (size_t i = 0; i < len; i += step) {
        size_t m = len - i < step ? len - i : step;
        sse_line_feed(&sl, buf + i, m, rec_line, r);
    }
    sse_line_free(&sl);
    free(buf);
    return 1;
}

static int feed_text(const char *text, EvRec *r)
{
    SseLine sl;
    sse_line_init(&sl);
    sse_line_feed(&sl, text, strlen(text), rec_line, r);
    sse_line_free(&sl);
    return 1;
}

/* The assertions every complete good stream satisfies; the WAV one also starts with RIFF. */
static int check_good_stream(const char *path, int expect_riff, int pcm_float)
{
    EvRec r;
    rec_init(&r);
    int ok = feed_file(path, 4096, &r);
    int pass = ok && r.n >= 2 && r.kinds[0] == VAIS_EV_AUDIO &&
               r.kinds[r.n - 1] == VAIS_EV_DONE && r.audio_len > 0;
    for (int i = 0; pass && i < r.n - 1; i++) if (r.kinds[i] != VAIS_EV_AUDIO) pass = 0;
    if (pass && expect_riff)
        pass = r.audio_len > 44 && memcmp(r.audio, "RIFF", 4) == 0 && memcmp(r.audio + 8, "WAVE", 4) == 0;
    if (pass && pcm_float) pass = r.audio_len % 4 == 0;
    rec_free(&r);
    return pass;
}

int test_vais_sse_audio_delta(void)
{
    TEST_BEGIN();
    EvRec r;
    rec_init(&r);
    ASSERT_EQ(feed_file(FIX_WAV, 65536, &r), 1);
    ASSERT_EQ(r.n, 4);
    ASSERT_EQ(r.kinds[0], (int)VAIS_EV_AUDIO);
    ASSERT_EQ(r.kinds[1], (int)VAIS_EV_AUDIO);
    ASSERT_EQ(r.kinds[2], (int)VAIS_EV_AUDIO);
    ASSERT_EQ(r.kinds[3], (int)VAIS_EV_DONE);
    ASSERT_TRUE(r.max_delta >= 20000);            /* the first delta: header and 20,000 bytes */
    ASSERT_EQ(r.audio_len, (size_t)(44 + 20000 + 4800 + 4800));
    ASSERT_TRUE(r.audio_len > 4 && memcmp(r.audio, "RIFF", 4) == 0);
    ASSERT_TRUE(r.audio_len > 12 && memcmp(r.audio + 8, "WAVE", 4) == 0);
    ASSERT_TRUE(r.st.audio_seen == 1);
    rec_free(&r);
    TEST_END();
}

int test_vais_sse_type_only(void)
{
    TEST_BEGIN();
    EvRec r;
    rec_init(&r);
    feed_text("data: {\"type\": \"speech.audio.delta\", \"audio_data\": \"QUJDRA==\"}\n\n"
              "data:{\"type\":\"speech.audio.done\",\"usage\":{}}\n\n", &r);
    ASSERT_EQ(r.n, 2);
    ASSERT_EQ(r.kinds[0], (int)VAIS_EV_AUDIO);
    ASSERT_EQ(r.audio_len, (size_t)4);
    ASSERT_TRUE(r.audio && memcmp(r.audio, "ABCD", 4) == 0);
    ASSERT_EQ(r.kinds[1], (int)VAIS_EV_DONE);
    rec_free(&r);
    /* The event: name alone, with a data object that has no type, is enough too. */
    rec_init(&r);
    feed_text("event: speech.audio.delta\ndata: {\"audio_data\": \"QUJDRA==\"}\n\n", &r);
    ASSERT_EQ(r.n, 1);
    ASSERT_EQ(r.kinds[0], (int)VAIS_EV_AUDIO);
    rec_free(&r);
    TEST_END();
}

int test_vais_sse_done_and_error(void)
{
    TEST_BEGIN();
    EvRec r;
    rec_init(&r);
    feed_text("event: speech.audio.delta\ndata: {\"type\":\"speech.audio.delta\",\"audio_data\":\"QUJDRA==\"}\n\n"
              "event: speech.audio.done\ndata: {\"type\":\"speech.audio.done\",\"usage\":{\"x\":1}}\n\n", &r);
    ASSERT_EQ(r.n, 2);
    ASSERT_EQ(r.kinds[1], (int)VAIS_EV_DONE);
    rec_free(&r);

    rec_init(&r);                               /* an error member */
    feed_text("data: {\"type\":\"speech.audio.delta\",\"audio_data\":\"QUJDRA==\"}\n\n"
              "data: {\"error\":{\"message\":\"boom\"}}\n\n", &r);
    ASSERT_EQ(r.n, 2);
    ASSERT_EQ(r.kinds[1], (int)VAIS_EV_ERROR);
    rec_free(&r);

    rec_init(&r);                               /* an error event, even with no JSON data */
    feed_text("event: error\ndata: whatever\n\n", &r);
    ASSERT_EQ(r.n, 1);
    ASSERT_EQ(r.kinds[0], (int)VAIS_EV_ERROR);
    rec_free(&r);

    rec_init(&r);                               /* the provisional error fixture */
    ASSERT_EQ(feed_file(FIX_ERR, 4096, &r), 1);
    ASSERT_EQ(r.n, 2);
    ASSERT_EQ(r.kinds[0], (int)VAIS_EV_AUDIO);
    ASSERT_EQ(r.kinds[1], (int)VAIS_EV_ERROR);
    rec_free(&r);

    rec_init(&r);                               /* unknown events and fields are ignored */
    feed_text(": a comment\nevent: speech.something.else\ndata: {\"type\":\"speech.something.else\"}\n\n"
              "id: 7\nretry: 100\n\n"
              "data: {\"type\":\"speech.audio.delta\",\"audio_data\":\"QUJDRA==\"}\n\n", &r);
    ASSERT_EQ(r.n, 1);
    ASSERT_EQ(r.kinds[0], (int)VAIS_EV_AUDIO);
    rec_free(&r);

    /* The return value tells the feed to stop on a terminal event only. */
    rec_init(&r);
    r.stop_on_terminal = 1;
    feed_text("data: {\"type\":\"speech.audio.delta\",\"audio_data\":\"QUJDRA==\"}\n\n"
              "data: {\"type\":\"speech.audio.done\"}\n\n"
              "data: {\"type\":\"speech.audio.delta\",\"audio_data\":\"QUJDRA==\"}\n\n", &r);
    ASSERT_EQ(r.n, 2);
    rec_free(&r);
    TEST_END();
}

int test_vais_sse_done_without_audio(void)
{
    TEST_BEGIN();
    EvRec r;
    rec_init(&r);
    feed_text("event: speech.audio.done\ndata: {\"type\":\"speech.audio.done\",\"usage\":{}}\n\n", &r);
    ASSERT_EQ(r.n, 1);
    ASSERT_EQ(r.kinds[0], (int)VAIS_EV_ERROR);
    rec_free(&r);
    /* An empty audio_data is not audio either. */
    rec_init(&r);
    feed_text("data: {\"type\":\"speech.audio.delta\",\"audio_data\":\"\"}\n\n"
              "data: {\"type\":\"speech.audio.done\"}\n\n", &r);
    ASSERT_EQ(r.n, 1);
    ASSERT_EQ(r.kinds[0], (int)VAIS_EV_ERROR);
    rec_free(&r);
    TEST_END();
}

int test_vais_sse_split_lines(void)
{
    TEST_BEGIN();
    EvRec whole, bytes, odd;
    rec_init(&whole);
    rec_init(&bytes);
    rec_init(&odd);
    ASSERT_EQ(feed_file(FIX_WAV, 65536, &whole), 1);
    ASSERT_EQ(feed_file(FIX_WAV, 1, &bytes), 1);
    ASSERT_EQ(feed_file(FIX_WAV, 7, &odd), 1);
    ASSERT_EQ(bytes.n, whole.n);
    ASSERT_EQ(odd.n, whole.n);
    for (int i = 0; i < whole.n && i < bytes.n && i < odd.n; i++) {
        ASSERT_EQ(bytes.kinds[i], whole.kinds[i]);
        ASSERT_EQ(odd.kinds[i], whole.kinds[i]);
    }
    ASSERT_EQ(bytes.audio_len, whole.audio_len);
    ASSERT_EQ(odd.audio_len, whole.audio_len);
    if (bytes.audio_len == whole.audio_len && whole.audio) ASSERT_TRUE(memcmp(bytes.audio, whole.audio, whole.audio_len) == 0);
    if (odd.audio_len == whole.audio_len && whole.audio) ASSERT_TRUE(memcmp(odd.audio, whole.audio, whole.audio_len) == 0);
    rec_free(&whole);
    rec_free(&bytes);
    rec_free(&odd);
    /* CRLF line ends give the same events. */
    EvRec crlf;
    rec_init(&crlf);
    feed_text("event: speech.audio.delta\r\ndata: {\"type\":\"speech.audio.delta\",\"audio_data\":\"QUJDRA==\"}\r\n\r\n"
              "event: speech.audio.done\r\ndata: {\"type\":\"speech.audio.done\"}\r\n\r\n", &crlf);
    ASSERT_EQ(crlf.n, 2);
    rec_free(&crlf);
    TEST_END();
}

int test_vais_sse_bad_base64(void)
{
    TEST_BEGIN();
    static const char *const bad[] = {
        "data: {\"type\":\"speech.audio.delta\",\"audio_data\":\"QU*DRA==\"}\n\n",
        "data: {\"type\":\"speech.audio.delta\",\"audio_data\":\"Q\"}\n\n",
        "data: {\"type\":\"speech.audio.delta\",\"audio_data\":\"QUJD RA==\"}\n\n",
        "data: {\"type\":\"speech.audio.delta\"}\n\n",
        "data: {\"type\":\"speech.audio.delta\",\"audio_data\":5}\n\n",
        "data: not json at all\n\n",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        EvRec r;
        rec_init(&r);
        feed_text(bad[i], &r);
        ASSERT_EQ(r.n, 1);
        ASSERT_EQ(r.kinds[0], (int)VAIS_EV_ERROR);
        ASSERT_EQ(r.audio_len, (size_t)0);
        rec_free(&r);
    }
    TEST_END();
}

/* A forced-error capture: it must never end as a clean reply (no AUDIO then DONE) and it
 * must prove something: the terminal event is ERROR, or the file has no `data:` line at
 * all (a plain, non-SSE HTTP error body). An empty file, or a parser that ignores
 * everything, proves nothing and fails. */
static int check_error_stream(const char *path)
{
    EvRec r;
    rec_init(&r);
    size_t len = 0;
    char *raw = read_all(path, &len);
    if (!raw || len == 0 || !feed_file(path, 4096, &r)) { free(raw); rec_free(&r); return 0; }
    int has_data_line = strstr(raw, "data:") != NULL;
    if (!has_data_line)
        printf("  check_error_stream: %s has no \"data:\" line; accepted as a plain HTTP error body\n", path);
    free(raw);
    int done = 0;
    for (int i = 0; i < r.n; i++) if (r.kinds[i] == VAIS_EV_DONE) done = 1;
    int terminal_error = r.n > 0 && r.kinds[r.n - 1] == VAIS_EV_ERROR;
    int ok = !done && (terminal_error || !has_data_line);
    rec_free(&r);
    return ok;
}

int test_vais_capture_error_check_not_empty(void)
{
    TEST_BEGIN();
    static const char *const empty = TEST_TMP_DIR "/nutshell_vais_empty.sse";
    static const char *const body = TEST_TMP_DIR "/nutshell_vais_body.sse";
    static const char *const clean = TEST_TMP_DIR "/nutshell_vais_clean.sse";
    static const char *const ignored = TEST_TMP_DIR "/nutshell_vais_ignored.sse";
    FILE *f = test_fopen_private(empty);
    ASSERT_NOT_NULL(f);
    if (f) fclose(f);
    f = test_fopen_private(body);
    ASSERT_NOT_NULL(f);
    if (f) { fputs("{\"detail\": \"bad voice\"}\n", f); fclose(f); }
    f = test_fopen_private(clean);
    ASSERT_NOT_NULL(f);
    if (f) {
        fputs("data: {\"type\":\"speech.audio.delta\",\"audio_data\":\"QUJDRA==\"}\n\n"
              "data: {\"type\":\"speech.audio.done\"}\n\n", f);
        fclose(f);
    }
    f = test_fopen_private(ignored);
    ASSERT_NOT_NULL(f);
    if (f) { fputs("event: speech.unknown\ndata: {\"type\":\"speech.unknown\"}\n\n", f); fclose(f); }
    ASSERT_EQ(check_error_stream(FIX_ERR), 1);     /* a real error stream passes */
    ASSERT_EQ(check_error_stream(body), 1);        /* a non-SSE HTTP error body, stated as such */
    ASSERT_EQ(check_error_stream(empty), 0);       /* nothing at all proves nothing */
    ASSERT_EQ(check_error_stream(clean), 0);       /* a clean reply is not an error capture */
    ASSERT_EQ(check_error_stream(ignored), 0);     /* SSE data that the parser ignored proves nothing */
    ASSERT_EQ(check_error_stream(TEST_TMP_DIR "/nutshell_vais_missing.sse"), 0);
    remove(empty);
    remove(body);
    remove(clean);
    remove(ignored);
    TEST_END();
}

/* Task 11: replace with the capture's real shape (these three assert the provisional one). */
int test_vais_sse_capture_wav(void)
{
    TEST_BEGIN();
    FILE *f = fopen(CAP_WAV, "rb");
    if (!f) TEST_SKIP("no capture: " CAP_WAV " (Task 0)");
    fclose(f);
    ASSERT_TRUE(check_good_stream(CAP_WAV, 1, 0));
    TEST_END();
}

/* Task 11: replace with the capture's real shape */
int test_vais_sse_capture_pcm(void)
{
    TEST_BEGIN();
    FILE *f = fopen(CAP_PCM, "rb");
    if (!f) TEST_SKIP("no capture: " CAP_PCM " (Task 0)");
    fclose(f);
    ASSERT_TRUE(check_good_stream(CAP_PCM, 0, 1));
    TEST_END();
}

/* Task 11: replace with the capture's real shape */
int test_vais_sse_capture_error(void)
{
    TEST_BEGIN();
    FILE *f = fopen(CAP_ERR, "rb");
    if (!f) TEST_SKIP("no capture: " CAP_ERR " (Task 0)");
    fclose(f);
    ASSERT_TRUE(check_error_stream(CAP_ERR));
    TEST_END();
}

/* I2: under another event name, data that is not JSON is ignored (R28). */
int test_vais_sse_unknown_event_non_json_ignored(void)
{
    TEST_BEGIN();
    EvRec r;
    rec_init(&r);
    feed_text("event: keepalive\ndata: ping\n\n"
              "data: {\"type\":\"speech.audio.delta\",\"audio_data\":\"QUJDRA==\"}\n\n", &r);
    ASSERT_EQ(r.n, 1);
    ASSERT_EQ(r.kinds[0], (int)VAIS_EV_AUDIO);
    rec_free(&r);
    /* With no event name, or a known one, non-JSON is still a failure. */
    rec_init(&r);
    feed_text("data: ping\n\n", &r);
    ASSERT_EQ(r.n, 1);
    ASSERT_EQ(r.kinds[0], (int)VAIS_EV_ERROR);
    rec_free(&r);
    rec_init(&r);
    feed_text("event: speech.audio.delta\ndata: ping\n\n", &r);
    ASSERT_EQ(r.n, 1);
    ASSERT_EQ(r.kinds[0], (int)VAIS_EV_ERROR);
    rec_free(&r);
    TEST_END();
}

/* Task 7b: under the done name, data that is not JSON fails the reply (R28 as amended). */
int test_vais_sse_done_non_json_is_error(void)
{
    TEST_BEGIN();
    EvRec r;
    rec_init(&r);
    feed_text("data: {\"type\":\"speech.audio.delta\",\"audio_data\":\"QUJDRA==\"}\n\n"
              "event: speech.audio.done\ndata: garbage not json\n\n", &r);
    ASSERT_EQ(r.n, 2);
    ASSERT_EQ(r.kinds[1], (int)VAIS_EV_ERROR);
    rec_free(&r);
    /* the same done with JSON data still ends cleanly, and with no data at all too */
    rec_init(&r);
    feed_text("data: {\"type\":\"speech.audio.delta\",\"audio_data\":\"QUJDRA==\"}\n\n"
              "event: speech.audio.done\ndata: {\"type\":\"speech.audio.done\"}\n\n", &r);
    ASSERT_EQ(r.n, 2);
    ASSERT_EQ(r.kinds[1], (int)VAIS_EV_DONE);
    rec_free(&r);
    TEST_END();
}

/* M2: a JSON null error member is not an error. */
int test_vais_sse_null_error_member(void)
{
    TEST_BEGIN();
    EvRec r;
    rec_init(&r);
    feed_text("data: {\"type\":\"speech.audio.delta\",\"audio_data\":\"QUJDRA==\"}\n\n"
              "data: {\"type\":\"speech.audio.done\",\"error\":null}\n\n", &r);
    ASSERT_EQ(r.n, 2);
    ASSERT_EQ(r.kinds[1], (int)VAIS_EV_DONE);
    rec_free(&r);
    rec_init(&r);
    feed_text("data: {\"error\":null,\"x\":1}\n\n", &r);
    ASSERT_EQ(r.n, 0);
    rec_free(&r);
    TEST_END();
}

/* M6: an unknown event's name is carried (name only) so it can be logged. */
int test_vais_sse_unknown_event_name(void)
{
    TEST_BEGIN();
    VoiceAisSse st;
    VoiceAisEv ev;
    voice_ais_sse_init(&st);
    voice_ais_sse_line(&st, "event: speech.something.else", 28, &ev);
    ASSERT_EQ(ev.kind, VAIS_EV_NONE);
    const char *d = "data: {\"type\":\"speech.something.else\",\"secret\":\"hush\"}";
    voice_ais_sse_line(&st, d, strlen(d), &ev);
    ASSERT_EQ(ev.kind, VAIS_EV_NONE);
    ASSERT_STR_EQ(ev.event, "speech.something.else");
    ASSERT_TRUE(strstr(ev.event, "hush") == NULL);
    voice_ais_sse_line(&st, "", 0, &ev);
    ASSERT_STR_EQ(ev.event, "");
    /* Type only, no event: line. */
    const char *d2 = "data: {\"type\":\"speech.other\"}";
    voice_ais_sse_line(&st, d2, strlen(d2), &ev);
    ASSERT_EQ(ev.kind, VAIS_EV_NONE);
    ASSERT_STR_EQ(ev.event, "speech.other");
    /* A known event carries no name. */
    const char *d3 = "data: {\"type\":\"speech.audio.delta\",\"audio_data\":\"QUJDRA==\"}";
    voice_ais_sse_line(&st, d3, strlen(d3), &ev);
    ASSERT_EQ(ev.kind, VAIS_EV_AUDIO);
    ASSERT_STR_EQ(ev.event, "");
    secure_zero(ev.audio, ev.audio_len);
    free(ev.audio);
    TEST_END();
}

/* M7: the shared key's empty message names the speech-to-text key. */
int test_vais_resolve_shared_key_message(void)
{
    TEST_BEGIN();
    static Settings s;
    VoiceAisTarget t;
    char err[200] = "";
    base_settings(&s);
    snprintf(s.voice_stt_provider, sizeof s.voice_stt_provider, "mistral");
    ASSERT_EQ(voice_ais_key_shared(&s), (int)VAIS_KEY_STT);
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 0);
    ASSERT_STR_EQ(err, "Enter a speech-to-text API key");
    snprintf(s.stt_api_key, sizeof s.stt_api_key, "k");
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 1);
    /* Not shared: the own-key message. */
    base_settings(&s);
    ASSERT_EQ(voice_ais_resolve(&s, &t, err, sizeof err), 0);
    ASSERT_STR_EQ(err, "Enter a speech API key");
    TEST_END();
}

/* ---- the offset map ---- */

/* The chunk "One two three four." sits at byte 10 of the text. */
static const char k_text[] = "0123456789One two three four.";

int test_vais_offset_complete(void)
{
    TEST_BEGIN();
    size_t from = 10, len = 19;
    ASSERT_EQ(voice_ais_offset(k_text, from, len, 0, 1000, 1), (size_t)10);
    size_t half = voice_ais_offset(k_text, from, len, 500, 1000, 1);
    ASSERT_EQ(half, (size_t)(10 + 8));              /* the start of "three" */
    ASSERT_TRUE(strncmp(k_text + half, "three", 5) == 0);
    ASSERT_EQ(voice_ais_offset(k_text, from, len, 1000, 1000, 1), (size_t)29);
    ASSERT_EQ(voice_ais_offset(k_text, from, len, 5000, 1000, 1), (size_t)29);   /* past the end */
    ASSERT_EQ(voice_ais_offset(k_text, from, len, 10, 0, 1), (size_t)10);        /* nothing synthesised */
    ASSERT_EQ(voice_ais_offset(k_text, from, 0, 10, 100, 1), (size_t)10);
    ASSERT_EQ(voice_ais_offset(NULL, from, len, 10, 100, 1), from);
    TEST_END();
}

int test_vais_offset_estimate(void)
{
    TEST_BEGIN();
    size_t from = 10, len = 19;
    size_t o = voice_ais_offset(k_text, from, len, (uint64_t)VOICE_AIS_EST_FRAMES_PER_BYTE * 5, 0, 0);
    ASSERT_TRUE(o <= 15);
    ASSERT_TRUE(o >= from);
    ASSERT_EQ(o, (size_t)14);                       /* the word start at or before byte 15: "two" */
    ASSERT_EQ(voice_ais_offset(k_text, from, len, 0, 0, 0), (size_t)10);
    /* The estimate never passes the chunk end, however much has played. */
    ASSERT_EQ(voice_ais_offset(k_text, from, len, (uint64_t)VOICE_AIS_EST_FRAMES_PER_BYTE * 500, 0, 0), (size_t)29);
    ASSERT_EQ(voice_ais_offset(k_text, from, len, UINT64_MAX, 0, 0), (size_t)29);
    ASSERT_EQ(voice_ais_offset(k_text, from, len, UINT64_MAX, UINT64_MAX, 1), (size_t)29);
    for (uint64_t p = 0; p < 40; p++) {
        size_t v = voice_ais_offset(k_text, from, len, p * VOICE_AIS_EST_FRAMES_PER_BYTE, 0, 0);
        ASSERT_TRUE(v >= from && v <= from + len);
    }
    TEST_END();
}

int test_vais_offset_utf8(void)
{
    TEST_BEGIN();
    /* Words of multi-byte characters and one long run with no spaces. */
    static const char t[] = "caf\xC3\xA9 \xE2\x9C\x93\xE2\x9C\x93\xE2\x9C\x93 na\xC3\xAFve \xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C";
    size_t len = sizeof t - 1;
    for (uint64_t played = 0; played <= 100; played++) {
        size_t a = voice_ais_offset(t, 0, len, played, 100, 1);
        ASSERT_TRUE(a <= len);
        ASSERT_TRUE(a == len || ((unsigned char)t[a] & 0xC0) != 0x80);
        size_t b = voice_ais_offset(t, 0, len, played * VOICE_AIS_EST_FRAMES_PER_BYTE / 4, 0, 0);
        ASSERT_TRUE(b <= len);
        ASSERT_TRUE(b == len || ((unsigned char)t[b] & 0xC0) != 0x80);
    }
    /* With a chunk that starts mid-text, the same holds. */
    for (uint64_t played = 0; played <= 100; played++) {
        size_t a = voice_ais_offset(t, 6, len - 6, played, 100, 1);
        ASSERT_TRUE(a >= 6 && a <= len);
        ASSERT_TRUE(a == len || ((unsigned char)t[a] & 0xC0) != 0x80);
    }
    TEST_END();
}

/* ---- TEST_SKIP itself ---- */

static int skipping_test(void)
{
    TEST_BEGIN();
    TEST_SKIP("framework self-test, not counted");
    ASSERT_TRUE(0);                                 /* never reached */
    TEST_END();
}

/* A test that has already failed must not hide it behind a skip. */
static int fail_then_skip(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(0 == 1);                            /* expected: framework self-test */
    TEST_SKIP("framework self-test, not counted");
    TEST_END();
}

int test_framework_skip_after_failure_fails(void)
{
    TEST_BEGIN();
    int skipped0 = _tf_skipped, failed0 = _tf_failed, run0 = _tf_run;
    printf("  (framework self-test: the next FAIL line is expected)\n");
    int rc = fail_then_skip();
    ASSERT_EQ(rc, 1);
    ASSERT_EQ(_tf_failed - failed0, 1);
    ASSERT_EQ(_tf_skipped - skipped0, 0);
    /* Take the demonstration back out. */
    _tf_failed = failed0;
    _tf_run = run0;
    TEST_END();
}

int test_framework_skip_counts(void)
{
    TEST_BEGIN();
    int skipped0 = _tf_skipped, failed0 = _tf_failed;
    int rc = skipping_test();
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(_tf_skipped - skipped0, 1);
    ASSERT_EQ(_tf_failed - failed0, 0);
    /* Take the demonstration back out, so the summary counts only real skips. */
    _tf_skipped = skipped0;
    _tf_run--;
        TEST_END();
}
