#include <stdlib.h>
#include <string.h>
#include "test_framework.h"
#include "../src/core/voice_wavstream.h"

/* ---- helpers: little-endian writers and a WAV builder ---- */
static size_t p16(unsigned char *b, size_t o, unsigned v)
{
    b[o] = (unsigned char)(v & 0xFF);
    b[o + 1] = (unsigned char)((v >> 8) & 0xFF);
    return o + 2;
}
static size_t p32(unsigned char *b, size_t o, uint32_t v)
{
    o = p16(b, o, v & 0xFFFF);
    return p16(b, o, v >> 16);
}
static size_t ptag(unsigned char *b, size_t o, const char *t)
{
    memcpy(b + o, t, 4);
    return o + 4;
}

/* RIFF header, optional LIST chunk (list_size bytes of 'x', padded when odd),
 * fmt, then the data chunk header. Returns the length. */
static size_t mkhdr(unsigned char *b, unsigned tag, unsigned bits, unsigned ch,
                    uint32_t rate, uint32_t data_size, uint32_t list_size)
{
    size_t o = 0;
    o = ptag(b, o, "RIFF");
    o = p32(b, o, 0xFFFFFFFFu);
    o = ptag(b, o, "WAVE");
    if (list_size) {
        uint32_t i;
        o = ptag(b, o, "LIST");
        o = p32(b, o, list_size);
        for (i = 0; i < list_size; i++) b[o++] = 'x';
        if (list_size & 1) b[o++] = 0;
    }
    o = ptag(b, o, "fmt ");
    o = p32(b, o, 16);
    o = p16(b, o, tag);
    o = p16(b, o, ch);
    o = p32(b, o, rate);
    o = p32(b, o, rate * ch * (bits / 8));
    o = p16(b, o, ch * (bits / 8));
    o = p16(b, o, bits);
    o = ptag(b, o, "data");
    o = p32(b, o, data_size);
    return o;
}

static size_t pf32(unsigned char *b, size_t o, float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return p32(b, o, u);
}

/* Feed `in` in pieces of `piece` bytes, cap = out_max(piece). Appends to out. */
static size_t feed_all(VoiceWavStream *w, const unsigned char *in, size_t n,
                       size_t piece, int16_t *out, size_t out_cap)
{
    size_t pos = 0, total = 0;
    while (pos < n) {
        size_t k = n - pos < piece ? n - pos : piece;
        int16_t tmp[256];
        size_t got;
        if (voice_wavs_out_max(k) > 256) return (size_t)-1;
        got = voice_wavs_feed(w, in + pos, k, tmp, voice_wavs_out_max(k));
        if (total + got > out_cap) return (size_t)-1;
        memcpy(out + total, tmp, got * sizeof(int16_t));
        total += got;
        pos += k;
    }
    return total;
}

int test_vwav_mono16(void)
{
    TEST_BEGIN();
    unsigned char b[600];
    int16_t out[200];
    VoiceWavStream w;
    size_t o = mkhdr(b, 1, 16, 1, 24000, 400, 0), i, got;
    for (i = 0; i < 200; i++) o = p16(b, o, (unsigned)(i * 150 - 10000) & 0xFFFF);
    voice_wavs_init(&w, VWS_RAW_NONE);
    ASSERT_EQ(voice_wavs_state(&w), VWS_HEADER);
    got = voice_wavs_feed(&w, b, o, out, voice_wavs_out_max(o));
    ASSERT_EQ(got, 200);
    for (i = 0; i < 200; i++) ASSERT_EQ(out[i], (int16_t)(i * 150 - 10000));
    ASSERT_EQ(voice_wavs_state(&w), VWS_DATA);
    ASSERT_EQ(voice_wavs_frames(&w), 200);
    ASSERT_EQ(voice_wavs_finish_ok(&w), 1);
    ASSERT_STR_EQ(voice_wavs_error(&w), "");
    voice_wavs_wipe(&w);
    TEST_END();
}

