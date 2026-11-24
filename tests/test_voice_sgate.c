#include "test_framework.h"
#include "voice_sgate.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    int16_t *s;
    size_t n, cap;
    int closed;
} Rec;

static void rec_audio(void *u, const int16_t *s, size_t n)
{
    Rec *r = (Rec *)u;
    if (r->n + n > r->cap) return;
    memcpy(r->s + r->n, s, n * sizeof *s);
    r->n += n;
}
static void rec_closed(void *u) { ((Rec *)u)->closed++; }

static int rec_init(Rec *r, size_t cap)
{
    r->s = (int16_t *)malloc(cap * sizeof *r->s);
    r->n = 0;
    r->cap = cap;
    r->closed = 0;
    return r->s != NULL;
}

/* Quiet filler with distinguishable samples, RMS about 58 (< 250). */
static void fill_quiet(int16_t *s, size_t from, size_t n)
{
    for (size_t i = 0; i < n; i++) s[i] = (int16_t)((int)((from + i) % 200) - 100);
}
/* Square wave of the given amplitude: RMS == amp. */
static void fill_speech(int16_t *s, size_t n, int amp)
{
    for (size_t i = 0; i < n; i++) s[i] = (int16_t)((i & 1) ? -amp : amp);
}

#define SINK(rec) { rec_audio, rec_closed, (rec) }

int test_voice_sgate_silence_never_opens(void)
{
    TEST_BEGIN();
    static int16_t in[32000];
    Rec r;
    ASSERT_TRUE(rec_init(&r, 40000));
    fill_speech(in, 32000, 100);
    VoiceSgate g;
    voice_sgate_init(&g, 250, 500);
    VoiceSgateSink k = SINK(&r);
    voice_sgate_feed(&g, in, 32000, &k);
    voice_sgate_flush(&g, &k);
    ASSERT_EQ(r.n, 0U);
    ASSERT_EQ(r.closed, 0);
    ASSERT_EQ(voice_sgate_open(&g), 0);
    free(r.s);
    TEST_END();
}

int test_voice_sgate_preroll(void)
{
    TEST_BEGIN();
    enum { TOTAL = 16000 + 8000 + 16000 };
    static int16_t in[TOTAL];
    Rec r;
    ASSERT_TRUE(rec_init(&r, TOTAL));
    fill_quiet(in, 0, 16000);
    fill_speech(in + 16000, 8000, 2000);
    fill_quiet(in + 24000, 24000, 16000);
    VoiceSgate g;
    voice_sgate_init(&g, 250, 500);
    VoiceSgateSink k = SINK(&r);
    voice_sgate_feed(&g, in, TOTAL, &k);
    /* 4800 pre-roll + (8000 - 640) + 8000 hangover */
    long expect = 4800 + (8000 - 640) + 8000;
    ASSERT_TRUE(labs((long)r.n - expect) <= 320);
    ASSERT_EQ(r.closed, 1);
    /* opened at the end of the second speech window (16640); the first
     * emitted sample is the one 300 ms earlier, and the stream is contiguous */
    size_t start = 16000 + 640 - 4800;
    ASSERT_EQ(r.s[0], in[start]);
    ASSERT_TRUE(r.n <= TOTAL - start && memcmp(r.s, in + start, r.n * sizeof *r.s) == 0);
    free(r.s);
    TEST_END();
}

int test_voice_sgate_preroll_short_history(void)
{
    TEST_BEGIN();
    static int16_t in[3200];
    Rec r;
    ASSERT_TRUE(rec_init(&r, 8000));
    fill_quiet(in, 0, 640);          /* only 40 ms of lead-in */
    fill_speech(in + 640, 1280, 2000);
    VoiceSgate g;
    voice_sgate_init(&g, 250, 500);
    VoiceSgateSink k = SINK(&r);
    voice_sgate_feed(&g, in, 1920, &k);
    /* ring held 2 quiet + 2 speech windows: everything seen, each once */
    ASSERT_EQ(r.n, 1920U);
    ASSERT_TRUE(memcmp(r.s, in, 1920 * sizeof *in) == 0);
    free(r.s);
    TEST_END();
}

int test_voice_sgate_click(void)
{
    TEST_BEGIN();
    static int16_t in[16000];
    Rec r;
    ASSERT_TRUE(rec_init(&r, 16000));
    fill_quiet(in, 0, 16000);
    fill_speech(in + 4800, 320, 8000);
    VoiceSgate g;
    voice_sgate_init(&g, 250, 500);
    VoiceSgateSink k = SINK(&r);
    voice_sgate_feed(&g, in, 16000, &k);
    ASSERT_EQ(voice_sgate_open(&g), 0);
    ASSERT_EQ(r.n, 0U);
    ASSERT_EQ(r.closed, 0);
    free(r.s);
    TEST_END();
}

