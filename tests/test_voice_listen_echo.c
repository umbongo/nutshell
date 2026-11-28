#include "test_framework.h"
#include "voice_listen.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

/*
 * Simulation of the loudspeaker-to-microphone path in Conversation mode: the
 * listener is fed, window by window (20 ms), the assistant's own voice as the
 * microphone hears it (a 200 Hz tone modulated at 4 Hz, as speech is) over
 * room noise at -60 dBFS, with an optional "user" (300 Hz, modulated at
 * 3.3 Hz) added on top. When the listener asks for a duck the echo drops
 * 15 dB until the un-duck, and after a cut it is silent, as in the app.
 *
 * Levels are the RMS of the unmodulated tone, so the loudest windows of the
 * modulated echo sit at about that level and the quietest 14 dB under it.
 * A person speaking at a laptop's microphone is taken as -15 dBFS, with
 * loudspeaker echo of -35 dBFS (a moderate volume).
 */

#define PI_E 3.14159265358979323846
#define E_NONE (-300.0)
#define E_ROOM (-60.0)
#define E_USER (-15.0)
#define E_DUCK_DB 15.0

typedef struct {
    VoiceListen l;
    VoiceListenSink k;
    uint32_t lcg;
    long t;
    int win;
    int ducked, cut;
    int first_duck, first_cut, ducks, cuts, unducks;
} ET;

static void et_audio(void *u, const int16_t *s, size_t n) { (void)u; (void)s; (void)n; }

static void et_event(void *u, VoiceListenEvent e)
{
    ET *t = (ET *)u;
    if (e == VLN_DUCK) {
        t->ducked = 1;
        if (t->ducks++ == 0) t->first_duck = t->win;
    } else if (e == VLN_UNDUCK) {
        t->ducked = 0;
        t->unducks++;
    } else if (e == VLN_CUT) {
        t->cut = 1;
        if (t->cuts++ == 0) t->first_cut = t->win;
    }
}

static void et_init(ET *t, VoiceListenMode m)
{
    memset(t, 0, sizeof *t);
    voice_listen_init(&t->l, 500);
    t->k.audio = et_audio;
    t->k.event = et_event;
    t->k.u = t;
    t->lcg = 12345u;
    t->first_duck = t->first_cut = -1;
    voice_listen_set_mode(&t->l, VLM_LISTEN);
    for (int i = 0; i < 40; i++) {      /* the floor settles on room noise */
        int16_t w[VVAD_WIN];
        for (int j = 0; j < VVAD_WIN; j++) {
            t->lcg = t->lcg * 1664525u + 1013904223u;
            w[j] = (int16_t)(((double)(t->lcg >> 8) / 8388608.0 - 1.0) * 32768.0 *
                             pow(10.0, E_ROOM / 20.0) * 1.7320508);
        }
        voice_listen_feed(&t->l, w, VVAD_WIN, &t->k);
        t->win++;
    }
    voice_listen_set_mode(&t->l, m);
    t->win = 0;                         /* windows are counted from the mode change */
}

/* One window: echo_db and user_db are the levels at the microphone (E_NONE for none). */
static void et_win(ET *t, double echo_db, double user_db)
{
    int16_t w[VVAD_WIN];
    double ns = pow(10.0, E_ROOM / 20.0) * 32768.0 * 1.7320508;
    double echo = t->cut ? E_NONE : (t->ducked ? echo_db - E_DUCK_DB : echo_db);
    double ea = pow(10.0, echo / 20.0) * 32768.0 * 1.4142136;
    double ua = pow(10.0, user_db / 20.0) * 32768.0 * 1.4142136;
    for (int i = 0; i < VVAD_WIN; i++, t->t++) {
        double sec = (double)t->t / 16000.0, x;
        t->lcg = t->lcg * 1664525u + 1013904223u;
        x = ((double)(t->lcg >> 8) / 8388608.0 - 1.0) * ns;
        if (echo > -200.0)
            x += ea * (0.6 + 0.4 * sin(2.0 * PI_E * 4.0 * sec + PI_E / 2.0)) * sin(2.0 * PI_E * 200.0 * sec);
        if (user_db > -200.0)
            x += ua * (0.6 + 0.4 * sin(2.0 * PI_E * 3.3 * sec)) * sin(2.0 * PI_E * 300.0 * sec);
        if (x > 32767.0) x = 32767.0;
        if (x < -32768.0) x = -32768.0;
        w[i] = (int16_t)x;
    }
    voice_listen_feed(&t->l, w, VVAD_WIN, &t->k);
    t->win++;
}