/* Build a stream, decode it whole, then at every split point and byte by byte. */
static int split_case(const unsigned char *b, size_t n, size_t expect)
{
    int16_t ref[100], got[100];
    VoiceWavStream w;
    size_t r, s, k, g;
    int bad = 0;
    voice_wavs_init(&w, VWS_RAW_NONE);
    r = feed_all(&w, b, n, n, ref, 100);
    if (r != expect) return 1;
    for (s = 1; s <= n; s++) {
        voice_wavs_init(&w, VWS_RAW_NONE);
        g = voice_wavs_feed(&w, b, s, got, voice_wavs_out_max(s));
        k = voice_wavs_feed(&w, b + s, n - s, got + g, voice_wavs_out_max(n - s));
        if (g + k != r || memcmp(got, ref, r * sizeof(int16_t)) != 0) bad = 1;
        if (voice_wavs_state(&w) != VWS_DATA || !voice_wavs_finish_ok(&w)) bad = 1;
    }
    voice_wavs_init(&w, VWS_RAW_NONE);
    g = feed_all(&w, b, n, 1, got, 100);
    if (g != r || memcmp(got, ref, r * sizeof(int16_t)) != 0) bad = 1;
    return bad;
}

int test_vwav_split_everywhere(void)
{
    TEST_BEGIN();
    unsigned char b[400];
    size_t o, i;
    o = mkhdr(b, 1, 16, 1, 24000, 0xFFFFFFFFu, 5);
    for (i = 0; i < 40; i++) o = p16(b, o, (unsigned)(i * 321) & 0xFFFF);
    ASSERT_EQ(split_case(b, o, 40), 0);
    o = mkhdr(b, 3, 32, 2, 24000, 0, 0);
    for (i = 0; i < 30; i++) {
        o = pf32(b, o, (float)i / 40.0f - 0.3f);
        o = pf32(b, o, 0.9f - (float)i / 50.0f);
    }
    ASSERT_EQ(split_case(b, o, 30), 0);
    TEST_END();
}

int test_vwav_placeholder_sizes(void)
{
    TEST_BEGIN();
    unsigned char b[300];
    int16_t out[50];
    VoiceWavStream w;
    uint32_t sizes[2] = { 0xFFFFFFFFu, 0 };
    int s;
    for (s = 0; s < 2; s++) {
        size_t o = mkhdr(b, 1, 16, 1, 24000, sizes[s], 0), i;
        for (i = 0; i < 50; i++) o = p16(b, o, (unsigned)(i + 1));
        voice_wavs_init(&w, VWS_RAW_NONE);
        ASSERT_EQ(voice_wavs_feed(&w, b, o, out, voice_wavs_out_max(o)), 50);
        ASSERT_EQ(out[49], 50);
        ASSERT_EQ(voice_wavs_state(&w), VWS_DATA);
    }
    /* LIST (odd size, padded) before fmt and data is skipped */
    {
        size_t o = mkhdr(b, 1, 16, 1, 24000, 100, 7), i;
        for (i = 0; i < 50; i++) o = p16(b, o, (unsigned)(i + 1));
        voice_wavs_init(&w, VWS_RAW_NONE);
        ASSERT_EQ(voice_wavs_feed(&w, b, o, out, voice_wavs_out_max(o)), 50);
        ASSERT_EQ(out[0], 1);
    }
    TEST_END();
}

int test_vwav_float32(void)
{
    TEST_BEGIN();
    unsigned char b[200];
    int16_t out[16];
    VoiceWavStream w;
    float v[] = { -1.5f, -1.0f, 0.0f, 0.5f, 1.0f, 99.0f, -0.5f };
    int16_t want[] = { -32767, -32767, 0, 16384, 32767, 32767, -16384 };
    size_t o = mkhdr(b, 3, 32, 1, 24000, 28, 0), i;
    for (i = 0; i < 7; i++) o = pf32(b, o, v[i]);
    voice_wavs_init(&w, VWS_RAW_NONE);
    ASSERT_EQ(voice_wavs_feed(&w, b, o, out, voice_wavs_out_max(o)), 7);
    for (i = 0; i < 7; i++) ASSERT_EQ(out[i], want[i]);
    /* NaN, +-Inf: NaN is silence, infinities clamp */
    o = mkhdr(b, 3, 32, 1, 24000, 12, 0);
    p32(b, o, 0x7FC00000u); o += 4;
    p32(b, o, 0x7F800000u); o += 4;
    p32(b, o, 0xFF800000u); o += 4;
    voice_wavs_init(&w, VWS_RAW_NONE);
    ASSERT_EQ(voice_wavs_feed(&w, b, o, out, voice_wavs_out_max(o)), 3);
    ASSERT_EQ(out[0], 0);
    ASSERT_EQ(out[1], 32767);
    ASSERT_EQ(out[2], -32767);
    TEST_END();
}

