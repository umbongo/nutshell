/* tests/test_dlog_files.c -- the pure file-handling decisions of the debug log:
 * timestamps, names, folder resolution, rotation and retention, FORMAT.txt,
 * and the category/level name tables. */
#include "test_framework.h"
#include "dlog_types.h"
#include "dlog_files.h"
#include <stdio.h>
#include <string.h>

/* ---- Name tables --------------------------------------------------------- */

int test_dlog_types_category_names_round_trip(void)
{
    TEST_BEGIN();
    for (int c = 0; c < DLOG_CAT_COUNT; c++) {
        const char *n = dlog_cat_name(c);
        ASSERT_NOT_NULL(n);
        ASSERT_EQ(dlog_cat_from_name(n), c);
    }
    ASSERT_STR_EQ(dlog_cat_name(DLOG_RENDER), "render");
    ASSERT_STR_EQ(dlog_cat_name(DLOG_SHELL), "shell");
    ASSERT_STR_EQ(dlog_cat_name(DLOG_GENERAL), "general");
    ASSERT_EQ(dlog_cat_from_name("RENDER"), (int)DLOG_RENDER);
    ASSERT_EQ(dlog_cat_from_name("nope"), -1);
    ASSERT_EQ(dlog_cat_from_name(NULL), -1);
    ASSERT_EQ(dlog_cat_from_name(""), -1);
    ASSERT_NULL(dlog_cat_name(-1));
    ASSERT_NULL(dlog_cat_name(DLOG_CAT_COUNT));
    TEST_END();
}

int test_dlog_types_level_names_round_trip(void)
{
    TEST_BEGIN();
    for (int l = 0; l < DLOG_LVL_COUNT; l++) {
        const char *n = dlog_level_name(l);
        ASSERT_NOT_NULL(n);
        ASSERT_EQ(dlog_level_from_name(n), l);
    }
    ASSERT_STR_EQ(dlog_level_name(DLOG_LVL_ERRORS), "errors");
    ASSERT_EQ(dlog_level_from_name("Trace"), (int)DLOG_LVL_TRACE);
    ASSERT_EQ(dlog_level_from_name("verbose"), -1);
    ASSERT_EQ(dlog_level_from_name(NULL), -1);
    ASSERT_NULL(dlog_level_name(99));
    TEST_END();
}

int test_dlog_types_level_max_event(void)
{
    TEST_BEGIN();
    ASSERT_EQ(dlog_level_max_event(DLOG_LVL_OFF), 0);
    ASSERT_EQ(dlog_level_max_event(DLOG_LVL_ERRORS), (int)DLOG_WARN);
    ASSERT_EQ(dlog_level_max_event(DLOG_LVL_INFO), (int)DLOG_INFO);
    ASSERT_EQ(dlog_level_max_event(DLOG_LVL_DEBUG), (int)DLOG_DEBUG);
    ASSERT_EQ(dlog_level_max_event(DLOG_LVL_TRACE), (int)DLOG_TRACE);
    ASSERT_EQ(dlog_level_max_event(-4), 0);
    ASSERT_EQ(dlog_level_max_event(77), (int)DLOG_TRACE);
    TEST_END();
}

int test_dlog_types_event_level_tags(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(dlog_ev_level_tag(DLOG_ERROR), "ERROR");
    ASSERT_STR_EQ(dlog_ev_level_tag(DLOG_WARN), "WARN ");
    ASSERT_STR_EQ(dlog_ev_level_tag(DLOG_INFO), "INFO ");
    ASSERT_STR_EQ(dlog_ev_level_tag(DLOG_DEBUG), "DEBUG");
    ASSERT_STR_EQ(dlog_ev_level_tag(DLOG_TRACE), "TRACE");
    ASSERT_STR_EQ(dlog_ev_level_tag(DLOG_ISSUE), "ISSUE");
    for (int e = 0; e <= DLOG_TRACE; e++)
        ASSERT_EQ(dlog_ev_level_from_tag(dlog_ev_level_tag(e)), e);
    ASSERT_EQ(dlog_ev_level_from_tag("WARN"), (int)DLOG_WARN);   /* trimmed form */
    ASSERT_EQ(dlog_ev_level_from_tag("FATAL"), -1);
    ASSERT_EQ(dlog_ev_level_from_word("warn"), (int)DLOG_WARN);
    ASSERT_EQ(dlog_ev_level_from_word("Error"), (int)DLOG_ERROR);
    ASSERT_EQ(dlog_ev_level_from_word("trace"), (int)DLOG_TRACE);
    ASSERT_EQ(dlog_ev_level_from_word("issue"), -1);   /* not a query word */
    ASSERT_EQ(dlog_ev_level_from_word(NULL), -1);
    TEST_END();
}

/* ---- Time ---------------------------------------------------------------- */

