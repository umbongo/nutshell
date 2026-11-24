#include "test_framework.h"
#include "voice_vad.h"
#include <math.h>
#include <stdint.h>

#define PI_ 3.14159265358979323846

/* Deterministic window synthesiser: white noise from a fixed LCG, plus an
 * optional 200 Hz tone amplitude-modulated at 4 Hz ("speech"). */
typedef struct {
    VoiceVad v;
    VoiceEndpoint e;
    uint32_t lcg;
    long t;                 /* sample clock */
    int win;                /* window index */
    int starts, ends, caps, abandons;
    int first_start, last_end, first_abandon, last_abandon, first_cap;
} Sim;

static void sim_init(Sim *s, int pause_ms)
{
    memset(s, 0, sizeof *s);
    voice_vad_init(&s->v);
    voice_ep_init(&s->e, pause_ms);
    s->lcg = 12345u;
    s->first_start = s->last_end = s->first_abandon = s->last_abandon = s->first_cap = -1;
}

static double lcg_unit(Sim *s)   /* uniform in [-1, 1) */
{
    s->lcg = s->lcg * 1664525u + 1013904223u;
    return (double)(s->lcg >> 8) / 8388608.0 - 1.0;
}

/* One window. tone_db < -200: no tone. noise_db < -200: no noise.
 * depth: 0 flat tone, 0.4 speech-like modulation 0.6 + 0.4 sin. */
static VoiceEpOut sim_win(Sim *s, double tone_db, double depth, double noise_db, double decay_db)
{
    int16_t w[VVAD_WIN];
    double nscale = pow(10.0, noise_db / 20.0) * 32768.0 * 1.7320508;
    double tamp = pow(10.0, (tone_db - decay_db) / 20.0) * 32768.0 * 1.4142136;
    for (int i = 0; i < VVAD_WIN; i++, s->t++) {
        double sec = (double)s->t / 16000.0;
        double x = 0.0;
        if (noise_db > -200.0) x += lcg_unit(s) * nscale;
        else (void)lcg_unit(s);
        if (tone_db > -200.0) {
            double env = depth > 0.0 ? 0.6 + depth * sin(2.0 * PI_ * 4.0 * sec + PI_ / 2.0) : 1.0;
            x += tamp * env * sin(2.0 * PI_ * 200.0 * sec);
        }
        if (x > 32767.0) x = 32767.0;
        if (x < -32768.0) x = -32768.0;
        w[i] = (int16_t)x;
    }
    VoiceVadOut o = voice_vad_window(&s->v, voice_window_db(w, VVAD_WIN));
    VoiceEpOut r = voice_ep_window(&s->e, o);
    if (r == VEP_START) { s->starts++; if (s->first_start < 0) s->first_start = s->win; }
    if (r == VEP_END) { s->ends++; s->last_end = s->win; }
    if (r == VEP_CAP) { s->caps++; if (s->first_cap < 0) s->first_cap = s->win; }
    if (r == VEP_ABANDON) { s->abandons++; s->last_abandon = s->win; if (s->first_abandon < 0) s->first_abandon = s->win; }
    s->win++;
    return r;
}

static void sim_run(Sim *s, int n, double tone_db, double depth, double noise_db)
{
    for (int i = 0; i < n; i++) sim_win(s, tone_db, depth, noise_db, 0.0);
}

#define NONE_DB (-300.0)
#define W(ms) ((ms) / 20)

int test_vvad_window_db(void)
{
    TEST_BEGIN();
    int16_t z[VVAD_WIN] = {0};
    ASSERT_TRUE(voice_window_db(z, VVAD_WIN) == -90.0);
    ASSERT_TRUE(voice_window_db(z, 0) == -90.0);
    int16_t full[VVAD_WIN];
    for (int i = 0; i < VVAD_WIN; i++) full[i] = (i & 1) ? -32768 : 32767;
    ASSERT_TRUE(fabs(voice_window_db(full, VVAD_WIN)) < 0.01);
    for (int i = 0; i < VVAD_WIN; i++) full[i] = (i & 1) ? -3277 : 3277;   /* about -20 dBFS */
    ASSERT_TRUE(fabs(voice_window_db(full, VVAD_WIN) + 20.0) < 0.05);
    TEST_END();
}

