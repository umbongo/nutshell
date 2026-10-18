/* tests/test_dlog_review.c -- regressions for the code-review findings on the
 * dlog core: OSC 52 tracking past the output cut, known secrets split across
 * reads, repeat collapsing of volatile fields, the crash path, shutdown with a
 * stuck writer, a failed write keeping its chunk, the idle writer, issue marks
 * handing their work to the writer, key-name redaction of real call sites.
 * Everything runs on the fake platform (tests/fake_dlog.h). */
#include "test_framework.h"
#include "fake_dlog.h"
#include "dlog.h"
#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned dlog_writer_wait_ms(void);     /* dlog_internal.h; the test only needs these */
unsigned dlog_secret_epoch(void);
size_t   dlog_streams_held_nonzero(void);

/* ---- helpers --------------------------------------------------------------- */

static int begin(size_t queue_bytes, size_t recorder_bytes)
{
    fk_reset();
    return fk_start(queue_bytes, recorder_bytes);
}

static void end(void)
{
    dlog_shutdown();
    fk_free();
}

static void apply(int enabled, int level, int content)
{
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = enabled;
    for (int i = 0; i < DLOG_CAT_COUNT; i++) c.levels[i] = (unsigned char)level;
    c.include_content = content;
    dlog_apply(&c);
}

static const char *logtext(void)
{
    (void)dlog_pump();
    return fk_text(FK_LOG_FILE);
}

static int count_in(const char *text, const char *needle)
{
    return fk_count_lines_containing(text, needle);
}

static int has(const char *text, const char *needle) { return text && strstr(text, needle) != NULL; }

/* Starts the logger on the threaded fake: the writer entry point is recorded, never run. */
static int start_threaded(int reset)
{
    if (reset) fk_reset();
    DlogStartInfo si;
    memset(&si, 0, sizeof(si));
    si.exe_dir = "C:\\Tools\\nutshell";
    si.exe_path = "C:\\Tools\\nutshell\\nutshell.exe";
    si.version = "1.2.36";
    si.pid = 8124;
    return dlog_start_with(fk_platform_thread(), &si);
}

#define READ_TERM(sid, data, n) DLOG_CONTENT(DLOG_TERM, DLOG_TRACE, (sid), "term.rx", "data", (data), (n))

/* ---- finding 1: OSC 52 state past the output cut ------------------------------------------------ */

int test_dlog_review_osc52_introducer_in_the_unwritten_tail_withholds_the_next_read(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply(1, DLOG_LVL_TRACE, 1);
    static char r1[4096];
    memset(r1, 'a', 3950);
    memcpy(r1 + 3950, "\x1b]52;c;", 7);              /* the introducer sits past the first 3900 bytes */
    memset(r1 + 3957, 'Q', sizeof(r1) - 3957);
    READ_TERM(3, r1, sizeof(r1));
    READ_TERM(3, "RVJFVEJBU0U2NA==\x07" "after", 22);
    const char *t = logtext();
    ASSERT_NOT_NULL(t);
    ASSERT_FALSE(has(t, "RVJFVEJBU0U2NA"));
    ASSERT_TRUE(has(t, "[osc52 bytes="));
    ASSERT_TRUE(has(t, "after"));
    end();
    TEST_END();
}

int test_dlog_review_osc52_introducer_after_the_output_filled_up_withholds_the_next_read(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply(1, DLOG_LVL_TRACE, 1);
    static char r1[3100];
    memset(r1, 0x01, 3000);                          /* 4 output bytes each: the output is full long before */
    memcpy(r1 + 3000, "\x1b]52;c;QQ", 9);
    READ_TERM(7, r1, 3009);
    READ_TERM(7, "SEVMTE9XT1JMRA==\x1b\\" "tail", 23);
    const char *t = logtext();
    ASSERT_FALSE(has(t, "SEVMTE9XT1JMRA"));
    ASSERT_TRUE(has(t, "tail"));
    end();
    TEST_END();
}

int test_dlog_review_escape_bytes_keeps_tracking_after_the_output_is_full(void)
{
    TEST_BEGIN();
    DlogEscState st;
    dlog_esc_init(&st);
    char in[200];
    memset(in, 'a', 120);
    memcpy(in + 120, "\x1b]52;c;", 7);
    char out[64];
    size_t used = 0;
    (void)dlog_escape_bytes(&st, in, 127, out, sizeof(out), &used);
    ASSERT_TRUE(used < 127);                          /* the output did fill up ... */
    char out2[128];
    size_t o = dlog_escape_bytes(&st, "SECRETPAYLOAD" "\x07" "z", 15, out2, sizeof(out2), &used);
    ASSERT_TRUE(strstr(out2, "SECRETPAYLOAD") == NULL);   /* ... yet the introducer was seen */
    ASSERT_TRUE(strstr(out2, "[osc52 bytes=") != NULL);
    ASSERT_EQ(out2[o - 1], 'z');
    TEST_END();
}

/* ---- finding 13: the OSC state is per exact session id ---------------------------------------------- */

int test_dlog_review_osc52_state_is_per_exact_session_not_sid_modulo_16(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply(1, DLOG_LVL_TRACE, 1);
    READ_TERM(1, "x\x1b]52;c;AAAA", 12);               /* session 1 is inside a payload */
    READ_TERM(17, "hello from seventeen", 20);         /* 17 % 16 == 1: must not be swallowed */
    READ_TERM(1, "BBBB\x07" "after1", 11);
    const char *t = logtext();
    ASSERT_TRUE(has(t, "hello from seventeen"));
    ASSERT_FALSE(has(t, "BBBB"));
    end();
    TEST_END();
}

