/* src/core/voice_aispeech.c -- see voice_aispeech.h. No logging, no I/O: the spoken
 * text, the bodies, the audio and the keys are user content. */
#include "voice_aispeech.h"
#include "voice_stt.h"
#include "base64.h"
#include "json_scan.h"
#include "json_validate.h"
#include "secure_zero.h"
#include "sse_line.h"
#include "../config/json_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MISTRAL_URL "https://api.mistral.ai/v1/audio/speech"
#define MISTRAL_HOST "api.mistral.ai"
#define SPEECH_SUFFIX "/audio/speech"
/* What Mistral is asked for. R25: "pcm" (raw float32, 24 kHz mono) is the one-line
 * switch if the capture shows one WAV header per delta. */
#define MISTRAL_FORMAT "wav"
#define CUSTOM_FORMAT "wav"

static const char *const k_mistral_models[] = {"voxtral-mini-tts-3-0", NULL};
static const char *const k_mistral_voices[] = {
    "voice_1787", "voice_1786", "voice_590", "gb_jane_neutral", NULL};
static const char *const k_custom_models[] = {"kokoro", NULL};
static const char *const k_custom_voices[] = {"bf_emma", "af_heart", NULL};

static const VoiceAisProvider k_providers[] = {
    {"mistral", "Mistral", "https://api.mistral.ai/v1", k_mistral_models, k_mistral_voices,
     VAIS_SHAPE_MISTRAL, 1},
    {"custom", "Custom", "", k_custom_models, k_custom_voices, VAIS_SHAPE_OPENAI, 0},
};

const VoiceAisProvider *voice_ais_providers(size_t *n)
{
    if (n) *n = sizeof k_providers / sizeof k_providers[0];
    return k_providers;
}

const VoiceAisProvider *voice_ais_provider_find(const char *id)
{
    if (!id) return NULL;
    for (size_t i = 0; i < sizeof k_providers / sizeof k_providers[0]; i++)
        if (strcmp(k_providers[i].id, id) == 0) return &k_providers[i];
    return NULL;
}

static void put_err(char *err, size_t cap, const char *msg)
{
    if (err && cap) snprintf(err, cap, "%s", msg);
}

static int blank(const char *s)
{
    for (; *s; s++)
        if (*s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') return 0;
    return 1;
}

/* host[:port] of an http(s) URL whose authority has no userinfo (authority_unreadable). */
static void url_host(const char *url, char *out, size_t cap)
{
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    size_t n = strcspn(p, "/?#");
    if (n + 1 > cap) n = cap ? cap - 1 : 0;
    if (cap) { memcpy(out, p, n); out[n] = '\0'; }
}

/* 1 when the authority (between "://" and the next "/", "?" or "#") has an "@" or a "[": a
 * userinfo or an IPv6 literal, which the privacy line's reader and WinHTTP's URL parser read
 * differently, so the line could name a host the request does not go to. */
static int authority_unreadable(const char *url)
{
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    size_t n = strcspn(p, "/?#");
    for (size_t i = 0; i < n; i++)
        if (p[i] == '@' || p[i] == '[') return 1;
    return 0;
}

void voice_ais_binding(const Settings *s, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!s) return;
    const VoiceAisProvider *p = voice_ais_provider_find(s->voice_tts_provider);
    if (!p) return;
    if (strcmp(p->id, "mistral") == 0) {
        if (cap > strlen("mistral")) memcpy(out, "mistral", sizeof "mistral");
        return;
    }
    char base[CFG_STR_MAX];
    if (!voice_base_url_normalise(s->voice_tts_url, base, sizeof base)) return;
    int n = snprintf(out, cap, "custom %s", base);
    if (n < 0 || (size_t)n >= cap) out[0] = '\0';   /* never a cut binding */
}

