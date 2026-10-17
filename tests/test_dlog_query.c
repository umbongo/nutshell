/* tests/test_dlog_query.c -- reading the debug log back (dlog_query.c): the
 * line parser, every filter alone and combined, the caps, partial lines,
 * summary mode, around_issue, the file source, the tool arguments. */
#include "test_framework.h"
#include "dlog.h"
#include "dlog_query.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- fixtures --------------------------------------------------------------- */

static long long t0_ms(void)       /* 2026-10-17 13:00:00 UTC == 14:00:00 at +01:00 */
{
    DlogTm tm = { 2026, 10, 17, 13, 0, 0, 0 };
    return dlog_unix_ms_from_civil(&tm, 0);
}

/* Append "<ts> <LEVEL> <cat> <sid> ui <rest>\n"; sec is seconds after t0. */
static void addl(char *buf, size_t cap, int sec, int off_min, const char *lvl, const char *cat,
                 const char *sid, const char *rest)
{
    char ts[DLOG_TS_LEN + 4];
    dlog_format_timestamp(t0_ms() + (long long)sec * 1000, off_min, ts);
    size_t n = strlen(buf);
    (void)snprintf(buf + n, cap - n, "%s %s %s %s ui %s\n", ts, lvl, cat, sid, rest);
}

/* Appends s to the NUL-terminated buf (capacity cap), cutting it if it does not fit. */
static void fx_append(char *buf, size_t cap, const char *s)
{
    size_t n = strlen(buf);
    if (n + 1 >= cap) return;
    (void)snprintf(buf + n, cap - n, "%s", s);
}

static void fixture1(char *buf, size_t cap)
{
    buf[0] = '\0';
    addl(buf, cap, 0, 60, "INFO ", "general", "-", "app.start version=1");
    addl(buf, cap, 10, 60, "TRACE", "render", "s2", "paint rows=24");
    addl(buf, cap, 20, 60, "DEBUG", "term", "s2", "mode alt=1");
    addl(buf, cap, 30, 60, "WARN ", "ssh", "s3", "ssh.auth.result ok=0");
    addl(buf, cap, 40, 60, "ERROR", "ai", "-", "ai.stream.fail code=7");
    addl(buf, cap, 50, 60, "INFO ", "render", "s3", "Paint rows=30");
    addl(buf, cap, 60, 60, "ISSUE", "general", "-", "issue.mark n=1 recorder=x.log screenshot=none");
}

static DlogQuery qdef(void)
{
    DlogQuery q;
    dlog_query_defaults(&q);
    q.since_minutes = 0;
    q.now_ms = t0_ms() + 120000;
    return q;
}

static int nlines(const char *s)
{
    int n = 0;
    for (; s && *s; s++) if (*s == '\n') n++;
    return n;
}

static int has(const char *hay, const char *needle) { return strstr(hay, needle) != NULL; }

/* ---- the line parser ------------------------------------------------------------ */

int test_dlog_line_parse_valid(void)
{
    TEST_BEGIN();
    const char *l = "2026-10-17T14:03:22.418+01:00 TRACE render s2 ui poll.cooldown_skip poll_rc=1";
    DlogLineInfo li;
    ASSERT_TRUE(dlog_line_parse(l, strlen(l), &li));
    ASSERT_EQ(li.ev_level, (int)DLOG_TRACE);
    ASSERT_EQ(li.cat, (int)DLOG_RENDER);
    ASSERT_EQ(li.sid, 2u);
    ASSERT_EQ(li.event_len, strlen("poll.cooldown_skip"));
    ASSERT_TRUE(strncmp(li.event, "poll.cooldown_skip", li.event_len) == 0);
    DlogTm tm = { 2026, 10, 17, 13, 3, 22, 418 };
    ASSERT_EQ(li.ms, dlog_unix_ms_from_civil(&tm, 0));
    l = "2026-10-17T14:03:22.418+01:00 ISSUE general - ui issue.mark";
    ASSERT_TRUE(dlog_line_parse(l, strlen(l), &li));
    ASSERT_EQ(li.ev_level, (int)DLOG_ISSUE);
    ASSERT_EQ(li.sid, 0u);
    ASSERT_EQ(li.event_len, strlen("issue.mark"));
    l = "2026-10-17T14:03:22.418+01:00 WARN  shell s12 pty child.exit";
    ASSERT_TRUE(dlog_line_parse(l, strlen(l), &li));
    ASSERT_EQ(li.ev_level, (int)DLOG_WARN);
    ASSERT_EQ(li.cat, (int)DLOG_SHELL);
    ASSERT_EQ(li.sid, 12u);
    TEST_END();
}

