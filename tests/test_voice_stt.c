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

/* The one builder takes the target: this builds one (an OpenAI-format provider
 * that accepts `prompt`, or not) from a model and a stored language id. */
static char *mpt_ex(const char *bnd, const unsigned char *wav, size_t wl, const char *model,
                    const char *lang, int accepts, size_t *len)
{
    SttTarget t;
    memset(&t, 0, sizeof t);
    snprintf(t.model, sizeof t.model, "%s", model);
    snprintf(t.language, sizeof t.language, "%s", lang ? lang : "");
    snprintf(t.provider, sizeof t.provider, "openai");
    t.accepts_prompt = accepts;
    return voice_stt_multipart(bnd, wav, wl, &t, len);
}

static char *mpt(const char *bnd, const unsigned char *wav, size_t wl, const char *model,
                 const char *lang, size_t *len)
{
    return mpt_ex(bnd, wav, wl, model, lang, 1, len);
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
    ASSERT_STR_EQ(voice_stt_use_ai_reason("", ""),
                  "Choose an AI provider that has a speech-to-text API");
    ASSERT_STR_EQ(voice_stt_use_ai_reason("xai", ""),
                  "Choose an AI provider that has a speech-to-text API");
    ASSERT_STR_EQ(voice_stt_use_ai_reason(NULL, ""),
                  "Choose an AI provider that has a speech-to-text API");
    ASSERT_NOT_NULL(voice_stt_use_ai_reason("custom", "ftp://h/chat/completions"));
    ASSERT_NOT_NULL(voice_stt_use_ai_reason("custom", "/chat/completions"));
    ASSERT_NOT_NULL(voice_stt_use_ai_reason("custom", "https:///chat/completions"));
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

    /* unknown or empty chat provider with empty / short / foreign-scheme URLs */
    static const char *const provs[] = {"", "xai", "custom"};
    static const char *const urls[] = {"", "x", "/", "ftp://h/chat/completions",
                                       "https://h/api/generate"};
    for (size_t i = 0; i < 3; i++)
        for (size_t j = 0; j < 5; j++) {
            if (strcmp(provs[i], "custom") != 0 && urls[j][0]) continue;
            snprintf(s.ai_provider, sizeof s.ai_provider, "%s", provs[i]);
            snprintf(s.ai_custom_url, sizeof s.ai_custom_url, "%s", urls[j]);
            err[0] = '\0';
            ASSERT_EQ(voice_stt_resolve(&s, &t, err, sizeof err), 0);
            ASSERT_TRUE(err[0] != '\0');
        }
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

/* The body is sized exactly up front (no realloc, so no unwiped copy of the
 * audio): the builder returns NULL if its own size was off, so a long model,
 * language and boundary, with and without each optional part, must build. */
int test_voice_stt_multipart_exact_size_long_fields(void)
{
    TEST_BEGIN();
    unsigned char wav[5000];
    memset(wav, 'W', sizeof wav);
    char model[201], lang[16], bnd[71];
    memset(model, 'm', 200); model[200] = '\0';
    memset(lang, 'l', 15); lang[15] = '\0';   /* SttTarget.language is char[16] */
    memset(bnd, 'b', 70); bnd[70] = '\0';
    const char *models[2] = { model, "gpt-4o-transcribe" };
    const char *langs[2] = { lang, "" };
    for (int m = 0; m < 2; m++) {
        for (int g = 0; g < 2; g++) {
            size_t len = 0;
            char *b = mpt(bnd, wav, sizeof wav, models[m], langs[g], &len);
            ASSERT_NOT_NULL(b);
            if (!b) continue;
            ASSERT_EQ(b[len], '\0');
            ASSERT_EQ(strlen(b), len);
            ASSERT_TRUE(len > sizeof wav + 200);
            ASSERT_TRUE(strstr(b, models[m]) != NULL);
            ASSERT_EQ(strstr(b, lang) != NULL, g == 0);
            ASSERT_EQ(strstr(b, "response_format") != NULL, m == 0);
            ASSERT_TRUE(len >= 4 && strcmp(b + len - 4, "--\r\n") == 0);
            free(b);
        }
    }
    TEST_END();
}

int test_voice_stt_multipart(void)
{
    TEST_BEGIN();
    const unsigned char wav[4] = {0, 1, 2, 0};
    size_t len = 0;
    char *b = mpt("b0", wav, sizeof wav, "whisper-1", "en", &len);
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
    ASSERT_NULL(mpt("", wav, sizeof wav, "m", "en", &len));
    ASSERT_NULL(mpt("b 0", wav, sizeof wav, "m", "en", &len));
    ASSERT_NULL(mpt("b\r\n0", wav, sizeof wav, "m", "en", &len));
    ASSERT_NULL(mpt("b\"0", wav, sizeof wav, "m", "en", &len));
    {
        char lb[80];
        memset(lb, 'a', 71); lb[71] = '\0';
        ASSERT_NULL(mpt(lb, wav, sizeof wav, "m", "en", &len));
        lb[70] = '\0';
        char *ok70 = mpt(lb, wav, sizeof wav, "m", "en", &len);
        ASSERT_NOT_NULL(ok70);
        free(ok70);
    }
    ASSERT_NULL(mpt("b0", wav, sizeof wav, "m\r\nX: y", "en", &len));
    ASSERT_NULL(mpt("b0", wav, sizeof wav, "m", "e\nn", &len));
    b = mpt("b0", wav, sizeof wav, "whisper-1", "", &len);
    ASSERT_NOT_NULL(b);
    if (b) {
        ASSERT_NULL(find_str(b, len, "name=\"language\""));
        free(b);
    }
    b = mpt("b0", wav, sizeof wav, "gpt-4o-mini-transcribe", "en", &len);
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
            if (tx) ASSERT_NOT_NULL(strstr(tx, " The language is English."));
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

int test_voice_stt_upload_timeouts(void)
{
    TEST_BEGIN();
    int snd = 0, rcv = 0;
    voice_stt_upload_timeouts(32044u, &snd, &rcv);          /* 1 s of 16 kHz mono */
    ASSERT_EQ(snd, 30000);
    ASSERT_EQ(rcv, 60000);
    voice_stt_upload_timeouts(19200044u, &snd, &rcv);       /* 10 minutes */
    ASSERT_EQ(snd, 30000 + 292000);
    ASSERT_EQ(rcv, 60000 + 60000);
    voice_stt_upload_timeouts((size_t)2147483648u, &snd, &rcv);   /* 2 GB */
    ASSERT_EQ(snd, 600000);
    ASSERT_EQ(rcv, 600000);
    voice_stt_upload_timeouts(10u, &snd, &rcv);             /* shorter than a header */
    ASSERT_EQ(snd, 30000);
    ASSERT_EQ(rcv, 60000);
    voice_stt_upload_timeouts(0u, NULL, NULL);              /* NULL outputs are safe */
    TEST_END();
}

int test_voice_stt_parse_large(void)
{
    TEST_BEGIN();
    enum { N = 70000 };
    char *body = (char *)malloc((size_t)N + 64);
    ASSERT_NOT_NULL(body);
    if (!body) { TEST_END(); }
    size_t off = (size_t)sprintf(body, "{\"text\":\"");
    memset(body + off, 'a', N);
    off += N;
    memcpy(body + off, "\"}", 3);
    off += 2;
    ASSERT_TRUE(VOICE_STT_TEXT_MIN >= 65536);
    size_t cap = (size_t)VOICE_STT_TEXT_MIN + 8192;
    char *out = (char *)malloc(cap);
    char err[128] = "";
    ASSERT_NOT_NULL(out);
    if (out) {
        ASSERT_EQ(voice_stt_parse(STT_SHAPE_OPENAI, body, off, out, cap, err, sizeof err), 1);
        ASSERT_EQ((int)strlen(out), N);
        free(out);
    }
    /* A 4,096-byte buffer cannot hold it: refused with a message, never written past
     * its end (the canary after the buffer stays intact). */
    char small[4096 + 16];
    memset(small, 0x5A, sizeof small);
    err[0] = '\0';
    ASSERT_EQ(voice_stt_parse(STT_SHAPE_OPENAI, body, off, small, 4096, err, sizeof err), 0);
    ASSERT_TRUE(err[0] != '\0');
    for (size_t i = 4096; i < sizeof small; i++) ASSERT_EQ((int)(unsigned char)small[i], 0x5A);
    /* A chat-shaped reply with escapes, longer than the parser's string limit */
    {
        size_t cap2 = (size_t)VOICE_STT_TEXT_MIN + 8192;
        char *b2 = (char *)malloc(20000);
        char *o2 = (char *)malloc(cap2);
        ASSERT_NOT_NULL(b2);
        ASSERT_NOT_NULL(o2);
        if (b2 && o2) {
            size_t k = (size_t)sprintf(b2, "{\"choices\":[{\"message\":{\"role\":\"a\",\"content\":\"  caf\\u00e9 \\ud83d\\ude00\\n");
            memset(b2 + k, 'b', 10000);
            k += 10000;
            k += (size_t)sprintf(b2 + k, "\\n\"}}]}");
            ASSERT_EQ(voice_stt_parse(STT_SHAPE_GEMINI_CHAT, b2, k, o2, cap2, err, sizeof err), 1);
            ASSERT_TRUE(strncmp(o2, "caf\xC3\xA9 \xF0\x9F\x98\x80\nbbb", 14) == 0);
            ASSERT_EQ((int)strlen(o2), 11 + 10000);   /* leading spaces and the trailing newline trimmed */
        }
        free(b2);
        free(o2);
    }
    free(body);
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

/* ---- language, languages[] and the spelling prompt ---- */

#define HINT_BRIT "Colour, organise, centre, travelled, favourite."
#define INSTR_BRIT "Use British English spelling, for example colour, organise and centre."

static char *mp(const char *model, const char *lang, int accepts, size_t *len)
{
    const unsigned char wav[4] = {0, 1, 2, 0};
    return mpt_ex("b0", wav, sizeof wav, model, lang, accepts, len);
}

int test_voice_stt_multipart_language_gpt_transcribe(void)
{
    TEST_BEGIN();
    size_t len = 0;
    char *b = mp("gpt-transcribe", "en-GB", 1, &len);
    ASSERT_NOT_NULL(b);
    if (b) {
        ASSERT_NOT_NULL(find_str(b, len, "name=\"languages[]\"\r\n\r\nen\r\n"));
        ASSERT_NULL(find_str(b, len, "name=\"language\""));
        ASSERT_NOT_NULL(find_str(b, len, "name=\"prompt\"\r\n\r\n" HINT_BRIT "\r\n"));
        ASSERT_NOT_NULL(find_str(b, len, "name=\"model\"\r\n\r\ngpt-transcribe\r\n"));
        free(b);
    }
    /* gpt-4o-transcribe is not gpt-transcribe: it takes the plain field */
    b = mp("gpt-4o-transcribe", "en-GB", 0, &len);
    ASSERT_NOT_NULL(b);
    if (b) {
        ASSERT_NULL(find_str(b, len, "languages[]"));
        ASSERT_NOT_NULL(find_str(b, len, "name=\"language\"\r\n\r\nen\r\n"));
        free(b);
    }
    TEST_END();
}

int test_voice_stt_multipart_language_whisper(void)
{
    TEST_BEGIN();
    size_t len = 0;
    char *b = mp("whisper-1", "en-GB", 1, &len);
    ASSERT_NOT_NULL(b);
    if (b) {
        ASSERT_NOT_NULL(find_str(b, len, "name=\"language\"\r\n\r\nen\r\n"));
        ASSERT_NULL(find_str(b, len, "languages[]"));
        ASSERT_NOT_NULL(find_str(b, len, "name=\"prompt\"\r\n\r\n" HINT_BRIT "\r\n"));
        ASSERT_NOT_NULL(find_str(b, len, "name=\"response_format\""));
        free(b);
    }
    /* a custom code goes out as its iso part */
    b = mp("whisper-1", "pt-BR", 1, &len);
    ASSERT_NOT_NULL(b);
    if (b) {
        ASSERT_NOT_NULL(find_str(b, len, "name=\"language\"\r\n\r\npt\r\n"));
        free(b);
    }
    TEST_END();
}

/* Resolve a target for one provider with the given language. */
static int target_for(const char *prov, const char *lang, SttTarget *t)
{
    Settings s;
    memset(&s, 0, sizeof s);
    s.voice_stt_use_ai = 0;
    strcpy(s.voice_stt_provider, prov);
    strcpy(s.voice_stt_url, "https://h/v1");
    strcpy(s.voice_stt_language, lang);
    char err[128];
    return voice_stt_resolve(&s, t, err, sizeof err);
}

int test_voice_stt_prompt_per_provider(void)
{
    TEST_BEGIN();
    SttTarget t;
    const char *yes[] = { "openai", "groq", "custom" };
    for (size_t i = 0; i < 3; i++) {
        ASSERT_EQ(target_for(yes[i], "en-GB", &t), 1);
        ASSERT_STR_EQ(t.provider, yes[i]);
        ASSERT_STR_EQ(voice_stt_prompt(&t), HINT_BRIT);
        ASSERT_EQ(target_for(yes[i], "en-US", &t), 1);
        ASSERT_NULL(voice_stt_prompt(&t));
        ASSERT_EQ(target_for(yes[i], "", &t), 1);
        ASSERT_NULL(voice_stt_prompt(&t));
    }
    ASSERT_EQ(target_for("mistral", "en-GB", &t), 1);
    ASSERT_NULL(voice_stt_prompt(&t));
    ASSERT_EQ(target_for("gemini", "en-GB", &t), 1);
    ASSERT_NULL(voice_stt_prompt(&t));
    ASSERT_NULL(voice_stt_prompt(NULL));
    /* the use-AI path: openai and custom accept it, gemini does not */
    Settings s;
    memset(&s, 0, sizeof s);
    s.voice_stt_use_ai = 1;
    strcpy(s.voice_stt_language, "en-GB");
    strcpy(s.ai_provider, "openai");
    char err[128];
    ASSERT_EQ(voice_stt_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(voice_stt_prompt(&t), HINT_BRIT);
    strcpy(s.ai_provider, "custom");
    strcpy(s.ai_custom_url, "https://h/v1/chat/completions");
    ASSERT_EQ(voice_stt_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_STR_EQ(voice_stt_prompt(&t), HINT_BRIT);
    strcpy(s.ai_provider, "gemini");
    ASSERT_EQ(voice_stt_resolve(&s, &t, err, sizeof err), 1);
    ASSERT_NULL(voice_stt_prompt(&t));
    TEST_END();
}

int test_voice_stt_multipart_mistral_no_prompt(void)
{
    TEST_BEGIN();
    SttTarget t;
    ASSERT_EQ(target_for("mistral", "en-GB", &t), 1);
    size_t len = 0;
    const unsigned char w4[4] = {0, 1, 2, 0};
    char *b = voice_stt_multipart("b0", w4, sizeof w4, &t, &len);
    ASSERT_NOT_NULL(b);
    if (b) {
        ASSERT_NOT_NULL(find_str(b, len, "name=\"language\"\r\n\r\nen\r\n"));
        ASSERT_NULL(find_str(b, len, "name=\"prompt\""));
        free(b);
    }
    TEST_END();
}

int test_voice_stt_multipart_en_us_no_prompt(void)
{
    TEST_BEGIN();
    SttTarget t;
    ASSERT_EQ(target_for("openai", "en-US", &t), 1);
    size_t len = 0;
    const unsigned char w4[4] = {0, 1, 2, 0};
    char *b = voice_stt_multipart("b0", w4, sizeof w4, &t, &len);
    ASSERT_NOT_NULL(b);
    if (b) {
        ASSERT_NOT_NULL(find_str(b, len, "name=\"language\"\r\n\r\nen\r\n"));
        ASSERT_NULL(find_str(b, len, "name=\"prompt\""));
        free(b);
    }
    TEST_END();
}

int test_voice_stt_multipart_auto_detect(void)
{
    TEST_BEGIN();
    SttTarget t;
    ASSERT_EQ(target_for("openai", "", &t), 1);
    size_t len = 0;
    const char *models[] = { "whisper-1", "gpt-transcribe" };
    for (int i = 0; i < 2; i++) {
        snprintf(t.model, sizeof t.model, "%s", models[i]);
        const unsigned char w4[4] = {0, 1, 2, 0};
        char *b = voice_stt_multipart("b0", w4, sizeof w4, &t, &len);
        ASSERT_NOT_NULL(b);
        if (b) {
            ASSERT_NULL(find_str(b, len, "language"));
            ASSERT_NULL(find_str(b, len, "name=\"prompt\""));
            free(b);
        }
    }
    TEST_END();
}

static char *gemini_text(const char *lang, char *out, size_t cap)
{
    const unsigned char wav[5] = {'R', 'I', 'F', 0, 255};
    size_t len = 0;
    char *b = voice_stt_gemini_body(wav, sizeof wav, "m", lang, &len);
    out[0] = '\0';
    if (!b) return NULL;
    JsonNode *root = json_parse(b);
    if (root) {
        JsonNode *msgs = json_obj_get(root, "messages");
        JsonNode *m0 = msgs && msgs->as.arr.size ? (JsonNode *)msgs->as.arr.data[0] : NULL;
        JsonNode *c = m0 ? json_obj_get(m0, "content") : NULL;
        JsonNode *txt = c && c->as.arr.size ? (JsonNode *)c->as.arr.data[0] : NULL;
        const char *tx = txt ? json_obj_str(txt, "text") : NULL;
        if (tx) snprintf(out, cap, "%s", tx);
        json_free(root);
    }
    return b;
}

int test_voice_stt_gemini_language_words(void)
{
    TEST_BEGIN();
    char tx[512];
    char *b = gemini_text("en-GB", tx, sizeof tx);
    ASSERT_NOT_NULL(b);
    free(b);
    ASSERT_NOT_NULL(strstr(tx, " The language is English."));
    ASSERT_NOT_NULL(strstr(tx, INSTR_BRIT));   /* Gemini is an LLM: an instruction */
    ASSERT_NULL(strstr(tx, "en-GB"));
    b = gemini_text("en-US", tx, sizeof tx);
    free(b);
    ASSERT_NOT_NULL(strstr(tx, " The language is English."));
    ASSERT_NULL(strstr(tx, "British"));
    b = gemini_text("fr", tx, sizeof tx);
    free(b);
    ASSERT_NOT_NULL(strstr(tx, " The language is French."));
    b = gemini_text("cy", tx, sizeof tx);
    free(b);
    ASSERT_NOT_NULL(strstr(tx, " The language is Welsh."));
    b = gemini_text("gd", tx, sizeof tx);
    free(b);
    ASSERT_NOT_NULL(strstr(tx, " The language is Scottish Gaelic."));
    b = gemini_text("", tx, sizeof tx);
    free(b);
    ASSERT_NULL(strstr(tx, "language"));
    TEST_END();
}

/* The single builder derives the prompt from the target itself, so a real
 * en-GB target for each provider carries it (or not) with no caller step. */
int test_voice_stt_multipart_target_prompt(void)
{
    TEST_BEGIN();
    const unsigned char w4[4] = {0, 1, 2, 0};
    const char *provs[] = { "openai", "groq", "custom", "mistral" };
    for (int i = 0; i < 4; i++) {
        SttTarget t;
        ASSERT_EQ(target_for(provs[i], "en-GB", &t), 1);
        size_t len = 0;
        char *b = voice_stt_multipart("b0", w4, sizeof w4, &t, &len);
        ASSERT_NOT_NULL(b);
        if (b) {
            ASSERT_EQ(find_str(b, len, "name=\"prompt\"\r\n\r\n" HINT_BRIT "\r\n") != NULL, i < 3);
            free(b);
        }
    }
    ASSERT_NULL(voice_stt_multipart("b0", w4, sizeof w4, NULL, NULL));
    TEST_END();
}

/* ---- the one retry without the language hint ---- */

int test_voice_stt_retry_plain(void)
{
    TEST_BEGIN();
    SttTarget t;
    ASSERT_EQ(target_for("openai", "en-GB", &t), 1);
    ASSERT_EQ(voice_stt_retry_plain(400, &t), 1);
    ASSERT_EQ(voice_stt_retry_plain(401, &t), 0);
    ASSERT_EQ(voice_stt_retry_plain(429, &t), 0);
    ASSERT_EQ(voice_stt_retry_plain(500, &t), 0);
    ASSERT_EQ(voice_stt_retry_plain(200, &t), 0);
    ASSERT_EQ(voice_stt_retry_plain(0, &t), 0);
    ASSERT_EQ(voice_stt_retry_plain(400, NULL), 0);
    /* a language alone (no prompt: en-US, mistral) is enough */
    ASSERT_EQ(target_for("openai", "en-US", &t), 1);
    ASSERT_EQ(voice_stt_retry_plain(400, &t), 1);
    ASSERT_EQ(target_for("mistral", "fr", &t), 1);
    ASSERT_EQ(voice_stt_retry_plain(400, &t), 1);
    ASSERT_EQ(target_for("gemini", "en-GB", &t), 1);
    ASSERT_EQ(voice_stt_retry_plain(400, &t), 1);
    /* Auto-detect with no prompt carried nothing to drop */
    ASSERT_EQ(target_for("openai", "", &t), 1);
    ASSERT_EQ(voice_stt_retry_plain(400, &t), 0);
    /* the plain copy: no language, no prompt, otherwise the same, so it never
     * retries again and its request carries neither part */
    SttTarget q, p;
    ASSERT_EQ(target_for("openai", "en-GB", &q), 1);
    voice_stt_target_plain(&q, &p);
    ASSERT_STR_EQ(p.language, "");
    ASSERT_EQ(p.accepts_prompt, 0);
    ASSERT_STR_EQ(p.model, q.model);
    ASSERT_STR_EQ(p.url, q.url);
    ASSERT_STR_EQ(p.provider, q.provider);
    ASSERT_EQ(voice_stt_retry_plain(400, &p), 0);
    ASSERT_STR_EQ(q.language, "en-GB");   /* the original is untouched */
    const unsigned char w4[4] = {0, 1, 2, 0};
    size_t len = 0;
    const char *models[] = { "whisper-1", "gpt-transcribe" };
    for (int i = 0; i < 2; i++) {
        snprintf(p.model, sizeof p.model, "%s", models[i]);
        char *b = voice_stt_multipart("b0", w4, sizeof w4, &p, &len);
        ASSERT_NOT_NULL(b);
        if (b) {
            ASSERT_NULL(find_str(b, len, "name=\"language\""));
            ASSERT_NULL(find_str(b, len, "languages[]"));
            ASSERT_NULL(find_str(b, len, "name=\"prompt\""));
            ASSERT_NOT_NULL(find_str(b, len, "name=\"model\""));
            free(b);
        }
    }
    /* the Gemini plain body names no language and has no spelling sentence */
    ASSERT_EQ(target_for("gemini", "en-GB", &q), 1);
    voice_stt_target_plain(&q, &p);
    char tx[512];
    char *g = gemini_text(p.language, tx, sizeof tx);
    ASSERT_NOT_NULL(g);
    free(g);
    ASSERT_NULL(strstr(tx, "language"));
    ASSERT_NULL(strstr(tx, "British"));
    TEST_END();
}
