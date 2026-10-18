/* Settings > Debugging page: the pure text and number rules, and the row table. */
#include "test_framework.h"
#include "settings_debug.h"
#include "settings_layout.h"
#include <string.h>

int test_sdbg_level_labels(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(settings_debug_level_label(DLOG_LVL_OFF), "Off");
    ASSERT_STR_EQ(settings_debug_level_label(DLOG_LVL_ERRORS), "Errors");
    ASSERT_STR_EQ(settings_debug_level_label(DLOG_LVL_INFO), "Info");
    ASSERT_STR_EQ(settings_debug_level_label(DLOG_LVL_DEBUG), "Debug");
    ASSERT_STR_EQ(settings_debug_level_label(DLOG_LVL_TRACE), "Trace");
    ASSERT_NULL(settings_debug_level_label(-1));
    ASSERT_NULL(settings_debug_level_label(DLOG_LVL_COUNT));
    TEST_END();
}

int test_sdbg_category_labels(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(settings_debug_cat_label(DLOG_RENDER), "Rendering");
    ASSERT_STR_EQ(settings_debug_cat_label(DLOG_TERM), "Terminal");
    ASSERT_STR_EQ(settings_debug_cat_label(DLOG_AI), "AI Assist");
    ASSERT_STR_EQ(settings_debug_cat_label(DLOG_SSH), "SSH connection");
    ASSERT_STR_EQ(settings_debug_cat_label(DLOG_SHELL), "Local shell");
    ASSERT_STR_EQ(settings_debug_cat_label(DLOG_GENERAL), "General");
    ASSERT_NULL(settings_debug_cat_label(DLOG_CAT_COUNT));
    ASSERT_NULL(settings_debug_cat_label(-1));
    TEST_END();
}

int test_sdbg_parse_mb_plain_and_clamped(void)
{
    TEST_BEGIN();
    ASSERT_EQ(settings_debug_parse_mb("10", 1, 100, 7), 10);
    ASSERT_EQ(settings_debug_parse_mb("  25  ", 1, 100, 7), 25);
    ASSERT_EQ(settings_debug_parse_mb("1", 1, 100, 7), 1);
    ASSERT_EQ(settings_debug_parse_mb("100", 1, 100, 7), 100);
    ASSERT_EQ(settings_debug_parse_mb("0", 1, 100, 7), 1);          /* below the range: lo */
    ASSERT_EQ(settings_debug_parse_mb("101", 1, 100, 7), 100);      /* above: hi */
    ASSERT_EQ(settings_debug_parse_mb("4", 5, 5000, 50), 5);
    ASSERT_EQ(settings_debug_parse_mb("6000", 5, 5000, 50), 5000);
    TEST_END();
}

int test_sdbg_parse_mb_garbage_keeps_current(void)
{
    TEST_BEGIN();
    ASSERT_EQ(settings_debug_parse_mb("", 1, 100, 7), 7);
    ASSERT_EQ(settings_debug_parse_mb("   ", 1, 100, 7), 7);
    ASSERT_EQ(settings_debug_parse_mb("abc", 1, 100, 7), 7);
    ASSERT_EQ(settings_debug_parse_mb("12x", 1, 100, 7), 7);
    ASSERT_EQ(settings_debug_parse_mb("-5", 1, 100, 7), 7);
    ASSERT_EQ(settings_debug_parse_mb("1.5", 1, 100, 7), 7);
    ASSERT_EQ(settings_debug_parse_mb(NULL, 1, 100, 7), 7);
    /* the kept value is itself clamped */
    ASSERT_EQ(settings_debug_parse_mb("zz", 1, 100, 500), 100);
    ASSERT_EQ(settings_debug_parse_mb("zz", 5, 5000, 0), 5);
    TEST_END();
}

int test_sdbg_parse_mb_overflow_is_hi(void)
{
    TEST_BEGIN();
    ASSERT_EQ(settings_debug_parse_mb("99999999999999999999", 1, 100, 7), 100);
    ASSERT_EQ(settings_debug_parse_mb("2147483648", 5, 5000, 50), 5000);
    TEST_END();
}

int test_sdbg_usage_text(void)
{
    TEST_BEGIN();
    char b[96];
    settings_debug_usage_text(b, sizeof b, 37ULL * 1048576ULL, 9);
    ASSERT_STR_EQ(b, "using 37 MB in 9 files");
    settings_debug_usage_text(b, sizeof b, 37ULL * 1048576ULL + 600000ULL, 9);   /* rounds to nearest */
    ASSERT_STR_EQ(b, "using 38 MB in 9 files");
    settings_debug_usage_text(b, sizeof b, 5ULL * 1048576ULL, 1);
    ASSERT_STR_EQ(b, "using 5 MB in 1 file");
    settings_debug_usage_text(b, sizeof b, 2000, 2);
    ASSERT_STR_EQ(b, "using under 1 MB in 2 files");
    settings_debug_usage_text(b, sizeof b, 0, 0);
    ASSERT_STR_EQ(b, "no log files yet");
    settings_debug_usage_text(b, sizeof b, 123, 0);
    ASSERT_STR_EQ(b, "no log files yet");
    TEST_END();
}

