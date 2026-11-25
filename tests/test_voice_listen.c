#include "test_framework.h"
#include "voice_listen.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PI_ 3.14159265358979323846
#define NONE_DB (-300.0)
#define MAXW 1300
#define MAXEV 64

/* Every fed window is archived; the sink finds the window a delivered buffer
 * came from by content (the noise is a fixed LCG, so every window is unique). */
static int16_t arch[MAXW][VVAD_WIN];

typedef struct {
    VoiceListen l;
    VoiceListenSink k;
    uint32_t lcg;
    long t;
    int win;                    /* next window index */
    int nev;
    VoiceListenEvent ev[MAXEV];
    int evwin[MAXEV];           /* window being fed when the event fired */
    int naudio, first_audio, last_audio, search, bad_order, unknown;
} LT;

static void lt_audio(void *u, const int16_t *s, size_t n)
{
    LT *t = (LT *)u;
    int found = -1;
    if (n != VVAD_WIN) { t->unknown++; return; }
    for (int i = t->search; i < t->win + 1 && i < MAXW; i++)
        if (memcmp(arch[i], s, sizeof arch[i]) == 0) { found = i; break; }
    if (found < 0) { t->unknown++; return; }
    if (t->naudio == 0) t->first_audio = found;
    else if (found != t->last_audio + 1) t->bad_order++;
    t->last_audio = found;
    t->search = found;
    t->naudio++;
}

static void lt_event(void *u, VoiceListenEvent e)
{
    LT *t = (LT *)u;
    if (t->nev < MAXEV) { t->ev[t->nev] = e; t->evwin[t->nev] = t->win; t->nev++; }
}

static void lt_init(LT *t, int pause_ms)
{
    memset(t, 0, sizeof *t);
    voice_listen_init(&t->l, pause_ms);
    t->k.audio = lt_audio;
    t->k.event = lt_event;
    t->k.u = t;
    t->lcg = 12345u;
    t->first_audio = t->last_audio = -1;
}

static double lt_unit(LT *t)
{
    t->lcg = t->lcg * 1664525u + 1013904223u;
    return (double)(t->lcg >> 8) / 8388608.0 - 1.0;
}

/* One window: white noise plus a 200 Hz tone modulated at 4 Hz ("speech"). */
static void lt_win(LT *t, double tone_db, double depth, double noise_db)
{
    int16_t *w = arch[t->win];
    double nscale = pow(10.0, noise_db / 20.0) * 32768.0 * 1.7320508;
    double tamp = pow(10.0, tone_db / 20.0) * 32768.0 * 1.4142136;
    for (int i = 0; i < VVAD_WIN; i++, t->t++) {
        double sec = (double)t->t / 16000.0, x = 0.0;
        if (noise_db > -200.0) x += lt_unit(t) * nscale;
        else (void)lt_unit(t);
        if (tone_db > -200.0) {
            double env = depth > 0.0 ? 0.6 + depth * sin(2.0 * PI_ * 4.0 * sec + PI_ / 2.0) : 1.0;
            x += tamp * env * sin(2.0 * PI_ * 200.0 * sec);
        }
        if (x > 32767.0) x = 32767.0;
        if (x < -32768.0) x = -32768.0;
        w[i] = (int16_t)x;
    }
    voice_listen_feed(&t->l, w, VVAD_WIN, &t->k);
    t->win++;
}

static void lt_run(LT *t, int n, double tone_db, double depth, double noise_db)
{
    for (int i = 0; i < n; i++) lt_win(t, tone_db, depth, noise_db);
}

/* index of the first event of kind e, or -1 */
static int lt_find(const LT *t, VoiceListenEvent e)
{
    for (int i = 0; i < t->nev; i++) if (t->ev[i] == e) return i;
    return -1;
}

static int lt_count(const LT *t, VoiceListenEvent e)
{
    int c = 0;
    for (int i = 0; i < t->nev; i++) if (t->ev[i] == e) c++;
    return c;
}