int test_dlog_review_many_sessions_evict_old_stream_state_without_trouble(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply(1, DLOG_LVL_TRACE, 1);
    for (unsigned sid = 1; sid <= 60; sid++) READ_TERM(sid, "\x1b]52;c;ZZ", 9);   /* far more than the table holds */
    READ_TERM(60, "ZZ\x07ok", 5);
    const char *t = logtext();
    ASSERT_TRUE(has(t, "ok"));
    ASSERT_FALSE(has(t, "ZZ"));                       /* the most recent session kept its state */
    end();
    TEST_END();
}

/* ---- finding 5: known secrets ----------------------------------------------------------------------- */

int test_dlog_review_secret_split_across_two_reads_is_scrubbed(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply(1, DLOG_LVL_TRACE, 1);
    ASSERT_TRUE(dlog_secret_add("hunter2xyz"));
    READ_TERM(4, "login: hunt", 11);
    READ_TERM(4, "er2xyz ok", 9);
    const char *t = logtext();
    ASSERT_FALSE(has(t, "hunt"));
    ASSERT_FALSE(has(t, "er2xyz"));
    ASSERT_TRUE(has(t, "[REDACTED]"));
    ASSERT_TRUE(has(t, "login: "));
    ASSERT_TRUE(has(t, " ok"));
    end();
    TEST_END();
}

int test_dlog_review_a_held_tail_that_is_not_a_secret_is_delivered_with_the_next_read(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply(1, DLOG_LVL_TRACE, 1);
    ASSERT_TRUE(dlog_secret_add("hunter2xyz"));
    READ_TERM(4, "say hun", 7);
    READ_TERM(4, "ting", 4);
    const char *t = logtext();
    ASSERT_TRUE(has(t, "say "));
    ASSERT_TRUE(has(t, "hunting"));
    ASSERT_FALSE(has(t, "[REDACTED]"));
    end();
    TEST_END();
}

int test_dlog_review_the_held_tail_belongs_to_one_session_and_one_call_kind(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply(1, DLOG_LVL_TRACE, 1);
    ASSERT_TRUE(dlog_secret_add("hunter2xyz"));
    READ_TERM(5, "hunt", 4);
    READ_TERM(6, "er2xyz", 6);                         /* another session: no join with session 5's tail */
    DLOG_CONTENT(DLOG_AI, DLOG_TRACE, 0, "chat.text", "text", "my hunt", 7);   /* not a stream: written whole */
    const char *t = logtext();
    ASSERT_TRUE(has(t, "er2xyz"));
    ASSERT_TRUE(has(t, "my hunt"));
    end();
    TEST_END();
}

int test_dlog_review_secret_straddling_the_internal_slice_boundary_is_scrubbed(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply(1, DLOG_LVL_TRACE, 1);
    ASSERT_TRUE(dlog_secret_add("hunter2xyz"));
    static char buf[3000];
    memset(buf, 'z', sizeof(buf));
    memcpy(buf + 1020, "hunter2xyz", 10);
    READ_TERM(2, buf, sizeof(buf));
    const char *t = logtext();
    ASSERT_FALSE(has(t, "hunter2xyz"));
    ASSERT_FALSE(has(t, "xyz" "zzz"));
    ASSERT_TRUE(has(t, "[REDACTED]"));
    end();
    TEST_END();
}

int test_dlog_review_secrets_are_reference_counted(void)
{
    TEST_BEGIN();
    char out[64];
    dlog_secret_clear();
    ASSERT_TRUE(dlog_secret_add("sharedpass"));       /* two connections, one password */
    ASSERT_TRUE(dlog_secret_add("sharedpass"));
    dlog_secret_remove("sharedpass");                 /* the first one ends */
    (void)dlog_scrub_copy(out, sizeof(out), "x sharedpass y", 14);
    ASSERT_STR_EQ(out, "x [REDACTED] y");             /* still scrubbed for the second */
    dlog_secret_remove("sharedpass");
    (void)dlog_scrub_copy(out, sizeof(out), "x sharedpass y", 14);
    ASSERT_STR_EQ(out, "x sharedpass y");
    dlog_secret_remove("sharedpass");                 /* extra removes are harmless */
    ASSERT_TRUE(dlog_secret_add("sharedpass"));
    dlog_secret_clear();
    TEST_END();
}

int test_dlog_review_environment_style_assignments_lose_their_value(void)
{
    TEST_BEGIN();
    char l[256];
    const char *in = "x data=\"export GITHUB_TOKEN=ghp_abc123 DB_PASSWORD=hunter2 MY_API_KEY=k1 USER=bob\"\n";
    size_t n = strlen(in);
    memcpy(l, in, n + 1);
    n = dlog_redact_line(l, n, sizeof(l));
    ASSERT_TRUE(strstr(l, "ghp_abc123") == NULL);
    ASSERT_TRUE(strstr(l, "hunter2") == NULL);
    ASSERT_TRUE(strstr(l, "k1") == NULL);
    ASSERT_TRUE(strstr(l, "GITHUB_TOKEN=[REDACTED]") != NULL);
    ASSERT_TRUE(strstr(l, "DB_PASSWORD=[REDACTED]") != NULL);
    ASSERT_TRUE(strstr(l, "USER=bob") != NULL);       /* other assignments are left alone */
    const char *in2 = "SECRET_THING=abc lower_token=zzz tokens_in=5 NOTHING=1\n";
    n = strlen(in2);
    memcpy(l, in2, n + 1);
    n = dlog_redact_line(l, n, sizeof(l));
    ASSERT_TRUE(strstr(l, "SECRET_THING=[REDACTED]") != NULL);
    ASSERT_TRUE(strstr(l, "lower_token=zzz") != NULL);    /* lower case: the key-name rule covers real fields */
    ASSERT_TRUE(strstr(l, "tokens_in=5") != NULL);
    ASSERT_TRUE(strstr(l, "NOTHING=1") != NULL);
    TEST_END();
}

