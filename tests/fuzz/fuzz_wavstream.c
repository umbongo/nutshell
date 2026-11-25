/* libFuzzer target: the AI speech WAV/PCM decoder. The input is fed in pieces
 * whose size comes from its first byte (1 to 256), to a WAV-header decoder and
 * to a raw float32 decoder, with cap = voice_wavs_out_max(piece) every time.
 * Build and run with `make fuzz fuzz-run`. */
#include "voice_wavstream.h"

#include <stdint.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void run(VoiceWavRaw raw, const uint8_t *data, size_t size, size_t piece)
{
    VoiceWavStream w;
    int16_t out[256 / 2 + 2];
    size_t pos = 0;
    voice_wavs_init(&w, raw);
    while (pos < size) {
        size_t k = size - pos < piece ? size - pos : piece;
        size_t got = voice_wavs_feed(&w, data + pos, k, out, voice_wavs_out_max(k));
        if (got > voice_wavs_out_max(k)) abort();
        pos += k;
    }
    (void)voice_wavs_finish_ok(&w);
    (void)voice_wavs_error(&w);
    voice_wavs_wipe(&w);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    size_t piece;
    if (size == 0) return 0;
    piece = (size_t)data[0] + 1;
    run(VWS_RAW_NONE, data + 1, size - 1, piece);
    run(VWS_RAW_F32_MONO, data + 1, size - 1, piece);
    return 0;
}