int test_dlog_timestamp_format_positive_offset(void)
{
    TEST_BEGIN();
    /* 2026-10-17T13:03:22.418Z == 14:03:22.418+01:00 */
    DlogTm tm = { 2026, 10, 17, 13, 3, 22, 418 };
    long long ms = dlog_unix_ms_from_civil(&tm, 0);
    char ts[DLOG_TS_LEN + 4];
    dlog_format_timestamp(ms, 60, ts);
    ASSERT_STR_EQ(ts, "2026-10-17T14:03:22.418+01:00");
    ASSERT_EQ(strlen(ts), (size_t)DLOG_TS_LEN);
    TEST_END();
}

int test_dlog_timestamp_format_negative_zero_and_half_hour_offsets(void)
{
    TEST_BEGIN();
    DlogTm tm = { 2026, 1, 2, 3, 4, 5, 6 };
    long long ms = dlog_unix_ms_from_civil(&tm, 0);
    char ts[DLOG_TS_LEN + 4];
    dlog_format_timestamp(ms, 0, ts);
    ASSERT_STR_EQ(ts, "2026-01-02T03:04:05.006+00:00");   /* ms is zero padded */
    dlog_format_timestamp(ms, -300, ts);
    ASSERT_STR_EQ(ts, "2026-01-01T22:04:05.006-05:00");   /* rolls the date back */
    dlog_format_timestamp(ms, 330, ts);
    ASSERT_STR_EQ(ts, "2026-01-02T08:34:05.006+05:30");
    dlog_format_timestamp(ms, 570, ts);                    /* +09:30 */
    ASSERT_STR_EQ(ts, "2026-01-02T12:34:05.006+09:30");
    TEST_END();
}

int test_dlog_timestamp_leap_day_and_year_end(void)
{
    TEST_BEGIN();
    DlogTm leap = { 2028, 2, 29, 23, 59, 59, 999 };
    long long ms = dlog_unix_ms_from_civil(&leap, 0);
    char ts[DLOG_TS_LEN + 4];
    dlog_format_timestamp(ms, 0, ts);
    ASSERT_STR_EQ(ts, "2028-02-29T23:59:59.999+00:00");
    dlog_format_timestamp(ms + 1, 0, ts);
    ASSERT_STR_EQ(ts, "2028-03-01T00:00:00.000+00:00");
    DlogTm ye = { 2026, 12, 31, 23, 59, 59, 0 };
    ms = dlog_unix_ms_from_civil(&ye, 0);
    dlog_format_timestamp(ms + 1000, 0, ts);
    ASSERT_STR_EQ(ts, "2027-01-01T00:00:00.000+00:00");
    TEST_END();
}

int test_dlog_timestamp_epoch(void)
{
    TEST_BEGIN();
    char ts[DLOG_TS_LEN + 4];
    dlog_format_timestamp(0, 0, ts);
    ASSERT_STR_EQ(ts, "1970-01-01T00:00:00.000+00:00");
    DlogTm tm;
    dlog_civil_from_unix_ms(86400000LL * 365 + 1, 0, &tm);
    ASSERT_EQ(tm.year, 1971);
    ASSERT_EQ(tm.mon, 1);
    ASSERT_EQ(tm.day, 1);
    ASSERT_EQ(tm.ms, 1);
    TEST_END();
}

int test_dlog_timestamp_parse_round_trip(void)
{
    TEST_BEGIN();
    const long long samples[] = { 0, 1792241143123LL, 1730000000000LL, 951782400000LL };
    const int offsets[] = { 0, 60, -300, 330, -570 };
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        for (size_t j = 0; j < sizeof(offsets) / sizeof(offsets[0]); j++) {
            char ts[DLOG_TS_LEN + 4];
            dlog_format_timestamp(samples[i], offsets[j], ts);
            long long back = -1;
            ASSERT_TRUE(dlog_parse_timestamp(ts, &back));
            ASSERT_EQ(back, samples[i]);
        }
    }
    TEST_END();
}

int test_dlog_timestamp_parse_ignores_trailing_text(void)
{
    TEST_BEGIN();
    long long ms = 0;
    ASSERT_TRUE(dlog_parse_timestamp("2026-10-17T14:03:22.418+01:00 INFO  general - ui x", &ms));
    char ts[DLOG_TS_LEN + 4];
    dlog_format_timestamp(ms, 60, ts);
    ASSERT_STR_EQ(ts, "2026-10-17T14:03:22.418+01:00");
    TEST_END();
}