/* ---- finding 3: repeats ignore volatile fields --------------------------------------------------------- */

int test_dlog_review_paint_lines_differing_only_in_tick_collapse(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply(1, DLOG_LVL_INFO, 0);
    DLOG(DLOG_RENDER, DLOG_INFO, 1, "paint", "rows=%d tick=%u", 24, 100u);
    DLOG(DLOG_RENDER, DLOG_INFO, 1, "paint", "rows=%d tick=%u", 24, 101u);
    DLOG(DLOG_RENDER, DLOG_INFO, 1, "paint", "rows=%d tick=%u", 24, 102u);
    DLOG(DLOG_RENDER, DLOG_INFO, 1, "other", "x=%d", 1);
    const char *t = logtext();
    ASSERT_EQ(count_in(t, " paint "), 1);
    ASSERT_EQ(count_in(t, "repeat n=2"), 1);
    ASSERT_TRUE(has(t, "of=paint"));
    end();
    TEST_END();
}

int test_dlog_review_us_ms_and_suffix_keys_are_volatile_other_keys_are_not(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply(1, DLOG_LVL_INFO, 0);
    for (int i = 0; i < 3; i++) DLOG(DLOG_RENDER, DLOG_INFO, 1, "render.draw", "rows=%d us=%d", 24, 500 + i);
    for (int i = 0; i < 3; i++) DLOG(DLOG_RENDER, DLOG_INFO, 1, "poll.skip", "n=%d since_paint_ms=%d wait_ms=%d", 5, i, i * 3);
    for (int i = 0; i < 3; i++) DLOG(DLOG_RENDER, DLOG_INFO, 1, "poll.tx", "n=%d ms=%d took_us=%d", 5, i, i);
    for (int i = 0; i < 3; i++) DLOG(DLOG_RENDER, DLOG_INFO, 1, "grid", "rows=%d", 20 + i);          /* real change */
    const char *t = logtext();
    ASSERT_EQ(count_in(t, " render.draw "), 1);
    ASSERT_EQ(count_in(t, " poll.skip "), 1);
    ASSERT_EQ(count_in(t, " poll.tx "), 1);
    ASSERT_EQ(count_in(t, " grid "), 3);
    end();
    TEST_END();
}

int test_dlog_review_a_volatile_value_may_be_quoted_and_the_event_still_matters(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply(1, DLOG_LVL_INFO, 0);
    DLOG(DLOG_RENDER, DLOG_INFO, 1, "a", "tick=%s n=%d", "x y", 1);
    DLOG(DLOG_RENDER, DLOG_INFO, 1, "a", "tick=%s n=%d", "p q r", 1);
    DLOG(DLOG_RENDER, DLOG_INFO, 1, "b", "tick=%s n=%d", "x y", 1);           /* other event: not a repeat */
    const char *t = logtext();
    ASSERT_EQ(count_in(t, " a "), 1);
    ASSERT_EQ(count_in(t, " b "), 1);
    ASSERT_EQ(count_in(t, "repeat n=1"), 1);
    end();
    TEST_END();
}

/* ---- finding 6: the crash path allocates and creates nothing ------------------------------------------- */

static void snap_calls(int c[5])
{
    c[0] = g_fk.mkdirs; c[1] = g_fk.list_calls; c[2] = g_fk.write_alls; c[3] = g_fk.remove_attempts; c[4] = g_fk.opens;
}

int test_dlog_review_crash_with_the_file_open_touches_no_folder_or_listing(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "before");
    (void)dlog_pump();
    int c0[5], c1[5];
    snap_calls(c0);
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "queued.before.crash");
    dlog_crash_note(0xC0000005ul, (const void *)(size_t)0x10);
    snap_calls(c1);
    ASSERT_EQ(c1[0], c0[0]);                          /* no mkdir */
    ASSERT_EQ(c1[1], c0[1]);                          /* no directory listing (retention) */
    ASSERT_EQ(c1[2], c0[2]);                          /* no FORMAT.txt / file_write_all */
    ASSERT_EQ(c1[3], c0[3]);
    ASSERT_EQ(c1[4], c0[4]);                          /* the open handle was reused */
    const char *t = fk_text(FK_LOG_FILE);
    ASSERT_TRUE(has(t, "queued.before.crash"));
    ASSERT_TRUE(has(t, "app.crash"));
    end();
    TEST_END();
}