int test_sdbg_usage_text_small_buffer_is_cut_and_terminated(void)
{
    TEST_BEGIN();
    char b[8];
    size_t n = settings_debug_usage_text(b, sizeof b, 37ULL * 1048576ULL, 9);
    ASSERT_TRUE(n < sizeof b);
    ASSERT_EQ((long)strlen(b), (long)n);
    ASSERT_EQ((long)settings_debug_usage_text(NULL, 0, 1, 1), 0L);
    char z[1];
    ASSERT_EQ((long)settings_debug_usage_text(z, 0, 1, 1), 0L);
    TEST_END();
}

int test_sdbg_current_text_cases(void)
{
    TEST_BEGIN();
    char b[512];
    DlogStatus st;

    memset(&st, 0, sizeof st);
    settings_debug_current_text(b, sizeof b, &st);
    ASSERT_STR_EQ(b, "none yet, created at the first error");

    st.enabled = 1;
    settings_debug_current_text(b, sizeof b, &st);
    ASSERT_STR_EQ(b, "none yet, created at the first event");

    strcpy(st.path, "C:\\Tools\\nutshell\\logs\\nutshell-20261017-140310-p8124.log");
    settings_debug_current_text(b, sizeof b, &st);
    ASSERT_STR_EQ(b, "C:\\Tools\\nutshell\\logs\\nutshell-20261017-140310-p8124.log");

    strcpy(st.note, "logs is read-only (error 5); using the local app data folder");
    settings_debug_current_text(b, sizeof b, &st);
    ASSERT_TRUE(strstr(b, "nutshell-20261017-140310-p8124.log") != NULL);
    ASSERT_TRUE(strstr(b, "read-only") != NULL);

    memset(&st, 0, sizeof st);
    st.mem_only = 1;
    settings_debug_current_text(b, sizeof b, &st);
    ASSERT_TRUE(strstr(b, "memory only") != NULL);
    strcpy(st.note, "no writable folder");
    settings_debug_current_text(b, sizeof b, &st);
    ASSERT_TRUE(strstr(b, "memory only") != NULL);
    ASSERT_TRUE(strstr(b, "no writable folder") != NULL);
    TEST_END();
}

int test_sdbg_current_text_null_and_tiny(void)
{
    TEST_BEGIN();
    char b[8];
    ASSERT_EQ((long)settings_debug_current_text(NULL, 0, NULL), 0L);
    ASSERT_TRUE(settings_debug_current_text(b, sizeof b, NULL) < sizeof b);
    DlogStatus st;
    memset(&st, 0, sizeof st);
    strcpy(st.path, "C:\\a\\very\\long\\path\\nutshell.log");
    size_t n = settings_debug_current_text(b, sizeof b, &st);
    ASSERT_TRUE(n < sizeof b);
    ASSERT_EQ((long)strlen(b), (long)n);
    TEST_END();
}

/* ---- the row table ---- */

int test_sdbg_layout_unticked_is_toggle_hint_and_current_file(void)
{
    TEST_BEGIN();
    SettingsDebugPos p[SDBG_COUNT];
    int total = settings_debug_layout(0, p);
    ASSERT_EQ(total, 4);   /* title row + three */
    ASSERT_EQ(p[SDBG_ENABLE].row, 1);
    ASSERT_EQ(p[SDBG_HINT_OFF].row, 2);
    ASSERT_EQ(p[SDBG_CURRENT].row, 3);
    ASSERT_TRUE(p[SDBG_ENABLE].visible && p[SDBG_HINT_OFF].visible && p[SDBG_CURRENT].visible);
    int visible = 0;
    for (int i = 0; i < SDBG_COUNT; i++) {
        if (p[i].visible) {
            visible++;
        } else {
            ASSERT_EQ(p[i].row, -1);
            ASSERT_EQ(p[i].rows, 0);
        }
    }
    ASSERT_EQ(visible, 3);
    TEST_END();
}

