/* src/core/voice_stt.c -- see voice_stt.h. No logging: transcripts and keys are
 * user content. */
#include "voice_stt.h"
#include "base64.h"
#include "json_validate.h"
#include "secure_zero.h"
#include "../config/json_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const k_openai_models[] = {
    "gpt-4o-mini-transcribe", "gpt-transcribe", "gpt-4o-transcribe", "whisper-1", NULL};
static const char *const k_groq_models[] = {
    "whisper-large-v3-turbo", "whisper-large-v3", NULL};
static const char *const k_mistral_models[] = {
    "voxtral-mini-transcribe-3", "voxtral-mini-latest", NULL};
static const char *const k_gemini_models[] = {
    "gemini-2.5-flash", "gemini-2.5-flash-lite", NULL};
static const char *const k_custom_models[] = {"whisper-1", NULL};

static const SttProvider k_providers[] = {
    {"openai", "OpenAI", "https://api.openai.com/v1", k_openai_models, STT_SHAPE_OPENAI},
    {"groq", "Groq", "https://api.groq.com/openai/v1", k_groq_models, STT_SHAPE_OPENAI},
    {"mistral", "Mistral", "https://api.mistral.ai/v1", k_mistral_models, STT_SHAPE_OPENAI},
    {"gemini", "Google Gemini", "https://generativelanguage.googleapis.com/v1beta/openai",
     k_gemini_models, STT_SHAPE_GEMINI_CHAT},
    {"custom", "Custom", "", k_custom_models, STT_SHAPE_OPENAI},
};

#define GEMINI_CHAT_URL "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"
#define CHAT_SUFFIX "/chat/completions"

const SttProvider *voice_stt_providers(size_t *n)
{
    if (n) *n = sizeof k_providers / sizeof k_providers[0];
    return k_providers;
}

const SttProvider *voice_stt_provider_find(const char *id)
{
    if (!id) return NULL;
    for (size_t i = 0; i < sizeof k_providers / sizeof k_providers[0]; i++)
        if (strcmp(k_providers[i].id, id) == 0) return &k_providers[i];
    return NULL;
}

/* Copy in trimmed of spaces/tabs and trailing '/'; returns the length or -1. */
static int trim_copy(const char *in, char *out, size_t cap, size_t *len_out)
{
    if (!in || !out || cap == 0) return 0;
    while (*in == ' ' || *in == '\t') in++;
    size_t n = strlen(in);
    while (n > 0 && (in[n - 1] == ' ' || in[n - 1] == '\t' || in[n - 1] == '/' ||
                     in[n - 1] == '\r' || in[n - 1] == '\n')) n--;
    if (n + 1 > cap) return 0;
    memcpy(out, in, n);
    out[n] = '\0';
    *len_out = n;
    return 1;
}

static int ends_with(const char *s, size_t n, const char *suf)
{
    size_t m = strlen(suf);
    return n >= m && memcmp(s + n - m, suf, m) == 0;
}

int voice_base_url_normalise(const char *in, char *out, size_t cap)
{
    size_t n;
    if (!trim_copy(in, out, cap, &n)) return 0;
    static const char *const suffixes[] = {"/audio/transcriptions", "/audio/speech"};
    for (size_t i = 0; i < 2; i++) {
        if (ends_with(out, n, suffixes[i])) {
            n -= strlen(suffixes[i]);
            while (n > 0 && out[n - 1] == '/') n--;
            out[n] = '\0';
            break;
        }
    }
    if (n == 0) return 0;
    if (strncmp(out, "http://", 7) != 0 && strncmp(out, "https://", 8) != 0) return 0;
    if (n <= (out[4] == 's' ? 8u : 7u)) return 0; /* scheme only */
    return 1;
}

#define CUSTOM_CHAT_REASON \
    "Set a separate voice provider: the custom chat URL has no /chat/completions to derive from"

/* The base a custom chat URL derives from: it minus "/chat/completions", normalised
 * (http/https, a host). Returns 0 when the URL is not usable. */
static int custom_chat_base(const char *chat_url, char *base, size_t cap)
{
    char buf[512];
    size_t n;
    if (!trim_copy(chat_url, buf, sizeof buf, &n) || !ends_with(buf, n, CHAT_SUFFIX))
        return 0;
    n -= strlen(CHAT_SUFFIX);
    buf[n] = '\0';
    return voice_base_url_normalise(buf, base, cap);
}