int test_voice_sgate_cough(void)
{
    TEST_BEGIN();
    enum { TOTAL = 16000 + 1600 + 16000 };
    static int16_t in[TOTAL];
    Rec r;
    ASSERT_TRUE(rec_init(&r, TOTAL));
    fill_quiet(in, 0, 16000);
    fill_speech(in + 16000, 1600, 2000);
    fill_quiet(in + 17600, 17600, 16000);
    VoiceSgate g;
    voice_sgate_init(&g, 250, 500);
    VoiceSgateSink k = SINK(&r);
    voice_sgate_feed(&g, in, TOTAL, &k);
    ASSERT_EQ(r.closed, 1);
    ASSERT_EQ(voice_sgate_open(&g), 0);
    ASSERT_TRUE(r.n > 4800 + 8000);
    free(r.s);
    TEST_END();
}

int test_voice_sgate_flush(void)
{
    TEST_BEGIN();
    static int16_t in[2000];
    Rec r;
    ASSERT_TRUE(rec_init(&r, 4000));
    fill_speech(in, 2000, 2000);
    VoiceSgate g;
    voice_sgate_init(&g, 250, 500);
    VoiceSgateSink k = SINK(&r);
    voice_sgate_feed(&g, in, 1000, &k);   /* 3 windows + 40 samples partial */
    ASSERT_EQ(voice_sgate_open(&g), 1);
    ASSERT_EQ(r.n, 960U);                 /* partial window still held */
    voice_sgate_flush(&g, &k);
    ASSERT_EQ(r.n, 1000U);
    ASSERT_EQ(r.closed, 1);
    ASSERT_EQ(voice_sgate_open(&g), 0);
    voice_sgate_flush(&g, &k);            /* closed gate: nothing */
    ASSERT_EQ(r.n, 1000U);
    ASSERT_EQ(r.closed, 1);
    /* a closed gate with a partial window emits nothing on flush */
    Rec r2;
    ASSERT_TRUE(rec_init(&r2, 4000));
    VoiceSgateSink k2 = SINK(&r2);
    VoiceSgate g2;
    voice_sgate_init(&g2, 250, 500);
    voice_sgate_feed(&g2, in, 100, &k2);
    voice_sgate_flush(&g2, &k2);
    ASSERT_EQ(r2.n, 0U);
    ASSERT_EQ(r2.closed, 0);
    free(r.s);
    free(r2.s);
    TEST_END();
}

int test_voice_sgate_two_utterances(void)
{
    TEST_BEGIN();
    /* every length a whole number of 20 ms windows */
    enum { LEAD = 8000, BURST = 4160, GAP = 32000, TAIL = 16000,
           B2 = LEAD + BURST + GAP, TOTAL = B2 + BURST + TAIL };
    static int16_t in[TOTAL];
    Rec r;
    ASSERT_TRUE(rec_init(&r, TOTAL));
    fill_quiet(in, 0, LEAD);
    fill_speech(in + LEAD, BURST, 2000);
    fill_quiet(in + LEAD + BURST, LEAD + BURST, GAP);
    fill_speech(in + B2, BURST, 2000);
    fill_quiet(in + B2 + BURST, B2 + BURST, TAIL);
    VoiceSgate g;
    voice_sgate_init(&g, 250, 500);
    VoiceSgateSink k = SINK(&r);
    /* odd chunking must not matter */
    for (size_t pos = 0; pos < TOTAL;) {
        size_t c = 333 < TOTAL - pos ? 333 : TOTAL - pos;
        voice_sgate_feed(&g, in + pos, c, &k);
        pos += c;
    }
    ASSERT_EQ(r.closed, 2);
    /* each utterance: 4800 pre-roll + (burst - 640) + 8000 hangover */
    size_t one = 4800 + (BURST - 640) + 8000;
    ASSERT_EQ(r.n, 2 * one);
    /* first block starts 300 ms before its opening; the second block's
     * pre-roll is fresh audio from after the first, not a replay */
    ASSERT_EQ(r.s[0], in[LEAD + 640 - 4800]);
    ASSERT_EQ(r.s[one], in[B2 + 640 - 4800]);
    ASSERT_TRUE(memcmp(r.s + one, in + B2 + 640 - 4800, one * sizeof *in) == 0);
    free(r.s);
    TEST_END();
}

int test_voice_sgate_reset_wipes(void)
{
    TEST_BEGIN();
    static int16_t in[1600];
    fill_speech(in, 1600, 2000);
    VoiceSgate g;
    voice_sgate_init(&g, 250, 500);
    voice_sgate_feed(&g, in, 100, NULL);  /* partial buffered, NULL sink is safe */
    ASSERT_EQ(g.part[0], 2000);
    voice_sgate_reset(&g);
    int dirty = 0;
    for (size_t i = 0; i < VSG_WIN; i++) if (g.part[i]) dirty++;
    for (size_t w = 0; w < VSG_PREROLL_WINDOWS; w++)
        for (size_t i = 0; i < VSG_WIN; i++) if (g.ring[w][i]) dirty++;
    ASSERT_EQ(dirty, 0);
    ASSERT_EQ(g.npart, 0U);
    TEST_END();
}