int test_dlog_timestamp_parse_rejects_malformed(void)
{
    TEST_BEGIN();
    long long ms = 7;
    ASSERT_FALSE(dlog_parse_timestamp(NULL, &ms));
    ASSERT_FALSE(dlog_parse_timestamp("", &ms));
    ASSERT_FALSE(dlog_parse_timestamp("garbage", &ms));
    ASSERT_FALSE(dlog_parse_timestamp("2026-10-17T14:03:22.418", &ms));       /* no offset */
    ASSERT_FALSE(dlog_parse_timestamp("2026-10-17 14:03:22.418+01:00", &ms)); /* no T */
    ASSERT_FALSE(dlog_parse_timestamp("2026-13-17T14:03:22.418+01:00", &ms)); /* month 13 */
    ASSERT_FALSE(dlog_parse_timestamp("2026-10-32T14:03:22.418+01:00", &ms)); /* day 32 */
    ASSERT_FALSE(dlog_parse_timestamp("2026-10-17T24:03:22.418+01:00", &ms)); /* hour 24 */
    ASSERT_FALSE(dlog_parse_timestamp("2026-10-17T14:60:22.418+01:00", &ms)); /* minute 60 */
    ASSERT_FALSE(dlog_parse_timestamp("2026-10-17T14:03:22.418*01:00", &ms)); /* sign */
    ASSERT_FALSE(dlog_parse_timestamp("2026-10-17T14:03:2x.418+01:00", &ms)); /* digit */
    ASSERT_EQ(ms, 7);   /* untouched on failure */
    ASSERT_TRUE(dlog_parse_timestamp("2026-10-17T14:03:22.418+01:00", NULL));   /* NULL out is fine */
    TEST_END();
}

/* ---- Names ----------------------------------------------------------------- */

int test_dlog_stem_name_uses_local_time_and_pid(void)
{
    TEST_BEGIN();
    DlogTm tm = { 2026, 10, 17, 13, 3, 10, 0 };
    long long ms = dlog_unix_ms_from_civil(&tm, 0);
    char stem[64];
    ASSERT_TRUE(dlog_stem_name(stem, sizeof(stem), ms, 60, 8124) > 0);
    ASSERT_STR_EQ(stem, "nutshell-20261017-140310-p8124");
    TEST_END();
}

int test_dlog_stem_name_does_not_fit(void)
{
    TEST_BEGIN();
    char tiny[10];
    ASSERT_EQ(dlog_stem_name(tiny, sizeof(tiny), 0, 0, 1), (size_t)0);
    ASSERT_EQ(dlog_stem_name(NULL, 0, 0, 0, 1), (size_t)0);
    TEST_END();
}

int test_dlog_file_name_part_numbers(void)
{
    TEST_BEGIN();
    char n[96];
    const char *stem = "nutshell-20261017-140310-p8124";
    ASSERT_TRUE(dlog_file_name(n, sizeof(n), stem, 0) > 0);
    ASSERT_STR_EQ(n, "nutshell-20261017-140310-p8124.log");
    ASSERT_TRUE(dlog_file_name(n, sizeof(n), stem, 1) > 0);
    ASSERT_STR_EQ(n, "nutshell-20261017-140310-p8124.log");
    ASSERT_TRUE(dlog_file_name(n, sizeof(n), stem, 2) > 0);
    ASSERT_STR_EQ(n, "nutshell-20261017-140310-p8124.2.log");
    ASSERT_TRUE(dlog_file_name(n, sizeof(n), stem, 13) > 0);
    ASSERT_STR_EQ(n, "nutshell-20261017-140310-p8124.13.log");
    char tiny[8];
    ASSERT_EQ(dlog_file_name(tiny, sizeof(tiny), stem, 1), (size_t)0);
    TEST_END();
}

int test_dlog_issue_file_name(void)
{
    TEST_BEGIN();
    char n[96];
    const char *stem = "nutshell-20261017-140310-p8124";
    ASSERT_TRUE(dlog_issue_file_name(n, sizeof(n), stem, 3, "log") > 0);
    ASSERT_STR_EQ(n, "nutshell-20261017-140310-p8124.issue3.log");
    ASSERT_TRUE(dlog_issue_file_name(n, sizeof(n), stem, 12, "png") > 0);
    ASSERT_STR_EQ(n, "nutshell-20261017-140310-p8124.issue12.png");
    ASSERT_EQ(dlog_issue_file_name(n, sizeof(n), stem, 1, NULL), (size_t)0);
    ASSERT_EQ(dlog_issue_file_name(n, 10, stem, 1, "log"), (size_t)0);
    TEST_END();
}

int test_dlog_name_parse_accepts_every_shape(void)
{
    TEST_BEGIN();
    DlogNameInfo i;
    ASSERT_TRUE(dlog_name_parse("nutshell-20261017-140310-p8124.log", &i));
    ASSERT_STR_EQ(i.stamp, "20261017-140310");
    ASSERT_EQ(i.pid, 8124u);
    ASSERT_EQ(i.part, 1u);
    ASSERT_EQ(i.issue, 0u);
    ASSERT_TRUE(dlog_name_parse("nutshell-20261017-140310-p8124.7.log", &i));
    ASSERT_EQ(i.part, 7u);
    ASSERT_TRUE(dlog_name_parse("nutshell-20261017-140310-p8124.issue4.log", &i));
    ASSERT_EQ(i.part, 0u);
    ASSERT_EQ(i.issue, 4u);
    ASSERT_FALSE(i.is_png);
    ASSERT_TRUE(dlog_name_parse("nutshell-20261017-140310-p8124.issue4.png", &i));
    ASSERT_TRUE(i.is_png);
    ASSERT_TRUE(dlog_name_parse("nutshell-20261017-140310-p8124.log", NULL));
    TEST_END();
}