const char *voice_stt_use_ai_reason(const char *chat_provider, const char *chat_custom_url)
{
    if (!chat_provider) chat_provider = "";
    if (strcmp(chat_provider, "anthropic") == 0) return "Anthropic has no speech-to-text API";
    if (strcmp(chat_provider, "deepseek") == 0) return "DeepSeek has no speech-to-text API";
    if (strcmp(chat_provider, "moonshot") == 0) return "Moonshot has no speech-to-text API";
    if (strcmp(chat_provider, "custom") == 0) {
        char base[512];
        if (!custom_chat_base(chat_custom_url, base, sizeof base)) return CUSTOM_CHAT_REASON;
        return NULL;
    }
    if (strcmp(chat_provider, "openai") == 0 || strcmp(chat_provider, "gemini") == 0)
        return NULL;
    return "Choose an AI provider that has a speech-to-text API";
}

static void put_err(char *err, size_t cap, const char *msg)
{
    if (err && cap) snprintf(err, cap, "%s", msg);
}

static void fill_common(const Settings *s, SttTarget *t, const SttProvider *p,
                        const char *fallback_model)
{
    snprintf(t->model, sizeof t->model, "%s",
             s->voice_stt_model[0] ? s->voice_stt_model : fallback_model);
    snprintf(t->language, sizeof t->language, "%s", s->voice_stt_language);
    snprintf(t->provider, sizeof t->provider, "%s", p->id);
    /* Mistral's context_bias is vocabulary biasing, not a spelling switch, and the
     * Gemini route is a chat body: neither gets `prompt`. */
    t->accepts_prompt = strcmp(p->id, "openai") == 0 || strcmp(p->id, "groq") == 0 ||
                        strcmp(p->id, "custom") == 0;
    t->shape = p->shape;
}

int voice_stt_retry_plain(int http_status, const SttTarget *t)
{
    if (!t || http_status != 400) return 0;
    return t->language[0] != '\0' || voice_stt_prompt(t) != NULL;
}

void voice_stt_target_plain(const SttTarget *t, SttTarget *out)
{
    if (!t || !out) return;
    *out = *t;
    out->language[0] = '\0';
    out->accepts_prompt = 0;
}

const char *voice_stt_prompt(const SttTarget *t)
{
    if (!t || !t->accepts_prompt) return NULL;
    return voice_lang_spelling_hint(t->language);
}

int voice_stt_resolve(const Settings *s, SttTarget *t, char *err, size_t cap)
{
    if (!s || !t) return 0;
    memset(t, 0, sizeof *t);
    if (s->voice_stt_use_ai) {
        const char *why = voice_stt_use_ai_reason(s->ai_provider, s->ai_custom_url);
        if (why) { put_err(err, cap, why); return 0; }
        const SttProvider *p = voice_stt_provider_find(s->ai_provider);
        if (!p) p = voice_stt_provider_find("custom");
        if (strcmp(p->id, "custom") == 0) {
            char buf[512];
            if (!custom_chat_base(s->ai_custom_url, buf, sizeof buf)) {
                put_err(err, cap, CUSTOM_CHAT_REASON);
                return 0;
            }
            if (snprintf(t->url, sizeof t->url, "%s/audio/transcriptions", buf) >= (int)sizeof t->url) {
                put_err(err, cap, "The custom chat URL is too long");
                return 0;
            }
        } else if (p->shape == STT_SHAPE_GEMINI_CHAT) {
            snprintf(t->url, sizeof t->url, "%s", GEMINI_CHAT_URL);
        } else {
            snprintf(t->url, sizeof t->url, "%s/audio/transcriptions", p->base);
        }
        fill_common(s, t, p, p->models[0]);
        t->key_from_chat = 1;
        return 1;
    }
    const SttProvider *p = voice_stt_provider_find(s->voice_stt_provider);
    if (!p) { put_err(err, cap, "Choose a speech-to-text provider"); return 0; }
    char base[512];
    if (strcmp(p->id, "custom") == 0) {
        if (!voice_base_url_normalise(s->voice_stt_url, base, sizeof base)) {
            put_err(err, cap, "Enter a base URL for the custom voice provider");
            return 0;
        }
    } else {
        snprintf(base, sizeof base, "%s", p->base);
    }
    const char *tail = p->shape == STT_SHAPE_GEMINI_CHAT ? "/chat/completions"
                                                          : "/audio/transcriptions";
    if (snprintf(t->url, sizeof t->url, "%s%s", base, tail) >= (int)sizeof t->url) {
        put_err(err, cap, "The voice provider URL is too long");
        return 0;
    }
    fill_common(s, t, p, p->models[0]);
    t->key_from_chat = 0;
    return 1;
}

