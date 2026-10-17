/* tests/test_dlog_live.c -- dlog_read_log() against the running logger (fake
 * platform), the recorder capture level, DLOG_EV, and that a line nothing wants
 * costs nothing. */
#include "test_framework.h"
#include "fake_dlog.h"
#include "dlog.h"
#include "dlog_query.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int begin(void)
{
    fk_reset();
    return fk_start(0, 0);
}

static void end(void)
{
    dlog_shutdown();
    fk_free();
}

static void apply(int enabled, int level, int recorder, int rec_level)
{
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = enabled;
    for (int i = 0; i < DLOG_CAT_COUNT; i++) c.levels[i] = (unsigned char)level;
    c.recorder = recorder;
    c.recorder_level = (unsigned char)rec_level;
    dlog_apply(&c);
}

static int has(const char *h, const char *n) { return strstr(h, n) != NULL; }

int test_dlog_live_read_log_flushes_and_reads_the_open_file(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    apply(1, DLOG_LVL_INFO, 1, DLOG_LVL_TRACE);
    DLOG(DLOG_RENDER, DLOG_INFO, 2, "paint", "rows=%d", 24);
    DLOG(DLOG_AI, DLOG_ERROR, 0, "ai.fail", "code=%d", 3);
    DlogQuery q;
    dlog_query_defaults(&q);
    char out[8192];
    size_t n = dlog_read_log(&q, out, sizeof(out));          /* nothing was pumped by the test */
    ASSERT_EQ(n, strlen(out));
    ASSERT_TRUE(has(out, "paint rows=24"));
    ASSERT_TRUE(has(out, "ai.fail code=3"));
    ASSERT_TRUE(has(out, FK_STEM ".log"));
    q.category = DLOG_AI;
    (void)dlog_read_log(&q, out, sizeof(out));
    ASSERT_TRUE(has(out, "ai.fail"));
    ASSERT_FALSE(has(out, "paint rows"));
    end();
    TEST_END();
}

int test_dlog_live_read_log_since_follows_the_platform_clock(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    apply(1, DLOG_LVL_INFO, 1, DLOG_LVL_TRACE);
    DLOG(DLOG_GENERAL, DLOG_INFO, 0, "early", "n=%d", 1);
    (void)dlog_pump();
    fk_advance(40u * 60u * 1000u);
    DLOG(DLOG_GENERAL, DLOG_INFO, 0, "late", "n=%d", 2);
    DlogQuery q;
    dlog_query_defaults(&q);                                   /* the last 30 minutes */
    char out[8192];
    (void)dlog_read_log(&q, out, sizeof(out));
    ASSERT_TRUE(has(out, " late "));
    ASSERT_FALSE(has(out, " early "));
    q.since_minutes = 60;
    (void)dlog_read_log(&q, out, sizeof(out));
    ASSERT_TRUE(has(out, " early "));
    end();
    TEST_END();
}

int test_dlog_live_read_log_falls_back_to_the_recorder_with_no_file(void)
{
    TEST_BEGIN();
    fk_reset();
    (void)snprintf(g_fk.deny_prefix, sizeof(g_fk.deny_prefix), "C:\\");
    ASSERT_TRUE(fk_start(0, 0));
    DLOG(DLOG_GENERAL, DLOG_ERROR, 0, "only.in.memory", "n=%d", 1);
    DlogQuery q;
    dlog_query_defaults(&q);
    char out[8192];
    (void)dlog_read_log(&q, out, sizeof(out));
    ASSERT_TRUE(has(out, "only.in.memory"));
    ASSERT_TRUE(has(out, "log.memory_only"));                   /* and it says why */
    end();
    TEST_END();
}

int test_dlog_live_read_log_with_nothing_logged_yet_uses_the_recorder(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    DlogQuery q;
    dlog_query_defaults(&q);
    char out[1024];
    (void)dlog_read_log(&q, out, sizeof(out));
    ASSERT_TRUE(has(out, "0 matching lines"));
    ASSERT_EQ(g_fk.ndirs, 0);                                    /* reading never creates the folder */
    end();
    TEST_END();
}

int test_dlog_live_around_issue_reads_the_recorder_dump(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    apply(0, DLOG_LVL_INFO, 1, DLOG_LVL_TRACE);                  /* debugging off: errors only to the file */
    DLOG(DLOG_RENDER, DLOG_WARN, 2, "stale.rows", "dirty=%d", 73);
    fk_advance(1000);
    DlogSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.has_term = 1;
    snap.term.sid = 2;
    snap.term.kind = "ssh";
    snap.term.rows = 24;
    snap.term.cols = 80;
    DlogIssue iss;
    ASSERT_TRUE(dlog_issue_mark(&snap, 0, &iss));
    DlogQuery q;
    dlog_query_defaults(&q);
    q.around_issue = 0;
    char out[8192];
    (void)dlog_read_log(&q, out, sizeof(out));
    ASSERT_TRUE(has(out, "issue 1"));
    ASSERT_TRUE(has(out, "stale.rows"));
    ASSERT_TRUE(has(out, "issue.mark n=1"));
    ASSERT_TRUE(has(out, "issue.term"));
    ASSERT_TRUE(has(out, "grid=24x80"));
    q.around_issue = 3;
    (void)dlog_read_log(&q, out, sizeof(out));
    ASSERT_TRUE(has(out, "no issue 3"));
    end();
    TEST_END();
}