static void et_started(ET *t) { voice_listen_audio_started(&t->l); }

static void et_run(ET *t, int n, double echo_db, double user_db)
{
    for (int i = 0; i < n; i++) et_win(t, echo_db, user_db);
}

int test_vlecho_hd_user_over_echo_cuts_within_600ms(void)
{
    TEST_BEGIN();
    static const int delay[3] = { 0, 30, 50 };  /* the echo starts at the mode change / 600 ms / 1 s later */
    for (int d = 0; d < 3; d++) {
        ET t;
        et_init(&t, VLM_SPEAK_HD);
        et_run(&t, delay[d], E_NONE, E_NONE);
        et_started(&t);
        et_run(&t, 100, -35.0, E_NONE);         /* 2 s of the reply alone */
        ASSERT_EQ(t.ducks, 0);
        int user_at = t.win;
        et_run(&t, 60, -35.0, E_USER);
        ASSERT_EQ(t.cuts, 1);
        ASSERT_EQ(t.first_cut - user_at <= 30, 1);
    }
    TEST_END();
}

int test_vlecho_aec_user_after_first_second_cuts_within_600ms(void)
{
    TEST_BEGIN();
    ET t;
    et_init(&t, VLM_SPEAK_AEC);
    et_started(&t);
    et_run(&t, 25, -45.0, E_NONE);              /* a faint residual, under the AEC detector */
    et_run(&t, 60, -50.0, E_NONE);              /* then converged: below the detector */
    ASSERT_EQ(t.ducks, 0);
    int user_at = t.win;                        /* 1.7 s after the reply's audio began */
    et_run(&t, 60, -50.0, E_USER);
    ASSERT_TRUE(t.cuts == 1);
    ASSERT_TRUE(t.first_cut - user_at <= 30);
    TEST_END();
}

int test_vlecho_aec_user_inside_first_second_cuts_within_1600ms(void)
{
    TEST_BEGIN();
    ET t;
    et_init(&t, VLM_SPEAK_AEC);
    et_started(&t);
    et_run(&t, 10, -36.0, E_NONE);
    int user_at = t.win;                        /* 200 ms into the reply */
    et_run(&t, 100, -50.0, E_USER);
    ASSERT_TRUE(t.cuts == 1);
    ASSERT_TRUE(t.first_cut - user_at <= 80);
    TEST_END();
}

int test_vlecho_cough_does_not_cut(void)
{
    TEST_BEGIN();
    static const VoiceListenMode modes[2] = { VLM_SPEAK_HD, VLM_SPEAK_AEC };
    for (int m = 0; m < 2; m++) {
        ET t;
        et_init(&t, modes[m]);
        et_started(&t);
        et_run(&t, 100, m == 0 ? -35.0 : -50.0, E_NONE);
        et_run(&t, 8, m == 0 ? -35.0 : -50.0, -15.0);   /* a 160 ms cough */
        et_run(&t, 100, m == 0 ? -35.0 : -50.0, E_NONE);
        ASSERT_EQ(t.cuts, 0);
    }
    TEST_END();
}