int voice_ais_key_shared(const Settings *s)
{
    if (!s) return VAIS_KEY_OWN;
    const VoiceAisProvider *p = voice_ais_provider_find(s->voice_tts_provider);
    if (!p) return VAIS_KEY_OWN;
    SttTarget st;
    if (!voice_stt_resolve(s, &st, NULL, 0)) return VAIS_KEY_OWN;
    if (strcmp(st.provider, p->id) != 0) return VAIS_KEY_OWN;
    if (strcmp(p->id, "custom") == 0) {
        char a[512], b[512];
        if (!voice_base_url_normalise(s->voice_tts_url, a, sizeof a)) return VAIS_KEY_OWN;
        if (!voice_base_url_normalise(st.url, b, sizeof b)) return VAIS_KEY_OWN;
        if (strcmp(a, b) != 0) return VAIS_KEY_OWN;
    }
    return st.key_from_chat ? VAIS_KEY_CHAT : VAIS_KEY_STT;
}

const char *voice_ais_key(const Settings *s, const VoiceAisTarget *t)
{
    if (!s || !t) return "";
    switch (t->key_from) {
    case VAIS_KEY_CHAT: return s->ai_api_key;
    case VAIS_KEY_STT: return s->stt_api_key;
    default: break;
    }
    char b[CFG_STR_MAX];
    voice_ais_binding(s, b, sizeof b);
    if (b[0] == '\0' || strcmp(b, s->tts_api_key_for) != 0) return "";
    return s->tts_api_key;
}

int voice_ais_key_still_bound(const char *typed_for, const char *now)
{
    return typed_for && now && strcmp(typed_for, now) == 0;
}

int voice_ais_resolve(const Settings *s, VoiceAisTarget *t, char *err, size_t cap)
{
    if (!s || !t) return 0;
    memset(t, 0, sizeof *t);
    const VoiceAisProvider *p = voice_ais_provider_find(s->voice_tts_provider);
    if (!p) { put_err(err, cap, "The speech provider is not known"); return 0; }
    snprintf(t->provider, sizeof t->provider, "%s", p->id);
    t->shape = p->shape;
    int custom = strcmp(p->id, "custom") == 0;
    if (custom) {
        char base[512];
        if (blank(s->voice_tts_url)) { put_err(err, cap, "Enter the speech Base URL"); return 0; }
        if (!voice_base_url_normalise(s->voice_tts_url, base, sizeof base) ||
            authority_unreadable(base) ||
            snprintf(t->url, sizeof t->url, "%s" SPEECH_SUFFIX, base) >= (int)sizeof t->url) {
            put_err(err, cap, "The speech Base URL is not valid");
            return 0;
        }
        url_host(base, t->host, sizeof t->host);
        char bind[CFG_STR_MAX];
        voice_ais_binding(s, bind, sizeof bind);
        /* "custom " + base does not fit: no own key could ever match. That matters only when an
         * own key is the one in use and one is typed; a keyless server, or one that shares the
         * speech-to-text or chat key, never reads the binding. */
        if (bind[0] == '\0' && voice_ais_key_shared(s) == VAIS_KEY_OWN && s->tts_api_key[0]) {
            put_err(err, cap, "The speech Base URL is too long");
            return 0;
        }
        if (s->voice_ai_tts_model[0] == '\0') {
            put_err(err, cap, "Enter the speech model your server uses");
            return 0;
        }
        if (s->voice_ai_voice[0] == '\0') {
            put_err(err, cap, "Enter the voice your server uses");
            return 0;
        }
        snprintf(t->model, sizeof t->model, "%s", s->voice_ai_tts_model);
        snprintf(t->voice, sizeof t->voice, "%s", s->voice_ai_voice);
        if (!blank(s->voice_ai_style)) {
            snprintf(t->style, sizeof t->style, "%s", s->voice_ai_style);
            t->send_instructions = 1;
        }
    } else {
        snprintf(t->url, sizeof t->url, "%s", MISTRAL_URL);
        snprintf(t->host, sizeof t->host, "%s", MISTRAL_HOST);
        snprintf(t->model, sizeof t->model, "%s",
                 s->voice_ai_tts_model[0] ? s->voice_ai_tts_model : p->models[0]);
        snprintf(t->voice, sizeof t->voice, "%s",
                 s->voice_ai_voice[0] ? s->voice_ai_voice : p->voices[0]);
    }
    t->key_from = (VoiceAisKey)voice_ais_key_shared(s);
    if (p->needs_key && voice_ais_key(s, t)[0] == '\0') {
        /* A shared key is the speech-to-text one; the speech key row is hidden then (R13). */
        put_err(err, cap, t->key_from == VAIS_KEY_OWN ? "Enter a speech API key"
                                                      : "Enter a speech-to-text API key");
        return 0;
    }
    return 1;
}