int test_dlog_line_parse_rejects_malformed(void)
{
    TEST_BEGIN();
    DlogLineInfo li;
    static const char *const bad[] = {
        "", "garbage", "2026-10-17T14:03:22.418+01:00",
        "2026-10-17T14:03:22.418+01:00 INFO",
        "2026-10-17T14:03:22.418+01:00 BOGUS render - ui x",
        "2026-10-17T14:03:22.418+01:00 INFO  nocat - ui x",
        "2026-10-17T14:03:22.418+01:00 INFO  render sX ui x",
        "2026-10-17 14:03:22.418+01:00 INFO  render - ui x",
        "2026-10-17T14:03:22.418+01:00 INFO  render -",
        "2026-10-17T14:03:22.418+01:00 INFO  render - ui"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        if (dlog_line_parse(bad[i], strlen(bad[i]), &li)) printf("  accepted: %s\n", bad[i]);
        ASSERT_FALSE(dlog_line_parse(bad[i], strlen(bad[i]), &li));
    }
    ASSERT_FALSE(dlog_line_parse(NULL, 5, &li));
    TEST_END();
}

/* ---- defaults and filters over one text ---------------------------------------------- */

int test_dlog_query_defaults(void)
{
    TEST_BEGIN();
    DlogQuery q;
    dlog_query_defaults(&q);
    ASSERT_EQ(q.since_minutes, DLOG_QUERY_DEFAULT_MINUTES);
    ASSERT_EQ(q.category, -1);
    ASSERT_EQ(q.min_level, 0);
    ASSERT_EQ(q.contains[0], '\0');
    ASSERT_EQ(q.session, 0u);
    ASSERT_EQ(q.around_issue, -1);
    ASSERT_EQ(q.summary, 0);
    ASSERT_EQ(q.max_lines, DLOG_QUERY_DEFAULT_LINES);
    TEST_END();
}

int test_dlog_query_text_no_filters_returns_everything_with_header(void)
{
    TEST_BEGIN();
    char fx[4096], out[8192];
    fixture1(fx, sizeof(fx));
    DlogQuery q = qdef();
    size_t n = dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_EQ(n, strlen(out));
    ASSERT_TRUE(out[0] == '#');
    ASSERT_TRUE(has(out, "7 matching lines"));
    ASSERT_EQ(nlines(out), 8);                       /* header + 7 */
    ASSERT_TRUE(has(out, "app.start"));
    ASSERT_TRUE(strstr(out, "app.start") < strstr(out, "issue.mark"));   /* oldest first */
    ASSERT_TRUE(has(out, "window 14:00:00-14:01:00"));
    TEST_END();
}

int test_dlog_query_category_filter_keeps_issue_marks(void)
{
    TEST_BEGIN();
    char fx[4096], out[8192];
    fixture1(fx, sizeof(fx));
    DlogQuery q = qdef();
    q.category = DLOG_RENDER;
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "rows=24"));
    ASSERT_TRUE(has(out, "rows=30"));
    ASSERT_FALSE(has(out, "mode alt"));
    ASSERT_FALSE(has(out, "ssh.auth"));
    ASSERT_TRUE(has(out, "issue.mark"));             /* an issue mark always shows */
    ASSERT_TRUE(has(out, "3 matching lines"));
    TEST_END();
}

int test_dlog_query_min_level_filter(void)
{
    TEST_BEGIN();
    char fx[4096], out[8192];
    fixture1(fx, sizeof(fx));
    DlogQuery q = qdef();
    q.min_level = DLOG_ERROR;
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "ai.stream.fail"));
    ASSERT_TRUE(has(out, "issue.mark"));
    ASSERT_FALSE(has(out, "ssh.auth"));
    ASSERT_TRUE(has(out, "2 matching lines"));
    q.min_level = DLOG_WARN;
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "ssh.auth"));
    ASSERT_TRUE(has(out, "3 matching lines"));
    q.min_level = DLOG_INFO;
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_FALSE(has(out, "mode alt"));              /* DEBUG is more verbose than info */
    ASSERT_TRUE(has(out, "paint rows=24") == 0);     /* TRACE too */
    ASSERT_TRUE(has(out, "Paint rows=30"));
    q.min_level = DLOG_TRACE;
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "7 matching lines"));
    TEST_END();
}

int test_dlog_query_contains_is_case_insensitive(void)
{
    TEST_BEGIN();
    char fx[4096], out[8192];
    fixture1(fx, sizeof(fx));
    DlogQuery q = qdef();
    (void)snprintf(q.contains, sizeof(q.contains), "PAINT");
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "2 matching lines"));
    ASSERT_TRUE(has(out, "paint rows=24"));
    ASSERT_TRUE(has(out, "Paint rows=30"));
    (void)snprintf(q.contains, sizeof(q.contains), "no-such-text");
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "0 matching lines"));
    ASSERT_EQ(nlines(out), 1);
    /* contains also matches the prefix (category, session) */
    (void)snprintf(q.contains, sizeof(q.contains), " s3 ");
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "2 matching lines"));
    TEST_END();
}

int test_dlog_query_session_filter(void)
{
    TEST_BEGIN();
    char fx[4096], out[8192];
    fixture1(fx, sizeof(fx));
    DlogQuery q = qdef();
    q.session = 2;
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "paint rows=24"));
    ASSERT_TRUE(has(out, "mode alt"));
    ASSERT_FALSE(has(out, "Paint rows=30"));
    ASSERT_TRUE(has(out, "issue.mark"));
    ASSERT_TRUE(has(out, "3 matching lines"));
    TEST_END();
}