int test_dlog_name_parse_rejects_lookalikes(void)
{
    TEST_BEGIN();
    ASSERT_FALSE(dlog_name_parse(NULL, NULL));
    ASSERT_FALSE(dlog_name_parse("", NULL));
    ASSERT_FALSE(dlog_name_parse("FORMAT.txt", NULL));
    ASSERT_FALSE(dlog_name_parse("nutshell.log", NULL));
    ASSERT_FALSE(dlog_name_parse("nutshell-20261017-140310.log", NULL));            /* no pid */
    ASSERT_FALSE(dlog_name_parse("nutshell-20261017-140310-p.log", NULL));          /* empty pid */
    ASSERT_FALSE(dlog_name_parse("nutshell-2026101-140310-p1.log", NULL));          /* 7 digits */
    ASSERT_FALSE(dlog_name_parse("nutshell-20261017-140310-p1.txt", NULL));
    ASSERT_FALSE(dlog_name_parse("nutshell-20261017-140310-p1.log.bak", NULL));
    ASSERT_FALSE(dlog_name_parse("Nutshell-20261017-140310-p1.log", NULL));         /* case */
    ASSERT_FALSE(dlog_name_parse("nutshell-20261017-140310-p1.1.log", NULL));       /* part 1 has no number */
    ASSERT_FALSE(dlog_name_parse("nutshell-20261017-140310-p1.0.log", NULL));
    ASSERT_FALSE(dlog_name_parse("nutshell-20261017-140310-p1.issue.log", NULL));   /* no number */
    ASSERT_FALSE(dlog_name_parse("nutshell-20261017-140310-p1.issue0.log", NULL));
    ASSERT_FALSE(dlog_name_parse("nutshell-20261017-140310-p1.issue2.txt", NULL));
    ASSERT_FALSE(dlog_name_parse("nutshell-20261017-140310-p1.2.png", NULL));
    TEST_END();
}

static int cmp_names(const char *a, const char *b)
{
    DlogNameInfo ia, ib;
    if (!dlog_name_parse(a, &ia) || !dlog_name_parse(b, &ib)) return 9999;
    return dlog_name_compare(&ia, &ib);
}

int test_dlog_name_compare_is_chronological(void)
{
    TEST_BEGIN();
    /* Plain strcmp would put ".2.log" before ".log" and "p10" before "p9". */
    ASSERT_TRUE(cmp_names("nutshell-20261017-140310-p1.log", "nutshell-20261017-140310-p1.2.log") < 0);
    ASSERT_TRUE(cmp_names("nutshell-20261017-140310-p1.2.log", "nutshell-20261017-140310-p1.10.log") < 0);
    ASSERT_TRUE(cmp_names("nutshell-20261017-140310-p9.log", "nutshell-20261017-140310-p10.log") < 0);
    ASSERT_TRUE(cmp_names("nutshell-20261017-140310-p5.log", "nutshell-20261017-140311-p1.log") < 0);
    ASSERT_TRUE(cmp_names("nutshell-20261016-235959-p5.log", "nutshell-20261017-000000-p1.log") < 0);
    ASSERT_TRUE(cmp_names("nutshell-20261017-140310-p1.log", "nutshell-20261017-140310-p1.issue1.log") < 0);
    ASSERT_TRUE(cmp_names("nutshell-20261017-140310-p1.issue1.log", "nutshell-20261017-140310-p1.issue2.log") < 0);
    ASSERT_TRUE(cmp_names("nutshell-20261017-140310-p1.issue2.log", "nutshell-20261017-140310-p1.issue10.log") < 0);
    ASSERT_EQ(cmp_names("nutshell-20261017-140310-p1.log", "nutshell-20261017-140310-p1.log"), 0);
    ASSERT_TRUE(cmp_names("nutshell-20261017-140311-p1.log", "nutshell-20261017-140310-p1.log") > 0);
    TEST_END();
}

/* ---- Folder resolution ------------------------------------------------------ */

static const char *fake_env(const char *name, void *ctx)
{
    (void)ctx;
    if (strcmp(name, "TEMP") == 0) return "C:\\Users\\me\\AppData\\Local\\Temp";
    if (strcmp(name, "EMPTY") == 0) return "";
    return NULL;
}

static int resolve(const char *setting, const char *exe, char *out, size_t cap)
{
    return dlog_resolve_dir_ex(setting, exe, fake_env, NULL, out, cap);
}

