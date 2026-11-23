/* src/core/voice_stt.h -- speech-to-text providers, endpoint resolution, request
 * bodies and response parsing. Pure: no I/O, no logging (the transcript and the key
 * are user content). */
#ifndef NUTSHELL_VOICE_STT_H
#define NUTSHELL_VOICE_STT_H

#include <stddef.h>
#include "../config/config.h"

typedef enum { STT_SHAPE_OPENAI = 0, STT_SHAPE_GEMINI_CHAT = 1 } SttShape;

typedef struct {
    const char *id;            /* "openai" "groq" "mistral" "gemini" "custom" */
    const char *label;         /* "OpenAI" "Groq" "Mistral" "Google Gemini" "Custom" */
    const char *base;          /* default base URL, "" for custom */
    const char *const *models; /* NULL-terminated seed list, [0] is the default */
    SttShape shape;
} SttProvider;

const SttProvider *voice_stt_providers(size_t *n);
const SttProvider *voice_stt_provider_find(const char *id);   /* NULL when unknown */

/* Trim spaces and trailing '/', then strip a trailing "/audio/transcriptions" or
 * "/audio/speech" (and any '/' it leaves). Returns 0 when the result is empty, not
 * http(s):// or does not fit cap, else 1. */
int voice_base_url_normalise(const char *in, char *out, size_t cap);

/* Why "Use current AI assistant" cannot be ticked for this chat provider, or NULL. */
const char *voice_stt_use_ai_reason(const char *chat_provider, const char *chat_custom_url);

typedef struct {
    char url[512];        /* full endpoint */
    char model[CFG_STR_MAX];
    char language[16];
    SttShape shape;
    int key_from_chat;    /* 1: use Settings.ai_api_key; 0: stt_api_key */
} SttTarget;

/* 1 and *t filled, or 0 with a user-facing reason in err. */
int voice_stt_resolve(const Settings *s, SttTarget *t, char *err, size_t cap);

/* multipart/form-data body (heap, free()); *out_len is its byte count. */
char *voice_stt_multipart(const char *boundary, const unsigned char *wav, size_t wav_len,
                          const char *model, const char *language, size_t *out_len);

/* OpenAI chat-completions body carrying the audio as an input_audio part. Heap,
 * NUL-terminated; *out_len excludes the NUL. */
char *voice_stt_gemini_body(const unsigned char *wav, size_t wav_len,
                            const char *model, const char *language, size_t *out_len);

/* 1 and the trimmed transcript in out, or 0 with err. */
int voice_stt_parse(SttShape shape, const char *body, size_t len,
                    char *out, size_t cap, char *err, size_t errcap);

/* User-facing text for a failed request. */
void voice_stt_http_error(int status, const char *transport_err, char *out, size_t cap);

#endif