int test_dlog_review_crash_before_any_file_does_one_plain_open_and_nothing_else(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "never.pumped");
    dlog_crash_note(0xC0000005ul, NULL);
    ASSERT_EQ(g_fk.mkdirs, 0);
    ASSERT_EQ(g_fk.list_calls, 0);
    ASSERT_EQ(g_fk.write_alls, 0);
    ASSERT_EQ(g_fk.remove_attempts, 0);
    ASSERT_EQ(g_fk.opens, 1);
    const char *t = fk_text(FK_LOG_FILE);
    ASSERT_TRUE(has(t, "never.pumped"));
    ASSERT_TRUE(has(t, "app.crash"));
    end();
    TEST_END();
}

int test_dlog_review_crash_after_a_failed_write_reopens_the_current_name_only(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "first");
    (void)dlog_pump();
    g_fk.fail_writes = 1;
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "lost.then.kept");
    (void)dlog_pump();                                 /* the write fails: the handle is closed */
    g_fk.fail_writes = 0;
    int c0[5], c1[5];
    snap_calls(c0);
    dlog_crash_note(0xC0000005ul, NULL);
    snap_calls(c1);
    ASSERT_EQ(c1[0], c0[0]);
    ASSERT_EQ(c1[1], c0[1]);
    ASSERT_EQ(c1[2], c0[2]);
    ASSERT_EQ(c1[3], c0[3]);
    ASSERT_EQ(c1[4], c0[4] + 1);                      /* one plain open of the file being written */
    const char *t = fk_text(FK_LOG_FILE);
    ASSERT_TRUE(has(t, "lost.then.kept"));
    ASSERT_TRUE(has(t, "app.crash"));
    end();
    TEST_END();
}

/* ---- finding 7: shutdown with a writer that does not stop; late emitters ---------------------------------- */

int test_dlog_review_shutdown_with_a_stuck_writer_leaks_instead_of_freeing(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(start_threaded(1));
    ASSERT_NOT_NULL(g_fk.thread_fn);
    void (*writer)(void *) = g_fk.thread_fn;
    void *warg = g_fk.thread_arg;
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "in.flight");
    g_fk.join_result = 0;                              /* the writer does not exit in time */
    dlog_shutdown();
    ASSERT_EQ(g_fk.joins, 1);
    ASSERT_FALSE(dlog_is_started());
    ASSERT_NULL(dlog_platform());
    dlog_emit(DLOG_GENERAL, DLOG_ERROR, 0, "late", "x=%d", 1);     /* a late emitter: a no-op, no crash */
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "late.too");
    ASSERT_EQ(dlog_recorder_size(), (size_t)0);
    DlogStatus st;
    dlog_status(&st);
    ASSERT_FALSE(st.started);
    /* the old writer is still alive: a restart is refused until it has exited */
    DlogStartInfo si;
    memset(&si, 0, sizeof(si));
    si.exe_dir = "C:\\Tools\\nutshell";
    si.version = "1";
    ASSERT_FALSE(dlog_start_with(fk_platform_thread(), &si));
    writer(warg);                                      /* it notices the stop flag and returns */
    ASSERT_TRUE(dlog_start_with(fk_platform_thread(), &si));
    g_fk.join_result = 1;
    dlog_shutdown();
    ASSERT_FALSE(dlog_is_started());
    fk_free();
    TEST_END();
}

int test_dlog_review_shutdown_with_a_joined_writer_frees_and_later_emits_are_ignored(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(start_threaded(1));
    g_fk.join_result = 1;
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "before");
    dlog_shutdown();
    ASSERT_FALSE(dlog_is_started());
    ASSERT_TRUE(has(fk_text(FK_LOG_FILE), "before"));
    dlog_emit(DLOG_GENERAL, DLOG_ERROR, 0, "after", "x=%d", 1);
    dlog_emit_fields(DLOG_GENERAL, DLOG_ERROR, 0, "after2", "");
    dlog_emit_content(DLOG_TERM, DLOG_TRACE, 1, "term.rx", "data", "zzz", 3);
    ASSERT_FALSE(has(fk_text(FK_LOG_FILE), "after"));
    ASSERT_TRUE(start_threaded(0));                    /* and it can start again */
    dlog_shutdown();
    fk_free();
    TEST_END();
}

/* ---- finding 9: a failed write keeps its chunk ------------------------------------------------------------- */

int test_dlog_review_a_failed_write_keeps_the_chunk_for_the_reopened_file(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "ev_one");
    (void)dlog_pump();
    g_fk.fail_writes = 1;
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "ev_two");
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "ev_three");
    (void)dlog_pump();                                 /* fails: handle closed, 2 s retry window */
    g_fk.fail_writes = 0;
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "ev_four");
    (void)dlog_pump();                                 /* inside the window: nothing opens, nothing is thrown away */
    const char *t1 = fk_text(FK_LOG_FILE);
    ASSERT_FALSE(has(t1, "ev_two"));
    fk_advance(2100);
    (void)dlog_pump();
    const char *t2 = fk_text(FK_LOG_DIR "\\" FK_STEM ".2.log");
    ASSERT_NOT_NULL(t2);
    const char *p2 = t2 ? strstr(t2, "ev_two") : NULL;
    const char *p3 = t2 ? strstr(t2, "ev_three") : NULL;
    const char *p4 = t2 ? strstr(t2, "ev_four") : NULL;
    ASSERT_TRUE(p2 && p3 && p4 && p2 < p3 && p3 < p4);
    DlogStatus st;
    dlog_status(&st);
    ASSERT_EQ(st.dropped_total, 0ull);
    end();
    TEST_END();
}

