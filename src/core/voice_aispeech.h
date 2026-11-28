/* src/core/voice_aispeech.h -- AI speech: the providers (Mistral, Custom), target
 * resolution, the speech key and what binds it to its provider, request bodies,
 * Mistral's server-sent events, failure classes and the cut-off offset map.
 *
 * Pure: no Win32, no I/O, no logging. The spoken text, the bodies, the audio and the
 * keys are user content, so nothing here prints them; every buffer this module frees
 * itself is secure_zero()ed first. Buffers it returns (a body, decoded audio) are the
 * caller's to secure_zero() and free(). */
#ifndef NUTSHELL_VOICE_AISPEECH_H
#define NUTSHELL_VOICE_AISPEECH_H

#include <stddef.h>
#include <stdint.h>
#include "../config/config.h"

typedef enum { VAIS_SHAPE_OPENAI = 0, VAIS_SHAPE_MISTRAL } VoiceAisShape;
typedef enum { VAIS_KEY_OWN = 0, VAIS_KEY_STT, VAIS_KEY_CHAT } VoiceAisKey;

typedef struct {
    const char *id;                  /* "mistral" "custom" */
    const char *label;               /* "Mistral" "Custom" */
    const char *base;                /* default base URL, "" for custom */
    const char *const *models;       /* NULL-terminated seeds; Mistral's [0] is its default */
    const char *const *voices;       /* NULL-terminated seeds; Mistral's [0] is its default */
    VoiceAisShape shape;
    int needs_key;                   /* 1: an empty key is a resolve error */
} VoiceAisProvider;

const VoiceAisProvider *voice_ais_providers(size_t *n);
const VoiceAisProvider *voice_ais_provider_find(const char *id);   /* NULL for any other id */

typedef struct {
    char url[512];                   /* the full endpoint */
    char model[CFG_STR_MAX];
    char voice[64];
    char style[1024];                /* the typed style, Custom only */
    char provider[16];               /* "mistral" "custom" */
    char host[256];                  /* for the privacy line: host[:port] the text goes to */
    VoiceAisShape shape;
    VoiceAisKey key_from;
    int send_instructions;           /* 1: Custom with a style typed */
} VoiceAisTarget;

/* 1 and *t filled, or 0 with a user-facing reason in err (err may be NULL). The
 * reasons: "The speech provider is not known", "Enter the speech Base URL", "The
 * speech Base URL is not valid", "The speech Base URL is too long" (the key binding "custom "
 * + the base would not fit, and the speech key is its own and one is typed), "Enter the speech model your server uses", "Enter
 * the voice your server uses" and, when the provider needs a key and
 * voice_ais_key() gives "", "Enter a speech API key". An empty model or voice takes
 * Mistral's default; Custom has none. */
/* t->host (and t->provider, url, model, voice) are filled before a later error such as a
 * missing model or key, so the Settings privacy line can name the host even then. */
int  voice_ais_resolve(const Settings *s, VoiceAisTarget *t, char *err, size_t cap);

/* The provider binding of the speech key: "mistral", "custom <normalised base>" or
 * "" (unknown provider, an unusable Custom base, or a binding that does not fit
 * cap, in which case "" and never a cut string). Bindings are compared whole. */
void voice_ais_binding(const Settings *s, char *out, size_t cap);

/* R43, the Settings page: a key typed while the page's binding was typed_for is still the
 * user's for the binding now on the page only when the two are equal; otherwise it is not
 * kept (and never rebound), and the key is asked for again. NULL is never kept. */
int  voice_ais_key_still_bound(const char *typed_for, const char *now);

/* The key the speech provider shares with the effective speech-to-text target: its
 * own key (VAIS_KEY_CHAT when it reads the AI assistant's, else VAIS_KEY_STT) when
 * the provider ids match (and for Custom the normalised bases), VAIS_KEY_OWN
 * otherwise or when that target does not resolve. Returns a VoiceAisKey. */
int  voice_ais_key_shared(const Settings *s);

/* The key to send: the shared one, or the saved tts_api_key when tts_api_key_for
 * equals the current binding exactly; "" otherwise (never NULL). */
const char *voice_ais_key(const Settings *s, const VoiceAisTarget *t);

/* The JSON body (heap; *out_len its length, out_len may be NULL), or NULL on bad
 * arguments, a CR or LF in the model or voice, or no memory. Mistral: model, input,
 * voice_id, response_format and stream:true. Custom: model, voice, input,
 * response_format "wav", and instructions when send_instructions and a style. In
 * the input, [ becomes ( and ] becomes ), byte for byte, so offsets stay valid; the
 * caller's text is not changed. */
char *voice_ais_body(const VoiceAisTarget *t, const char *text, size_t len, size_t *out_len);

/* Failures as VoiceTtsMsg.hr in an AI context; positive values are HTTP statuses. */
#define VOICE_AIS_HR_NET     (-1L)   /* transport, passing */
#define VOICE_AIS_HR_RESOLVE (-2L)   /* a setting that cannot work */
#define VOICE_AIS_HR_FORMAT  (-3L)   /* unsupported audio format: cannot work */
#define VOICE_AIS_HR_DEVICE  (-4L)   /* render lost or pre-empted while playing: passing */
#define VOICE_AIS_HR_STREAM  (-5L)   /* a stream failure of R32: passing */

