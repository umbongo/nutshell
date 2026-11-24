/* src/core/voice_live.h -- live (as-you-speak) dictation over a provider's realtime
 * WebSocket: which settings make dictation live, the connection target, the JSON
 * client messages and the parser for server messages. Pure: no I/O, no logging (the
 * transcript and the key are user content). */
#ifndef NUTSHELL_VOICE_LIVE_H
#define NUTSHELL_VOICE_LIVE_H

#include <stddef.h>
#include <stdint.h>
#include "../config/config.h"

typedef enum { VLP_NONE = 0, VLP_OPENAI, VLP_MISTRAL } VoiceLiveProvider;

#define VOICE_LIVE_HANGOVER_OPENAI_MS 500
#define VOICE_LIVE_HANGOVER_MISTRAL_MS 800
#define VOICE_LIVE_CHUNK_MS 100
#define VOICE_LIVE_MIN_COMMIT_MS 100    /* OpenAI rejects a commit of less audio than this */

typedef struct {
    VoiceLiveProvider prov;
    char host[128];
    int port;
    int secure;
    char path[512];             /* path and query */
    char model[CFG_STR_MAX];
    char iso[8];                /* "" = auto */
    const char *prompt;         /* static spelling hint or NULL (OpenAI only) */
    int key_from_chat;          /* as SttTarget */
    unsigned rate;              /* 24000 OpenAI, 16000 Mistral */
    int hangover_ms;
} VoiceLiveTarget;

/* The effective speech-to-text provider when it is OpenAI or Mistral, else VLP_NONE. */
VoiceLiveProvider voice_live_provider(const Settings *s);
/* 1 and *t filled when live dictation applies. 0 when the provider is not live-capable,
 * resolution fails, or the model has a character outside [A-Za-z0-9._-]. */
int voice_live_resolve(const Settings *s, VoiceLiveTarget *t);
/* 1 when the effective provider is live-capable but the typed live model name is not valid
 * (so voice_live_resolve fails and dictation would silently fall back to batch): the glue
 * says so in the header. 0 otherwise, including an unusable provider (key missing). */
int voice_live_model_invalid(const Settings *s);
/* 1 when the Voice page shows the Live model combo, 0 when it shows the dim note:
 * the provider id the page has chosen (the AI provider's id while use_ai) is OpenAI or
 * Mistral. Takes the page's current choice, not the saved settings. */
int voice_live_row_kind(const char *provider, int use_ai, const char *ai_provider);
/* NULL-terminated seed list, [0] the default. */
const char *const *voice_live_models(VoiceLiveProvider p);

/* 1 when `samples` appended since the last commit (at `rate` Hz) are at least
 * VOICE_LIVE_MIN_COMMIT_MS: a shorter commit would be an error, so the sender skips it and
 * the audio stays in the provider's buffer for the next commit. */
int voice_live_commit_due(size_t samples, unsigned rate);

/* Client messages: heap JSON, NUL-terminated, *len excludes the NUL; the caller wipes and
 * frees. NULL on failure. */
char *voice_live_msg_session(const VoiceLiveTarget *t, size_t *len);
char *voice_live_msg_audio(const VoiceLiveTarget *t, const int16_t *pcm, size_t n, size_t *len);
/* OpenAI commit, Mistral flush. */
char *voice_live_msg_gate_closed(const VoiceLiveTarget *t, size_t *len);
/* Mistral input_audio.end; NULL with *len 0 for OpenAI (the caller sends a commit only
 * when audio is uncommitted). */
char *voice_live_msg_end(const VoiceLiveTarget *t, size_t *len);

typedef enum {
    VLE_IGNORE = 0, VLE_CREATED, VLE_READY, VLE_DELTA, VLE_COMPLETED, VLE_COMMITTED,
    VLE_ITEM_FAILED, VLE_DONE, VLE_ERROR, VLE_CLOSED /* set by the transport, never parsed */
} VoiceLiveEvKind;

typedef struct {
    VoiceLiveEvKind kind;
    char item[64];   /* OpenAI item_id; "" for Mistral */
    char *text;      /* heap: delta / transcript / done text / error message; may be NULL */
} VoiceLiveEvent;

/* 1 for a valid message (unknown types give VLE_IGNORE), 0 for malformed JSON or a missing
 * required field (kind VLE_IGNORE). */
int voice_live_parse(VoiceLiveProvider p, const char *json, size_t len, VoiceLiveEvent *ev);
/* secure_zero and free text. */
void voice_live_event_free(VoiceLiveEvent *ev);

#endif