int test_dlog_resolve_dir_empty_means_logs_beside_exe(void)
{
    TEST_BEGIN();
    char out[260];
    ASSERT_TRUE(resolve("", "C:\\Tools\\nutshell", out, sizeof(out)));
    ASSERT_STR_EQ(out, "C:\\Tools\\nutshell\\logs");
    ASSERT_TRUE(resolve(NULL, "C:\\Tools\\nutshell", out, sizeof(out)));
    ASSERT_STR_EQ(out, "C:\\Tools\\nutshell\\logs");
    TEST_END();
}

int test_dlog_resolve_dir_relative_joins_exe_dir(void)
{
    TEST_BEGIN();
    char out[260];
    ASSERT_TRUE(resolve("logs", "C:\\Tools\\nutshell", out, sizeof(out)));
    ASSERT_STR_EQ(out, "C:\\Tools\\nutshell\\logs");
    ASSERT_TRUE(resolve("diag\\run1", "C:\\Tools\\nutshell", out, sizeof(out)));
    ASSERT_STR_EQ(out, "C:\\Tools\\nutshell\\diag\\run1");
    ASSERT_TRUE(resolve("..\\shared", "C:\\Tools\\nutshell\\", out, sizeof(out)));
    ASSERT_STR_EQ(out, "C:\\Tools\\nutshell\\..\\shared");   /* exe_dir trailing slash not doubled */
    TEST_END();
}

int test_dlog_resolve_dir_relative_needs_exe_dir(void)
{
    TEST_BEGIN();
    char out[260];
    ASSERT_FALSE(resolve("logs", "", out, sizeof(out)));
    ASSERT_FALSE(resolve("logs", NULL, out, sizeof(out)));
    ASSERT_FALSE(resolve("", NULL, out, sizeof(out)));
    /* An absolute setting does not need it. */
    ASSERT_TRUE(resolve("D:\\logs", NULL, out, sizeof(out)));
    ASSERT_STR_EQ(out, "D:\\logs");
    TEST_END();
}

int test_dlog_resolve_dir_absolute_forms(void)
{
    TEST_BEGIN();
    char out[260];
    ASSERT_TRUE(resolve("D:\\logs", "C:\\x", out, sizeof(out)));
    ASSERT_STR_EQ(out, "D:\\logs");
    ASSERT_TRUE(resolve("d:/logs/nutshell", "C:\\x", out, sizeof(out)));
    ASSERT_STR_EQ(out, "d:/logs/nutshell");
    ASSERT_TRUE(resolve("\\\\server\\share\\logs", "C:\\x", out, sizeof(out)));
    ASSERT_STR_EQ(out, "\\\\server\\share\\logs");
    ASSERT_TRUE(resolve("\\rooted\\logs", "C:\\x", out, sizeof(out)));
    ASSERT_STR_EQ(out, "\\rooted\\logs");
    ASSERT_TRUE(resolve("/rooted/logs", "C:\\x", out, sizeof(out)));
    ASSERT_STR_EQ(out, "/rooted/logs");
    TEST_END();
}

int test_dlog_resolve_dir_trims_trailing_separators(void)
{
    TEST_BEGIN();
    char out[260];
    ASSERT_TRUE(resolve("D:\\logs\\", "C:\\x", out, sizeof(out)));
    ASSERT_STR_EQ(out, "D:\\logs");
    ASSERT_TRUE(resolve("D:\\logs\\\\", "C:\\x", out, sizeof(out)));
    ASSERT_STR_EQ(out, "D:\\logs");
    ASSERT_TRUE(resolve("logs/", "C:\\x", out, sizeof(out)));
    ASSERT_STR_EQ(out, "C:\\x\\logs");
    ASSERT_TRUE(resolve("D:\\", "C:\\x", out, sizeof(out)));
    ASSERT_STR_EQ(out, "D:\\");                                /* drive root keeps its one */
    TEST_END();
}

int test_dlog_resolve_dir_reuses_forward_slash_style(void)
{
    TEST_BEGIN();
    char out[260];
    ASSERT_TRUE(resolve("logs", "build/exe", out, sizeof(out)));
    ASSERT_STR_EQ(out, "build/exe/logs");
    ASSERT_TRUE(resolve("logs", "C:\\mixed/dir\\here", out, sizeof(out)));
    ASSERT_STR_EQ(out, "C:\\mixed/dir\\here\\logs");           /* has a backslash: use it */
    TEST_END();
}

int test_dlog_resolve_dir_expands_environment(void)
{
    TEST_BEGIN();
    char out[260];
    ASSERT_TRUE(resolve("%TEMP%\\nutshell", "C:\\x", out, sizeof(out)));
    ASSERT_STR_EQ(out, "C:\\Users\\me\\AppData\\Local\\Temp\\nutshell");
    ASSERT_TRUE(resolve("%UNSET%\\logs", "C:\\x", out, sizeof(out)));
    ASSERT_STR_EQ(out, "C:\\x\\%UNSET%\\logs");                 /* unknown stays literal, relative */
    ASSERT_TRUE(resolve("100%\\done", "C:\\x", out, sizeof(out)));
    ASSERT_STR_EQ(out, "C:\\x\\100%\\done");                    /* lone % stays */
    ASSERT_TRUE(resolve("D:\\a%EMPTY%b", "C:\\x", out, sizeof(out)));
    ASSERT_STR_EQ(out, "D:\\ab");
    TEST_END();
}