int test_vwav_stereo_average(void)
{
    TEST_BEGIN();
    unsigned char b[200];
    int16_t out[8];
    VoiceWavStream w;
    /* average = (L + R) / 2 with a half rounded away from zero:
     * (1,2) -> 2, (-1,-2) -> -2, (-5,2) sums -3 -> -2, (3,-2) sums 1 -> 1 */
    int16_t l[] = { 1000, 1, -1, -5, 3, 32767, -32768 };
    int16_t r[] = { 3000, 2, -2, 2, -2, 32767, -32768 };
    int16_t want[] = { 2000, 2, -2, -2, 1, 32767, -32768 };
    size_t o = mkhdr(b, 1, 16, 2, 24000, 28, 0), i;
    for (i = 0; i < 7; i++) {
        o = p16(b, o, (unsigned)l[i] & 0xFFFF);
        o = p16(b, o, (unsigned)r[i] & 0xFFFF);
    }
    voice_wavs_init(&w, VWS_RAW_NONE);
    ASSERT_EQ(voice_wavs_feed(&w, b, o, out, voice_wavs_out_max(o)), 7);
    for (i = 0; i < 7; i++) ASSERT_EQ(out[i], want[i]);
    TEST_END();
}

int test_vwav_raw_f32(void)
{
    TEST_BEGIN();
    unsigned char b[40];
    int16_t out[8], all[8];
    VoiceWavStream w;
    float v[] = { -1.5f, -1.0f, 0.0f, 0.5f, 1.0f };
    int16_t want[] = { -32767, -32767, 0, 16384, 32767 };
    size_t o = 0, i;
    for (i = 0; i < 5; i++) o = pf32(b, o, v[i]);
    voice_wavs_init(&w, VWS_RAW_F32_MONO);
    ASSERT_EQ(voice_wavs_state(&w), VWS_DATA);
    ASSERT_EQ(voice_wavs_feed(&w, b, o, out, voice_wavs_out_max(o)), 5);
    for (i = 0; i < 5; i++) ASSERT_EQ(out[i], want[i]);
    /* one byte at a time, frames split across feeds */
    voice_wavs_init(&w, VWS_RAW_F32_MONO);
    ASSERT_EQ(feed_all(&w, b, o, 1, all, 8), 5);
    for (i = 0; i < 5; i++) ASSERT_EQ(all[i], want[i]);
    ASSERT_EQ(voice_wavs_finish_ok(&w), 1);
    /* a partial frame is not a clean end */
    voice_wavs_init(&w, VWS_RAW_F32_MONO);
    voice_wavs_feed(&w, b, 6, out, voice_wavs_out_max(6));
    ASSERT_EQ(voice_wavs_finish_ok(&w), 0);
    TEST_END();
}