int test_vlecho_hd_small_level_jump_does_not_cut(void)
{
    TEST_BEGIN();
    ET t;
    et_init(&t, VLM_SPEAK_HD);
    et_started(&t);
    et_run(&t, 100, -35.0, E_NONE);
    et_run(&t, 200, -29.0, E_NONE);             /* the volume is turned up 6 dB */
    ASSERT_EQ(t.cuts, 0);
    TEST_END();
}

int test_vlecho_hd_late_echo_start_does_not_cut(void)
{
    TEST_BEGIN();
    static const double level[2] = { -35.0, -30.0 };
    static const int delay[2] = { 30, 50 };     /* the reply's first audio comes 600 ms / 1 s after the mode change */
    for (int a = 0; a < 2; a++)
        for (int d = 0; d < 2; d++) {
            ET t;
            et_init(&t, VLM_SPEAK_HD);
            et_run(&t, delay[d], E_NONE, E_NONE);
            et_started(&t);
            et_run(&t, 200, level[a], E_NONE);  /* 4 s of the reply, nobody speaking */
            ASSERT_EQ(t.ducks, 0);
            ASSERT_EQ(t.cuts, 0);
        }
    TEST_END();
}

int test_vlecho_hd_audio_stall_does_not_cut(void)
{
    TEST_BEGIN();
    ET t;
    et_init(&t, VLM_SPEAK_HD);
    et_started(&t);
    et_run(&t, 75, -30.0, E_NONE);
    et_run(&t, 125, E_NONE, E_NONE);            /* the network stalls for 2.5 s */
    et_run(&t, 150, -30.0, E_NONE);             /* and the audio resumes at the same volume */
    ASSERT_EQ(t.ducks, 0);
    ASSERT_EQ(t.cuts, 0);
    TEST_END();
}

/* Windows' canceller has not converged at the start of a reply: the leftover
 * echo is 26 to 34 dB over the room noise, then it falls away within a second
 * (here: 0.8 s of residual, then 12 dB over the room noise). Nobody speaks, so
 * the volume must not pump (duck) and the reply must not be cut. */
int test_vlecho_aec_unconverged_start_does_not_duck(void)
{
    TEST_BEGIN();
    static const double residual[3] = { -34.0, -30.0, -26.0 };
    static const int delay[2] = { 0, 40 };      /* audio starts at the mode change / 800 ms later */
    for (int r = 0; r < 3; r++)
        for (int d = 0; d < 2; d++) {
            ET t;
            et_init(&t, VLM_SPEAK_AEC);
            et_run(&t, delay[d], E_NONE, E_NONE);
            et_started(&t);
            et_run(&t, 40, residual[r], E_NONE);
            et_run(&t, 100, -48.0, E_NONE);
            ASSERT_EQ(t.ducks, 0);
            ASSERT_EQ(t.cuts, 0);
        }
    TEST_END();
}

/* The same, with the residual falling 30 dB over a second, as a canceller
 * converges, and a strong one (38 dB over the noise) that only the first
 * second's stricter rule can hold back. */
int test_vlecho_aec_converging_residual_does_not_cut(void)
{
    TEST_BEGIN();
    static const double start[2] = { -30.0, -22.0 };
    for (int a = 0; a < 2; a++) {
        ET t;
        et_init(&t, VLM_SPEAK_AEC);
        et_started(&t);
        for (int i = 0; i < 50; i++) et_win(&t, start[a] - 30.0 * (double)i / 50.0, E_NONE);
        et_run(&t, 100, E_NONE, E_NONE);
        ASSERT_EQ(t.ducks, 0);
        ASSERT_EQ(t.cuts, 0);
    }
    TEST_END();
}

/* A user with no audible echo (a headset, quiet loudspeakers) speaks into a
 * reply that the microphone barely hears: the user's own voice must not be
 * taken for the echo. The signal that the reply's audio plays comes from the
 * speech engine, not from the microphone. */