int test_dlog_resolve_dir_overlong_is_refused(void)
{
    TEST_BEGIN();
    char out[40];
    ASSERT_FALSE(resolve("a-very-long-relative-folder-name", "C:\\Tools\\nutshell", out, sizeof(out)));
    char big[600];
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    char out2[260];
    ASSERT_FALSE(resolve(big, "C:\\x", out2, sizeof(out2)));
    ASSERT_FALSE(resolve("logs", "C:\\x", out, 0));
    ASSERT_FALSE(resolve("logs", "C:\\x", NULL, 40));
    /* exactly fits */
    char exact[16];
    ASSERT_TRUE(resolve("D:\\123456789012", "C:\\x", exact, sizeof(exact)));   /* 15 chars + NUL */
    ASSERT_FALSE(resolve("D:\\1234567890123", "C:\\x", exact, sizeof(exact)));
    TEST_END();
}

int test_dlog_resolve_dir_default_lookup_does_not_crash(void)
{
    TEST_BEGIN();
    char out[260];
    ASSERT_TRUE(dlog_resolve_dir("logs", "C:\\Tools\\nutshell", out, sizeof(out)));
    ASSERT_STR_EQ(out, "C:\\Tools\\nutshell\\logs");
    TEST_END();
}

int test_dlog_fallback_dir(void)
{
    TEST_BEGIN();
    char out[260];
    ASSERT_TRUE(dlog_fallback_dir("C:\\Users\\me\\AppData\\Local\\Temp", out, sizeof(out)));
    ASSERT_STR_EQ(out, "C:\\Users\\me\\AppData\\Local\\Temp\\Nutshell\\logs");
    ASSERT_TRUE(dlog_fallback_dir("C:\\Users\\me\\AppData\\Local\\Temp\\", out, sizeof(out)));
    ASSERT_STR_EQ(out, "C:\\Users\\me\\AppData\\Local\\Temp\\Nutshell\\logs");
    ASSERT_FALSE(dlog_fallback_dir("", out, sizeof(out)));
    ASSERT_FALSE(dlog_fallback_dir(NULL, out, sizeof(out)));
    char tiny[12];
    ASSERT_FALSE(dlog_fallback_dir("C:\\Temp", tiny, sizeof(tiny)));
    TEST_END();
}

int test_dlog_join(void)
{
    TEST_BEGIN();
    char out[64];
    ASSERT_TRUE(dlog_join(out, sizeof(out), "C:\\logs", "a.log"));
    ASSERT_STR_EQ(out, "C:\\logs\\a.log");
    ASSERT_TRUE(dlog_join(out, sizeof(out), "C:\\logs\\", "a.log"));
    ASSERT_STR_EQ(out, "C:\\logs\\a.log");
    ASSERT_TRUE(dlog_join(out, sizeof(out), "build/logs", "a.log"));
    ASSERT_STR_EQ(out, "build/logs/a.log");
    ASSERT_TRUE(dlog_join(out, sizeof(out), "D:\\", "a.log"));
    ASSERT_STR_EQ(out, "D:\\a.log");
    ASSERT_FALSE(dlog_join(out, 8, "C:\\logs", "a.log"));
    ASSERT_FALSE(dlog_join(out, sizeof(out), NULL, "a.log"));
    TEST_END();
}

/* ---- Rotation caps ------------------------------------------------------------ */

int test_dlog_file_cap_bytes(void)
{
    TEST_BEGIN();
    ASSERT_EQ(dlog_file_cap_bytes(0, 10), 1ULL * 1024 * 1024);      /* off: 1 MB whatever it says */
    ASSERT_EQ(dlog_file_cap_bytes(0, 100), 1ULL * 1024 * 1024);
    ASSERT_EQ(dlog_file_cap_bytes(1, 10), 10ULL * 1024 * 1024);
    ASSERT_EQ(dlog_file_cap_bytes(1, 1), 1ULL * 1024 * 1024);
    ASSERT_EQ(dlog_file_cap_bytes(1, 0), 1ULL * 1024 * 1024);       /* clamped up */
    ASSERT_EQ(dlog_file_cap_bytes(1, -7), 1ULL * 1024 * 1024);
    ASSERT_EQ(dlog_file_cap_bytes(1, 100), 100ULL * 1024 * 1024);
    ASSERT_EQ(dlog_file_cap_bytes(1, 5000), 100ULL * 1024 * 1024);  /* clamped down */
    TEST_END();
}