int test_vwav_rejects_unsupported(void)
{
    TEST_BEGIN();
    struct { unsigned tag, bits, ch; uint32_t rate; } c[] = {
        { 1, 16, 1, 22050 }, { 1, 16, 1, 48000 }, { 1, 8, 1, 24000 },
        { 1, 24, 1, 24000 }, { 1, 16, 3, 24000 }, { 0xFFFE, 16, 1, 24000 },
        { 3, 16, 1, 24000 }, { 1, 32, 1, 24000 }, { 1, 16, 0, 24000 }
    };
    size_t k;
    for (k = 0; k < sizeof c / sizeof c[0]; k++) {
        unsigned char b[200];
        int16_t out[64];
        VoiceWavStream w;
        size_t o = mkhdr(b, c[k].tag, c[k].bits, c[k].ch, c[k].rate, 0, 0), i;
        for (i = 0; i < 20; i++) b[o++] = 0x11;
        voice_wavs_init(&w, VWS_RAW_NONE);
        ASSERT_EQ(voice_wavs_feed(&w, b, o, out, voice_wavs_out_max(o)), 0);
        ASSERT_EQ(voice_wavs_state(&w), VWS_ERROR);
        ASSERT_EQ(voice_wavs_error_is_format(&w), 1);
        ASSERT_EQ(voice_wavs_error_is_notwav(&w), 0);
        ASSERT_STR_EQ(voice_wavs_error(&w), "unsupported audio format");
        ASSERT_EQ(voice_wavs_finish_ok(&w), 0);
        /* an error is final: more bytes give nothing */
        ASSERT_EQ(voice_wavs_feed(&w, b, 8, out, voice_wavs_out_max(8)), 0);
    }
    TEST_END();
}

int test_vwav_not_wav(void)
{
    TEST_BEGIN();
    const char *s[] = { "OggS\0\2\0\0\0\0\0\0", "ID3\4\0\0\0\0\0\0\0\0", "garbage garbage",
                        "RIFF\0\0\0\0WAVX", "RIFX" };
    size_t k;
    for (k = 0; k < 5; k++) {
        VoiceWavStream w;
        int16_t out[16];
        size_t n = k == 4 ? 4 : 12;
        voice_wavs_init(&w, VWS_RAW_NONE);
        ASSERT_EQ(voice_wavs_feed(&w, (const unsigned char *)s[k], n, out, voice_wavs_out_max(n)), 0);
        ASSERT_EQ(voice_wavs_state(&w), VWS_ERROR);
        ASSERT_STR_EQ(voice_wavs_error(&w), "not a WAV stream");
        ASSERT_EQ(voice_wavs_error_is_format(&w), 0);
        ASSERT_EQ(voice_wavs_error_is_notwav(&w), 1);
        ASSERT_EQ((int)voice_wavs_frames(&w), 0);
    }
    /* a data chunk with no fmt before it */
    {
        unsigned char b[40];
        VoiceWavStream w;
        int16_t out[16];
        size_t o = ptag(b, 0, "RIFF");
        o = p32(b, o, 0); o = ptag(b, o, "WAVE"); o = ptag(b, o, "data");
        o = p32(b, o, 8);
        voice_wavs_init(&w, VWS_RAW_NONE);
        voice_wavs_feed(&w, b, o + 8, out, voice_wavs_out_max(o + 8));
        ASSERT_STR_EQ(voice_wavs_error(&w), "not a WAV stream");
    }
    /* a mismatch shows as soon as it arrives: one byte */
    {
        VoiceWavStream w;
        int16_t out[4];
        voice_wavs_init(&w, VWS_RAW_NONE);
        voice_wavs_feed(&w, (const unsigned char *)"O", 1, out, voice_wavs_out_max(1));
        ASSERT_EQ(voice_wavs_state(&w), VWS_ERROR);
    }
    TEST_END();
}

int test_vwav_header_too_long(void)
{
    TEST_BEGIN();
    unsigned char b[5000];
    int16_t out[64];
    VoiceWavStream w;
    size_t o = ptag(b, 0, "RIFF");
    o = p32(b, o, 0); o = ptag(b, o, "WAVE");
    /* a LIST chunk of 4 GiB - 1 (64-bit walk, no wrap) */
    o = ptag(b, o, "LIST"); o = p32(b, o, 0xFFFFFFFFu);
    voice_wavs_init(&w, VWS_RAW_NONE);
    voice_wavs_feed(&w, b, o, out, voice_wavs_out_max(o));
    ASSERT_STR_EQ(voice_wavs_error(&w), "WAV header too long");
    ASSERT_EQ(voice_wavs_error_is_format(&w), 0);
    /* a fmt chunk bigger than the buffer */
    o = ptag(b, 0, "RIFF");
    o = p32(b, o, 0); o = ptag(b, o, "WAVE");
    o = ptag(b, o, "fmt "); o = p32(b, o, 0xFFFFFFF0u);
    voice_wavs_init(&w, VWS_RAW_NONE);
    voice_wavs_feed(&w, b, o, out, voice_wavs_out_max(o));
    ASSERT_STR_EQ(voice_wavs_error(&w), "WAV header too long");
    /* many small chunks that never reach data: the buffer fills */
    o = ptag(b, 0, "RIFF");
    o = p32(b, o, 0); o = ptag(b, o, "WAVE");
    while (o + 8 <= 4096 + 600 && o + 8 <= sizeof b) {
        o = ptag(b, o, "junk"); o = p32(b, o, 0);
    }
    voice_wavs_init(&w, VWS_RAW_NONE);
    {
        size_t pos = 0;
        while (pos < o) {
            size_t k = o - pos < 100 ? o - pos : 100;
            voice_wavs_feed(&w, b + pos, k, out, voice_wavs_out_max(k));
            pos += k;
        }
    }
    ASSERT_STR_EQ(voice_wavs_error(&w), "WAV header too long");
    TEST_END();
}