int test_dlog_review_lines_given_up_for_a_missing_folder_are_counted(void)
{
    TEST_BEGIN();
    fk_reset();
    (void)snprintf(g_fk.deny_prefix, sizeof(g_fk.deny_prefix), "C:\\");
    ASSERT_TRUE(fk_start(0, 0));
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "e1");
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "e2");
    (void)dlog_pump();                                 /* nothing is writable: memory-only */
    DlogStatus st;
    dlog_status(&st);
    ASSERT_TRUE(st.mem_only);
    ASSERT_EQ(st.dropped_total, 2ull);
    end();
    TEST_END();
}

/* ---- finding 12: the dropped notice follows the lines it is about; the writer idles ------------------------- */

int test_dlog_review_the_dropped_line_follows_the_older_chunk_lines(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(8192, 65536));
    apply(1, DLOG_LVL_INFO, 0);
    for (int i = 0; i < 200; i++)
        DLOG(DLOG_RENDER, DLOG_INFO, 1, "line", "i=%d pad=%s", i, "0123456789012345678901234567890123456789");
    const char *t = logtext();
    ASSERT_NOT_NULL(t);
    ASSERT_EQ(count_in(t, "dlog.dropped"), 1);
    size_t n = t ? strlen(t) : 0;
    ASSERT_TRUE(n > 2);
    const char *last = t;
    for (const char *p = t; p && p < t + n - 1; p++)
        if (*p == '\n') last = p + 1;
    ASSERT_TRUE(strstr(last, "dlog.dropped") != NULL);       /* the very last line */
    end();
    TEST_END();
}

int test_dlog_review_the_writer_sleeps_until_woken_when_debugging_is_off_and_idle(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    ASSERT_EQ(dlog_writer_wait_ms(), DLOG_WAIT_FOREVER);
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "work");
    ASSERT_EQ(dlog_writer_wait_ms(), 250u);                 /* something queued */
    (void)dlog_pump();
    ASSERT_EQ(dlog_writer_wait_ms(), DLOG_WAIT_FOREVER);
    apply(1, DLOG_LVL_INFO, 0);
    ASSERT_EQ(dlog_writer_wait_ms(), 250u);                 /* debugging on: the normal cadence */
    apply(0, DLOG_LVL_INFO, 0);
    (void)dlog_pump();
    ASSERT_EQ(dlog_writer_wait_ms(), DLOG_WAIT_FOREVER);
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "same");
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "same");           /* a repeat is pending: it needs its tick */
    ASSERT_EQ(dlog_writer_wait_ms(), 250u);
    (void)dlog_pump();
    fk_advance(DLOG_REPEAT_MS + 1);
    (void)dlog_pump();
    ASSERT_EQ(dlog_writer_wait_ms(), DLOG_WAIT_FOREVER);
    end();
    TEST_END();
}

int test_dlog_review_configure_and_the_loggers_own_lines_wake_the_writer(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    int w0 = g_fk.wakes;
    apply(1, DLOG_LVL_INFO, 0);
    ASSERT_TRUE(g_fk.wakes > w0);
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "x");
    (void)dlog_pump();
    w0 = g_fk.wakes;
    apply(1, DLOG_LVL_DEBUG, 0);                            /* writes a dlog.config line (INFO, forced) */
    ASSERT_TRUE(g_fk.wakes > w0);
    end();
    TEST_END();
}

/* ---- finding 11: the issue mark does not do the heavy work on its caller ------------------------------------- */

int test_dlog_review_issue_mark_hands_the_dump_to_the_writer_and_waits_bounded_real_time(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(start_threaded(1));
    g_fk.join_result = 1;
    g_fk.advance_on_sleep = 1;
    g_fk.sleep_quantum = 15;                           /* Sleep(1) is really ~15 ms */
    DlogIssue iss;
    ASSERT_TRUE(dlog_issue_mark(NULL, 1, &iss));
    ASSERT_TRUE(iss.ok);
    ASSERT_TRUE(iss.pending);                          /* the writer has not run */
    ASSERT_TRUE(g_fk.sleeps <= 40);                    /* 500 ms of real time, not 500 sleeps */
    ASSERT_EQ(g_fk.mkdirs, 0);                         /* nothing heavy on the caller */
    ASSERT_EQ(g_fk.list_calls, 0);
    ASSERT_EQ(g_fk.write_alls, 0);
    ASSERT_EQ(g_fk.opens, 0);
    ASSERT_TRUE(strstr(iss.recorder_path, ".issue1.log") != NULL);
    ASSERT_TRUE(strstr(iss.screenshot_path, ".issue1.png") != NULL);
    g_fk.advance_on_sleep = 0;
    (void)dlog_pump();                                 /* the writer's turn */
    FkFile *f = fk_find(iss.recorder_path);
    ASSERT_NOT_NULL(f);
    if (f) ASSERT_TRUE(f->len > 0);
    ASSERT_TRUE(has(fk_text(FK_LOG_FILE), "issue.mark"));
    ASSERT_NOT_NULL(fk_find(FK_LOG_DIR "\\FORMAT.txt"));
    dlog_shutdown();
    fk_free();
    TEST_END();
}