int test_vvad_steady_noise_never_speech(void)
{
    TEST_BEGIN();
    static const double levels[] = { -60.0, -45.0, -35.0, -25.0 };
    for (int k = 0; k < 4; k++) {
        Sim s;
        sim_init(&s, 1200);
        sim_run(&s, W(30000), NONE_DB, 0.0, levels[k]);
        ASSERT_EQ(s.starts, 0);
    }
    TEST_END();
}

int test_vvad_noise_starting_midway(void)
{
    TEST_BEGIN();
    Sim s;
    sim_init(&s, 1200);
    sim_run(&s, W(5000), NONE_DB, 0.0, -70.0);
    int fan_start = s.win;
    sim_run(&s, W(10000), NONE_DB, 0.0, -35.0);
    ASSERT_EQ(s.ends, 0);
    ASSERT_TRUE(s.starts >= 1);
    ASSERT_TRUE(s.first_abandon >= 0);
    ASSERT_TRUE(s.first_abandon - fan_start <= W(2000));
    ASSERT_TRUE(fabs(voice_vad_floor_db(&s.v) + 35.0) < 3.0);
    ASSERT_FALSE(voice_ep_in_turn(&s.e));
    TEST_END();
}

int test_vvad_click_never_turn(void)
{
    TEST_BEGIN();
    for (int len = 1; len <= 2; len++) {
        Sim s;
        sim_init(&s, 500);
        sim_run(&s, W(3000), NONE_DB, 0.0, -60.0);
        for (int i = 0; i < len; i++) sim_win(&s, -6.0, 0.0, -60.0, 0.0);
        sim_run(&s, W(3000), NONE_DB, 0.0, -60.0);
        ASSERT_EQ(s.starts, 0);
        ASSERT_EQ(s.ends, 0);
    }
    TEST_END();
}

int test_vvad_door_slam_never_turn(void)
{
    TEST_BEGIN();
    Sim s;
    sim_init(&s, 500);
    sim_run(&s, W(3000), NONE_DB, 0.0, -60.0);
    /* 200 ms burst at -10 dBFS decaying at 30 dB per 300 ms (2 dB a window). */
    for (int i = 0; i < W(200); i++) sim_win(&s, -10.0, 0.0, -60.0, 2.0 * i);
    sim_run(&s, W(3000), NONE_DB, 0.0, -60.0);
    ASSERT_EQ(s.ends, 0);
    ASSERT_FALSE(voice_ep_in_turn(&s.e));
    TEST_END();
}

int test_vvad_speech_over_noise(void)
{
    TEST_BEGIN();
    Sim s;
    sim_init(&s, 1200);
    sim_run(&s, W(3000), NONE_DB, 0.0, -50.0);
    int onset = s.win;
    sim_run(&s, W(1500), -20.0, 0.4, -50.0);
    sim_run(&s, W(3000), NONE_DB, 0.0, -50.0);
    ASSERT_EQ(s.starts, 1);
    ASSERT_TRUE(s.first_start - onset <= W(200));
    ASSERT_EQ(s.ends, 1);
    /* last speech window = onset + 75 - 1; END a pause later */
    ASSERT_EQ(s.last_end, onset + W(1500) - 1 + W(1200));
    TEST_END();
}

int test_vep_pause_lengths(void)
{
    TEST_BEGIN();
    static const int pauses[] = { 500, 1200, 3000 };
    for (int k = 0; k < 3; k++) {
        Sim s;
        sim_init(&s, pauses[k]);
        sim_run(&s, W(2000), NONE_DB, 0.0, -60.0);
        sim_run(&s, W(1000), -20.0, 0.4, -60.0);
        int last_speech = s.win - 1;
        sim_run(&s, W(5000), NONE_DB, 0.0, -60.0);
        ASSERT_EQ(s.ends, 1);
        ASSERT_EQ(s.last_end - last_speech, pauses[k] / 20);
    }
    TEST_END();
}

int test_vep_minimum_speech(void)
{
    TEST_BEGIN();
    static const int ms[] = { 280, 300 };
    for (int k = 0; k < 2; k++) {
        Sim s;
        sim_init(&s, 500);
        sim_run(&s, W(2000), NONE_DB, 0.0, -60.0);
        sim_run(&s, W(ms[k]), -20.0, 0.0, -60.0);
        sim_run(&s, W(3000), NONE_DB, 0.0, -60.0);
        ASSERT_EQ(s.starts, 1);
        ASSERT_EQ(s.ends, k);
        ASSERT_EQ(s.abandons, 1 - k);
    }
    TEST_END();
}