int test_dlog_total_cap_bytes(void)
{
    TEST_BEGIN();
    ASSERT_EQ(dlog_total_cap_bytes(50), 50ULL * 1024 * 1024);
    ASSERT_EQ(dlog_total_cap_bytes(5), 5ULL * 1024 * 1024);
    ASSERT_EQ(dlog_total_cap_bytes(1), 5ULL * 1024 * 1024);
    ASSERT_EQ(dlog_total_cap_bytes(-1), 5ULL * 1024 * 1024);
    ASSERT_EQ(dlog_total_cap_bytes(5000), 5000ULL * 1024 * 1024);
    ASSERT_EQ(dlog_total_cap_bytes(99999), 5000ULL * 1024 * 1024);
    TEST_END();
}

int test_dlog_should_rotate_threshold(void)
{
    TEST_BEGIN();
    ASSERT_FALSE(dlog_should_rotate(0, 100, 1000));
    ASSERT_FALSE(dlog_should_rotate(900, 100, 1000));    /* lands exactly on the cap */
    ASSERT_TRUE(dlog_should_rotate(901, 100, 1000));
    ASSERT_TRUE(dlog_should_rotate(1000, 1, 1000));
    ASSERT_FALSE(dlog_should_rotate(0, 5000, 1000));     /* an empty file takes any single line */
    ASSERT_TRUE(dlog_should_rotate(1, 5000, 1000));
    TEST_END();
}

/* ---- Retention ------------------------------------------------------------------ */

static DlogFileInfo mkfile(const char *name, unsigned long long size)
{
    DlogFileInfo f;
    memset(&f, 0, sizeof(f));
    (void)snprintf(f.name, sizeof(f.name), "%s", name);
    f.size = size;
    return f;
}

int test_dlog_retention_deletes_oldest_first_until_under_cap(void)
{
    TEST_BEGIN();
    DlogFileInfo fs[4];
    fs[0] = mkfile("nutshell-20261017-120000-p1.log", 40);
    fs[1] = mkfile("nutshell-20261015-120000-p1.log", 40);   /* oldest */
    fs[2] = mkfile("nutshell-20261016-120000-p1.log", 40);
    fs[3] = mkfile("nutshell-20261018-120000-p1.log", 40);   /* current */
    size_t v[4];
    size_t n = dlog_retention_pick(fs, 4, 100, "nutshell-20261018-120000-p1.log", v, 4);
    ASSERT_EQ(n, (size_t)2);          /* 160 -> 120 -> 80 */
    ASSERT_EQ(v[0], (size_t)1);
    ASSERT_EQ(v[1], (size_t)2);
    TEST_END();
}

int test_dlog_retention_never_deletes_current_even_over_cap(void)
{
    TEST_BEGIN();
    DlogFileInfo fs[2];
    fs[0] = mkfile("nutshell-20261017-120000-p1.log", 500);
    fs[1] = mkfile("nutshell-20261016-120000-p1.log", 10);
    size_t v[2];
    size_t n = dlog_retention_pick(fs, 2, 100, "nutshell-20261017-120000-p1.log", v, 2);
    ASSERT_EQ(n, (size_t)1);
    ASSERT_EQ(v[0], (size_t)1);       /* the old one goes, the current stays though over cap */
    n = dlog_retention_pick(fs, 1, 100, "nutshell-20261017-120000-p1.log", v, 2);
    ASSERT_EQ(n, (size_t)0);
    TEST_END();
}

int test_dlog_retention_under_cap_deletes_nothing(void)
{
    TEST_BEGIN();
    DlogFileInfo fs[2];
    fs[0] = mkfile("nutshell-20261017-120000-p1.log", 30);
    fs[1] = mkfile("nutshell-20261016-120000-p1.log", 70);
    size_t v[2];
    ASSERT_EQ(dlog_retention_pick(fs, 2, 100, "nutshell-20261017-120000-p1.log", v, 2), (size_t)0);
    ASSERT_EQ(dlog_retention_pick(fs, 0, 100, NULL, v, 2), (size_t)0);
    ASSERT_EQ(dlog_retention_pick(NULL, 0, 100, NULL, v, 2), (size_t)0);
    TEST_END();
}

int test_dlog_retention_ignores_foreign_files(void)
{
    TEST_BEGIN();
    DlogFileInfo fs[4];
    fs[0] = mkfile("FORMAT.txt", 5000);
    fs[1] = mkfile("notes.txt", 5000);
    fs[2] = mkfile("nutshell.log", 5000);
    fs[3] = mkfile("nutshell-20261016-120000-p1.log", 50);
    size_t v[4];
    size_t n = dlog_retention_pick(fs, 4, 10, NULL, v, 4);
    ASSERT_EQ(n, (size_t)1);          /* only our own file is ever a victim */
    ASSERT_EQ(v[0], (size_t)3);
    /* Foreign bytes do not count towards the total either. */
    ASSERT_EQ(dlog_retention_pick(fs, 3, 10, NULL, v, 4), (size_t)0);
    TEST_END();
}