int test_vwav_second_header_fails(void)
{
    TEST_BEGIN();
    unsigned char b[1200];
    int16_t out[600];
    VoiceWavStream w;
    size_t o = mkhdr(b, 1, 16, 1, 24000, 0, 0), i, h2, s;
    for (i = 0; i < 100; i++) o = p16(b, o, (unsigned)(i + 1));
    h2 = o;
    o += mkhdr(b + o, 1, 16, 1, 24000, 0, 0);
    for (i = 0; i < 100; i++) o = p16(b, o, (unsigned)(i + 1));
    voice_wavs_init(&w, VWS_RAW_NONE);
    voice_wavs_feed(&w, b, o, out, voice_wavs_out_max(o));
    ASSERT_EQ(voice_wavs_state(&w), VWS_ERROR);
    ASSERT_STR_EQ(voice_wavs_error(&w), "second WAV header");
    ASSERT_EQ(voice_wavs_error_is_format(&w), 0);
    ASSERT_EQ(voice_wavs_error_is_notwav(&w), 0);   /* it broke after frames: a stream failure */
    ASSERT_EQ(voice_wavs_finish_ok(&w), 0);
    /* the 12-byte window split at every point across two feeds, and bytewise */
    for (s = h2 + 1; s < h2 + 12; s++) {
        voice_wavs_init(&w, VWS_RAW_NONE);
        voice_wavs_feed(&w, b, s, out, voice_wavs_out_max(s));
        voice_wavs_feed(&w, b + s, o - s, out, voice_wavs_out_max(o - s));
        ASSERT_STR_EQ(voice_wavs_error(&w), "second WAV header");
    }
    voice_wavs_init(&w, VWS_RAW_NONE);
    feed_all(&w, b, o, 1, out, 600);
    ASSERT_STR_EQ(voice_wavs_error(&w), "second WAV header");
    /* the same in raw mode */
    voice_wavs_init(&w, VWS_RAW_F32_MONO);
    voice_wavs_feed(&w, b + h2, 12, out, voice_wavs_out_max(12));
    ASSERT_STR_EQ(voice_wavs_error(&w), "second WAV header");
    /* RIFF....WAVE with one wrong byte is audio, not a header */
    b[h2 + 9] = 'X';
    voice_wavs_init(&w, VWS_RAW_NONE);
    voice_wavs_feed(&w, b, o, out, voice_wavs_out_max(o));
    ASSERT_EQ(voice_wavs_state(&w), VWS_DATA);
    /* the first header never trips the check */
    o = mkhdr(b, 1, 16, 1, 24000, 0, 0);
    voice_wavs_init(&w, VWS_RAW_NONE);
    feed_all(&w, b, o, 1, out, 600);
    ASSERT_EQ(voice_wavs_state(&w), VWS_DATA);
    TEST_END();
}

