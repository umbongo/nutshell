#ifndef NUTSHELL_CORE_VOICE_WAVSTREAM_H
#define NUTSHELL_CORE_VOICE_WAVSTREAM_H

/* Streaming decoder for AI speech: a WAV stream (or Mistral's raw float32 PCM)
 * in, 24 kHz mono PCM16 out. Pure: no Win32, no I/O, no logging, no heap.
 *
 * The bytes are untrusted. Every length and offset in the header is checked,
 * chunk sizes before `data` are walked in 64-bit arithmetic (odd sizes with
 * the RIFF pad byte), and the header never takes more than 4096 bytes.
 *
 * Accepted: format tag 1 with 16 bits, or tag 3 with 32 bits; 1 or 2 channels;
 * exactly 24,000 Hz. Stereo is averaged ((L + R) / 2, a half rounds away from
 * zero). Float is clamped to [-1, 1] and scaled by 32,767, a half rounding
 * away from zero (0.5 gives 16384); NaN is silence. The `data` chunk size is
 * honoured (spec 8.4): only 0 and 0xFFFFFFFF mean "to the end of the stream".
 * Any other size is rounded down to whole frames and the bytes after it (a
 * LIST or id3 trailer) are skipped, never played and never an error; a size
 * larger than the bytes that arrive just plays what arrives.
 *
 * Output buffer: voice_wavs_feed() needs cap >= voice_wavs_out_max(n). A
 * smaller cap is a caller error: nothing is written, nothing is consumed and
 * the stream goes to VWS_ERROR ("output buffer too small"). out_max is
 * n / 2 + 1, not n / 2: a frame split across feeds is completed by the next
 * feed's first byte, so 1-byte feeds of 16-bit mono still give a sample per
 * second byte, and the carry can add one sample to n / 2.
 *
 * The 12-byte window RIFF????WAVE inside the data (in either mode, split
 * across feeds or not) is the error "second WAV header". A feed that finds
 * it returns 0 samples. */

#include <stddef.h>
#include <stdint.h>

#define VOICE_WAVS_RATE 24000
#define VOICE_WAVS_HDR_MAX 4096
/* The most that may follow a finite data chunk (a LIST or id3 trailer is far smaller); more is
 * audio the header did not declare. */
#define VOICE_WAVS_TRAILER_MAX 65536u

typedef enum { VWS_HEADER = 0, VWS_DATA, VWS_ERROR } VoiceWavState;
typedef enum { VWS_RAW_NONE = 0, VWS_RAW_F32_MONO } VoiceWavRaw;

typedef struct {
    VoiceWavState state;
    int err;                           /* a VWS_E_* code, private to the .c */
    unsigned fmt_tag, bits, channels;  /* 1/16 or 3/32; 1 or 2 */
    size_t hdr_len;
    unsigned char hdr[VOICE_WAVS_HDR_MAX];
    unsigned char carry[8];            /* a partial frame */
    size_t carry_n;
    unsigned char win[12];             /* ring of the last 12 data bytes */
    size_t win_head;                   /* next slot to write */
    size_t win_n;                      /* bytes held, up to 12 */
    uint64_t frames;                   /* output samples produced */
    int bounded;                       /* 1: a finite data size is being counted down */
    uint64_t data_left;                /* bytes of the data chunk still to take */
    uint64_t trailer;                  /* bytes skipped after the declared end of the data */
} VoiceWavStream;

void   voice_wavs_init(VoiceWavStream *w, VoiceWavRaw raw);
/* Returns the 24 kHz mono PCM16 samples written to out (never more than cap). */
size_t voice_wavs_feed(VoiceWavStream *w, const unsigned char *in, size_t n,
                       int16_t *out, size_t cap);
size_t voice_wavs_out_max(size_t n_in);
VoiceWavState voice_wavs_state(const VoiceWavStream *w);
/* 1: in VWS_DATA, no partial frame, frames > 0. */
int    voice_wavs_finish_ok(const VoiceWavStream *w);
uint64_t voice_wavs_frames(const VoiceWavStream *w);
/* 1: the declared data size was finite and all of it has been delivered. "To the end" sizes
 * (0 and 0xFFFFFFFF) are never done. */
int    voice_wavs_done(const VoiceWavStream *w);
/* Bytes skipped after the declared end (a count, never content). */
uint64_t voice_wavs_trailer(const VoiceWavStream *w);
/* What to do once the data is done: keep reading, end as a success, or fail because more than
 * VOICE_WAVS_TRAILER_MAX bytes followed. Not done: always CONTINUE (the caller's own end-of-
 * stream check applies). */
typedef enum { VWS_AFTER_CONTINUE = 0, VWS_AFTER_END_OK, VWS_AFTER_FAIL } VoiceWavAfter;
VoiceWavAfter voice_wavs_after_end(const VoiceWavStream *w, int stream_ended);
/* "" or a static reason; never any of the stream's content. */
const char *voice_wavs_error(const VoiceWavStream *w);
/* 1 for "unsupported audio format". */
int    voice_wavs_error_is_format(const VoiceWavStream *w);
/* 1 for "not a WAV stream" (the header is not RIFF/WAVE, or lacks a usable fmt before data).
 * Only the header can fail this way, so it is always before the first frame. */
int    voice_wavs_error_is_notwav(const VoiceWavStream *w);
void   voice_wavs_wipe(VoiceWavStream *w);

#endif