int test_dlog_query_filters_combine(void)
{
    TEST_BEGIN();
    char fx[4096], out[8192];
    fixture1(fx, sizeof(fx));
    DlogQuery q = qdef();
    q.category = DLOG_RENDER;
    q.session = 3;
    q.min_level = DLOG_INFO;
    (void)snprintf(q.contains, sizeof(q.contains), "rows");
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "Paint rows=30"));
    ASSERT_FALSE(has(out, "paint rows=24"));
    ASSERT_FALSE(has(out, "issue.mark"));            /* contains still applies to marks */
    ASSERT_TRUE(has(out, "1 matching lines"));
    TEST_END();
}

int test_dlog_query_since_minutes_window(void)
{
    TEST_BEGIN();
    char fx[4096], out[8192];
    fixture1(fx, sizeof(fx));
    DlogQuery q = qdef();
    q.since_minutes = 1;
    q.now_ms = t0_ms() + 90000;                       /* window starts at +30 s */
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_FALSE(has(out, "mode alt"));
    ASSERT_TRUE(has(out, "ssh.auth.result"));
    ASSERT_TRUE(has(out, "4 matching lines"));
    q.now_ms = t0_ms() + 3600000;                     /* an hour later: nothing */
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "0 matching lines"));
    TEST_END();
}

int test_dlog_query_since_compares_instants_not_clock_text(void)
{
    TEST_BEGIN();
    /* a DST change between two lines: the same wall clock text, different instants */
    char fx[2048], out[4096];
    fx[0] = '\0';
    addl(fx, sizeof(fx), 20, 60, "INFO ", "general", "-", "before.change");    /* 14:00:20+01:00 = 13:00:20Z */
    addl(fx, sizeof(fx), 25, 0, "INFO ", "general", "-", "after.change");      /* 13:00:25+00:00 = 13:00:25Z */
    DlogQuery q = qdef();
    q.since_minutes = 1;
    q.now_ms = t0_ms() + 80000;                       /* window starts at 13:00:20Z */
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "before.change"));
    ASSERT_TRUE(has(out, "after.change"));
    q.now_ms = t0_ms() + 83000;                       /* 13:00:23Z: only the later instant */
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_FALSE(has(out, "before.change"));
    ASSERT_TRUE(has(out, "after.change"));
    TEST_END();
}

/* ---- caps ---------------------------------------------------------------------------------- */

int test_dlog_query_max_lines_keeps_the_newest(void)
{
    TEST_BEGIN();
    char fx[4096], out[8192];
    fixture1(fx, sizeof(fx));
    DlogQuery q = qdef();
    q.max_lines = 3;
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "7 matching lines, showing last 3"));
    ASSERT_EQ(nlines(out), 4);
    ASSERT_FALSE(has(out, "app.start"));
    ASSERT_TRUE(has(out, "issue.mark"));
    TEST_END();
}

int test_dlog_query_max_lines_clamps(void)
{
    TEST_BEGIN();
    char fx[4096], out[8192];
    fixture1(fx, sizeof(fx));
    DlogQuery q = qdef();
    q.max_lines = -5;
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "showing last 200") == 0);   /* 7 lines < the default of 200: not truncated */
    ASSERT_TRUE(has(out, "7 matching lines"));
    q.max_lines = 99999;
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "7 matching lines"));
    TEST_END();
}

int test_dlog_query_output_is_capped_at_16kb_newest_first(void)
{
    TEST_BEGIN();
    size_t cap = 400 * 200;
    char *fx = (char *)malloc(cap);
    char *out = (char *)malloc(65536);
    ASSERT_NOT_NULL(fx);
    ASSERT_NOT_NULL(out);
    if (fx && out) {
        fx[0] = '\0';
        for (int i = 0; i < 400; i++) {
            char rest[160];
            (void)snprintf(rest, sizeof(rest), "tick n=%d pad=0123456789012345678901234567890123456789012345678901234567890123", i);
            addl(fx, cap, i % 50, 60, "INFO ", "render", "s1", rest);
        }
        DlogQuery q = qdef();
        q.max_lines = 1000;
        size_t n = dlog_query_text(&q, fx, strlen(fx), out, 65536);
        ASSERT_TRUE(n <= (size_t)DLOG_QUERY_OUT_MAX);
        ASSERT_TRUE(n > (size_t)DLOG_QUERY_OUT_MAX - 400);
        ASSERT_TRUE(has(out, "400 matching lines, showing last "));
        ASSERT_TRUE(has(out, "n=399 "));              /* the newest survived */
        ASSERT_FALSE(has(out, "n=0 "));               /* the oldest did not */
        ASSERT_EQ(out[n - 1], '\n');
        /* a small caller buffer is respected too */
        char small[1000];
        n = dlog_query_text(&q, fx, strlen(fx), small, sizeof(small));
        ASSERT_TRUE(n < sizeof(small));
        ASSERT_EQ(n, strlen(small));
    }
    free(fx);
    free(out);
    TEST_END();
}