int test_vwav_finish_states(void)
{
    TEST_BEGIN();
    unsigned char b[200];
    int16_t out[64];
    VoiceWavStream w;
    size_t o = mkhdr(b, 1, 16, 1, 24000, 0, 0);
    /* cut inside its header */
    voice_wavs_init(&w, VWS_RAW_NONE);
    voice_wavs_feed(&w, b, 20, out, voice_wavs_out_max(20));
    ASSERT_EQ(voice_wavs_state(&w), VWS_HEADER);
    ASSERT_EQ(voice_wavs_finish_ok(&w), 0);
    /* a complete header and no frames */
    voice_wavs_init(&w, VWS_RAW_NONE);
    voice_wavs_feed(&w, b, o, out, voice_wavs_out_max(o));
    ASSERT_EQ(voice_wavs_state(&w), VWS_DATA);
    ASSERT_EQ(voice_wavs_frames(&w), 0);
    ASSERT_EQ(voice_wavs_finish_ok(&w), 0);
    /* a frame and one byte over */
    p16(b, o, 5); o += 2; b[o++] = 7;
    voice_wavs_init(&w, VWS_RAW_NONE);
    voice_wavs_feed(&w, b, o, out, voice_wavs_out_max(o));
    ASSERT_EQ(voice_wavs_frames(&w), 1);
    ASSERT_EQ(voice_wavs_finish_ok(&w), 0);
    /* the missing byte arrives */
    b[0] = 0;
    voice_wavs_feed(&w, b, 1, out, voice_wavs_out_max(1));
    ASSERT_EQ(voice_wavs_frames(&w), 2);
    ASSERT_EQ(voice_wavs_finish_ok(&w), 1);
    TEST_END();
}

int test_vwav_small_cap_is_an_error(void)
{
    TEST_BEGIN();
    unsigned char b[100];
    int16_t out[64];
    VoiceWavStream w;
    size_t o = mkhdr(b, 1, 16, 1, 24000, 0, 0), i;
    for (i = 0; i < 20; i++) o = p16(b, o, 9);
    ASSERT_EQ(voice_wavs_out_max(0), 1);
    ASSERT_TRUE(voice_wavs_out_max(1000) >= 500);
    ASSERT_TRUE(voice_wavs_out_max(1000) <= 504);
    voice_wavs_init(&w, VWS_RAW_NONE);
    memset(out, 0x55, sizeof out);
    ASSERT_EQ(voice_wavs_feed(&w, b, o, out, voice_wavs_out_max(o) - 1), 0);
    ASSERT_EQ(voice_wavs_state(&w), VWS_ERROR);
    ASSERT_STR_EQ(voice_wavs_error(&w), "output buffer too small");
    ASSERT_EQ(voice_wavs_error_is_format(&w), 0);
    ASSERT_EQ(out[0], 0x5555);
    /* NULL out with cap 0 and n 0 is fine; n 0 never fails */
    voice_wavs_init(&w, VWS_RAW_NONE);
    ASSERT_EQ(voice_wavs_feed(&w, b, 0, NULL, 0), 0);
    ASSERT_EQ(voice_wavs_state(&w), VWS_HEADER);
    /* the worst case with a carry: 1-byte feeds sized by out_max never overflow */
    voice_wavs_init(&w, VWS_RAW_NONE);
    ASSERT_EQ(feed_all(&w, b, o, 1, out, 64), 20);
    TEST_END();
}

int test_vwav_wipe(void)
{
    TEST_BEGIN();
    VoiceWavStream w;
    unsigned char b[64];
    unsigned char zero[sizeof w];
    size_t o = mkhdr(b, 1, 16, 1, 24000, 0, 0);
    int16_t out[16];
    voice_wavs_init(&w, VWS_RAW_NONE);
    voice_wavs_feed(&w, b, o - 3, out, voice_wavs_out_max(o));
    voice_wavs_wipe(&w);
    memset(zero, 0, sizeof zero);
    ASSERT_EQ(memcmp(&w, zero, sizeof w), 0);
    TEST_END();
}

/* A finite data chunk size is honoured (spec 8.4: only 0 and 0xFFFFFFFF mean
 * "to the end"); bytes after it are a trailer and are never played. */
static size_t feed_whole(unsigned char *b, size_t n, int16_t *out, VoiceWavStream *w)
{
    voice_wavs_init(w, VWS_RAW_NONE);
    return voice_wavs_feed(w, b, n, out, voice_wavs_out_max(n));
}

