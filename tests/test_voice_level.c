#include "test_framework.h"
#include "voice_level.h"
#include <math.h>
#include <string.h>

static int near_db(double a, double b, double tol) { return fabs(a - b) <= tol; }

int test_voice_dbfs(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(near_db(voice_dbfs(0), -60.0, 1e-9));
    ASSERT_TRUE(near_db(voice_dbfs(32768), 0.0, 1e-9));
    ASSERT_TRUE(near_db(voice_dbfs(16384), -6.02, 0.01));
    ASSERT_TRUE(near_db(voice_dbfs(328), -40.0, 0.1));
    ASSERT_TRUE(near_db(voice_dbfs(1), -60.0, 1e-9)); /* -90 dB clamps to the floor */
    TEST_END();
}

int test_voice_level_classify_boundaries(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_level_classify(-45.1), VLEVEL_SILENT);
    ASSERT_EQ(voice_level_classify(-45.0), VLEVEL_QUIET);
    ASSERT_EQ(voice_level_classify(-18.1), VLEVEL_QUIET);
    ASSERT_EQ(voice_level_classify(-18.0), VLEVEL_GOOD);
    ASSERT_EQ(voice_level_classify(-6.0), VLEVEL_GOOD);
    ASSERT_EQ(voice_level_classify(-5.9), VLEVEL_HOT);
    ASSERT_EQ(voice_level_classify(-3.1), VLEVEL_HOT);
    ASSERT_EQ(voice_level_classify(-3.0), VLEVEL_LOUD);
    ASSERT_EQ(voice_level_classify(0.0), VLEVEL_LOUD);
    TEST_END();
}

int test_voice_level_labels(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(voice_level_label(VLEVEL_SILENT), "Speak to see the level");
    ASSERT_STR_EQ(voice_level_label(VLEVEL_QUIET), "Too quiet \xE2\x80\x94 raise the gain");
    ASSERT_STR_EQ(voice_level_label(VLEVEL_GOOD), "Good");
    ASSERT_STR_EQ(voice_level_label(VLEVEL_HOT), "Good");
    ASSERT_STR_EQ(voice_level_label(VLEVEL_LOUD), "Too loud \xE2\x80\x94 lower the gain");
    TEST_END();
}

int test_voice_meter_attack_release(void)
{
    TEST_BEGIN();
    VoiceMeter m;
    voice_meter_reset(&m);
    ASSERT_TRUE(near_db(m.level_db, -60.0, 1e-9));
    voice_meter_feed(&m, 16384, 50);
    ASSERT_TRUE(near_db(m.level_db, -6.02, 0.01));
    voice_meter_feed(&m, 0, 500);
    ASSERT_TRUE(near_db(m.level_db, -16.02, 0.5));
    /* release never goes below the floor */
    voice_meter_feed(&m, 0, 10000);
    ASSERT_TRUE(near_db(m.level_db, -60.0, 1e-9));
    /* a louder peak is taken at once, a quieter one only releases */
    voice_meter_feed(&m, 16384, 10);
    voice_meter_feed(&m, 8192, 10);
    ASSERT_TRUE(m.level_db > -6.5);
    TEST_END();
}

int test_voice_meter_hold(void)
{
    TEST_BEGIN();
    VoiceMeter m;
    voice_meter_reset(&m);
    voice_meter_feed(&m, 16384, 50);
    for (int i = 0; i < 18; i++) voice_meter_feed(&m, 0, 50); /* 900 ms */
    ASSERT_TRUE(near_db(m.hold_db, -6.02, 0.01));
    for (int i = 0; i < 12; i++) voice_meter_feed(&m, 0, 50); /* 1500 ms total */
    ASSERT_TRUE(near_db(m.hold_db, -6.02 - 10.0, 1.0));
    TEST_END();
}

int test_voice_meter_window_expires(void)
{
    TEST_BEGIN();
    VoiceMeter m;
    voice_meter_reset(&m);
    ASSERT_TRUE(near_db(voice_meter_window_peak_db(&m), -60.0, 1e-9));
    voice_meter_feed(&m, 16384, 50);
    ASSERT_TRUE(near_db(voice_meter_window_peak_db(&m), -6.02, 0.01));
    for (int i = 0; i < 18; i++) voice_meter_feed(&m, 0, 50); /* 900 ms: still in window */
    ASSERT_TRUE(near_db(voice_meter_window_peak_db(&m), -6.02, 0.01));
    for (int i = 0; i < 4; i++) voice_meter_feed(&m, 0, 50); /* 1100 ms total */
    ASSERT_TRUE(near_db(voice_meter_window_peak_db(&m), -60.0, 1e-9));
    TEST_END();
}

int test_voice_meter_frac(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(near_db(voice_meter_frac(-60.0), 0.0, 1e-9));
    ASSERT_TRUE(near_db(voice_meter_frac(0.0), 1.0, 1e-9));
    ASSERT_TRUE(near_db(voice_meter_frac(-30.0), 0.5, 1e-9));
    ASSERT_TRUE(near_db(voice_meter_frac(-90.0), 0.0, 1e-9));
    ASSERT_TRUE(near_db(voice_meter_frac(6.0), 1.0, 1e-9));
    TEST_END();
}