int test_vlisten_turn_with_preroll(void)
{
    TEST_BEGIN();
    LT t;
    lt_init(&t, 500);
    voice_listen_set_mode(&t.l, VLM_LISTEN);
    lt_run(&t, 100, NONE_DB, 0.0, -60.0);
    lt_run(&t, 40, -20.0, 0.4, -60.0);          /* speech from window 100 */
    lt_run(&t, 60, NONE_DB, 0.0, -60.0);
    ASSERT_EQ(t.nev, 2);
    ASSERT_EQ((int)t.ev[0], (int)VLN_TURN_START);
    ASSERT_EQ((int)t.ev[1], (int)VLN_TURN_END);
    ASSERT_EQ(t.unknown, 0);
    ASSERT_EQ(t.bad_order, 0);
    /* start after VVAD_START_NEED candidates; the ring holds the 15 windows to there */
    ASSERT_EQ(t.evwin[0], 100 + VVAD_START_NEED - 1);
    ASSERT_EQ(t.first_audio, 100 - 15 + VVAD_START_NEED);
    ASSERT_EQ(t.last_audio, t.evwin[1]);
    ASSERT_EQ(t.naudio, t.last_audio - t.first_audio + 1);
    TEST_END();
}

int test_vlisten_hold_emits_nothing(void)
{
    TEST_BEGIN();
    LT t;
    lt_init(&t, 500);
    voice_listen_set_mode(&t.l, VLM_HOLD);
    lt_run(&t, 100, NONE_DB, 0.0, -60.0);
    lt_run(&t, 40, -20.0, 0.4, -60.0);
    lt_run(&t, 60, NONE_DB, 0.0, -60.0);
    ASSERT_EQ(t.nev, 0);
    ASSERT_EQ(t.naudio, 0);
    TEST_END();
}

int test_vlisten_cough_abandoned(void)
{
    TEST_BEGIN();
    LT t;
    lt_init(&t, 500);
    voice_listen_set_mode(&t.l, VLM_LISTEN);
    lt_run(&t, 100, NONE_DB, 0.0, -60.0);
    lt_run(&t, 6, -20.0, 0.4, -60.0);           /* 120 ms */
    lt_run(&t, 60, NONE_DB, 0.0, -60.0);
    ASSERT_EQ(t.nev, 2);
    ASSERT_EQ((int)t.ev[0], (int)VLN_TURN_START);
    ASSERT_EQ((int)t.ev[1], (int)VLN_TURN_ABANDON);
    TEST_END();
}

int test_vlisten_cutin_aec_duck_then_cut(void)
{
    TEST_BEGIN();
    LT t;
    lt_init(&t, 500);
    voice_listen_set_mode(&t.l, VLM_LISTEN);
    lt_run(&t, 40, NONE_DB, 0.0, -60.0);
    voice_listen_set_mode(&t.l, VLM_SPEAK_AEC);
    lt_run(&t, 10, NONE_DB, 0.0, -60.0);
    lt_run(&t, 40, -20.0, 0.4, -60.0);          /* voice from window 50 */
    lt_run(&t, 60, NONE_DB, 0.0, -60.0);
    int d = lt_find(&t, VLN_DUCK), s = lt_find(&t, VLN_TURN_START), c = lt_find(&t, VLN_CUT);
    int e = lt_find(&t, VLN_TURN_END);
    ASSERT_TRUE(d >= 0 && s == d + 1 && c > s && e > c);
    ASSERT_TRUE(t.evwin[d] == 59);
    ASSERT_TRUE(t.evwin[c] == 69);
    ASSERT_EQ(lt_count(&t, VLN_TURN_START), 1);
    ASSERT_EQ(lt_count(&t, VLN_UNDUCK), 0);
    ASSERT_EQ(t.unknown, 0);
    ASSERT_EQ(t.bad_order, 0);
    ASSERT_TRUE(t.first_audio < 50);            /* the pre-roll covers the first words */
    ASSERT_EQ(t.first_audio, t.evwin[s] - 14);
    ASSERT_EQ(t.last_audio, t.evwin[e]);
    TEST_END();
}