/* ---- request bodies ---- */

static int is_gpt4o_model(const char *m)
{
    return m && strncmp(m, "gpt-4o", 6) == 0;
}

static int buf_put(char **buf, size_t *len, size_t *cap, const void *src, size_t n)
{
    if (*len + n + 1 > *cap) {
        size_t nc = *cap ? *cap : 256;
        while (nc < *len + n + 1) nc *= 2;
        char *nb = (char *)realloc(*buf, nc);
        if (!nb) return 0;
        *buf = nb;
        *cap = nc;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 1;
}

static int buf_str(char **buf, size_t *len, size_t *cap, const char *s)
{
    return buf_put(buf, len, cap, s, strlen(s));
}

/* The multipart header texts, shared by the size computation and the writers
 * so the two cannot drift. */
static const char k_file_hdr[] =
    "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"speech.wav\"\r\n"
    "Content-Type: audio/wav\r\n\r\n";
static const char k_part_hdr[] = "\r\nContent-Disposition: form-data; name=\"";
static const char k_part_mid[] = "\"\r\n\r\n";
#define K_LEN(a) (sizeof(a) - 1)

static int text_part(char **b, size_t *l, size_t *c, const char *boundary,
                     const char *name, const char *value)
{
    return buf_str(b, l, c, "--") && buf_str(b, l, c, boundary) &&
           buf_str(b, l, c, k_part_hdr) &&
           buf_str(b, l, c, name) && buf_str(b, l, c, k_part_mid) &&
           buf_str(b, l, c, value) && buf_str(b, l, c, "\r\n");
}

/* OpenAI's transcription reference (developers.openai.com, audio/transcriptions/
 * create, read 2026-11-24): `language` is an ISO-639-1 string for any model;
 * `languages` is an array "supported by gpt-transcribe", sent as repeated
 * `languages[]` parts (the official SDKs' brackets array format); `prompt` is
 * accepted for gpt-transcribe and gpt-4o-transcribe. If a provider still rejects
 * one of them, the caller retries once without them (voice_stt_retry_plain). */
static int is_gpt_transcribe_model(const char *m)
{
    return m && strncmp(m, "gpt-transcribe", 14) == 0;
}

char *voice_stt_multipart(const char *boundary, const unsigned char *wav, size_t wav_len,
                          const SttTarget *t, size_t *out_len)
{
    if (!boundary || !wav || !t) return NULL;
    const char *model = t->model;
    const char *language = t->language;
    const char *prompt = voice_stt_prompt(t);
    size_t bl = strlen(boundary);
    if (bl == 0 || bl > 70) return NULL;
    for (size_t i = 0; i < bl; i++) {
        char ch = boundary[i];
        int okc = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                  (ch >= '0' && ch <= '9') || (ch != '\0' && strchr("'()+_,-./:=?", ch));
        if (!okc) return NULL;
    }
    if (strpbrk(model, "\r\n") || (language && strpbrk(language, "\r\n")) ||
        (prompt && strpbrk(prompt, "\r\n"))) return NULL;
    char iso[64];
    voice_lang_iso(language, iso, sizeof iso);
    const char *lang_name = is_gpt_transcribe_model(model) ? "languages[]" : "language";
    /* The exact size is known up front, so the buffer is allocated once and
     * never grown: a realloc would leave an unwiped copy of the audio. */
    const size_t part_fixed = 2 + bl + K_LEN(k_part_hdr) + K_LEN(k_part_mid) + 2;
    const int with_lang = iso[0] != '\0';
    const int with_prompt = prompt && prompt[0];
    const int with_fmt = !is_gpt4o_model(model);
    size_t total = 2 + bl + K_LEN(k_file_hdr) + wav_len + 2
                 + part_fixed + K_LEN("model") + strlen(model)
                 + 2 + bl + 4;
    if (with_lang) total += part_fixed + strlen(lang_name) + strlen(iso);
    if (with_prompt) total += part_fixed + K_LEN("prompt") + strlen(prompt);
    if (with_fmt) total += part_fixed + K_LEN("response_format") + K_LEN("json");
#undef K_LEN
    char *b = (char *)malloc(total + 1);
    if (!b) return NULL;
    b[0] = '\0';
    size_t l = 0, c = total + 1;
    int ok = buf_str(&b, &l, &c, "--") && buf_str(&b, &l, &c, boundary) &&
             buf_str(&b, &l, &c, k_file_hdr) &&
             buf_put(&b, &l, &c, wav, wav_len) && buf_str(&b, &l, &c, "\r\n") &&
             text_part(&b, &l, &c, boundary, "model", model);
    if (ok && with_lang)
        ok = text_part(&b, &l, &c, boundary, lang_name, iso);
    if (ok && with_prompt)
        ok = text_part(&b, &l, &c, boundary, "prompt", prompt);
    if (ok && with_fmt)
        ok = text_part(&b, &l, &c, boundary, "response_format", "json");
    ok = ok && buf_str(&b, &l, &c, "--") && buf_str(&b, &l, &c, boundary) &&
         buf_str(&b, &l, &c, "--\r\n");
    if (ok && (l != total || c != total + 1)) ok = 0;   /* the size must have been exact */
    if (!ok) { secure_zero(b, c); free(b); return NULL; }   /* it holds audio */
    if (out_len) *out_len = l;
    return b;
}

char *voice_stt_gemini_body(const unsigned char *wav, size_t wav_len,
                            const char *model, const char *language, size_t *out_len)
{
    if (!wav || !model) return NULL;
    static const char prompt[] =
        "Transcribe this audio verbatim. Reply with only the words spoken, or nothing if no one speaks.";
    char text[512];
    char words[64] = "";
    if (language && language[0]) {
        char iso[16];
        voice_lang_iso(language, iso, sizeof iso);
        const char *nm = voice_lang_name(iso);
        snprintf(words, sizeof words, "%s", nm ? nm : iso);
    }
    const char *hint = voice_lang_spelling_instruction(language);
    snprintf(text, sizeof text, "%s%s%s%s%s%s", prompt,
             words[0] ? " The language is " : "", words, words[0] ? "." : "",
             hint ? " " : "", hint ? hint : "");
    size_t b64cap = ((wav_len + 2) / 3) * 4 + 1;
    char *b64 = (char *)malloc(b64cap);
    if (!b64) return NULL;
    if (base64_encode(wav, wav_len, b64, b64cap) == 0 && wav_len > 0) {
        secure_zero(b64, b64cap);
        free(b64);
        return NULL;
    }
    size_t mlen = strlen(model), tlen = strlen(text);
    size_t cap = strlen(b64) + 12 * (mlen + tlen) + 256;
    char *out = (char *)malloc(cap);
    if (!out) { secure_zero(b64, b64cap); free(b64); return NULL; }
    size_t pos = 0;
#define PUT(lit) do { size_t k_ = strlen(lit); if (pos + k_ >= cap) goto fail; memcpy(out + pos, lit, k_); pos += k_; } while (0)
    PUT("{\"model\":");
    pos = json_escape_string(model, mlen, out, cap, pos, 1);
    if (!pos) goto fail;
    PUT(",\"messages\":[{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":");
    pos = json_escape_string(text, tlen, out, cap, pos, 1);
    if (!pos) goto fail;
    PUT("},{\"type\":\"input_audio\",\"input_audio\":{\"data\":\"");
    {
        size_t k = strlen(b64);
        if (pos + k >= cap) goto fail;
        memcpy(out + pos, b64, k);
        pos += k;
    }
    PUT("\",\"format\":\"wav\"}}]}]}");
#undef PUT
    out[pos] = '\0';
    secure_zero(b64, b64cap);   /* the audio, encoded */
    free(b64);
    if (out_len) *out_len = pos;
    return out;
fail:
    secure_zero(b64, b64cap);
    secure_zero(out, cap);
    free(b64);
    free(out);
    return NULL;
}

/* ---- response ---- */

static void trim_into(const char *s, char *out, size_t cap, int *fits)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) n--;
    if (n + 1 > cap) { *fits = 0; return; }
    memcpy(out, s, n);
    out[n] = '\0';
    *fits = 1;
}