int test_dlog_query_long_lines_are_cut_with_the_byte_count(void)
{
    TEST_BEGIN();
    char fx[4096], out[8192];
    fx[0] = '\0';
    char rest[2200];
    memset(rest, 'x', sizeof(rest) - 1);
    memcpy(rest, "big pad=", 8);
    rest[sizeof(rest) - 1] = '\0';
    addl(fx, sizeof(fx), 1, 60, "INFO ", "general", "-", rest);
    DlogQuery q = qdef();
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    char *nl = strchr(out, '\n');
    ASSERT_NOT_NULL(nl);
    if (nl) {
        char *l2 = nl + 1;
        char *e = strchr(l2, '\n');
        ASSERT_NOT_NULL(e);
        if (e) {
            ASSERT_TRUE((size_t)(e - l2) < (size_t)DLOG_QUERY_LINE_MAX + 24);
            *e = '\0';
            ASSERT_TRUE(strstr(l2, " +") != NULL);
            ASSERT_TRUE(strstr(l2 + strlen(l2) - 12, "bytes") != NULL);
        }
    }
    TEST_END();
}

int test_dlog_query_ignores_a_partial_last_line(void)
{
    TEST_BEGIN();
    char fx[4096], out[8192];
    fixture1(fx, sizeof(fx));
    fx_append(fx, sizeof(fx), "2026-10-17T14:01:10.000+01:00 INFO  general - ui being.writ");   /* no LF yet */
    DlogQuery q = qdef();
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "7 matching lines"));
    ASSERT_FALSE(has(out, "being.writ"));
    TEST_END();
}

int test_dlog_query_skips_and_counts_malformed_lines(void)
{
    TEST_BEGIN();
    char fx[4096], out[8192];
    fixture1(fx, sizeof(fx));
    fx_append(fx, sizeof(fx), "this is not a log line\n");
    fx_append(fx, sizeof(fx), "\n");
    fx_append(fx, sizeof(fx), "2026-10-17T14:01:10.000+01:00 BOGUS render - ui x\n");
    DlogQuery q = qdef();
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "7 matching lines"));
    ASSERT_TRUE(has(out, "skipped 3 unreadable lines") || has(out, "skipped 2 unreadable lines"));
    ASSERT_FALSE(has(out, "this is not"));
    TEST_END();
}

int test_dlog_query_empty_and_null_text(void)
{
    TEST_BEGIN();
    char out[1024];
    DlogQuery q = qdef();
    ASSERT_TRUE(dlog_query_text(&q, "", 0, out, sizeof(out)) > 0);
    ASSERT_TRUE(has(out, "0 matching lines"));
    ASSERT_TRUE(dlog_query_text(&q, NULL, 0, out, sizeof(out)) > 0);
    ASSERT_EQ(dlog_query_text(&q, "x", 1, out, 0), (size_t)0);
    ASSERT_EQ(dlog_query_text(NULL, "x", 1, out, sizeof(out)), (size_t)0);
    char tiny[20];
    size_t n = dlog_query_text(&q, "", 0, tiny, sizeof(tiny));
    ASSERT_EQ(n, strlen(tiny));
    ASSERT_TRUE(n < sizeof(tiny));
    TEST_END();
}

/* ---- summary ----------------------------------------------------------------------------------- */

int test_dlog_query_summary_counts_and_lists_issues(void)
{
    TEST_BEGIN();
    char fx[4096], out[8192];
    fixture1(fx, sizeof(fx));
    DlogQuery q = qdef();
    q.summary = 1;
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "# summary: 7 matching lines"));
    ASSERT_TRUE(has(out, "categories: "));
    ASSERT_TRUE(has(out, "render=2"));
    ASSERT_TRUE(has(out, "general=2"));
    ASSERT_TRUE(has(out, "levels: "));
    ASSERT_TRUE(has(out, "ERROR=1"));
    ASSERT_TRUE(has(out, "ISSUE=1"));
    ASSERT_TRUE(has(out, "events: "));
    ASSERT_TRUE(has(out, "paint=1"));
    ASSERT_TRUE(has(out, "Paint=1"));
    ASSERT_TRUE(has(out, "issues: "));
    ASSERT_TRUE(has(out, "n=1"));
    ASSERT_TRUE(has(out, "14:01:00"));
    ASSERT_FALSE(has(out, "rows=24"));                /* no log lines in a summary */
    TEST_END();
}

int test_dlog_query_summary_respects_filters(void)
{
    TEST_BEGIN();
    char fx[4096], out[8192];
    fixture1(fx, sizeof(fx));
    DlogQuery q = qdef();
    q.summary = 1;
    q.category = DLOG_SSH;
    (void)dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
    ASSERT_TRUE(has(out, "ssh=1"));
    ASSERT_FALSE(has(out, "render="));
    TEST_END();
}