int test_sdbg_layout_ticked_packs_every_slot_in_order(void)
{
    TEST_BEGIN();
    SettingsDebugPos p[SDBG_COUNT];
    int total = settings_debug_layout(1, p);
    ASSERT_EQ(total, 21);
    ASSERT_TRUE(!p[SDBG_HINT_OFF].visible);
    ASSERT_EQ(p[SDBG_HINT_OFF].row, -1);
    int next = 1;
    for (int i = 0; i < SDBG_COUNT; i++) {
        if (i == SDBG_HINT_OFF) continue;
        ASSERT_TRUE(p[i].visible);
        ASSERT_EQ(p[i].row, next);
        ASSERT_EQ(p[i].rows, settings_debug_slot_rows(i));
        next += p[i].rows;
    }
    ASSERT_EQ(next, total);
    /* the six level rows are consecutive, in DlogCat order */
    for (int c = 0; c < DLOG_CAT_COUNT; c++)
        ASSERT_EQ(p[SDBG_LEVEL_RENDER + c].row, p[SDBG_LEVEL_RENDER].row + c);
    ASSERT_EQ(p[SDBG_LEVEL_RENDER].row, 3);
    ASSERT_EQ(p[SDBG_CURRENT].row, 17);
    ASSERT_EQ(p[SDBG_NOTE].row, 18);
    ASSERT_EQ(p[SDBG_NOTE].rows, 3);
    TEST_END();
}

int test_sdbg_layout_slot_rows_bounds(void)
{
    TEST_BEGIN();
    ASSERT_EQ(settings_debug_slot_rows(-1), 0);
    ASSERT_EQ(settings_debug_slot_rows(SDBG_COUNT), 0);
    ASSERT_EQ(settings_debug_slot_rows(SDBG_CONTENT_WARN), 2);
    ASSERT_EQ(settings_debug_slot_rows(SDBG_ENABLE), 1);
    TEST_END();
}

int test_sdbg_layout_null_pos_still_counts(void)
{
    TEST_BEGIN();
    ASSERT_EQ(settings_debug_layout(0, NULL), 4);
    ASSERT_EQ(settings_debug_layout(1, NULL), 21);
    TEST_END();
}

/* Pixel position of the Current log file row in both states, at 96 and 192 DPI:
 * pad + row * row_h + (row_h - ctrl_h) / 2, and the 192 figure is exactly double. */
int test_sdbg_layout_row_pixels_96_and_192(void)
{
    TEST_BEGIN();
    SettingsDebugPos on[SDBG_COUNT], off[SDBG_COUNT];
    (void)settings_debug_layout(1, on);
    (void)settings_debug_layout(0, off);
    SettingsMetrics m96, m192;
    settings_metrics_init(&m96, 96);
    settings_metrics_init(&m192, 192);
    SettingsRect l, c;

    settings_row_rects(on[SDBG_CURRENT].row, 600, 0, 0, &m96, &l, &c);
    ASSERT_EQ(c.y, 14 + 17 * 30 + 4);
    settings_row_rects(off[SDBG_CURRENT].row, 600, 0, 0, &m96, &l, &c);
    ASSERT_EQ(c.y, 14 + 3 * 30 + 4);
    settings_row_rects(on[SDBG_LEVEL_RENDER].row, 600, 0, 0, &m96, &l, &c);
    ASSERT_EQ(c.y, 14 + 3 * 30 + 4);

    settings_row_rects(on[SDBG_CURRENT].row, 1200, 0, 0, &m192, &l, &c);
    ASSERT_EQ(c.y, 28 + 17 * 60 + 8);
    settings_row_rects(off[SDBG_CURRENT].row, 1200, 0, 0, &m192, &l, &c);
    ASSERT_EQ(c.y, 28 + 3 * 60 + 8);
    TEST_END();
}

int test_sdbg_layout_page_height_ticked_vs_unticked(void)
{
    TEST_BEGIN();
    SettingsDebugPos p[SDBG_COUNT];
    SettingsMetrics m96, m192;
    settings_metrics_init(&m96, 96);
    settings_metrics_init(&m192, 192);
    int off_rows = settings_debug_layout(0, p);
    int on_rows  = settings_debug_layout(1, p);
    ASSERT_EQ(settings_page_height(off_rows, &m96), 28 + 4 * 30);
    ASSERT_EQ(settings_page_height(on_rows, &m96), 28 + 21 * 30);
    ASSERT_EQ(settings_page_height(on_rows, &m192), 2 * settings_page_height(on_rows, &m96));
    /* the ticked page overflows the default 560 px window and scrolls; the unticked one does not */
    ASSERT_TRUE(settings_page_height(on_rows, &m96) > 560 - m96.btnbar_h);
    ASSERT_TRUE(settings_page_height(off_rows, &m96) < 560 - m96.btnbar_h);
    TEST_END();
}
