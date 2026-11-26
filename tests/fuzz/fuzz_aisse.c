/* libFuzzer target: Mistral's server-sent events. The bytes go through
 * sse_line_feed (as the stream thread feeds them, whole and split in two) into
 * voice_ais_sse_line, and every event's audio is freed. Build and run with
 * `make fuzz fuzz-run`. */
#include "sse_line.h"
#include "voice_aispeech.h"
#include "secure_zero.h"

#include <stdint.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static int on_line(const char *line, size_t len, void *ctx)
{
    VoiceAisSse *st = (VoiceAisSse *)ctx;
    VoiceAisEv ev;
    voice_ais_sse_line(st, line, len, &ev);
    if (ev.audio) { secure_zero(ev.audio, ev.audio_len); free(ev.audio); }
    return 0;
}

static void run(const uint8_t *data, size_t size, size_t cut)
{
    VoiceAisSse st;
    SseLine sl;
    voice_ais_sse_init(&st);
    sse_line_init(&sl);
    sse_line_feed(&sl, (const char *)data, cut, on_line, &st);
    sse_line_feed(&sl, (const char *)data + cut, size - cut, on_line, &st);
    sse_line_free(&sl);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    run(data, size, size);          /* in one piece */
    run(data, size, size / 2);      /* split anywhere */
    return 0;
}
