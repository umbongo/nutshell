/* src/core/voice_live.c -- live dictation provider protocol. See voice_live.h. */
#include "voice_live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base64.h"
#include "json_scan.h"
#include "json_validate.h"
#include "secure_zero.h"
#include "voice_lang.h"
#include "voice_stt.h"
#include "../config/json_parser.h"

#define MODEL_MAX_LEN 100

static const char *const k_models_openai[] = {"gpt-live-transcribe", "gpt-realtime-whisper", NULL};
static const char *const k_models_mistral[] = {"voxtral-mini-transcribe-realtime-3", NULL};

const char *const *voice_live_models(VoiceLiveProvider p)
{
    if (p == VLP_OPENAI) return k_models_openai;
    if (p == VLP_MISTRAL) return k_models_mistral;
    return NULL;
}

static VoiceLiveProvider provider_of(const SttTarget *st)
{
    if (strcmp(st->provider, "openai") == 0) return VLP_OPENAI;
    if (strcmp(st->provider, "mistral") == 0) return VLP_MISTRAL;
    return VLP_NONE;
}

VoiceLiveProvider voice_live_provider(const Settings *s)
{
    SttTarget st;
    char err[200];
    if (!voice_stt_resolve(s, &st, err, sizeof err)) return VLP_NONE;
    return provider_of(&st);
}