int test_dlog_review_issue_marks_beyond_the_job_slots_fail_cleanly(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(start_threaded(1));
    g_fk.join_result = 1;
    g_fk.advance_on_sleep = 1;
    g_fk.sleep_quantum = 100;
    DlogIssue a, b, c;
    ASSERT_TRUE(dlog_issue_mark(NULL, 0, &a));
    ASSERT_TRUE(dlog_issue_mark(NULL, 0, &b));
    ASSERT_TRUE(dlog_issue_mark(NULL, 0, &c));          /* the writer never ran: both slots are busy */
    ASSERT_TRUE(a.ok && b.ok);
    ASSERT_FALSE(c.ok);
    ASSERT_TRUE(c.dump_failed);                         /* not "no folder": the writer was busy */
    ASSERT_FALSE(a.dump_failed || b.dump_failed);
    g_fk.advance_on_sleep = 0;
    dlog_shutdown();                                    /* shutdown drains: the queued dumps are written */
    ASSERT_NOT_NULL(fk_find(a.recorder_path));
    ASSERT_NOT_NULL(fk_find(b.recorder_path));
    {
        const char *t = fk_text(FK_LOG_FILE);
        ASSERT_EQ(count_in(t, "issue.dump_failed"), 1);   /* one line, for issue 3 only */
        ASSERT_TRUE(has(t, "issue.dump_failed"));
        ASSERT_TRUE(has(t, "n=3"));
        ASSERT_TRUE(has(t, "writer busy"));
        ASSERT_EQ(count_in(t, "WARN"), 1);
    }
    fk_free();
    TEST_END();
}

/* ---- finding 2: another instance's live file ------------------------------------------------------------------ */

int test_dlog_review_retention_skips_a_file_a_second_instance_has_open(void)
{
    TEST_BEGIN();
    fk_reset();
    char path[300];
    for (int i = 0; i < 4; i++) {
        (void)snprintf(path, sizeof(path), FK_LOG_DIR "\\nutshell-2026100%d-120000-p1.log", i + 1);
        fk_put_file(path, "old\n", 2ull * 1024ull * 1024ull, 1000 + i);
    }
    (void)snprintf(g_fk.dirs[g_fk.ndirs++], DLOG_MAX_PATH, "%s", FK_LOG_DIR);
    /* instance A has its live file open the way the real writer opens it (no delete sharing) */
    void *a = NULL;
    unsigned long long sz = 0;
    ASSERT_EQ(fk_platform()->file_open(FK_LOG_DIR "\\nutshell-20261001-120000-p1.log", &a, &sz), 0);
    ASSERT_TRUE(fk_start(0, 0));                        /* instance B */
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = 1;
    c.max_total_mb = 5;
    dlog_apply(&c);
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "first");
    (void)dlog_pump();
    ASSERT_NOT_NULL(fk_find(FK_LOG_DIR "\\nutshell-20261001-120000-p1.log"));   /* delete refused, skipped */
    ASSERT_NULL(fk_find(FK_LOG_DIR "\\nutshell-20261002-120000-p1.log"));       /* the next oldest went */
    ASSERT_TRUE(g_fk.remove_attempts >= 2);
    fk_platform()->file_close(a);
    end();
    TEST_END();
}

int test_dlog_review_the_fake_refuses_to_delete_an_open_file_until_it_is_closed(void)
{
    TEST_BEGIN();
    fk_reset();
    fk_put_file(FK_LOG_DIR "\\nutshell-20261001-120000-p1.log", "x", 0, 1);
    void *h = NULL;
    unsigned long long sz = 0;
    const DlogPlatform *p = fk_platform();
    ASSERT_EQ(p->file_open(FK_LOG_DIR "\\nutshell-20261001-120000-p1.log", &h, &sz), 0);
    ASSERT_EQ(p->remove_file(FK_LOG_DIR "\\nutshell-20261001-120000-p1.log"), 32);
    p->file_close(h);
    ASSERT_EQ(p->remove_file(FK_LOG_DIR "\\nutshell-20261001-120000-p1.log"), 0);
    fk_free();
    TEST_END();
}

/* ---- finding 15: FORMAT.txt ----------------------------------------------------------------------------------- */

int test_dlog_review_format_txt_names_the_shell_thread_and_the_repeat_rule(void)
{
    TEST_BEGIN();
    char buf[16384];
    size_t n = dlog_format_text(buf, sizeof(buf), "1.2.36");
    ASSERT_TRUE(n > 100 && n < 8192);                  /* it is written through an 8 KB buffer */
    ASSERT_TRUE(strstr(buf, "ui, ssh, ai, shell, set") != NULL);
    ASSERT_TRUE(strstr(buf, "ai, pty") == NULL);
    ASSERT_TRUE(strstr(buf, "since_paint_ms") != NULL);
    ASSERT_TRUE(strstr(buf, "ending _ms or") != NULL);
    ASSERT_TRUE(strstr(buf, "OSC 52") != NULL);
    TEST_END();
}

/* ---- finding 8: key-name redaction must never hit a real diagnostic ---------------------------------------------- */

int test_dlog_review_known_safe_names_that_contain_key_are_not_redacted(void)
{
    TEST_BEGIN();
    ASSERT_FALSE(dlog_key_is_sensitive("keystroke_ms"));
    ASSERT_FALSE(dlog_key_is_sensitive("strict_hostkey"));
    ASSERT_FALSE(dlog_key_is_sensitive("Keystroke_MS"));
    ASSERT_TRUE(dlog_key_is_sensitive("api_key"));
    ASSERT_TRUE(dlog_key_is_sensitive("hostkey_secret"));
    TEST_END();
}

