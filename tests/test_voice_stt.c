#include "test_framework.h"
#include "config.h"
#include "voice_stt.h"
#include "base64.h"
#include "json_parser.h"
#include <stdlib.h>
#include <string.h>

static int has_model(const SttProvider *p, const char *m)
{
    for (size_t i = 0; p->models[i]; i++)
        if (strcmp(p->models[i], m) == 0) return 1;
    return 0;
}

/* memmem is not portable to MinGW; a plain search. */
static const char *find_bytes(const char *h, size_t hl, const char *n, size_t nl)
{
    if (nl == 0 || hl < nl) return NULL;
    for (size_t i = 0; i + nl <= hl; i++)
        if (memcmp(h + i, n, nl) == 0) return h + i;
    return NULL;
}

static const char *find_str(const char *h, size_t hl, const char *n)
{
    return find_bytes(h, hl, n, strlen(n));
}

int test_voice_stt_provider_table(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const SttProvider *t = voice_stt_providers(&n);
    ASSERT_EQ(n, (size_t)5);
    ASSERT_STR_EQ(t[0].id, "openai");
    ASSERT_STR_EQ(t[1].id, "groq");
    ASSERT_STR_EQ(t[2].id, "mistral");
    ASSERT_STR_EQ(t[3].id, "gemini");
    ASSERT_STR_EQ(t[4].id, "custom");
    ASSERT_STR_EQ(t[0].label, "OpenAI");
    ASSERT_STR_EQ(t[3].label, "Google Gemini");
    ASSERT_STR_EQ(t[0].models[0], "gpt-4o-mini-transcribe");
    ASSERT_STR_EQ(t[1].models[0], "whisper-large-v3-turbo");
    ASSERT_STR_EQ(t[2].models[0], "voxtral-mini-transcribe-3");
    ASSERT_STR_EQ(t[3].models[0], "gemini-2.5-flash");
    ASSERT_TRUE(has_model(&t[0], "gpt-transcribe"));
    ASSERT_TRUE(has_model(&t[2], "voxtral-mini-latest"));
    ASSERT_EQ(t[3].shape, STT_SHAPE_GEMINI_CHAT);
    ASSERT_EQ(t[0].shape, STT_SHAPE_OPENAI);
    ASSERT_STR_EQ(t[4].base, "");
    ASSERT_NOT_NULL(voice_stt_provider_find("groq"));
    ASSERT_NULL(voice_stt_provider_find("nope"));
    ASSERT_NULL(voice_stt_provider_find(NULL));
    TEST_END();
}

int test_voice_base_url_normalise(void)
{
    TEST_BEGIN();
    char o[256];
    ASSERT_EQ(voice_base_url_normalise("https://h/v1/", o, sizeof o), 1);
    ASSERT_STR_EQ(o, "https://h/v1");
    ASSERT_EQ(voice_base_url_normalise("https://h/v1/audio/transcriptions", o, sizeof o), 1);
    ASSERT_STR_EQ(o, "https://h/v1");
    ASSERT_EQ(voice_base_url_normalise("https://h/v1/audio/speech/", o, sizeof o), 1);
    ASSERT_STR_EQ(o, "https://h/v1");
    ASSERT_EQ(voice_base_url_normalise("  http://localhost:8000/v1  ", o, sizeof o), 1);
    ASSERT_STR_EQ(o, "http://localhost:8000/v1");
    ASSERT_EQ(voice_base_url_normalise("", o, sizeof o), 0);
    ASSERT_EQ(voice_base_url_normalise("ftp://x", o, sizeof o), 0);
    ASSERT_EQ(voice_base_url_normalise("/audio/transcriptions", o, sizeof o), 0);
    ASSERT_EQ(voice_base_url_normalise(NULL, o, sizeof o), 0);
    char small[8];
    ASSERT_EQ(voice_base_url_normalise("https://h/v1", small, sizeof small), 0);
    TEST_END();
}