int test_vwav_data_size_honoured(void)
{
    TEST_BEGIN();
    unsigned char b[400];
    int16_t out[100];
    VoiceWavStream w;
    size_t o, i, h;
    /* smaller than what arrives: 20 samples declared, 50 sent; the rest is a trailer */
    o = mkhdr(b, 1, 16, 1, 24000, 40, 0);
    h = o;
    for (i = 0; i < 50; i++) o = p16(b, o, (unsigned)(i + 1));
    ASSERT_EQ(feed_whole(b, o, out, &w), 20);
    ASSERT_EQ(out[19], 20);
    ASSERT_EQ(voice_wavs_state(&w), VWS_DATA);
    ASSERT_EQ(voice_wavs_finish_ok(&w), 1);
    ASSERT_EQ(voice_wavs_frames(&w), 20);
    /* an id3-style trailer of odd length after the chunk is not an error */
    o = h + 40;
    memcpy(b + o, "ID3", 3);
    o += 3;
    ASSERT_EQ(feed_whole(b, o, out, &w), 20);
    ASSERT_EQ(voice_wavs_finish_ok(&w), 1);
    /* a trailer holding a RIFF....WAVE window is a trailer, not a second header */
    o = h + 40;
    memcpy(b + o, "RIFF\1\2\3\4WAVE", 12);
    o += 12;
    ASSERT_EQ(feed_whole(b, o, out, &w), 20);
    ASSERT_EQ(voice_wavs_state(&w), VWS_DATA);
    /* larger than what arrives: all of it plays, the stream is not an error */
    o = mkhdr(b, 1, 16, 1, 24000, 1000, 0);
    for (i = 0; i < 30; i++) o = p16(b, o, (unsigned)(i + 1));
    ASSERT_EQ(feed_whole(b, o, out, &w), 30);
    ASSERT_EQ(voice_wavs_state(&w), VWS_DATA);
    /* a huge even size and a huge odd size: bytes that arrive, whole frames only */
    o = mkhdr(b, 1, 16, 1, 24000, 0xFFFFFFFEu, 0);
    for (i = 0; i < 30; i++) o = p16(b, o, (unsigned)(i + 1));
    ASSERT_EQ(feed_whole(b, o, out, &w), 30);
    o = mkhdr(b, 1, 16, 1, 24000, 0xFFFFFFFDu, 0);
    for (i = 0; i < 30; i++) o = p16(b, o, (unsigned)(i + 1));
    ASSERT_EQ(feed_whole(b, o, out, &w), 30);
    ASSERT_EQ(voice_wavs_state(&w), VWS_DATA);
    /* an odd size for 16-bit samples: the half sample is dropped */
    o = mkhdr(b, 1, 16, 1, 24000, 41, 0);
    for (i = 0; i < 50; i++) o = p16(b, o, (unsigned)(i + 1));
    ASSERT_EQ(feed_whole(b, o, out, &w), 20);
    ASSERT_EQ(voice_wavs_finish_ok(&w), 1);
    /* a size smaller than one frame: no samples, not finishable, no error */
    o = mkhdr(b, 1, 16, 2, 24000, 3, 0);
    for (i = 0; i < 10; i++) o = p16(b, o, 7);
    ASSERT_EQ(feed_whole(b, o, out, &w), 0);
    ASSERT_EQ(voice_wavs_state(&w), VWS_DATA);
    ASSERT_EQ(voice_wavs_finish_ok(&w), 0);
    /* stereo float: 5 frames of 8 bytes declared, 10 sent */
    o = mkhdr(b, 3, 32, 2, 24000, 40, 0);
    for (i = 0; i < 20; i++) o = pf32(b, o, 0.5f);
    ASSERT_EQ(feed_whole(b, o, out, &w), 5);
    TEST_END();
}