int test_dlog_retention_orders_parts_issues_and_pids_naturally(void)
{
    TEST_BEGIN();
    DlogFileInfo fs[5];
    fs[0] = mkfile("nutshell-20261017-120000-p10.log", 10);
    fs[1] = mkfile("nutshell-20261017-120000-p9.2.log", 10);
    fs[2] = mkfile("nutshell-20261017-120000-p9.log", 10);
    fs[3] = mkfile("nutshell-20261017-120000-p9.issue1.log", 10);
    fs[4] = mkfile("nutshell-20261017-120000-p9.issue1.png", 10);
    size_t v[5];
    size_t n = dlog_retention_pick(fs, 5, 0, "nutshell-20261017-120000-p10.log", v, 5);
    ASSERT_EQ(n, (size_t)4);
    ASSERT_EQ(v[0], (size_t)2);       /* p9 .log */
    ASSERT_EQ(v[1], (size_t)1);       /* p9 .2.log */
    ASSERT_EQ(v[2], (size_t)3);       /* p9 issue1.log */
    ASSERT_EQ(v[3], (size_t)4);       /* p9 issue1.png */
    TEST_END();
}

int test_dlog_retention_next_skips_failed_deletions(void)
{
    TEST_BEGIN();
    DlogFileInfo fs[3];
    fs[0] = mkfile("nutshell-20261014-120000-p1.log", 40);   /* held open elsewhere */
    fs[1] = mkfile("nutshell-20261015-120000-p1.log", 40);
    fs[2] = mkfile("nutshell-20261017-120000-p1.log", 40);   /* current */
    unsigned char state[3] = { 0, 0, 0 };
    const char *cur = "nutshell-20261017-120000-p1.log";
    int i = dlog_retention_next(fs, 3, cur, 100, state);
    ASSERT_EQ(i, 0);
    state[0] = 2;                      /* delete failed: still counts, never retried */
    i = dlog_retention_next(fs, 3, cur, 100, state);
    ASSERT_EQ(i, 1);
    state[1] = 1;                      /* deleted: 80 left, but file 0 still counts */
    i = dlog_retention_next(fs, 3, cur, 100, state);
    ASSERT_EQ(i, -1);                  /* 80 <= 100 */
    i = dlog_retention_next(fs, 3, cur, 50, state);
    ASSERT_EQ(i, -1);                  /* over cap but nothing deletable left */
    TEST_END();
}

int test_dlog_files_total_counts_only_ours(void)
{
    TEST_BEGIN();
    DlogFileInfo fs[3];
    fs[0] = mkfile("nutshell-20261017-120000-p1.log", 100);
    fs[1] = mkfile("FORMAT.txt", 999);
    fs[2] = mkfile("nutshell-20261017-120000-p1.issue1.png", 50);
    size_t count = 0;
    ASSERT_EQ(dlog_files_total(fs, 3, &count), 150ULL);
    ASSERT_EQ(count, (size_t)2);
    ASSERT_EQ(dlog_files_total(fs, 0, NULL), 0ULL);
    TEST_END();
}

/* ---- FORMAT.txt ------------------------------------------------------------------ */

int test_dlog_format_text_describes_the_format(void)
{
    TEST_BEGIN();
    char buf[8192];
    size_t n = dlog_format_text(buf, sizeof(buf), "1.2.36");
    ASSERT_TRUE(n > 500);
    ASSERT_EQ(strlen(buf), n);
    ASSERT_TRUE(strstr(buf, "1.2.36") != NULL);
    char *nl = strchr(buf, '\n');
    ASSERT_NOT_NULL(nl);
    if (nl) { *nl = '\0'; ASSERT_TRUE(strstr(buf, "1.2.36") != NULL); *nl = '\n'; }  /* version on line 1 */
    for (int c = 0; c < DLOG_CAT_COUNT; c++)
        ASSERT_TRUE(strstr(buf, dlog_cat_name(c)) != NULL);
    ASSERT_TRUE(strstr(buf, "issue.mark") != NULL);
    ASSERT_TRUE(strstr(buf, "REDACTED") != NULL);
    ASSERT_TRUE(strstr(buf, "repeat") != NULL);
    ASSERT_TRUE(strstr(buf, "app.start") != NULL);
    TEST_END();
}

int test_dlog_format_text_cut_to_cap_and_null_version(void)
{
    TEST_BEGIN();
    char small[64];
    size_t n = dlog_format_text(small, sizeof(small), "9.9.9");
    ASSERT_EQ(n, strlen(small));
    ASSERT_TRUE(n < sizeof(small));
    char buf[8192];
    ASSERT_TRUE(dlog_format_text(buf, sizeof(buf), NULL) > 0);
    ASSERT_EQ(dlog_format_text(NULL, 0, "1"), (size_t)0);
    TEST_END();
}