int test_vlecho_hd_no_echo_user_cuts_within_600ms(void)
{
    TEST_BEGIN();
    static const double echo[2] = { E_NONE, -52.0 };   /* nothing at all / inaudible */
    for (int e = 0; e < 2; e++) {
        ET t;
        et_init(&t, VLM_SPEAK_HD);
        et_started(&t);
        et_run(&t, 100, echo[e], E_NONE);       /* 2 s of the reply, heard by nobody */
        ASSERT_EQ(t.ducks, 0);
        int user_at = t.win;
        et_run(&t, 60, echo[e], E_USER);
        ASSERT_EQ(t.cuts, 1);
        ASSERT_TRUE(t.first_cut - user_at <= 30);
    }
    TEST_END();
}

/* Converged canceller: the residual is below the detector, the user speaks a
 * short "stop" (500 ms) 2 s into the reply, and a longer utterance at 200 ms. */
int test_vlecho_aec_quiet_residual_user_cuts(void)
{
    TEST_BEGIN();
    ET t;
    et_init(&t, VLM_SPEAK_AEC);
    et_started(&t);
    et_run(&t, 100, -55.0, E_NONE);
    int user_at = t.win;
    et_run(&t, 25, -55.0, E_USER);              /* a 500 ms "stop" */
    et_run(&t, 50, -55.0, E_NONE);
    ASSERT_EQ(t.cuts, 1);
    ASSERT_TRUE(t.first_cut - user_at <= 30);
    TEST_END();
}

/* The user speaks into silence at 200 ms, 500 ms and 2 s of the reply, with a
 * converged canceller (no residual echo, so no strict first second): as fast
 * as before the strict first second existed. */
int test_vlecho_aec_user_into_silence_cuts_fast(void)
{
    TEST_BEGIN();
    static const int at[3] = { 10, 25, 100 };
    for (int a = 0; a < 3; a++) {
        ET t;
        et_init(&t, VLM_SPEAK_AEC);
        et_started(&t);
        et_run(&t, at[a], E_NONE, E_NONE);
        int user_at = t.win;
        et_run(&t, 60, E_NONE, E_USER);
        ASSERT_EQ(t.cuts, 1);
        ASSERT_TRUE(t.first_cut - user_at <= 30);
    }
    TEST_END();
}

/* The signal arrives 800 ms after the mode is set, the echo from then on:
 * nothing counts before it and nothing cuts the reply after it. */
int test_vlecho_late_start_signal_does_not_cut(void)
{
    TEST_BEGIN();
    static const VoiceListenMode modes[2] = { VLM_SPEAK_HD, VLM_SPEAK_AEC };
    for (int m = 0; m < 2; m++) {
        ET t;
        et_init(&t, modes[m]);
        et_run(&t, 40, E_NONE, E_NONE);
        et_started(&t);
        et_run(&t, 200, m == 0 ? -35.0 : -48.0, E_NONE);
        ASSERT_EQ(t.ducks, 0);
        ASSERT_EQ(t.cuts, 0);
    }
    TEST_END();
}

/* No signal at all (a speech engine that sent no word event): nothing counts
 * for the first 3 s; after that the first loud window starts the learning
 * period, as before the signal existed. */
int test_vlecho_missing_signal_falls_back_after_3s(void)
{
    TEST_BEGIN();
    ET t;
    et_init(&t, VLM_SPEAK_HD);
    et_run(&t, 100, -35.0, E_NONE);             /* 2 s: the echo only */
    et_run(&t, 40, -35.0, E_USER);              /* the user at 2 s: not heard yet */
    ASSERT_EQ(t.cuts, 0);
    ASSERT_EQ(t.ducks, 0);
    et_run(&t, 100, -35.0, E_NONE);             /* past 3 s the echo is learned */
    ASSERT_EQ(t.ducks, 0);
    int user_at = t.win;
    et_run(&t, 60, -35.0, E_USER);
    ASSERT_EQ(t.cuts, 1);
    ASSERT_TRUE(t.first_cut - user_at <= 30);
    TEST_END();
}