/* ---- request bodies ---- */

typedef struct { char *buf; size_t cap; size_t pos; int ok; } Out;

static void out_lit(Out *o, const char *lit)
{
    size_t n = strlen(lit);
    if (!o->ok || o->pos + n >= o->cap) { o->ok = 0; return; }
    memcpy(o->buf + o->pos, lit, n);
    o->pos += n;
}

/* A quoted, escaped JSON string. */
static void out_str(Out *o, const char *s, size_t n)
{
    if (!o->ok) return;
    size_t np = json_escape_string(s, n, o->buf, o->cap, o->pos, 1);
    if (np == 0) { o->ok = 0; return; }
    o->pos = np;
}

static int has_crlf(const char *s)
{
    return strchr(s, '\r') != NULL || strchr(s, '\n') != NULL;
}

char *voice_ais_body(const VoiceAisTarget *t, const char *text, size_t len, size_t *out_len)
{
    if (!t || (!text && len)) return NULL;
    if (has_crlf(t->model) || has_crlf(t->voice)) return NULL;
    size_t style_len = strlen(t->style);
    size_t model_len = strlen(t->model), voice_len = strlen(t->voice);
    if (len > (size_t)1 << 28) return NULL;
    char *mapped = (char *)malloc(len + 1);
    if (!mapped) return NULL;
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        mapped[i] = c == '[' ? '(' : c == ']' ? ')' : c;
    }
    Out o;
    o.cap = 6 * (len + model_len + voice_len + style_len) + 256;
    o.buf = (char *)malloc(o.cap);
    o.pos = 0;
    o.ok = o.buf != NULL;
    if (o.ok) {
        if (t->shape == VAIS_SHAPE_MISTRAL) {
            out_lit(&o, "{\"model\":");
            out_str(&o, t->model, model_len);
            out_lit(&o, ",\"input\":");
            out_str(&o, mapped, len);
            out_lit(&o, ",\"voice_id\":");
            out_str(&o, t->voice, voice_len);
            out_lit(&o, ",\"response_format\":\"" MISTRAL_FORMAT "\",\"stream\":true}");
        } else {
            out_lit(&o, "{\"model\":");
            out_str(&o, t->model, model_len);
            out_lit(&o, ",\"voice\":");
            out_str(&o, t->voice, voice_len);
            out_lit(&o, ",\"input\":");
            out_str(&o, mapped, len);
            out_lit(&o, ",\"response_format\":\"" CUSTOM_FORMAT "\"");
            if (t->send_instructions && style_len) {
                out_lit(&o, ",\"instructions\":");
                out_str(&o, t->style, style_len);
            }
            out_lit(&o, "}");
        }
    }
    secure_zero(mapped, len);
    free(mapped);
    if (!o.ok) {
        if (o.buf) { secure_zero(o.buf, o.cap); free(o.buf); }
        return NULL;
    }
    o.buf[o.pos] = '\0';
    if (out_len) *out_len = o.pos;
    return o.buf;
}

/* ---- failures ---- */

int voice_ais_failure_fatal(long hr)
{
    return hr == VOICE_AIS_HR_RESOLVE || hr == VOICE_AIS_HR_FORMAT ||
           (hr >= 300 && hr <= 399) ||                   /* a redirect: the Base URL is wrong */
           hr == 400 || hr == 401 || hr == 403 || hr == 404 || hr == 422;
}

long voice_ais_decoder_failure(int format_err, int notwav_err, uint64_t frames_decoded)
{
    if (format_err) return VOICE_AIS_HR_FORMAT;
    if (notwav_err && frames_decoded == 0) return VOICE_AIS_HR_FORMAT;
    return VOICE_AIS_HR_STREAM;
}