/* 1 for a setting that cannot work: RESOLVE, FORMAT, 400, 401, 403, 404, 422 and 300 to 399. */
int  voice_ais_failure_fatal(long hr);
/* The hr of a decoder failure of a 200 reply: VOICE_AIS_HR_FORMAT for an unsupported format
 * (format_err) and for audio that is not WAV at its start (notwav_err with no frame decoded
 * yet), which Nutshell cannot read and a retry cannot fix; VOICE_AIS_HR_STREAM for anything
 * else, including a stream that began as valid WAV and broke later. */
long voice_ais_decoder_failure(int format_err, int notwav_err, uint64_t frames_decoded);
/* The reason for the chat note, no full stop: for RESOLVE it is resolve_err. */
void voice_ais_failure_reason(long hr, const char *resolve_err, char *out, size_t cap);
/* The same for a known provider id ("mistral" or "custom"; NULL is as the one above): a 3xx
 * for Mistral does not say "check the Base URL", which it has no row for. */
void voice_ais_failure_reason_for(const char *provider, long hr, const char *resolve_err,
                                  char *out, size_t cap);

/* The most audio one request may deliver: 5 minutes of 24 kHz frames. A server that sends
 * more (an endless body) fails that chunk as VOICE_AIS_HR_STREAM and the request is closed. */
#define VOICE_AIS_MAX_FRAMES 7200000u
/* 1 when `total` frames already taken plus `add` more pass VOICE_AIS_MAX_FRAMES (exactly the
 * cap is allowed); the addition cannot wrap. */
int  voice_ais_audio_over_cap(uint64_t total, uint64_t add);
/* The most wall-clock time one request may stay open: 6 minutes, 20% over the 5 minutes of
 * audio the frame cap allows, so a server that streams slightly slower than real time still
 * finishes, while a stream that never ends and carries no audio (Mistral keep-alive lines, a
 * server that stalls after the header) cannot hold the request, the render queue and the
 * speech-request ledger slot until the 10-minute Speaking watchdog fires. */
#define VOICE_AIS_MAX_REQUEST_MS 360000u
/* 1 when more than cap_ms milliseconds passed between two reads of one 64-bit tick source
 * (GetTickCount64): the unsigned difference, so a counter that wrapped between the reads
 * still gives the true elapsed time; exactly the cap is allowed. */
int  voice_ais_request_too_long(uint64_t start_ms, uint64_t now_ms, uint64_t cap_ms);
/* voice_ais_audio_over_cap() against any cap (the wintest seam lowers it): 1 when
 * total + add > cap. */
int  voice_ais_frames_over(uint64_t total, uint64_t add, uint64_t cap);

/* The Voice page's note for a Play sample that failed after it started:
 * "Could not play the sample: <voice_ais_failure_reason(hr)>". Cut and terminated to cap. */
void voice_ais_sample_failure_note(long hr, char *out, size_t cap);
void voice_ais_sample_failure_note_for(const char *provider, long hr, char *out, size_t cap);

/* Mistral's stream, one SSE line at a time (lines from sse_line_feed). */
typedef enum { VAIS_EV_NONE = 0, VAIS_EV_AUDIO, VAIS_EV_DONE, VAIS_EV_ERROR } VoiceAisEvKind;
typedef struct { char event[64]; int audio_seen; } VoiceAisSse;
typedef struct {
    VoiceAisEvKind kind;
    unsigned char *audio;            /* AUDIO only: decoded bytes, the caller frees */
    size_t audio_len;
    char event[64];                  /* an unknown event's name (name only, never data), for the log */
} VoiceAisEv;

void voice_ais_sse_init(VoiceAisSse *s);
/* Fills *ev (always, zeroed first). Returns 1 when the stream is over (DONE or
 * ERROR), else 0, so it can be returned from an SseLineFn to stop the feed.
 * speech.audio.delta (by its event: name or its data "type") carries audio_data,
 * base64; speech.audio.done ends it, and is an ERROR when no audio came before it;
 * an error event or member, an undecodable delta, or data that is not JSON under
 * no event name or under a known one (delta, done, error) is an ERROR; anything
 * else, including non-JSON data under an unknown name, is ignored. */
int  voice_ais_sse_line(VoiceAisSse *s, const char *line, size_t len, VoiceAisEv *ev);

/* While a reply streams: 1,600 frames of 24 kHz audio per UTF-8 byte of its chunk. */
#define VOICE_AIS_EST_FRAMES_PER_BYTE 1600

/* The byte offset into text where reading should resume: within the chunk
 * text[chunk_from, chunk_from + chunk_len), `played` frames in. With complete, total
 * is the whole chunk's frames and the offset is in proportion; otherwise it is
 * estimated at VOICE_AIS_EST_FRAMES_PER_BYTE. Snapped back to a word start and
 * never inside a UTF-8 character; chunk_from when nothing played, the chunk's end
 * when all of it has. */
size_t voice_ais_offset(const char *text, size_t chunk_from, size_t chunk_len,
                        uint64_t played, uint64_t total, int complete);

#endif /* NUTSHELL_VOICE_AISPEECH_H */