int test_dlog_query_summary_with_many_distinct_events_stays_small(void)
{
    TEST_BEGIN();
    size_t cap = 300 * 100;
    char *fx = (char *)malloc(cap);
    char out[DLOG_QUERY_OUT_MAX + 16];
    ASSERT_NOT_NULL(fx);
    if (fx) {
        fx[0] = '\0';
        for (int i = 0; i < 300; i++) {
            char rest[40];
            (void)snprintf(rest, sizeof(rest), "ev.%d", i);
            addl(fx, cap, i % 50, 60, "INFO ", "render", "-", rest);
        }
        DlogQuery q = qdef();
        q.summary = 1;
        size_t n = dlog_query_text(&q, fx, strlen(fx), out, sizeof(out));
        ASSERT_TRUE(n < 2500);
        ASSERT_TRUE(has(out, "300 matching lines"));
        ASSERT_TRUE(has(out, "more"));
    }
    free(fx);
    TEST_END();
}

/* ---- files ------------------------------------------------------------------------------------- */

typedef struct {
    int         n;
    const char *name[8];
    const char *text[8];
    long long   mtime[8];
    int         reads;
} FakeSrc;

static int fs_list(void *ctx, DlogFileInfo *out, size_t cap)
{
    FakeSrc *f = (FakeSrc *)ctx;
    int n = 0;
    for (int i = 0; i < f->n && (size_t)n < cap; i++) {
        (void)snprintf(out[n].name, sizeof(out[n].name), "%s", f->name[i]);
        out[n].size = strlen(f->text[i]);
        out[n].mtime_ms = f->mtime[i];
        n++;
    }
    return n;
}

static long fs_read(void *ctx, const char *name, unsigned long long off, void *buf, size_t cap)
{
    FakeSrc *f = (FakeSrc *)ctx;
    f->reads++;
    for (int i = 0; i < f->n; i++) {
        if (strcmp(f->name[i], name) != 0) continue;
        size_t len = strlen(f->text[i]);
        if (off >= len) return 0;
        size_t n = len - (size_t)off;
        if (n > cap) n = cap;
        memcpy(buf, f->text[i] + off, n);
        return (long)n;
    }
    return -1;
}

int test_dlog_query_run_reads_files_oldest_first_and_skips_stale_ones(void)
{
    TEST_BEGIN();
    char a[2048], b[2048], c[2048], out[8192];
    a[0] = b[0] = c[0] = '\0';
    addl(a, sizeof(a), 5, 60, "INFO ", "general", "-", "ancient.line");
    addl(b, sizeof(b), 100, 60, "INFO ", "general", "-", "older.line");
    addl(c, sizeof(c), 200, 60, "INFO ", "general", "-", "newer.line");
    FakeSrc f;
    memset(&f, 0, sizeof(f));
    f.n = 3;
    f.name[0] = "nutshell-20261017-140500-p9.log";   f.text[0] = c;  f.mtime[0] = t0_ms() + 210000;
    f.name[1] = "nutshell-20261017-135000-p8.log";   f.text[1] = a;  f.mtime[1] = t0_ms() - 600000;
    f.name[2] = "nutshell-20261017-140100-p9.log";   f.text[2] = b;  f.mtime[2] = t0_ms() + 110000;
    DlogQueryIO io = { fs_list, fs_read, &f };
    DlogQuery q = qdef();
    q.since_minutes = 5;
    q.now_ms = t0_ms() + 240000;
    size_t n = dlog_query_run(&q, &io, out, sizeof(out));
    ASSERT_EQ(n, strlen(out));
    ASSERT_FALSE(has(out, "ancient.line"));           /* its file is older than the window: never read */
    ASSERT_TRUE(has(out, "older.line"));
    ASSERT_TRUE(has(out, "newer.line"));
    ASSERT_TRUE(strstr(out, "older.line") < strstr(out, "newer.line"));
    ASSERT_TRUE(has(out, "nutshell-20261017-140100-p9.log"));    /* the header names the files */
    TEST_END();
}

int test_dlog_query_run_ignores_issue_files_png_and_foreign_names(void)
{
    TEST_BEGIN();
    char a[2048], b[2048], out[8192];
    a[0] = b[0] = '\0';
    addl(a, sizeof(a), 5, 60, "INFO ", "general", "-", "main.line");
    addl(b, sizeof(b), 6, 60, "INFO ", "general", "-", "issue.file.line");
    FakeSrc f;
    memset(&f, 0, sizeof(f));
    f.n = 4;
    f.name[0] = "nutshell-20261017-140000-p9.log";        f.text[0] = a; f.mtime[0] = t0_ms();
    f.name[1] = "nutshell-20261017-140000-p9.issue1.log"; f.text[1] = b; f.mtime[1] = t0_ms();
    f.name[2] = "nutshell-20261017-140000-p9.issue1.png"; f.text[2] = b; f.mtime[2] = t0_ms();
    f.name[3] = "nutshell-notes.log";                     f.text[3] = b; f.mtime[3] = t0_ms();
    DlogQueryIO io = { fs_list, fs_read, &f };
    DlogQuery q = qdef();
    (void)dlog_query_run(&q, &io, out, sizeof(out));
    ASSERT_TRUE(has(out, "main.line"));
    ASSERT_FALSE(has(out, "issue.file.line"));
    TEST_END();
}