int test_vlisten_cutin_cough_unducks(void)
{
    TEST_BEGIN();
    LT t;
    lt_init(&t, 500);
    voice_listen_set_mode(&t.l, VLM_LISTEN);
    lt_run(&t, 40, NONE_DB, 0.0, -60.0);
    voice_listen_set_mode(&t.l, VLM_SPEAK_AEC);
    lt_run(&t, 10, NONE_DB, 0.0, -60.0);
    lt_run(&t, 12, -20.0, 0.4, -60.0);          /* 240 ms of voice from window 50 */
    lt_run(&t, 60, NONE_DB, 0.0, -60.0);
    int d = lt_find(&t, VLN_DUCK), s = lt_find(&t, VLN_TURN_START);
    int u = lt_find(&t, VLN_UNDUCK), a = lt_find(&t, VLN_TURN_ABANDON);
    ASSERT_TRUE(d >= 0 && s == d + 1 && u == s + 1 && a == u + 1);
    ASSERT_EQ(t.nev, 4);
    ASSERT_EQ(lt_find(&t, VLN_CUT), -1);
    /* last voice window is 61; 15 silent windows later */
    ASSERT_TRUE(abs(t.evwin[u] - (61 + 15)) <= 1);
    TEST_END();
}

int test_vlisten_cutin_hd_no_audio_before_cut(void)
{
    TEST_BEGIN();
    LT t;
    lt_init(&t, 500);
    voice_listen_set_mode(&t.l, VLM_LISTEN);
    lt_run(&t, 40, NONE_DB, 0.0, -60.0);
    voice_listen_set_mode(&t.l, VLM_SPEAK_HD);
    lt_run(&t, 60, -30.0, 0.4, -60.0);          /* our own reply, -30 dBFS, from window 40 */
    ASSERT_EQ(t.nev, 0);
    lt_run(&t, 40, -12.0, 0.0, -60.0);          /* the user, -12 dBFS, from window 100 */
    lt_run(&t, 60, NONE_DB, 0.0, -60.0);
    int d = lt_find(&t, VLN_DUCK), c = lt_find(&t, VLN_CUT), s = lt_find(&t, VLN_TURN_START);
    ASSERT_TRUE(d >= 0 && c > d && s == c + 1);
    ASSERT_TRUE(t.evwin[d] == 109);
    ASSERT_TRUE(t.evwin[c] == 119);
    ASSERT_EQ(t.evwin[c], t.evwin[s]);
    ASSERT_EQ(t.unknown, 0);
    ASSERT_EQ(t.bad_order, 0);
    ASSERT_TRUE(t.naudio > 0);
    ASSERT_TRUE(t.first_audio >= t.evwin[c]);   /* nothing from before the cut */
    ASSERT_EQ(lt_count(&t, VLN_UNDUCK), 0);
    ASSERT_EQ(lt_count(&t, VLN_TURN_END), 1);
    TEST_END();
}

int test_vlisten_hd_cough_unducks_without_abandon(void)
{
    TEST_BEGIN();
    LT t;
    lt_init(&t, 500);
    voice_listen_set_mode(&t.l, VLM_LISTEN);
    lt_run(&t, 40, NONE_DB, 0.0, -60.0);
    voice_listen_set_mode(&t.l, VLM_SPEAK_HD);
    lt_run(&t, 60, -30.0, 0.4, -60.0);
    lt_run(&t, 12, -12.0, 0.0, -60.0);
    lt_run(&t, 60, -30.0, 0.4, -60.0);
    ASSERT_EQ(lt_count(&t, VLN_DUCK), 1);
    ASSERT_EQ(lt_count(&t, VLN_UNDUCK), 1);
    ASSERT_EQ(lt_count(&t, VLN_TURN_START), 0);
    ASSERT_EQ(lt_count(&t, VLN_TURN_ABANDON), 0);
    ASSERT_EQ(lt_count(&t, VLN_CUT), 0);
    ASSERT_EQ(t.naudio, 0);
    TEST_END();
}

int test_vlisten_hd_echo_alone_never_cuts(void)
{
    TEST_BEGIN();
    LT t;
    lt_init(&t, 500);
    voice_listen_set_mode(&t.l, VLM_LISTEN);
    lt_run(&t, 40, NONE_DB, 0.0, -60.0);
    voice_listen_set_mode(&t.l, VLM_SPEAK_HD);
    for (int i = 0; i < 1000; i++) lt_win(&t, -25.0, 0.4, -60.0);   /* 20 s */
    ASSERT_EQ(lt_count(&t, VLN_DUCK), 0);
    ASSERT_EQ(t.nev, 0);
    TEST_END();
}

