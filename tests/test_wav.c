#include "test_framework.h"
#include "wav.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static unsigned rd16(const unsigned char *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }
static unsigned long rd32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

int test_wav_header_fields(void)
{
    TEST_BEGIN();
    unsigned char h[WAV_HEADER_BYTES];
    wav_header_write(h, 16000, 32000);
    ASSERT_TRUE(memcmp(h, "RIFF", 4) == 0);
    ASSERT_EQ(rd32(h + 4), 36UL + 32000UL);
    ASSERT_TRUE(memcmp(h + 8, "WAVE", 4) == 0);
    ASSERT_TRUE(memcmp(h + 12, "fmt ", 4) == 0);
    ASSERT_EQ(rd32(h + 16), 16UL);
    ASSERT_EQ(rd16(h + 20), 1U);
    ASSERT_EQ(rd16(h + 22), 1U);
    ASSERT_EQ(rd32(h + 24), 16000UL);
    ASSERT_EQ(rd32(h + 28), 32000UL);
    ASSERT_EQ(rd16(h + 32), 2U);
    ASSERT_EQ(rd16(h + 34), 16U);
    ASSERT_TRUE(memcmp(h + 36, "data", 4) == 0);
    ASSERT_EQ(rd32(h + 40), 32000UL);
    TEST_END();
}

int test_pcm_append_caps(void)
{
    TEST_BEGIN();
    PcmBuf b;
    int16_t src[600];
    for (int i = 0; i < 600; i++) src[i] = (int16_t)i;
    pcm_init(&b, 1000);
    ASSERT_EQ(pcm_full(&b), 0);
    ASSERT_EQ(pcm_append(&b, src, 600), (size_t)600);
    ASSERT_EQ(pcm_full(&b), 0);
    ASSERT_EQ(pcm_append(&b, src, 600), (size_t)400);
    ASSERT_EQ(pcm_full(&b), 1);
    ASSERT_EQ(pcm_append(&b, src, 10), (size_t)0);
    ASSERT_EQ(b.n, (size_t)1000);
    ASSERT_EQ((int)b.s[600], 0);
    ASSERT_EQ((int)b.s[999], 399);
    pcm_free(&b);
    ASSERT_NULL(b.s);
    ASSERT_EQ(b.n, (size_t)0);
    TEST_END();
}

int test_pcm_to_wav_layout(void)
{
    TEST_BEGIN();
    PcmBuf b;
    const int16_t src[3] = { 1, -1, 32767 };
    size_t len = 0;
    pcm_init(&b, 100);
    pcm_append(&b, src, 3);
    unsigned char *w = pcm_to_wav(&b, 16000, &len);
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(len, (size_t)50);
    if (w) {
        const unsigned char want[6] = { 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0x7F };
        ASSERT_TRUE(memcmp(w + 44, want, 6) == 0);
        ASSERT_EQ(rd32(w + 40), 6UL);
        free(w);
    }
    pcm_free(&b);
    TEST_END();
}

int test_pcm_levels(void)
{
    TEST_BEGIN();
    const int16_t a[3] = { 0, 16384, -32768 };
    ASSERT_TRUE(fabs(pcm_peak(a, 3) - 1.0) < 1e-9);
    int16_t c[100];
    for (int i = 0; i < 100; i++) c[i] = 3277;
    ASSERT_TRUE(fabs(pcm_rms(c, 100) - 0.1) < 0.001);
    ASSERT_TRUE(pcm_peak(NULL, 0) == 0.0);
    ASSERT_TRUE(pcm_rms(NULL, 0) == 0.0);
    ASSERT_TRUE(pcm_rms(c, 0) == 0.0);
    TEST_END();
}

static void fill_noise(int16_t *s, size_t n, int amp)
{
    unsigned long x = 12345;
    for (size_t i = 0; i < n; i++) {
        x = x * 1103515245UL + 12345UL;
        s[i] = (int16_t)((int)((x >> 16) % (unsigned)(2 * amp + 1)) - amp);
    }
}

int test_pcm_classify(void)
{
    TEST_BEGIN();
    static int16_t buf[16000];
    memset(buf, 0, sizeof buf);
    ASSERT_EQ(pcm_classify(buf, 16000), PCM_ALL_ZERO);

    fill_noise(buf, 16000, 100);
    ASSERT_EQ(pcm_classify(buf, 16000), PCM_SILENT);

    for (int i = 0; i < PCM_WINDOW; i++)
        buf[4000 + i] = (int16_t)(8000.0 * sin(2.0 * 3.14159265358979 * 440.0 * i / 16000.0));
    ASSERT_EQ(pcm_classify(buf, 16000), PCM_SPEECH);

    memset(buf, 0, sizeof buf);
    buf[5000] = 1;
    ASSERT_EQ(pcm_classify(buf, 16000), PCM_SILENT);
    fill_noise(buf, 16000, 100);
    buf[5000] = 1;
    ASSERT_EQ(pcm_classify(buf, 16000), PCM_SILENT);

    ASSERT_EQ(pcm_classify(buf, 0), PCM_EMPTY);
    ASSERT_EQ(pcm_classify(NULL, 0), PCM_EMPTY);

    for (int i = 0; i < 100; i++) buf[i] = (i & 1) ? 8000 : -8000;
    ASSERT_EQ(pcm_classify(buf, 100), PCM_SPEECH);
    TEST_END();
}