int test_voice_stt_use_ai_reason(void)
{
    TEST_BEGIN();
    ASSERT_NULL(voice_stt_use_ai_reason("openai", ""));
    ASSERT_NULL(voice_stt_use_ai_reason("gemini", ""));
    ASSERT_NULL(voice_stt_use_ai_reason("custom", "https://h/v1/chat/completions/"));
    ASSERT_STR_EQ(voice_stt_use_ai_reason("anthropic", ""),
                  "Anthropic has no speech-to-text API");
    ASSERT_STR_EQ(voice_stt_use_ai_reason("deepseek", ""),
                  "DeepSeek has no speech-to-text API");
    ASSERT_STR_EQ(voice_stt_use_ai_reason("moonshot", ""),
                  "Moonshot has no speech-to-text API");
    ASSERT_STR_EQ(voice_stt_use_ai_reason("custom", "https://h/api/generate"),
                  "Set a separate voice provider: the custom chat URL has no /chat/completions to derive from");
    ASSERT_NOT_NULL(voice_stt_use_ai_reason("custom", ""));
    TEST_END();
}

static void base_settings(Settings *s)
{
    memset(s, 0, sizeof *s);
}

int test_voice_stt_resolve_use_ai(void)
{
    TEST_BEGIN();
    static Settings s;
    SttTarget t;
    char err[200];
    base_settings(&s);
    s.voice_stt_use_ai = 1;
    strcpy(s.ai_provider, "openai");
    ASSERT_EQ(voice_stt_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(t.url, "https://api.openai.com/v1/audio/transcriptions");
    ASSERT_EQ(t.key_from_chat, 1);
    ASSERT_EQ(t.shape, STT_SHAPE_OPENAI);
    ASSERT_STR_EQ(t.model, "gpt-4o-mini-transcribe");

    strcpy(s.ai_provider, "custom");
    strcpy(s.ai_custom_url, "https://h/v1/chat/completions");
    ASSERT_EQ(voice_stt_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(t.url, "https://h/v1/audio/transcriptions");
    ASSERT_STR_EQ(t.model, "whisper-1");

    strcpy(s.ai_provider, "gemini");
    ASSERT_EQ(voice_stt_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(t.url, "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions");
    ASSERT_EQ(t.shape, STT_SHAPE_GEMINI_CHAT);
    ASSERT_STR_EQ(t.model, "gemini-2.5-flash");

    strcpy(s.ai_provider, "anthropic");
    ASSERT_EQ(voice_stt_resolve(&s, &t, err, sizeof err), 0);
    ASSERT_STR_EQ(err, "Anthropic has no speech-to-text API");
    TEST_END();
}

int test_voice_stt_resolve_separate(void)
{
    TEST_BEGIN();
    static Settings s;
    SttTarget t;
    char err[200];
    base_settings(&s);
    strcpy(s.ai_provider, "anthropic"); /* must not matter when unticked */
    strcpy(s.voice_stt_provider, "groq");
    ASSERT_EQ(voice_stt_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(t.url, "https://api.groq.com/openai/v1/audio/transcriptions");
    ASSERT_EQ(t.key_from_chat, 0);
    ASSERT_STR_EQ(t.model, "whisper-large-v3-turbo");

    strcpy(s.voice_stt_provider, "gemini");
    ASSERT_EQ(voice_stt_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(t.url, "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions");
    ASSERT_EQ(t.shape, STT_SHAPE_GEMINI_CHAT);

    strcpy(s.voice_stt_provider, "custom");
    strcpy(s.voice_stt_url, "https://h/v1/audio/transcriptions");
    ASSERT_EQ(voice_stt_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(t.url, "https://h/v1/audio/transcriptions");

    s.voice_stt_url[0] = '\0';
    ASSERT_EQ(voice_stt_resolve(&s, &t, err, sizeof err), 0);
    ASSERT_STR_EQ(err, "Enter a base URL for the custom voice provider");

    strcpy(s.voice_stt_provider, "");
    ASSERT_EQ(voice_stt_resolve(&s, &t, err, sizeof err), 0);
    ASSERT_STR_EQ(err, "Choose a speech-to-text provider");
    strcpy(s.voice_stt_provider, "nope");
    ASSERT_EQ(voice_stt_resolve(&s, &t, err, sizeof err), 0);

    strcpy(s.voice_stt_provider, "openai");
    strcpy(s.voice_stt_model, "x");
    strcpy(s.voice_stt_language, "en");
    ASSERT_EQ(voice_stt_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(t.model, "x");
    ASSERT_STR_EQ(t.language, "en");
    TEST_END();
}

int test_voice_stt_multipart(void)
{
    TEST_BEGIN();
    const unsigned char wav[4] = {0, 1, 2, 0};
    size_t len = 0;
    char *b = voice_stt_multipart("b0", wav, sizeof wav, "whisper-1", "en", &len);
    ASSERT_NOT_NULL(b);
    if (b) {
        ASSERT_TRUE(len > 6 && memcmp(b, "--b0\r\n", 6) == 0);
        static const char fh[] =
            "Content-Disposition: form-data; name=\"file\"; filename=\"speech.wav\"\r\n"
            "Content-Type: audio/wav\r\n\r\n";
        const char *p = find_bytes(b, len, fh, sizeof fh - 1);
        ASSERT_NOT_NULL(p);
        if (p) ASSERT_TRUE(memcmp(p + sizeof fh - 1, wav, 4) == 0);
        ASSERT_NOT_NULL(find_str(b, len, "name=\"model\"\r\n\r\nwhisper-1\r\n"));
        ASSERT_NOT_NULL(find_str(b, len, "name=\"language\"\r\n\r\nen\r\n"));
        ASSERT_NOT_NULL(find_str(b, len, "name=\"response_format\"\r\n\r\njson\r\n"));
        ASSERT_TRUE(len >= 8 && memcmp(b + len - 8, "--b0--\r\n", 8) == 0);
        free(b);
    }
    b = voice_stt_multipart("b0", wav, sizeof wav, "whisper-1", "", &len);
    ASSERT_NOT_NULL(b);
    if (b) {
        ASSERT_NULL(find_str(b, len, "name=\"language\""));
        free(b);
    }
    b = voice_stt_multipart("b0", wav, sizeof wav, "gpt-4o-mini-transcribe", "en", &len);
    ASSERT_NOT_NULL(b);
    if (b) {
        ASSERT_NULL(find_str(b, len, "response_format"));
        ASSERT_NOT_NULL(find_str(b, len, "name=\"model\"\r\n\r\ngpt-4o-mini-transcribe\r\n"));
        free(b);
    }
    TEST_END();
}

int test_voice_stt_gemini_body(void)
{
    TEST_BEGIN();
    const unsigned char wav[5] = {'R', 'I', 'F', 0, 255};
    size_t len = 0;
    char *b = voice_stt_gemini_body(wav, sizeof wav, "gemini-2.5-flash", "", &len);
    ASSERT_NOT_NULL(b);
    if (b) {
        ASSERT_EQ(len, strlen(b));
        JsonNode *root = json_parse(b);
        ASSERT_NOT_NULL(root);
        if (root) {
            ASSERT_STR_EQ(json_obj_str(root, "model"), "gemini-2.5-flash");
            JsonNode *msgs = json_obj_get(root, "messages");
            ASSERT_TRUE(msgs && msgs->type == JSON_ARRAY && msgs->as.arr.size == 1);
            if (msgs && msgs->as.arr.size == 1) {
                JsonNode *m0 = (JsonNode *)msgs->as.arr.data[0];
                ASSERT_STR_EQ(json_obj_str(m0, "role"), "user");
                JsonNode *c = json_obj_get(m0, "content");
                ASSERT_TRUE(c && c->type == JSON_ARRAY && c->as.arr.size == 2);
                if (c && c->as.arr.size == 2) {
                    JsonNode *txt = (JsonNode *)c->as.arr.data[0];
                    const char *tx = json_obj_str(txt, "text");
                    ASSERT_NOT_NULL(tx);
                    if (tx) {
                        ASSERT_TRUE(strstr(tx, "Transcribe this audio verbatim. Reply with only the words spoken, or nothing if no one speaks.") == tx);
                        ASSERT_NULL(strstr(tx, "language"));
                    }
                    JsonNode *au = (JsonNode *)c->as.arr.data[1];
                    JsonNode *ia = json_obj_get(au, "input_audio");
                    ASSERT_NOT_NULL(ia);
                    if (ia) {
                        ASSERT_STR_EQ(json_obj_str(ia, "format"), "wav");
                        ASSERT_STR_EQ(json_obj_str(ia, "data"), "UklGAP8=");
                    }
                }
            }
            json_free(root);
        }
        free(b);
    }
    b = voice_stt_gemini_body(wav, sizeof wav, "m", "en", &len);
    ASSERT_NOT_NULL(b);
    if (b) {
        JsonNode *root = json_parse(b);
        ASSERT_NOT_NULL(root);
        if (root) {
            JsonNode *msgs = json_obj_get(root, "messages");
            JsonNode *m0 = msgs && msgs->as.arr.size ? (JsonNode *)msgs->as.arr.data[0] : NULL;
            JsonNode *c = m0 ? json_obj_get(m0, "content") : NULL;
            JsonNode *txt = c && c->as.arr.size ? (JsonNode *)c->as.arr.data[0] : NULL;
            const char *tx = txt ? json_obj_str(txt, "text") : NULL;
            ASSERT_NOT_NULL(tx);
            if (tx) ASSERT_NOT_NULL(strstr(tx, " The language is en."));
            json_free(root);
        }
        free(b);
    }
    TEST_END();
}

int test_voice_stt_parse(void)
{
    TEST_BEGIN();
    char out[128], err[128];
    const char *a = "{\"text\":\"  list my files \\n\"}";
    ASSERT_EQ(voice_stt_parse(STT_SHAPE_OPENAI, a, strlen(a), out, sizeof out, err, sizeof err), 1);
    ASSERT_STR_EQ(out, "list my files");
    const char *g = "{\"choices\":[{\"message\":{\"content\":\"Install nginx.\"}}]}";
    ASSERT_EQ(voice_stt_parse(STT_SHAPE_GEMINI_CHAT, g, strlen(g), out, sizeof out, err, sizeof err), 1);
    ASSERT_STR_EQ(out, "Install nginx.");
    const char *e = "{\"text\":\"\"}";
    ASSERT_EQ(voice_stt_parse(STT_SHAPE_OPENAI, e, strlen(e), out, sizeof out, err, sizeof err), 1);
    ASSERT_STR_EQ(out, "");
    const char *bad = "not json";
    ASSERT_EQ(voice_stt_parse(STT_SHAPE_OPENAI, bad, strlen(bad), out, sizeof out, err, sizeof err), 0);
    ASSERT_TRUE(err[0] != '\0');
    const char *miss = "{\"other\":1}";
    ASSERT_EQ(voice_stt_parse(STT_SHAPE_OPENAI, miss, strlen(miss), out, sizeof out, err, sizeof err), 0);
    ASSERT_EQ(voice_stt_parse(STT_SHAPE_GEMINI_CHAT, miss, strlen(miss), out, sizeof out, err, sizeof err), 0);
    const char *eb = "{\"error\":{\"message\":\"bad key\"}}";
    ASSERT_EQ(voice_stt_parse(STT_SHAPE_OPENAI, eb, strlen(eb), out, sizeof out, err, sizeof err), 0);
    ASSERT_NOT_NULL(strstr(err, "bad key"));
    /* output too small for the transcript */
    char tiny[4];
    ASSERT_EQ(voice_stt_parse(STT_SHAPE_OPENAI, a, strlen(a), tiny, sizeof tiny, err, sizeof err), 0);
    TEST_END();
}

int test_voice_stt_http_error(void)
{
    TEST_BEGIN();
    char o[200];
    voice_stt_http_error(401, NULL, o, sizeof o);
    ASSERT_STR_EQ(o, "The speech-to-text key was refused (HTTP 401). Check it in Settings > Voice.");
    voice_stt_http_error(403, NULL, o, sizeof o);
    ASSERT_STR_EQ(o, "The speech-to-text key was refused (HTTP 403). Check it in Settings > Voice.");
    voice_stt_http_error(404, NULL, o, sizeof o);
    ASSERT_STR_EQ(o, "The speech-to-text model or URL was not found (HTTP 404). Check them in Settings > Voice.");
    voice_stt_http_error(413, NULL, o, sizeof o);
    ASSERT_STR_EQ(o, "The recording was too large for the provider.");
    voice_stt_http_error(429, NULL, o, sizeof o);
    ASSERT_STR_EQ(o, "The speech-to-text provider is rate limiting (HTTP 429). Try again shortly.");
    voice_stt_http_error(500, NULL, o, sizeof o);
    ASSERT_STR_EQ(o, "Speech-to-text failed (HTTP 500).");
    voice_stt_http_error(0, "timed out", o, sizeof o);
    ASSERT_STR_EQ(o, "Speech-to-text could not connect: timed out");
    TEST_END();
}