void voice_ais_failure_reason(long hr, const char *resolve_err, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    switch (hr) {
    case 401:
    case 403:
        snprintf(out, cap, "the speech provider refused the key (HTTP %ld)", hr);
        break;
    case 404:
        snprintf(out, cap, "the speech model or voice was not found (HTTP 404)");
        break;
    case 400:
    case 422:
        snprintf(out, cap, "the speech provider refused the request (HTTP %ld)", hr);
        break;
    case VOICE_AIS_HR_FORMAT:
        snprintf(out, cap, "unsupported audio format");
        break;
    case VOICE_AIS_HR_RESOLVE:
        snprintf(out, cap, "%s", resolve_err && resolve_err[0] ? resolve_err
                                                               : "the speech settings cannot be used");
        break;
    case VOICE_AIS_HR_NET:
        snprintf(out, cap, "the speech provider could not be reached");
        break;
    case VOICE_AIS_HR_DEVICE:
        snprintf(out, cap, "the audio device was lost");
        break;
    case VOICE_AIS_HR_STREAM:
        snprintf(out, cap, "the speech stream was broken");
        break;
    default:
        if (hr >= 300 && hr <= 399)
            snprintf(out, cap, "the server redirected the request (HTTP %ld): check the Base URL", hr);
        else if (hr > 0) snprintf(out, cap, "the speech provider returned HTTP %ld", hr);
        else snprintf(out, cap, "the speech request failed");
        break;
    }
}

void voice_ais_sample_failure_note(long hr, char *out, size_t cap)
{
    if (!out || cap == 0) return;
    char reason[160];
    voice_ais_failure_reason(hr, NULL, reason, sizeof reason);
    snprintf(out, cap, "Could not play the sample: %s", reason);
}

int voice_ais_frames_over(uint64_t total, uint64_t add, uint64_t cap)
{
    if (total > cap) return 1;
    return add > cap - total;
}

int voice_ais_audio_over_cap(uint64_t total, uint64_t add)
{
    return voice_ais_frames_over(total, add, VOICE_AIS_MAX_FRAMES);
}

/* ---- Mistral's events ---- */

#define EV_DELTA "speech.audio.delta"
#define EV_DONE "speech.audio.done"
#define EV_ERROR "error"

void voice_ais_sse_init(VoiceAisSse *s)
{
    if (s) memset(s, 0, sizeof *s);
}

static int field_value(const char *line, size_t len, const char *name, const char **v, size_t *vl)
{
    size_t nl = strlen(name);
    if (len < nl || memcmp(line, name, nl) != 0) return 0;
    size_t i = nl;
    if (i < len && line[i] == ' ') i++;
    *v = line + i;
    *vl = len - i;
    return 1;
}

static int is_name(const char *a, const char *b)
{
    return a && strcmp(a, b) == 0;
}

static void free_scan(char *p, size_t n)
{
    if (p) { secure_zero(p, n); free(p); }
}

