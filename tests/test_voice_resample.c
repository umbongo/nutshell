#include "test_framework.h"
#include "voice_resample.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI_D 3.14159265358979323846

static int16_t *make_tone(size_t n, double hz, double amp)
{
    int16_t *s = (int16_t *)malloc(n * sizeof *s);
    if (!s) return NULL;
    for (size_t i = 0; i < n; i++)
        s[i] = (int16_t)lround(amp * sin(2.0 * PI_D * hz * (double)i / 16000.0));
    return s;
}

/* Resample in chunks of `chunk` (0 = one call); returns count, out sized by caller. */
static size_t run_chunked(const int16_t *in, size_t n, size_t chunk, int16_t *out, size_t cap)
{
    VoiceResampler r;
    voice_rs_init(&r);
    size_t total = 0, pos = 0;
    if (chunk == 0) chunk = n;
    while (pos < n) {
        size_t c = n - pos < chunk ? n - pos : chunk;
        total += voice_rs_16_to_24(&r, in + pos, c, out + total, cap - total);
        pos += c;
    }
    return total;
}

int test_voice_rs_length(void)
{
    TEST_BEGIN();
    int16_t *in = make_tone(16000, 1000.0, 8000.0);
    size_t cap = voice_rs_out_max(16000);
    int16_t *out = (int16_t *)malloc(cap * sizeof *out);
    ASSERT_NOT_NULL(in);
    ASSERT_NOT_NULL(out);
    if (in && out) {
        size_t n = run_chunked(in, 16000, 0, out, cap);
        ASSERT_TRUE(n >= 24000 - 12 && n <= 24000 + 12);
        ASSERT_TRUE(n <= cap);
        /* odd totals and tiny calls obey the bound from any state */
        VoiceResampler r;
        voice_rs_init(&r);
        for (size_t k = 0; k < 40; k++) {
            size_t m = k % 5 + 1;
            size_t got = voice_rs_16_to_24(&r, in, m, out, cap);
            ASSERT_TRUE(got <= voice_rs_out_max(m));
        }
    }
    free(in);
    free(out);
    TEST_END();
}

int test_voice_rs_chunking_identical(void)
{
    TEST_BEGIN();
    static const size_t chunks[] = { 1, 7, 160, 333 };
    int16_t *in = make_tone(4801, 1000.0, 16000.0);
    size_t cap = voice_rs_out_max(4801);
    int16_t *ref = (int16_t *)malloc(cap * sizeof *ref);
    int16_t *alt = (int16_t *)malloc(cap * sizeof *alt);
    ASSERT_NOT_NULL(in);
    if (in && ref && alt) {
        size_t nref = run_chunked(in, 4801, 0, ref, cap);
        ASSERT_TRUE(nref > 7000);
        for (size_t c = 0; c < sizeof chunks / sizeof chunks[0]; c++) {
            memset(alt, 0x55, cap * sizeof *alt);
            size_t n = run_chunked(in, 4801, chunks[c], alt, cap);
            ASSERT_EQ(n, nref);
            ASSERT_TRUE(n == nref && memcmp(ref, alt, nref * sizeof *ref) == 0);
        }
        /* the mixed sequence 1, 7, 160, 333, then the rest */
        VoiceResampler r;
        voice_rs_init(&r);
        size_t sizes[] = { 1, 7, 160, 333, 4801 - 501 };
        size_t pos = 0, tot = 0;
        for (size_t i = 0; i < 5; i++) {
            tot += voice_rs_16_to_24(&r, in + pos, sizes[i], alt + tot, cap - tot);
            pos += sizes[i];
        }
        ASSERT_EQ(tot, nref);
        ASSERT_TRUE(tot == nref && memcmp(ref, alt, nref * sizeof *ref) == 0);
    }
    free(in);
    free(ref);
    free(alt);
    TEST_END();
}

int test_voice_rs_dc(void)
{
    TEST_BEGIN();
    int16_t in[16000];
    for (size_t i = 0; i < 16000; i++) in[i] = 1000;
    size_t cap = voice_rs_out_max(16000);
    int16_t *out = (int16_t *)malloc(cap * sizeof *out);
    ASSERT_NOT_NULL(out);
    if (out) {
        size_t n = run_chunked(in, 16000, 0, out, cap);
        int bad = 0;
        for (size_t i = 24; i < n; i++) /* 16 taps of start-up history = 24 outputs */
            if (abs(out[i] - 1000) > 1) bad++;
        ASSERT_EQ(bad, 0);
        ASSERT_TRUE(n > 23000);
    }
    free(out);
    TEST_END();
}

int test_voice_rs_tone(void)
{
    TEST_BEGIN();
    int16_t *in = make_tone(16000, 1000.0, 16000.0);
    size_t cap = voice_rs_out_max(16000);
    int16_t *out = (int16_t *)malloc(cap * sizeof *out);
    ASSERT_NOT_NULL(in);
    ASSERT_NOT_NULL(out);
    if (in && out) {
        size_t n = run_chunked(in, 16000, 0, out, cap);
        size_t from = n - 12000; /* last 0.5 s at 24 kHz */
        int crossings = 0;
        double sum_out = 0.0, sum_in = 0.0;
        for (size_t i = from + 1; i < n; i++)
            if ((out[i - 1] < 0) != (out[i] < 0)) crossings++;
        for (size_t i = from; i < n; i++) sum_out += (double)out[i] * (double)out[i];
        for (size_t i = 8000; i < 16000; i++) sum_in += (double)in[i] * (double)in[i];
        double rms_out = sqrt(sum_out / 12000.0);
        double rms_in = sqrt(sum_in / 8000.0);
        double db = 20.0 * log10(rms_out / rms_in);
        ASSERT_TRUE(crossings >= 998 && crossings <= 1002);
        ASSERT_TRUE(fabs(db) <= 0.5);
    }
    free(in);
    free(out);
    TEST_END();
}

int test_voice_rs_no_overflow(void)
{
    TEST_BEGIN();
    enum { N = 16000, HALF = 200 };
    int16_t in[N];
    for (size_t i = 0; i < N; i++) in[i] = ((i / HALF) % 2 == 0) ? 32767 : -32768;
    size_t cap = voice_rs_out_max(N);
    int16_t *out = (int16_t *)malloc(cap * sizeof *out);
    ASSERT_NOT_NULL(out);
    if (out) {
        size_t n = run_chunked(in, N, 0, out, cap);
        int wrong = 0, checked = 0, over = 0;
        for (size_t m = 100; m < n; m++) {
            int pos = (int)((2 * m) / 3) - 8; /* aligned to the input square */
            if (pos < 0) continue;
            int within = pos % HALF;
            if (within < 20 || within > HALF - 20) continue; /* away from the edges */
            int high = (pos / HALF) % 2 == 0;
            checked++;
            if (high && out[m] <= 0) wrong++;
            if (!high && out[m] >= 0) wrong++;
        }
        for (size_t m = 100; m < n; m++) {
            /* the filter overshoots at an edge; saturation must clamp, not wrap */
            if (out[m] == 32767 || out[m] == -32768) over++;
        }
        ASSERT_TRUE(checked > 10000);
        ASSERT_EQ(wrong, 0);
        ASSERT_TRUE(over > 0);
    }
    free(out);
    TEST_END();
}