int test_vep_turn_cap(void)
{
    TEST_BEGIN();
    Sim s;
    sim_init(&s, 1200);
    sim_run(&s, W(2000), NONE_DB, 0.0, -60.0);
    for (int i = 0; i < 6100 && s.caps == 0; i++) sim_win(&s, -20.0, 0.4, -60.0, 0.0);
    ASSERT_EQ(s.starts, 1);
    ASSERT_EQ(s.caps, 1);
    ASSERT_EQ(s.first_cap - s.first_start, 6000);
    ASSERT_EQ(s.ends, 0);
    TEST_END();
}

int test_vvad_after_floor_change(void)
{
    TEST_BEGIN();
    Sim s;
    sim_init(&s, 500);
    sim_run(&s, W(3000), NONE_DB, 0.0, -60.0);
    sim_run(&s, W(10000), NONE_DB, 0.0, -40.0);
    ASSERT_EQ(s.ends, 0);
    int ends0 = s.ends;
    sim_run(&s, W(1000), -15.0, 0.4, -40.0);
    sim_run(&s, W(3000), NONE_DB, 0.0, -40.0);
    ASSERT_EQ(s.ends, ends0 + 1);
    TEST_END();
}

int test_vvad_extra_db(void)
{
    TEST_BEGIN();
    /* Floor -60 (steady noise). The margin is 12 dB plus extra_db, so with
     * extra_db 6 the bar is 18 dB over the floor: 15 dB over is NOT detected
     * (the brief said detected; it contradicts the margin, see the report),
     * 20 dB over is; with extra_db 0, 15 dB over is. */
    static const struct { double extra, over; int detected; } c[] = {
        { 0.0, 15.0, 1 }, { 0.0, 10.0, 0 }, { 6.0, 15.0, 0 }, { 6.0, 10.0, 0 }, { 6.0, 20.0, 1 },
    };
    for (int k = 0; k < 5; k++) {
        Sim s;
        sim_init(&s, 500);
        voice_vad_set_extra_db(&s.v, c[k].extra);
        sim_run(&s, W(3000), NONE_DB, 0.0, -60.0);
        sim_run(&s, W(600), -60.0 + c[k].over + 0.5, 0.0, -60.0);   /* flat tone, no dips */
        ASSERT_EQ(s.starts > 0, c[k].detected);
    }
    TEST_END();
}

int test_vvad_freeze_floor(void)
{
    TEST_BEGIN();
    VoiceVad v;
    voice_vad_init(&v);
    voice_vad_window(&v, -60.0);
    voice_vad_freeze_floor(&v, 1);
    for (int i = 0; i < 500; i++) voice_vad_window(&v, -80.0);
    ASSERT_TRUE(voice_vad_floor_db(&v) == -60.0);
    voice_vad_freeze_floor(&v, 0);
    voice_vad_window(&v, -80.0);
    ASSERT_TRUE(voice_vad_floor_db(&v) == -80.0);
    TEST_END();
}

int test_vep_pause_clamp_and_reset(void)
{
    TEST_BEGIN();
    VoiceEndpoint e;
    voice_ep_init(&e, 100);
    ASSERT_EQ(voice_ep_window(&e, VV_SPEECH), VEP_START);
    ASSERT_TRUE(voice_ep_in_turn(&e));
    voice_ep_reset(&e);
    ASSERT_FALSE(voice_ep_in_turn(&e));
    ASSERT_EQ(voice_ep_window(&e, VV_SILENCE), VEP_NONE);
    ASSERT_EQ(voice_ep_window(&e, VV_NOISE_RESET), VEP_NONE);
    ASSERT_EQ(voice_ep_window(&e, VV_SPEECH), VEP_START);
    /* 100 clamps to 500: 25 silent windows end it (abandon: too short). */
    int out = VEP_NONE, n = 0;
    while (out == VEP_NONE && n < 100) { out = voice_ep_window(&e, VV_SILENCE); n++; }
    ASSERT_EQ(n, 25);
    ASSERT_EQ(out, VEP_ABANDON);
    TEST_END();
}