/* The data of one event. The event: name and the data "type" each name it. */
static void handle_data(VoiceAisSse *s, const char *data, size_t len, VoiceAisEv *ev)
{
    char *type = NULL;
    size_t type_len = 0;
    if (len > 0) {
        if (!json_scan_string(data, len, 1, "type", 63, &type, &type_len)) type = NULL;
    }
    char type_name[64];
    snprintf(type_name, sizeof type_name, "%s", type ? type : "");
    int err_name = is_name(s->event, EV_ERROR) || is_name(type, EV_ERROR);
    int delta = is_name(s->event, EV_DELTA) || is_name(type, EV_DELTA);
    int done = is_name(s->event, EV_DONE) || is_name(type, EV_DONE);
    free_scan(type, type_len + 1);

    if (err_name) { ev->kind = VAIS_EV_ERROR; return; }
    if (delta) {
        char *b64 = NULL;
        size_t b64_len = 0;
        if (len == 0 || !json_scan_string(data, len, 1, "audio_data", SSE_LINE_MAX, &b64, &b64_len)) {
            ev->kind = VAIS_EV_ERROR;               /* a delta with no audio_data string */
            return;
        }
        size_t cap = b64_len / 4 * 3 + 3;
        unsigned char *raw = (unsigned char *)malloc(cap);
        size_t n = raw ? base64_decode(b64, b64_len, raw, cap) : (size_t)-1;
        free_scan(b64, b64_len + 1);
        if (n == (size_t)-1) {
            if (raw) { secure_zero(raw, cap); free(raw); }
            ev->kind = VAIS_EV_ERROR;
            return;
        }
        if (n == 0) {                               /* an empty delta is no audio */
            free(raw);
            return;
        }
        s->audio_seen = 1;
        ev->kind = VAIS_EV_AUDIO;
        ev->audio = raw;
        ev->audio_len = n;
        return;
    }
    /* Everything else is small: parse it for an error member. */
    JsonNode *root = NULL;
    if (len > 0) {
        char *z = (char *)malloc(len + 1);
        if (z) {
            memcpy(z, data, len);
            z[len] = '\0';
            root = json_parse(z);
            secure_zero(z, len);
            free(z);
        }
    }
    int parsed = root != NULL;
    const JsonNode *errm = (root && root->type == JSON_OBJECT) ? json_obj_get(root, "error") : NULL;
    int has_error = errm && errm->type != JSON_NULL;   /* "error": null is no error */
    json_free(root);
    if (has_error) { ev->kind = VAIS_EV_ERROR; return; }
    if (done) {
        if (len > 0 && !parsed) {                   /* a known name with data that is not JSON fails */
            ev->kind = VAIS_EV_ERROR;
            return;
        }
        ev->kind = s->audio_seen ? VAIS_EV_DONE : VAIS_EV_ERROR;   /* a reply with no audio is a failure */
        return;
    }
    /* Data that is not JSON fails the reply under no event name; under any other
     * (unknown) name the event is ignored, as R28 says of unknown events. */
    if (len > 0 && !parsed && s->event[0] == '\0') {
        ev->kind = VAIS_EV_ERROR;
        return;
    }
    /* An ignored event: its name (never its data) for the log. */
    snprintf(ev->event, sizeof ev->event, "%s", s->event[0] ? s->event : type_name);
}

int voice_ais_sse_line(VoiceAisSse *s, const char *line, size_t len, VoiceAisEv *ev)
{
    if (!ev) return 0;
    memset(ev, 0, sizeof *ev);
    if (!s || (!line && len)) return 0;
    const char *v;
    size_t vl;
    if (len == 0) {
        /* A blank line ends an event; an "event: error" with no data is still one. */
        if (is_name(s->event, EV_ERROR)) ev->kind = VAIS_EV_ERROR;
        s->event[0] = '\0';
    } else if (field_value(line, len, "event:", &v, &vl)) {
        if (vl >= sizeof s->event) vl = 0;          /* longer than any name we know */
        memcpy(s->event, v, vl);
        s->event[vl] = '\0';
    } else if (field_value(line, len, "data:", &v, &vl)) {
        handle_data(s, v, vl, ev);
        s->event[0] = '\0';
    }
    return ev->kind == VAIS_EV_DONE || ev->kind == VAIS_EV_ERROR;
}

/* ---- the offset map ---- */

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

size_t voice_ais_offset(const char *text, size_t chunk_from, size_t chunk_len,
                        uint64_t played, uint64_t total, int complete)
{
    if (!text || chunk_len == 0 || played == 0) return chunk_from;
    size_t into;
    if (complete) {
        if (total == 0) return chunk_from;
        if (played >= total) return chunk_from + chunk_len;
        into = (size_t)((double)played / (double)total * (double)chunk_len);
    } else {
        uint64_t bytes = played / VOICE_AIS_EST_FRAMES_PER_BYTE;
        if (bytes >= chunk_len) return chunk_from + chunk_len;
        into = (size_t)bytes;
    }
    if (into >= chunk_len) return chunk_from + chunk_len;
    size_t idx = chunk_from + into;
    while (idx > chunk_from && ((unsigned char)text[idx] & 0xC0) == 0x80) idx--;
    while (idx > chunk_from && !is_space(text[idx - 1])) idx--;
    return idx;
}