int test_dlog_live_summary_is_small(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    apply(1, DLOG_LVL_TRACE, 1, DLOG_LVL_TRACE);
    for (int i = 0; i < 300; i++) DLOG(DLOG_RENDER, DLOG_TRACE, 1, "paint", "n=%d", i);
    DlogQuery q;
    dlog_query_defaults(&q);
    q.summary = 1;
    q.category = DLOG_RENDER;
    char out[DLOG_QUERY_OUT_MAX + 1];
    size_t n = dlog_read_log(&q, out, sizeof(out));
    ASSERT_TRUE(n < 1200);
    ASSERT_TRUE(has(out, "300 matching lines"));
    ASSERT_TRUE(has(out, "paint=300"));
    end();
    TEST_END();
}

/* ---- recorder capture level ---------------------------------------------------------------- */

int test_dlog_recorder_level_sets_what_the_recorder_captures(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    apply(1, DLOG_LVL_INFO, 1, DLOG_LVL_DEBUG);
    ASSERT_EQ(dlog_capture_level(DLOG_RENDER), (int)DLOG_DEBUG);
    apply(1, DLOG_LVL_INFO, 1, DLOG_LVL_TRACE);
    ASSERT_EQ(dlog_capture_level(DLOG_RENDER), (int)DLOG_TRACE);
    apply(1, DLOG_LVL_INFO, 1, DLOG_LVL_OFF);
    ASSERT_EQ(dlog_capture_level(DLOG_RENDER), (int)DLOG_INFO);   /* the recorder asks for nothing extra */
    apply(1, DLOG_LVL_INFO, 1, DLOG_LVL_ERRORS);
    ASSERT_EQ(dlog_capture_level(DLOG_RENDER), (int)DLOG_INFO);   /* never below what the file wants */
    apply(1, DLOG_LVL_TRACE, 1, DLOG_LVL_DEBUG);
    ASSERT_EQ(dlog_capture_level(DLOG_RENDER), (int)DLOG_TRACE);  /* nor below the file's own level */
    apply(1, DLOG_LVL_INFO, 0, DLOG_LVL_TRACE);                   /* recorder unticked */
    ASSERT_EQ(dlog_capture_level(DLOG_RENDER), (int)DLOG_INFO);
    end();
    TEST_END();
}

int test_dlog_recorder_level_debug_keeps_debug_but_not_trace(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    apply(1, DLOG_LVL_INFO, 1, DLOG_LVL_DEBUG);
    DLOG_EV(DLOG_TERM, DLOG_DEBUG, 0, "dbg.line");
    DLOG_EV(DLOG_TERM, DLOG_TRACE, 0, "trc.line");
    char rec[2048];
    size_t n = dlog_recorder_copy(rec, sizeof(rec) - 1);
    rec[n] = '\0';
    ASSERT_TRUE(has(rec, "dbg.line"));
    ASSERT_FALSE(has(rec, "trc.line"));
    const char *t = fk_text(FK_LOG_FILE);
    ASSERT_TRUE(t == NULL || !has(t, "dbg.line"));                /* the file stays at Info */
    end();
    TEST_END();
}

/* ---- DLOG_EV and the cost of a skipped line -------------------------------------------------------- */

int test_dlog_ev_writes_a_fieldless_event(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 5, "just.an.event");
    (void)dlog_pump();
    const char *t = fk_text(FK_LOG_FILE);
    ASSERT_NOT_NULL(t);
    if (t) ASSERT_TRUE(has(t, " ERROR general s5 ui just.an.event\n"));
    end();
    TEST_END();
}

int test_dlog_a_line_nobody_wants_never_touches_the_platform(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());                                          /* off: errors only */
    int clock = g_fk.clock_calls;
    int locks = g_fk.lock_count;
    for (int i = 0; i < 1000; i++) {
        DLOG(DLOG_RENDER, DLOG_TRACE, 1, "paint", "n=%d", i);
        DLOG_EV(DLOG_RENDER, DLOG_DEBUG, 1, "dbg");
        DLOG_CONTENT(DLOG_TERM, DLOG_TRACE, 1, "term.rx", "data", "abc", 3);
    }
    ASSERT_EQ(g_fk.clock_calls, clock);
    ASSERT_EQ(g_fk.lock_count, locks);
    end();
    TEST_END();
}

int test_dlog_issue_lines_are_never_collapsed(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    DLOG_EV(DLOG_GENERAL, DLOG_ISSUE, 0, "issue.twice");
    DLOG_EV(DLOG_GENERAL, DLOG_ISSUE, 0, "issue.twice");
    DLOG_EV(DLOG_GENERAL, DLOG_ISSUE, 0, "issue.twice");
    (void)dlog_pump();
    const char *t = fk_text(FK_LOG_FILE);
    ASSERT_NOT_NULL(t);
    if (t) {
        int n = 0;
        for (const char *p = t; (p = strstr(p, "issue.twice")) != NULL; p++) n++;
        ASSERT_EQ(n, 3);
        ASSERT_FALSE(has(t, " repeat "));
    }
    end();
    TEST_END();
}

int test_dlog_apply_clamps_out_of_range_values(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin());
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = 5;
    c.levels[DLOG_AI] = 200;
    c.recorder_level = 99;
    c.max_file_mb = 100000;
    c.max_total_mb = -3;
    dlog_apply(&c);
    ASSERT_TRUE(dlog_enabled());
    ASSERT_EQ(dlog_capture_level(DLOG_AI), (int)DLOG_TRACE);       /* default Info file level, Trace recorder */
    DLOG(DLOG_AI, DLOG_INFO, 0, "ai.line", "x=%d", 1);
    (void)dlog_pump();
    ASSERT_TRUE(has(fk_text(FK_LOG_FILE), "ai.line"));
    dlog_apply(NULL);                                              /* ignored */
    end();
    TEST_END();
}