int test_vwav_data_size_split_everywhere(void)
{
    TEST_BEGIN();
    unsigned char b[400];
    size_t o, i;
    /* 40 samples declared, then 10 more samples and an odd trailer: every
     * split of the header and of the data/trailer border gives the same 40 */
    o = mkhdr(b, 1, 16, 1, 24000, 80, 5);
    for (i = 0; i < 50; i++) o = p16(b, o, (unsigned)(i * 321) & 0xFFFF);
    b[o++] = 'x';
    ASSERT_EQ(split_case(b, o, 40), 0);
    /* odd size, stereo */
    o = mkhdr(b, 1, 16, 2, 24000, 83, 0);
    for (i = 0; i < 60; i++) o = p16(b, o, (unsigned)(i * 77) & 0xFFFF);
    ASSERT_EQ(split_case(b, o, 20), 0);
    TEST_END();
}

/* The decision after the declared end: keep reading, end cleanly, or fail. */
static void feed_zeros(VoiceWavStream *w, size_t n)
{
    unsigned char z[1024] = {0};
    int16_t out[600];
    while (n > 0) {
        size_t k = n < sizeof z ? n : sizeof z;
        voice_wavs_feed(w, z, k, out, voice_wavs_out_max(k));
        n -= k;
    }
}

int test_vwav_after_end_decision(void)
{
    TEST_BEGIN();
    unsigned char b[400];
    int16_t out[100];
    VoiceWavStream w;
    size_t o, i;
    o = mkhdr(b, 1, 16, 1, 24000, 40, 0);
    for (i = 0; i < 20; i++) o = p16(b, o, (unsigned)(i + 1));
    /* exactly the declared size: done, nothing skipped; ends cleanly when the stream closes */
    ASSERT_EQ(feed_whole(b, o, out, &w), 20);
    ASSERT_EQ(voice_wavs_done(&w), 1);
    ASSERT_EQ(voice_wavs_trailer(&w), 0);
    ASSERT_EQ(voice_wavs_after_end(&w, 0), VWS_AFTER_CONTINUE);
    ASSERT_EQ(voice_wavs_after_end(&w, 1), VWS_AFTER_END_OK);
    /* a 200-byte trailer */
    feed_zeros(&w, 200);
    ASSERT_EQ(voice_wavs_trailer(&w), 200);
    ASSERT_EQ(voice_wavs_after_end(&w, 0), VWS_AFTER_CONTINUE);
    ASSERT_EQ(voice_wavs_after_end(&w, 1), VWS_AFTER_END_OK);
    /* 64 KB in all is still a trailer; one byte more is audio the header did not declare */
    feed_zeros(&w, VOICE_WAVS_TRAILER_MAX - 200);
    ASSERT_EQ(voice_wavs_trailer(&w), VOICE_WAVS_TRAILER_MAX);
    ASSERT_EQ(voice_wavs_after_end(&w, 1), VWS_AFTER_END_OK);
    feed_zeros(&w, 1);
    ASSERT_EQ(voice_wavs_after_end(&w, 0), VWS_AFTER_FAIL);
    ASSERT_EQ(voice_wavs_after_end(&w, 1), VWS_AFTER_FAIL);
    /* half the declared size has arrived: not done */
    voice_wavs_init(&w, VWS_RAW_NONE);
    voice_wavs_feed(&w, b, 44 + 20, out, voice_wavs_out_max(64));
    ASSERT_EQ(voice_wavs_done(&w), 0);
    ASSERT_EQ(voice_wavs_after_end(&w, 1), VWS_AFTER_CONTINUE);
    /* "to the end" sizes are never done, however much arrives */
    {
        uint32_t sizes[2] = { 0, 0xFFFFFFFFu };
        for (i = 0; i < 2; i++) {
            o = mkhdr(b, 1, 16, 1, 24000, sizes[i], 0);
            o = p16(b, o, 1);
            ASSERT_EQ(feed_whole(b, o, out, &w), 1);
            feed_zeros(&w, VOICE_WAVS_TRAILER_MAX + 100);
            ASSERT_EQ(voice_wavs_done(&w), 0);
            ASSERT_EQ(voice_wavs_after_end(&w, 0), VWS_AFTER_CONTINUE);
            ASSERT_EQ(voice_wavs_after_end(&w, 1), VWS_AFTER_CONTINUE);
        }
    }
    TEST_END();
}