static int ident_c(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* Comments to spaces (newlines kept); strings and character literals untouched. */
static char *strip_comments(const char *src)
{
    size_t n = strlen(src);
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    size_t i = 0;
    while (i < n) {
        if (src[i] == '/' && i + 1 < n && src[i + 1] == '*') {
            out[i] = out[i + 1] = ' ';
            i += 2;
            while (i < n && !(src[i] == '*' && i + 1 < n && src[i + 1] == '/')) { out[i] = (src[i] == '\n') ? '\n' : ' '; i++; }
            if (i < n) { out[i] = out[i + 1] = ' '; i += 2; }
        } else if (src[i] == '/' && i + 1 < n && src[i + 1] == '/') {
            while (i < n && src[i] != '\n') { out[i] = ' '; i++; }
        } else if (src[i] == '"' || src[i] == '\'') {
            char q = src[i];
            out[i] = src[i];
            i++;
            while (i < n && src[i] != q) {
                if (src[i] == '\\' && i + 1 < n) { out[i] = src[i]; i++; }
                out[i] = src[i];
                i++;
            }
            if (i < n) { out[i] = src[i]; i++; }
        } else {
            out[i] = src[i];
            i++;
        }
    }
    out[n] = '\0';
    return out;
}

/* Every key that an event's format string gives a conversion to ("key=%d", "key=\"%s\""), in the
 * argument lists of the log calls of one source text. Returns how many keys the formatter would
 * redact (dlog_key_is_sensitive); *checked counts all keys; the first offenders are named. */
static int scan_keys(const char *src, int *checked, char *names, size_t names_cap)
{
    static const char *const calls[] = { "DLOG_EV(", "DLOG(", "dlog_emit_fields(", "dlog_emit(" };
    char *t = strip_comments(src);
    if (!t) return 0;
    size_t n = strlen(t);
    int bad = 0;
    for (size_t i = 0; i < n; i++) {
        if (t[i] == '"' || t[i] == '\'') {
            char q = t[i++];
            while (i < n && t[i] != q) { if (t[i] == '\\') i++; i++; }
            continue;
        }
        if (i > 0 && ident_c(t[i - 1])) continue;
        for (size_t c = 0; c < sizeof(calls) / sizeof(calls[0]); c++) {
            size_t cl = strlen(calls[c]);
            if (strncmp(t + i, calls[c], cl) != 0) continue;
            /* the literals of this call, concatenated the way the compiler does */
            char lit[4096];
            size_t ll = 0;
            size_t j = i + cl;
            int depth = 1;
            while (j < n && depth > 0) {
                if (t[j] == '(') depth++;
                else if (t[j] == ')') depth--;
                else if (t[j] == '"') {
                    j++;
                    if (ll > 0 && ll + 2 < sizeof(lit)) lit[ll++] = ' ';       /* adjacent literals: keep them apart */
                    while (j < n && t[j] != '"') {
                        if (ll + 2 < sizeof(lit)) lit[ll++] = t[j];
                        if (t[j] == '\\' && j + 1 < n) { j++; if (ll + 2 < sizeof(lit)) lit[ll++] = t[j]; }
                        j++;
                    }
                }
                j++;
            }
            lit[ll] = '\0';
            for (size_t k = 1; k < ll; k++) {
                if (lit[k] != '=') continue;
                size_t a = k;
                while (a > 0 && (ident_c(lit[a - 1]) || lit[a - 1] == '.' || lit[a - 1] == '-')) a--;
                size_t v = k + 1;
                if (lit[v] == '\\' && lit[v + 1] == '"') v += 2;
                else if (lit[v] == '"') v++;
                if (a == k || lit[v] != '%') continue;
                char key[80];
                size_t kl = k - a < sizeof(key) - 1 ? k - a : sizeof(key) - 1;
                memcpy(key, lit + a, kl);
                key[kl] = '\0';
                if (checked) (*checked)++;
                if (dlog_key_is_sensitive(key)) {
                    bad++;
                    size_t nl = names ? strlen(names) : 0;
                    if (names && nl + kl + 2 < names_cap) {
                        memcpy(names + nl, key, kl);
                        names[nl + kl] = ' ';
                        names[nl + kl + 1] = '\0';
                    }
                }
            }
            i = j - 1;
            break;
        }
    }
    free(t);
    return bad;
}

int test_dlog_review_key_scanner_finds_sensitive_and_safe_keys_in_planted_calls(void)
{
    TEST_BEGIN();
    int checked = 0;
    char names[200] = "";
    const char *src =
        "DLOG(DLOG_RENDER, DLOG_INFO, 1, \"e\", \"rows=%d \" \"keystroke_ms=%d\", a, b);\n"
        "dlog_emit(DLOG_SSH, DLOG_INFO, 0, \"e\", \"user=%s api_key=%s hostkey=\\\"%s\\\"\", u, k, h);\n"
        "/* DLOG(x, y, z, \"e\", \"password=%s\", p); */\n";
    int bad = scan_keys(src, &checked, names, sizeof(names));
    ASSERT_EQ(checked, 5);
    ASSERT_EQ(bad, 2);
    ASSERT_TRUE(strstr(names, "api_key") != NULL && strstr(names, "hostkey") != NULL);
    TEST_END();
}

int test_dlog_review_key_scanner_names_buffer_is_bounded_at_its_capacity(void)
{
    TEST_BEGIN();
    const char *src =
        "dlog_emit(DLOG_SSH, DLOG_INFO, 0, \"e\", \"api_key=%s hostkey=%s\", k, h);\n";
    /* a canary after the names buffer must survive whatever the capacity */
    for (size_t cap = 0; cap <= 20; cap++) {
        struct { char names[20]; char canary[4]; } w;
        memset(&w, 'Z', sizeof(w));
        w.names[0] = '\0';
        int checked = 0;
        int bad = scan_keys(src, &checked, cap ? w.names : NULL, cap);
        ASSERT_EQ(bad, 2);
        ASSERT_EQ(checked, 2);
        ASSERT_TRUE(memcmp(w.canary, "ZZZZ", 4) == 0);
        if (cap) ASSERT_TRUE(strlen(w.names) < cap);
    }
    TEST_END();
}

static int scan_tree_dir(const char *dir, int *checked, int *files, char *names, size_t names_cap)
{
    int bad = 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t nl = strlen(ent->d_name);
        if (nl < 3 || (strcmp(ent->d_name + nl - 2, ".c") != 0 && strcmp(ent->d_name + nl - 2, ".h") != 0)) continue;
        char path[400];
        (void)snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = (char *)malloc((size_t)sz + 1);
        if (buf) {
            size_t got = fread(buf, 1, (size_t)sz, f);
            buf[got] = '\0';
            (*files)++;
            int b = scan_keys(buf, checked, names, names_cap);
            if (b) printf("  %s: %d key(s) the formatter would redact\n", path, b);
            bad += b;
            free(buf);
        }
        fclose(f);
    }
    closedir(d);
    return bad;
}