int test_vlisten_set_mode_drops_turn(void)
{
    TEST_BEGIN();
    LT t;
    lt_init(&t, 500);
    voice_listen_set_mode(&t.l, VLM_LISTEN);
    lt_run(&t, 100, NONE_DB, 0.0, -60.0);
    lt_run(&t, 20, -20.0, 0.4, -60.0);
    ASSERT_EQ(t.nev, 1);
    int audio_before = t.naudio;
    voice_listen_set_mode(&t.l, VLM_HOLD);
    lt_run(&t, 20, -20.0, 0.4, -60.0);
    lt_run(&t, 60, NONE_DB, 0.0, -60.0);
    ASSERT_EQ(t.nev, 1);
    ASSERT_EQ(t.naudio, audio_before);
    voice_listen_set_mode(&t.l, VLM_LISTEN);    /* a fresh turn is possible */
    ASSERT_FALSE(voice_ep_in_turn(&t.l.ep));
    TEST_END();
}

int test_vlisten_partial_windows_and_wipe(void)
{
    TEST_BEGIN();
    LT t;
    lt_init(&t, 500);
    voice_listen_set_mode(&t.l, VLM_LISTEN);
    /* feed 100 windows of noise in odd-sized chunks: same windows, no events */
    lt_run(&t, 100, NONE_DB, 0.0, -60.0);
    int nonzero = 0;
    for (int i = 0; i < VL_PREROLL; i++)
        for (int j = 0; j < VVAD_WIN; j++) if (t.l.ring[i][j]) nonzero = 1;
    ASSERT_TRUE(nonzero);
    int16_t half[VVAD_WIN / 2];
    for (int j = 0; j < VVAD_WIN / 2; j++) half[j] = 100;
    voice_listen_feed(&t.l, half, VVAD_WIN / 2, &t.k);
    ASSERT_EQ((int)t.l.part_n, VVAD_WIN / 2);
    voice_listen_wipe(&t.l);
    ASSERT_EQ((int)t.l.part_n, 0);
    nonzero = 0;
    for (int i = 0; i < VL_PREROLL; i++)
        for (int j = 0; j < VVAD_WIN; j++) if (t.l.ring[i][j]) nonzero = 1;
    for (int j = 0; j < VVAD_WIN; j++) if (t.l.part[j]) nonzero = 1;
    ASSERT_FALSE(nonzero);
    ASSERT_EQ(t.l.ring_n, 0);
    TEST_END();
}

int test_vlisten_cutin_hd_modulated_user(void)
{
    TEST_BEGIN();
    LT t;
    lt_init(&t, 500);
    voice_listen_set_mode(&t.l, VLM_LISTEN);
    lt_run(&t, 40, NONE_DB, 0.0, -60.0);
    voice_listen_set_mode(&t.l, VLM_SPEAK_HD);
    lt_run(&t, 60, -30.0, 0.4, -60.0);
    lt_run(&t, 80, -12.0, 0.4, -60.0);          /* 4 Hz modulated user from window 100 */
    lt_run(&t, 60, NONE_DB, 0.0, -60.0);
    int d = lt_find(&t, VLN_DUCK), c = lt_find(&t, VLN_CUT);
    ASSERT_TRUE(d >= 0 && c > d);
    ASSERT_EQ(lt_count(&t, VLN_UNDUCK), 0);
    ASSERT_TRUE(t.first_audio >= t.evwin[c]);
    TEST_END();
}

int test_vlisten_cutin_aec_modulated_user(void)
{
    TEST_BEGIN();
    LT t;
    lt_init(&t, 500);
    voice_listen_set_mode(&t.l, VLM_LISTEN);
    lt_run(&t, 40, NONE_DB, 0.0, -60.0);
    voice_listen_set_mode(&t.l, VLM_SPEAK_AEC);
    lt_run(&t, 10, NONE_DB, 0.0, -60.0);
    lt_run(&t, 80, -12.0, 0.4, -60.0);
    lt_run(&t, 60, NONE_DB, 0.0, -60.0);
    int d = lt_find(&t, VLN_DUCK), c = lt_find(&t, VLN_CUT);
    ASSERT_TRUE(d >= 0 && c > d);
    ASSERT_EQ(lt_count(&t, VLN_UNDUCK), 0);
    ASSERT_EQ(lt_count(&t, VLN_TURN_END), 1);
    TEST_END();
}