static int model_ok(const char *m)
{
    size_t n = strlen(m);
    if (n == 0 || n > MODEL_MAX_LEN) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = m[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

int voice_live_resolve(const Settings *s, VoiceLiveTarget *t)
{
    SttTarget st;
    char err[200];
    if (!s || !t) return 0;
    memset(t, 0, sizeof *t);
    if (!voice_stt_resolve(s, &st, err, sizeof err)) return 0;
    VoiceLiveProvider p = provider_of(&st);
    if (p == VLP_NONE) return 0;
    const char *model = s->voice_stt_live_model[0] ? s->voice_stt_live_model
                                                   : voice_live_models(p)[0];
    if (!model_ok(model)) return 0;
    t->prov = p;
    t->port = 443;
    t->secure = 1;
    snprintf(t->model, sizeof t->model, "%s", model);
    voice_lang_iso(st.language, t->iso, sizeof t->iso);
    t->key_from_chat = st.key_from_chat;
    if (p == VLP_OPENAI) {
        snprintf(t->host, sizeof t->host, "api.openai.com");
        snprintf(t->path, sizeof t->path, "/v1/realtime?intent=transcription");
        t->prompt = voice_lang_spelling_hint(st.language);
        t->rate = 24000;
        t->hangover_ms = VOICE_LIVE_HANGOVER_OPENAI_MS;
    } else {
        snprintf(t->host, sizeof t->host, "api.mistral.ai");
        snprintf(t->path, sizeof t->path, "/v1/audio/transcriptions/realtime?model=%s", t->model);
        t->prompt = NULL;
        t->rate = 16000;
        t->hangover_ms = VOICE_LIVE_HANGOVER_MISTRAL_MS;
    }
    return 1;
}

/* ---- client messages ---- */

static char *dup_literal(const char *s, size_t *len)
{
    size_t n = strlen(s);
    char *r = malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n + 1);
    if (len) *len = n;
    return r;
}

/* Append a quoted, escaped string at *pos. 0 on overflow. */
static int put_str(char *buf, size_t cap, size_t *pos, const char *s)
{
    size_t np = json_escape_string(s, strlen(s), buf, cap, *pos, 1);
    if (np == 0) return 0;
    *pos = np;
    return 1;
}

static int put_raw(char *buf, size_t cap, size_t *pos, const char *s)
{
    size_t n = strlen(s);
    if (*pos + n + 1 > cap) return 0;
    memcpy(buf + *pos, s, n);
    *pos += n;
    buf[*pos] = '\0';
    return 1;
}

char *voice_live_msg_session(const VoiceLiveTarget *t, size_t *len)
{
    if (len) *len = 0;
    if (!t || (t->prov != VLP_OPENAI && t->prov != VLP_MISTRAL)) return NULL;
    size_t cap = 768 + 6 * (strlen(t->model) + (t->prompt ? strlen(t->prompt) : 0) + strlen(t->iso));
    char *b = malloc(cap);
    if (!b) return NULL;
    size_t pos = 0;
    char num[32];
    int ok = 1;
    b[0] = '\0';
    if (t->prov == VLP_OPENAI) {
        snprintf(num, sizeof num, "%u", t->rate);
        ok = put_raw(b, cap, &pos, "{\"type\":\"session.update\",\"session\":{\"type\":\"transcription\","
                                   "\"audio\":{\"input\":{\"format\":{\"type\":\"audio/pcm\",\"rate\":") &&
             put_raw(b, cap, &pos, num) &&
             put_raw(b, cap, &pos, "},\"transcription\":{\"model\":") &&
             put_str(b, cap, &pos, t->model);
        if (ok && t->iso[0])
            ok = put_raw(b, cap, &pos, ",\"languages\":[") && put_str(b, cap, &pos, t->iso) &&
                 put_raw(b, cap, &pos, "]");
        if (ok && t->prompt && t->prompt[0])
            ok = put_raw(b, cap, &pos, ",\"prompt\":") && put_str(b, cap, &pos, t->prompt);
        if (ok) ok = put_raw(b, cap, &pos, "},\"turn_detection\":null}}}}");
    } else {
        snprintf(num, sizeof num, "%u", t->rate);
        ok = put_raw(b, cap, &pos, "{\"type\":\"session.update\",\"session\":{\"audio_format\":"
                                   "{\"encoding\":\"pcm_s16le\",\"sample_rate\":") &&
             put_raw(b, cap, &pos, num) &&
             put_raw(b, cap, &pos, "},\"target_streaming_delay_ms\":480");
        if (ok && t->iso[0])
            ok = put_raw(b, cap, &pos, ",\"language\":") && put_str(b, cap, &pos, t->iso);
        if (ok) ok = put_raw(b, cap, &pos, "}}");
    }
    if (!ok) { free(b); return NULL; }
    b[pos] = '\0';
    if (len) *len = pos;
    return b;
}

char *voice_live_msg_audio(const VoiceLiveTarget *t, const int16_t *pcm, size_t n, size_t *len)
{
    if (len) *len = 0;
    if (!t || !pcm || n == 0 || n > (size_t)1 << 24) return NULL;
    const char *head;
    if (t->prov == VLP_OPENAI) head = "{\"type\":\"input_audio_buffer.append\",\"audio\":\"";
    else if (t->prov == VLP_MISTRAL) head = "{\"type\":\"input_audio.append\",\"audio\":\"";
    else return NULL;
    size_t nbytes = n * 2;
    size_t b64cap = ((nbytes + 2) / 3) * 4 + 1;
    unsigned char *raw = malloc(nbytes);
    char *b64 = malloc(b64cap);
    char *out = NULL;
    if (raw && b64) {
        for (size_t i = 0; i < n; i++) {
            uint16_t u = (uint16_t)pcm[i];
            raw[2 * i] = (unsigned char)(u & 0xFF);
            raw[2 * i + 1] = (unsigned char)(u >> 8);
        }
        size_t w = base64_encode(raw, nbytes, b64, b64cap);
        if (w > 0) {
            size_t hl = strlen(head);
            out = malloc(hl + w + 3);
            if (out) {
                memcpy(out, head, hl);
                memcpy(out + hl, b64, w);
                memcpy(out + hl + w, "\"}", 3);
                if (len) *len = hl + w + 2;
            }
        }
    }
    if (raw) { secure_zero(raw, nbytes); free(raw); }
    if (b64) { secure_zero(b64, b64cap); free(b64); }
    return out;
}

char *voice_live_msg_gate_closed(const VoiceLiveTarget *t, size_t *len)
{
    if (len) *len = 0;
    if (!t) return NULL;
    if (t->prov == VLP_OPENAI) return dup_literal("{\"type\":\"input_audio_buffer.commit\"}", len);
    if (t->prov == VLP_MISTRAL) return dup_literal("{\"type\":\"input_audio.flush\"}", len);
    return NULL;
}

char *voice_live_msg_end(const VoiceLiveTarget *t, size_t *len)
{
    if (len) *len = 0;
    if (t && t->prov == VLP_MISTRAL) return dup_literal("{\"type\":\"input_audio.end\"}", len);
    return NULL;
}

/* ---- server messages ---- */

static char *dup_str(const char *s)
{
    size_t n = strlen(s);
    char *r = malloc(n + 1);
    if (r) memcpy(r, s, n + 1);
    return r;
}

/* The message of an `error` object: error.message as a string, or as an object its
 * "detail" or "message" string, or error itself as a string; else a fixed text. */
static const char *error_text(const JsonNode *root)
{
    const JsonNode *e = json_obj_get(root, "error");
    if (e && e->type == JSON_STRING && e->as.str_val) return e->as.str_val;
    if (e && e->type == JSON_OBJECT) {
        const JsonNode *m = json_obj_get(e, "message");
        if (m && m->type == JSON_STRING && m->as.str_val) return m->as.str_val;
        const JsonNode *o = (m && m->type == JSON_OBJECT) ? m : e;
        const char *d = json_obj_str(o, "detail");
        if (d) return d;
        d = json_obj_str(o, "message");
        if (d) return d;
    }
    return "Provider error";
}

/* json_free releases string values with plain free(), and they hold transcript text and
 * error strings (user content). Wipe every string value in the tree before freeing it. */
static void wipe_tree(const JsonNode *n)
{
    if (!n) return;
    if (n->type == JSON_STRING) {
        /* Exactly strlen bytes: the allocation is strlen+1 even when str_len is larger
         * (a \u0000 escape ends the C string early). */
        if (n->as.str_val) secure_zero(n->as.str_val, strlen(n->as.str_val));
    } else if (n->type == JSON_ARRAY) {
        size_t c = vec_size(&n->as.arr);
        for (size_t i = 0; i < c; i++) wipe_tree((const JsonNode *)vec_get(&n->as.arr, i));
    } else if (n->type == JSON_OBJECT) {
        size_t c = vec_size(&n->as.obj.vals);
        for (size_t i = 0; i < c; i++) wipe_tree((const JsonNode *)vec_get(&n->as.obj.vals, i));
    }
}

/* The tokenizer refuses a string over 8,191 bytes, so a long final transcript fails
 * json_parse. Scan the type, and for a completion (only) the text and item id by hand.
 * 1 and *ev filled, or 0. */
static int parse_large_final(VoiceLiveProvider p, const char *json, size_t len, VoiceLiveEvent *ev)
{
    char *type = NULL, *item = NULL, *text = NULL;
    size_t tl = 0, il = 0, xl = 0;
    int ok = 0;
    if (!json_scan_string(json, len, 1, "type", 128, &type, &tl)) goto out;
    VoiceLiveEvKind kind;
    const char *field;
    if (p == VLP_MISTRAL && strcmp(type, "transcription.done") == 0) {
        kind = VLE_DONE; field = "text";
    } else if (p == VLP_OPENAI && strcmp(type, "conversation.item.input_audio_transcription.completed") == 0) {
        kind = VLE_COMPLETED; field = "transcript";
    } else {
        goto out;
    }
    if (kind == VLE_COMPLETED &&
        !json_scan_string(json, len, 1, "item_id", sizeof ev->item - 1, &item, &il))
        goto out;
    if (!json_scan_string(json, len, 1, field, (size_t)1 << 20, &text, &xl)) goto out;
    ev->kind = kind;
    ev->text = text;
    text = NULL;
    if (item) snprintf(ev->item, sizeof ev->item, "%s", item);
    ok = 1;
out:
    if (type) { secure_zero(type, tl); free(type); }
    if (item) { secure_zero(item, il); free(item); }
    if (text) { secure_zero(text, xl); free(text); }
    return ok;
}

int voice_live_parse(VoiceLiveProvider p, const char *json, size_t len, VoiceLiveEvent *ev)
{
    if (!ev) return 0;
    memset(ev, 0, sizeof *ev);
    ev->kind = VLE_IGNORE;
    if (!json || (p != VLP_OPENAI && p != VLP_MISTRAL)) return 0;
    char *z = malloc(len + 1);
    if (!z) return 0;
    memcpy(z, json, len);
    z[len] = '\0';
    JsonNode *root = json_parse(z);
    secure_zero(z, len);
    free(z);
    if (!root) return len > 8192 ? parse_large_final(p, json, len, ev) : 0;
    int ok = 0;
    const char *type = root->type == JSON_OBJECT ? json_obj_str(root, "type") : NULL;
    if (!type) goto done;

    VoiceLiveEvKind kind = VLE_IGNORE;
    const char *field = NULL;  /* text field to copy */
    int need_item = 0;
    int err_text = 0;
    if (strcmp(type, "session.created") == 0) kind = VLE_CREATED;
    else if (strcmp(type, "session.updated") == 0) kind = VLE_READY;
    else if (strcmp(type, "error") == 0) { kind = VLE_ERROR; err_text = 1; }
    else if (p == VLP_OPENAI) {
        if (strcmp(type, "input_audio_buffer.committed") == 0) { kind = VLE_COMMITTED; need_item = 1; }
        else if (strcmp(type, "conversation.item.input_audio_transcription.delta") == 0) {
            kind = VLE_DELTA; field = "delta"; need_item = 1;
        } else if (strcmp(type, "conversation.item.input_audio_transcription.completed") == 0) {
            kind = VLE_COMPLETED; field = "transcript"; need_item = 1;
        } else if (strcmp(type, "conversation.item.input_audio_transcription.failed") == 0) {
            kind = VLE_ITEM_FAILED; need_item = 1; err_text = 1;
        }
    } else {
        if (strcmp(type, "transcription.text.delta") == 0) { kind = VLE_DELTA; field = "text"; }
        else if (strcmp(type, "transcription.done") == 0) { kind = VLE_DONE; field = "text"; }
    }

    const char *item = NULL;
    if (need_item) {
        item = json_obj_str(root, "item_id");
        if (!item || strlen(item) >= sizeof ev->item) goto done;
    }
    char *text = NULL;
    if (field) {
        const char *v = json_obj_str(root, field);
        /* A completion without a transcript still counts as a completion (the caller
         * matches commits to completions); every other text field is required. */
        if (!v && kind != VLE_COMPLETED) goto done;
        if (v) {
            text = dup_str(v);
            if (!text) goto done;
        }
    } else if (err_text) {
        text = dup_str(error_text(root));
        if (!text) goto done;
    }
    ev->kind = kind;
    ev->text = text;
    if (item) snprintf(ev->item, sizeof ev->item, "%s", item);
    ok = 1;
done:
    wipe_tree(root);
    json_free(root);
    return ok;
}

void voice_live_event_free(VoiceLiveEvent *ev)
{
    if (!ev) return;
    if (ev->text) {
        secure_zero(ev->text, strlen(ev->text));
        free(ev->text);
        ev->text = NULL;
    }
}