int test_dlog_review_no_real_call_site_key_is_redacted_by_name(void)
{
    TEST_BEGIN();
    static const char *const dirs[] = { "src", "src/core", "src/config", "src/crypto", "src/term", "src/ssh", "src/ui" };
    int checked = 0, files = 0, bad = 0;
    char names[300] = "";
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) bad += scan_tree_dir(dirs[i], &checked, &files, names, sizeof(names));
    if (bad) printf("  redacted keys: %s\n", names);
    ASSERT_TRUE(files > 100);
    ASSERT_TRUE(checked > 100);                          /* it really found the call sites' keys */
    ASSERT_EQ(bad, 0);                                   /* real diagnostics never go through key redaction */
    TEST_END();
}

/* ---- a dump that is queued and written is not a failure -------------------------------------------------- */

int test_dlog_review_issue_mark_whose_dump_is_written_is_not_a_dump_failure(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DlogIssue iss;
    ASSERT_TRUE(dlog_issue_mark(NULL, 0, &iss));       /* unthreaded fake: dump written by the pump */
    ASSERT_FALSE(iss.dump_failed);
    ASSERT_FALSE(has(logtext(), "issue.dump_failed"));
    end();
    TEST_END();
}

/* ---- a secret's tail is wiped when its last registration ends ----------------------------------------------- */

int test_dlog_review_removing_the_last_registration_wipes_the_held_tail(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply(1, DLOG_LVL_TRACE, 1);
    ASSERT_TRUE(dlog_secret_add("hunter2xyz"));
    READ_TERM(4, "login: hunt", 11);                    /* "hunt" is held back */
    ASSERT_TRUE(dlog_streams_held_nonzero() > 0);
    dlog_secret_remove("hunter2xyz");
    ASSERT_EQ(dlog_streams_held_nonzero(), 0);          /* wiped, not merely forgotten */
    READ_TERM(4, "er2xyz ok", 9);
    const char *t = logtext();
    ASSERT_TRUE(has(t, "login: "));
    ASSERT_FALSE(has(t, "hunter2xyz"));                 /* the prefix is not joined back on */
    ASSERT_FALSE(has(t, "hunt"));
    end();
    TEST_END();
}

int test_dlog_review_a_remaining_registration_keeps_the_held_tail(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply(1, DLOG_LVL_TRACE, 1);
    ASSERT_TRUE(dlog_secret_add("hunter2xyz"));
    ASSERT_TRUE(dlog_secret_add("hunter2xyz"));         /* two connections */
    READ_TERM(4, "login: hunt", 11);
    unsigned e0 = dlog_secret_epoch();
    dlog_secret_remove("hunter2xyz");                   /* one still holds it */
    ASSERT_EQ(dlog_secret_epoch(), e0);
    ASSERT_TRUE(dlog_streams_held_nonzero() > 0);
    READ_TERM(4, "er2xyz ok", 9);
    const char *t = logtext();
    ASSERT_FALSE(has(t, "hunter2xyz"));
    ASSERT_TRUE(has(t, "[REDACTED]"));
    dlog_secret_remove("hunter2xyz");
    ASSERT_TRUE(dlog_secret_epoch() != e0);             /* other threads wipe on their next use */
    end();
    TEST_END();
}

int test_dlog_review_clearing_all_secrets_wipes_the_held_tails(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply(1, DLOG_LVL_TRACE, 1);
    ASSERT_TRUE(dlog_secret_add("hunter2xyz"));
    READ_TERM(4, "a hunt", 6);
    READ_TERM(9, "b hun", 5);
    ASSERT_TRUE(dlog_streams_held_nonzero() > 0);
    dlog_secret_clear();
    ASSERT_EQ(dlog_streams_held_nonzero(), 0);
    end();
    TEST_END();
}