int test_dlog_query_run_with_no_files_says_so(void)
{
    TEST_BEGIN();
    FakeSrc f;
    memset(&f, 0, sizeof(f));
    DlogQueryIO io = { fs_list, fs_read, &f };
    char out[1024];
    DlogQuery q = qdef();
    size_t n = dlog_query_run(&q, &io, out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(has(out, "no log files"));
    ASSERT_EQ(dlog_query_run(&q, NULL, out, sizeof(out)), (size_t)0);
    TEST_END();
}

int test_dlog_query_run_reads_large_files_in_chunks_without_losing_lines(void)
{
    TEST_BEGIN();
    size_t cap = 9000 * 120;
    char *big = (char *)malloc(cap);
    char *out = (char *)malloc(65536);
    ASSERT_NOT_NULL(big);
    ASSERT_NOT_NULL(out);
    if (big && out) {
        big[0] = '\0';
        for (int i = 0; i < 9000; i++) {
            char rest[40];
            (void)snprintf(rest, sizeof(rest), "row n=%d", i);
            addl(big, cap, i % 100, 60, "INFO ", "term", "s1", rest);
        }
        FakeSrc f;
        memset(&f, 0, sizeof(f));
        f.n = 1;
        f.name[0] = "nutshell-20261017-140000-p9.log";
        f.text[0] = big;
        f.mtime[0] = t0_ms();
        DlogQueryIO io = { fs_list, fs_read, &f };
        DlogQuery q = qdef();
        q.max_lines = 1000;
        (void)dlog_query_run(&q, &io, out, 65536);
        ASSERT_TRUE(has(out, "9000 matching lines"));
        ASSERT_TRUE(has(out, "n=8999"));
        ASSERT_TRUE(f.reads > 4);                     /* it did not read the whole file in one go */
    }
    free(big);
    free(out);
    TEST_END();
}

static void issue_fixture(char *a, size_t cap)
{
    a[0] = '\0';
    addl(a, cap, 0, 60, "INFO ", "general", "-", "app.start version=1");
    addl(a, cap, 10, 60, "TRACE", "render", "s2", "far.before rows=1");          /* 5 min before the mark */
    addl(a, cap, 290, 60, "TRACE", "render", "s2", "just.before rows=2");        /* 10 s before */
    addl(a, cap, 300, 60, "ISSUE", "general", "-", "issue.mark n=1 recorder=x screenshot=none");
    addl(a, cap, 300, 60, "ISSUE", "general", "-", "issue.window client=1500x900");
}

int test_dlog_query_around_issue_reads_the_recorder_file_two_minutes_before(void)
{
    TEST_BEGIN();
    char main_log[1024], i1[4096], i2[4096], out[8192];
    main_log[0] = '\0';
    addl(main_log, sizeof(main_log), 1, 60, "INFO ", "general", "-", "main.only");
    issue_fixture(i1, sizeof(i1));
    issue_fixture(i2, sizeof(i2));
    fx_append(i2, sizeof(i2), "2026-10-17T14:05:01.000+01:00 ISSUE general - ui issue.ai panel=1\n");
    FakeSrc f;
    memset(&f, 0, sizeof(f));
    f.n = 3;
    f.name[0] = "nutshell-20261017-140000-p9.log";        f.text[0] = main_log; f.mtime[0] = t0_ms() + 400000;
    f.name[1] = "nutshell-20261017-140000-p9.issue1.log"; f.text[1] = i1;       f.mtime[1] = t0_ms() + 300000;
    f.name[2] = "nutshell-20261017-140000-p9.issue2.log"; f.text[2] = i2;       f.mtime[2] = t0_ms() + 350000;
    DlogQueryIO io = { fs_list, fs_read, &f };
    DlogQuery q = qdef();
    q.since_minutes = 1;                              /* ignored with around_issue */
    q.around_issue = 0;                               /* the latest: issue 2 */
    (void)dlog_query_run(&q, &io, out, sizeof(out));
    ASSERT_TRUE(has(out, "just.before"));
    ASSERT_FALSE(has(out, "far.before"));             /* more than 2 minutes before the mark */
    ASSERT_FALSE(has(out, "main.only"));
    ASSERT_TRUE(has(out, "issue.mark"));
    ASSERT_TRUE(has(out, "issue.window"));
    ASSERT_TRUE(has(out, "issue.ai"));                /* issue 2's own file */
    ASSERT_TRUE(has(out, "issue 2"));                 /* the header says which */
    q.around_issue = 1;
    (void)dlog_query_run(&q, &io, out, sizeof(out));
    ASSERT_FALSE(has(out, "issue.ai"));
    ASSERT_TRUE(has(out, "just.before"));
    ASSERT_TRUE(has(out, "issue 1"));
    TEST_END();
}

int test_dlog_query_around_issue_missing(void)
{
    TEST_BEGIN();
    char i1[4096], out[2048];
    issue_fixture(i1, sizeof(i1));
    FakeSrc f;
    memset(&f, 0, sizeof(f));
    f.n = 1;
    f.name[0] = "nutshell-20261017-140000-p9.issue1.log";
    f.text[0] = i1;
    f.mtime[0] = t0_ms();
    DlogQueryIO io = { fs_list, fs_read, &f };
    DlogQuery q = qdef();
    q.around_issue = 7;
    (void)dlog_query_run(&q, &io, out, sizeof(out));
    ASSERT_TRUE(has(out, "no issue 7"));
    ASSERT_TRUE(has(out, "1"));                       /* it lists the numbers that exist */
    FakeSrc none;
    memset(&none, 0, sizeof(none));
    DlogQueryIO io2 = { fs_list, fs_read, &none };
    q.around_issue = 0;
    (void)dlog_query_run(&q, &io2, out, sizeof(out));
    ASSERT_TRUE(has(out, "no issue marks"));
    TEST_END();
}

int test_dlog_query_around_issue_picks_the_newest_run_for_a_repeated_number(void)
{
    TEST_BEGIN();
    char old_i[2048], new_i[2048], out[4096];
    old_i[0] = new_i[0] = '\0';
    addl(old_i, sizeof(old_i), 10, 60, "INFO ", "general", "-", "old.run");
    addl(old_i, sizeof(old_i), 20, 60, "ISSUE", "general", "-", "issue.mark n=1");
    addl(new_i, sizeof(new_i), 10, 60, "INFO ", "general", "-", "new.run");
    addl(new_i, sizeof(new_i), 20, 60, "ISSUE", "general", "-", "issue.mark n=1");
    FakeSrc f;
    memset(&f, 0, sizeof(f));
    f.n = 2;
    f.name[0] = "nutshell-20261016-100000-p5.issue1.log"; f.text[0] = old_i; f.mtime[0] = t0_ms();
    f.name[1] = "nutshell-20261017-100000-p6.issue1.log"; f.text[1] = new_i; f.mtime[1] = t0_ms();
    DlogQueryIO io = { fs_list, fs_read, &f };
    DlogQuery q = qdef();
    q.around_issue = 1;
    (void)dlog_query_run(&q, &io, out, sizeof(out));
    ASSERT_TRUE(has(out, "new.run"));
    ASSERT_FALSE(has(out, "old.run"));
    TEST_END();
}

/* ---- the tool arguments ----------------------------------------------------------------------------- */

int test_dlog_query_from_json_defaults(void)
{
    TEST_BEGIN();
    DlogQuery q;
    char err[120];
    ASSERT_TRUE(dlog_query_from_json(NULL, &q, err, sizeof(err)));
    ASSERT_EQ(q.since_minutes, 30);
    ASSERT_TRUE(dlog_query_from_json("", &q, err, sizeof(err)));
    ASSERT_TRUE(dlog_query_from_json("{}", &q, err, sizeof(err)));
    ASSERT_EQ(q.max_lines, 200);
    ASSERT_EQ(q.category, -1);
    ASSERT_EQ(q.around_issue, -1);
    TEST_END();
}

int test_dlog_query_from_json_every_key(void)
{
    TEST_BEGIN();
    DlogQuery q;
    char err[120];
    const char *j = "{\"since_minutes\": 90, \"category\": \"shell\", \"min_level\": \"warn\", "
                    "\"contains\": \"Poll\", \"session\": \"s4\", \"around_issue\": 2, "
                    "\"summary\": true, \"max_lines\": 50, \"unknown\": 1}";
    ASSERT_TRUE(dlog_query_from_json(j, &q, err, sizeof(err)));
    ASSERT_EQ(q.since_minutes, 90);
    ASSERT_EQ(q.category, (int)DLOG_SHELL);
    ASSERT_EQ(q.min_level, (int)DLOG_WARN);
    ASSERT_STR_EQ(q.contains, "Poll");
    ASSERT_EQ(q.session, 4u);
    ASSERT_EQ(q.around_issue, 2);
    ASSERT_EQ(q.summary, 1);
    ASSERT_EQ(q.max_lines, 50);
    ASSERT_TRUE(dlog_query_from_json("{\"session\": 7}", &q, err, sizeof(err)));
    ASSERT_EQ(q.session, 7u);
    ASSERT_TRUE(dlog_query_from_json("{\"session\": \"5\"}", &q, err, sizeof(err)));
    ASSERT_EQ(q.session, 5u);
    TEST_END();
}

int test_dlog_query_from_json_clamps_numbers(void)
{
    TEST_BEGIN();
    DlogQuery q;
    char err[120];
    ASSERT_TRUE(dlog_query_from_json("{\"since_minutes\": 0, \"max_lines\": 0}", &q, err, sizeof(err)));
    ASSERT_EQ(q.since_minutes, 1);
    ASSERT_EQ(q.max_lines, 1);
    ASSERT_TRUE(dlog_query_from_json("{\"since_minutes\": 999999, \"max_lines\": 99999}", &q, err, sizeof(err)));
    ASSERT_EQ(q.since_minutes, 1440);
    ASSERT_EQ(q.max_lines, 1000);
    ASSERT_TRUE(dlog_query_from_json("{\"around_issue\": -3}", &q, err, sizeof(err)));
    ASSERT_EQ(q.around_issue, 0);
    char longc[400];
    memset(longc, 'z', sizeof(longc));
    longc[sizeof(longc) - 1] = '\0';
    char json[600];
    (void)snprintf(json, sizeof(json), "{\"contains\": \"%s\"}", longc);
    ASSERT_TRUE(dlog_query_from_json(json, &q, err, sizeof(err)));
    ASSERT_EQ(strlen(q.contains), (size_t)128);
    TEST_END();
}

int test_dlog_query_from_json_rejects_bad_input(void)
{
    TEST_BEGIN();
    DlogQuery q;
    char err[120];
    ASSERT_FALSE(dlog_query_from_json("not json", &q, err, sizeof(err)));
    ASSERT_TRUE(err[0] != '\0');
    ASSERT_FALSE(dlog_query_from_json("[1,2]", &q, err, sizeof(err)));
    ASSERT_FALSE(dlog_query_from_json("{\"category\": \"nope\"}", &q, err, sizeof(err)));
    ASSERT_TRUE(strstr(err, "category") != NULL);
    ASSERT_FALSE(dlog_query_from_json("{\"min_level\": \"loud\"}", &q, err, sizeof(err)));
    ASSERT_TRUE(strstr(err, "min_level") != NULL);
    ASSERT_FALSE(dlog_query_from_json("{\"session\": \"x9\"}", &q, err, sizeof(err)));
    ASSERT_FALSE(dlog_query_from_json("{\"since_minutes\": \"soon\"}", &q, err, sizeof(err)));
    ASSERT_FALSE(dlog_query_from_json("{\"summary\": \"yes\"}", &q, err, sizeof(err)));
    ASSERT_FALSE(dlog_query_from_json("{}", NULL, err, sizeof(err)));
    TEST_END();
}

int test_dlog_query_describe(void)
{
    TEST_BEGIN();
    DlogQuery q;
    char d[200];
    dlog_query_defaults(&q);
    dlog_query_describe(&q, d, sizeof(d));
    ASSERT_STR_EQ(d, "last 30 min");
    q.category = DLOG_RENDER;
    dlog_query_describe(&q, d, sizeof(d));
    ASSERT_STR_EQ(d, "render, last 30 min");
    q.summary = 1;
    q.session = 3;
    dlog_query_describe(&q, d, sizeof(d));
    ASSERT_TRUE(strstr(d, "summary") != NULL);
    ASSERT_TRUE(strstr(d, "s3") != NULL);
    dlog_query_defaults(&q);
    q.around_issue = 0;
    dlog_query_describe(&q, d, sizeof(d));
    ASSERT_STR_EQ(d, "around the latest issue");
    q.around_issue = 4;
    dlog_query_describe(&q, d, sizeof(d));
    ASSERT_STR_EQ(d, "around issue 4");
    char tiny[8];
    dlog_query_describe(&q, tiny, sizeof(tiny));
    ASSERT_TRUE(strlen(tiny) < sizeof(tiny));
    TEST_END();
}

int test_dlog_tool_metadata(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(DLOG_TOOL_NAME, "read_debug_log");
    const char *d = dlog_tool_description();
    ASSERT_NOT_NULL(d);
    ASSERT_TRUE(strlen(d) > 80 && strlen(d) < 900);   /* the description is sent every turn: keep it lean */
    ASSERT_TRUE(strstr(d, "summary") != NULL);
    ASSERT_TRUE(strstr(d, "around_issue") != NULL);
    const char *s = dlog_tool_schema();
    ASSERT_NOT_NULL(s);
    static const char *const keys[] = { "since_minutes", "category", "min_level", "contains", "session",
                                        "around_issue", "summary", "max_lines" };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) ASSERT_TRUE(strstr(s, keys[i]) != NULL);
    for (int c = 0; c < DLOG_CAT_COUNT; c++) ASSERT_TRUE(strstr(s, dlog_cat_name(c)) != NULL);
    ASSERT_TRUE(strstr(s, "\"enum\"") != NULL);
    ASSERT_EQ(DLOG_TOOL_MAX_CALLS_PER_MSG, 4);
    ASSERT_EQ(DLOG_QUERY_OUT_MAX, 16384);
    TEST_END();
}

/* ---- live: the recorder when there is no file --------------------------------------------------------- */

int test_dlog_read_log_not_started_is_harmless(void)
{
    TEST_BEGIN();
    char out[512];
    DlogQuery q;
    dlog_query_defaults(&q);
    size_t n = dlog_read_log(&q, out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(has(out, "not running"));
    TEST_END();
}