void voice_stt_upload_timeouts(size_t wav_len, int *send_ms, int *recv_ms)
{
    const unsigned long long cap = 600000ull;
    unsigned long long snd = 30000ull + 1000ull * ((unsigned long long)wav_len / 65536ull);
    unsigned long long secs = wav_len > 44 ? ((unsigned long long)wav_len - 44ull) / 32000ull : 0ull;
    unsigned long long rcv = 60000ull + 1000ull * (secs / 10ull);
    if (snd > cap) snd = cap;
    if (rcv > cap) rcv = cap;
    if (send_ms) *send_ms = (int)snd;
    if (recv_ms) *recv_ms = (int)rcv;
}

/* The JSON parser holds a string of at most 8,191 bytes (TOK_VALUE_MAX), so a
 * long transcript cannot go through it. For a body that fails to parse and is
 * longer than that, scan for the one string the reply is about: the top-level
 * "text" of an OpenAI-shaped reply, or choices[0].message.content (the first
 * "content" key at depth 4) of a chat-shaped one, and decode it into out.
 * 1 on success (out holds the decoded text, NUL-terminated), -1 when the
 * string is not there or malformed, 0 when it does not fit cap. */
static int put_utf8(char *out, size_t cap, size_t *n, unsigned long cp)
{
    unsigned char b[4];
    size_t k;
    if (cp < 0x80) { b[0] = (unsigned char)cp; k = 1; }
    else if (cp < 0x800) { b[0] = (unsigned char)(0xC0 | (cp >> 6)); b[1] = (unsigned char)(0x80 | (cp & 0x3F)); k = 2; }
    else if (cp < 0x10000) {
        b[0] = (unsigned char)(0xE0 | (cp >> 12)); b[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (unsigned char)(0x80 | (cp & 0x3F)); k = 3;
    } else {
        b[0] = (unsigned char)(0xF0 | (cp >> 18)); b[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        b[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (unsigned char)(0x80 | (cp & 0x3F)); k = 4;
    }
    if (*n + k + 1 > cap) return 0;
    memcpy(out + *n, b, k);
    *n += k;
    return 1;
}

static int hex4(const char *p, const char *end, unsigned long *v)
{
    if (end - p < 4) return 0;
    *v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? 10 + c - 'a'
              : (c >= 'A' && c <= 'F') ? 10 + c - 'A' : -1;
        if (d < 0) return 0;
        *v = (*v << 4) | (unsigned long)d;
    }
    return 1;
}

/* p points just after the opening quote; decodes up to the closing quote. */
static int decode_json_string(const char *p, const char *end, char *out, size_t cap)
{
    size_t n = 0;
    while (p < end && *p != '"') {
        unsigned long cp;
        if (*p != '\\') {
            if (n + 2 > cap) return 0;
            out[n++] = *p++;
            continue;
        }
        if (++p >= end) return -1;
        char c = *p++;
        switch (c) {
        case 'n': cp = '\n'; break;
        case 't': cp = '\t'; break;
        case 'r': cp = '\r'; break;
        case 'b': cp = '\b'; break;
        case 'f': cp = '\f'; break;
        case '"': case '\\': case '/': cp = (unsigned char)c; break;
        case 'u':
            if (!hex4(p, end, &cp)) return -1;
            p += 4;
            if (cp >= 0xD800 && cp < 0xDC00) {
                unsigned long lo;
                if (end - p >= 6 && p[0] == '\\' && p[1] == 'u' && hex4(p + 2, end, &lo) &&
                    lo >= 0xDC00 && lo < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    p += 6;
                } else {
                    cp = 0xFFFD;
                }
            } else if (cp >= 0xDC00 && cp < 0xE000) {
                cp = 0xFFFD;
            }
            break;
        default: return -1;
        }
        if (!put_utf8(out, cap, &n, cp)) return 0;
    }
    if (p >= end) return -1;
    out[n] = '\0';
    return 1;
}

static int scan_large_text(SttShape shape, const char *body, size_t len, char *out, size_t cap)
{
    const char *p = body, *end = body + len;
    const int want_depth = shape == STT_SHAPE_GEMINI_CHAT ? 4 : 1;
    const char *want_key = shape == STT_SHAPE_GEMINI_CHAT ? "content" : "text";
    size_t kl = strlen(want_key);
    int depth = 0;
    while (p < end) {
        char c = *p;
        if (c == '{' || c == '[') { depth++; p++; continue; }
        if (c == '}' || c == ']') { depth--; p++; continue; }
        if (c != '"') { p++; continue; }
        const char *s0 = p + 1, *q = s0;   /* a string: a key when a colon follows */
        while (q < end && *q != '"') q += (*q == '\\' && q + 1 < end) ? 2 : 1;
        if (q >= end) return -1;
        const char *after = q + 1;
        while (after < end && (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n')) after++;
        int is_key = after < end && *after == ':';
        if (is_key && depth == want_depth && (size_t)(q - s0) == kl && memcmp(s0, want_key, kl) == 0) {
            after++;
            while (after < end && (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n')) after++;
            if (after >= end || *after != '"') return -1;
            return decode_json_string(after + 1, end, out, cap);
        }
        p = q + 1;
    }
    return -1;
}

int voice_stt_parse(SttShape shape, const char *body, size_t len,
                    char *out, size_t cap, char *err, size_t errcap)
{
    if (out && cap) out[0] = '\0';
    if (!body || !out || cap == 0) { put_err(err, errcap, "Empty speech-to-text response"); return 0; }
    JsonNode *root = json_parse(body);
    if (!root || root->type != JSON_OBJECT) {
        json_free(root);
        if (len > 8192) {   /* maybe a transcript longer than the parser's string limit */
            int fits = scan_large_text(shape, body, len, out, cap);
            if (fits == 1) {
                size_t n = strlen(out), a = 0;
                while (a < n && (out[a] == ' ' || out[a] == '\t' || out[a] == '\r' || out[a] == '\n')) a++;
                while (n > a && (out[n - 1] == ' ' || out[n - 1] == '\t' || out[n - 1] == '\r' || out[n - 1] == '\n')) n--;
                memmove(out, out + a, n - a);
                out[n - a] = '\0';
                return 1;
            }
            if (fits == 0) {
                out[0] = '\0';
                put_err(err, errcap, "The transcript was too long");
                return 0;
            }
        }
        put_err(err, errcap, "The speech-to-text response was not valid JSON");
        return 0;
    }
    const char *text = NULL;
    if (shape == STT_SHAPE_GEMINI_CHAT) {
        JsonNode *ch = json_obj_get(root, "choices");
        if (ch && ch->type == JSON_ARRAY && ch->as.arr.size > 0) {
            JsonNode *c0 = (JsonNode *)ch->as.arr.data[0];
            JsonNode *msg = c0 && c0->type == JSON_OBJECT ? json_obj_get(c0, "message") : NULL;
            if (msg && msg->type == JSON_OBJECT) text = json_obj_str(msg, "content");
        }
    } else {
        text = json_obj_str(root, "text");
    }
    int rc = 0;
    if (text) {
        int fits = 0;
        trim_into(text, out, cap, &fits);
        if (fits) rc = 1;
        else put_err(err, errcap, "The transcript was too long");
    } else {
        JsonNode *e = json_obj_get(root, "error");
        const char *msg = NULL;
        if (e && e->type == JSON_OBJECT) msg = json_obj_str(e, "message");
        else if (e && e->type == JSON_STRING) msg = e->as.str_val;
        if (msg && err && errcap)
            snprintf(err, errcap, "Speech-to-text error: %s", msg);
        else
            put_err(err, errcap, "The speech-to-text response had no transcript");
    }
    json_free(root);
    return rc;
}

void voice_stt_http_error(int status, const char *transport_err, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    if (status == 401 || status == 403)
        snprintf(out, cap, "The speech-to-text key was refused (HTTP %d). Check it in Settings > Voice.", status);
    else if (status == 404)
        snprintf(out, cap, "The speech-to-text model or URL was not found (HTTP 404). Check them in Settings > Voice.");
    else if (status == 413)
        snprintf(out, cap, "The recording was too large for the provider.");
    else if (status == 429)
        snprintf(out, cap, "The speech-to-text provider is rate limiting (HTTP 429). Try again shortly.");
    else if (status >= 400)
        snprintf(out, cap, "Speech-to-text failed (HTTP %d).", status);
    else
        snprintf(out, cap, "Speech-to-text could not connect: %s",
                 transport_err && transport_err[0] ? transport_err : "unknown error");
}
