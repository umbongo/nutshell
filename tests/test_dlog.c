/* tests/test_dlog.c -- the core logger: filtering, formatting, redaction,
 * repeat suppression, the rings, file creation / rotation / retention /
 * fallback through the fake platform, issue marks and configuration. */
#include "test_framework.h"
#include "fake_dlog.h"
#include "dlog.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void apply_levels(int enabled, int level_all, int recorder, int content)
{
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = enabled;
    for (int i = 0; i < DLOG_CAT_COUNT; i++) c.levels[i] = (unsigned char)level_all;
    c.recorder = recorder;
    c.include_content = content;
    dlog_apply(&c);
}

static const char *logtext(void)
{
    (void)dlog_pump();
    return fk_text(FK_LOG_FILE);
}

/* Copy the first line of `text` containing `needle` into out. */
static int line_with(const char *text, const char *needle, char *out, size_t cap)
{
    if (!text) return 0;
    const char *p = text;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l < cap) {
            memcpy(out, p, l);
            out[l] = '\0';
            if (strstr(out, needle)) return 1;
        }
        if (!e) break;
        p = e + 1;
    }
    out[0] = '\0';
    return 0;
}

static int count_lines(const char *text, const char *needle)
{
    return fk_count_lines_containing(text, needle);
}

static int all_lines_complete(const char *text)
{
    size_t l = text ? strlen(text) : 0;
    return l == 0 || text[l - 1] == '\n';
}

/* ---- lifecycle and laziness -------------------------------------------------- */

int test_dlog_start_is_lazy_and_never_creates_the_folder(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    ASSERT_TRUE(dlog_is_started());
    (void)dlog_pump();
    ASSERT_EQ(g_fk.ndirs, 0);
    ASSERT_EQ(g_fk.nfiles, 0);
    dlog_shutdown();                       /* --help / --version never get past here */
    ASSERT_EQ(g_fk.ndirs, 0);
    ASSERT_EQ(g_fk.nfiles, 0);
    ASSERT_FALSE(dlog_is_started());
    end();
    TEST_END();
}

int test_dlog_start_twice_is_refused_and_shutdown_is_idempotent(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    ASSERT_FALSE(fk_start(0, 0));
    dlog_shutdown();
    dlog_shutdown();                       /* safe when not started */
    ASSERT_FALSE(dlog_is_started());
    ASSERT_TRUE(fk_start(0, 0));           /* and restartable */
    end();
    TEST_END();
}

int test_dlog_emit_before_start_and_after_shutdown_is_ignored(void)
{
    TEST_BEGIN();
    fk_reset();
    dlog_emit(DLOG_GENERAL, DLOG_ERROR, 0, "early", "x=%d", 1);   /* not started: no crash */
    ASSERT_TRUE(fk_start(0, 0));
    dlog_shutdown();
    dlog_emit(DLOG_GENERAL, DLOG_ERROR, 0, "late", "x=%d", 1);
    ASSERT_EQ(g_fk.nfiles, 0);
    end();
    TEST_END();
}

int test_dlog_start_with_null_platform_or_missing_calls_fails(void)
{
    TEST_BEGIN();
    DlogStartInfo si;
    memset(&si, 0, sizeof(si));
    ASSERT_FALSE(dlog_start_with(NULL, &si));
    DlogPlatform bad = *fk_platform();
    bad.lock = NULL;
    fk_reset();
    ASSERT_FALSE(dlog_start_with(&bad, &si));
    ASSERT_FALSE(dlog_is_started());
    end();
    TEST_END();
}

int test_dlog_off_mode_writes_errors_and_warnings_only(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DLOG_EV(DLOG_RENDER, DLOG_TRACE, 0, "t.trace");
    DLOG_EV(DLOG_AI, DLOG_DEBUG, 0, "t.debug");
    DLOG_EV(DLOG_SSH, DLOG_INFO, 0, "t.info");
    DLOG_EV(DLOG_SHELL, DLOG_WARN, 0, "t.warn");
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "t.error");
    const char *t = logtext();
    ASSERT_NOT_NULL(t);
    if (t) {
        ASSERT_EQ(count_lines(t, "t.trace"), 0);
        ASSERT_EQ(count_lines(t, "t.debug"), 0);
        ASSERT_EQ(count_lines(t, "t.info"), 0);
        ASSERT_EQ(count_lines(t, "t.warn"), 1);
        ASSERT_EQ(count_lines(t, "t.error"), 1);
        ASSERT_EQ(count_lines(t, "app.start"), 1);
    }
    end();
    TEST_END();
}

int test_dlog_clean_run_with_info_only_leaves_no_file(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DLOG_EV(DLOG_GENERAL, DLOG_INFO, 0, "just.info");
    (void)dlog_pump();
    dlog_shutdown();
    ASSERT_EQ(g_fk.nfiles, 0);
    ASSERT_EQ(g_fk.ndirs, 0);
    end();
    TEST_END();
}

int test_dlog_first_line_creates_folder_file_header_and_format_txt(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DLOG(DLOG_GENERAL, DLOG_ERROR, 0, "boom", "code=%d", 7);
    const char *t = logtext();
    ASSERT_NOT_NULL(t);
    ASSERT_EQ(g_fk.ndirs, 1);
    ASSERT_STR_EQ(g_fk.dirs[0], FK_LOG_DIR);
    ASSERT_NOT_NULL(fk_find(FK_LOG_DIR "\\FORMAT.txt"));
    const char *ft = fk_text(FK_LOG_DIR "\\FORMAT.txt");
    ASSERT_TRUE(ft && strstr(ft, "1.2.36") != NULL);
    if (t) {
        /* the header comes first, then the line */
        ASSERT_TRUE(strncmp(t, "2026-10-17T14:03:10.000+01:00 INFO  general - ui app.start version=1.2.36 pid=8124", 80) == 0);
        ASSERT_TRUE(strstr(t, "exe=\"C:\\\\Tools\\\\nutshell\\\\nutshell.exe\"") != NULL);
        ASSERT_TRUE(strstr(t, "content=0") != NULL);
        char l[512];
        ASSERT_TRUE(line_with(t, "boom", l, sizeof(l)));
        ASSERT_STR_EQ(l, "2026-10-17T14:03:10.000+01:00 ERROR general - ui boom code=7");
        ASSERT_TRUE(all_lines_complete(t));
    }
    end();
    TEST_END();
}

/* ---- filtering ------------------------------------------------------------------ */

int test_dlog_every_level_against_every_threshold(void)
{
    TEST_BEGIN();
    static const DlogEvLevel evs[5] = { DLOG_ERROR, DLOG_WARN, DLOG_INFO, DLOG_DEBUG, DLOG_TRACE };
    for (int lvl = DLOG_LVL_OFF; lvl < DLOG_LVL_COUNT; lvl++) {
        ASSERT_TRUE(begin(0, 0));
        apply_levels(1, lvl, 0, 0);
        for (int e = 0; e < 5; e++)
            DLOG(DLOG_TERM, evs[e], 0, "probe", "e=%d", e);
        const char *t = logtext();
        int expect = 0;
        for (int e = 0; e < 5; e++) if ((int)evs[e] <= dlog_level_max_event(lvl)) expect++;
        int got = t ? count_lines(t, " probe ") : 0;
        if (got != expect) printf("  level %d expected %d got %d\n", lvl, expect, got);
        ASSERT_EQ(got, expect);
        end();
    }
    TEST_END();
}

int test_dlog_off_category_writes_nothing_but_others_do(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = 1;
    c.recorder = 0;
    c.levels[DLOG_RENDER] = DLOG_LVL_OFF;
    c.levels[DLOG_AI] = DLOG_LVL_TRACE;
    dlog_apply(&c);
    DLOG_EV(DLOG_RENDER, DLOG_ERROR, 0, "r.err");
    DLOG_EV(DLOG_AI, DLOG_TRACE, 0, "a.trace");
    const char *t = logtext();
    ASSERT_NOT_NULL(t);
    if (t) {
        ASSERT_EQ(count_lines(t, "r.err"), 0);
        ASSERT_EQ(count_lines(t, "a.trace"), 1);
    }
    end();
    TEST_END();
}

int test_dlog_issue_level_passes_every_threshold_including_off(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply_levels(1, DLOG_LVL_OFF, 0, 0);
    DLOG_EV(DLOG_GENERAL, DLOG_ISSUE, 0, "forced");
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "not.forced");
    const char *t = logtext();
    ASSERT_NOT_NULL(t);
    if (t) {
        char l[300];
        ASSERT_TRUE(line_with(t, "forced", l, sizeof(l)));
        ASSERT_TRUE(strstr(l, " ISSUE general ") != NULL);
        ASSERT_EQ(count_lines(t, "not.forced"), 0);
    }
    end();
    TEST_END();
}

static int g_side_effect;
static int bump(void) { return ++g_side_effect; }

int test_dlog_macro_does_not_evaluate_arguments_below_capture(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    g_side_effect = 0;
    DLOG(DLOG_RENDER, DLOG_TRACE, 0, "x", "n=%d", bump());   /* off mode: nothing captured */
    ASSERT_EQ(g_side_effect, 0);
    DLOG(DLOG_RENDER, DLOG_ERROR, 0, "x", "n=%d", bump());
    ASSERT_EQ(g_side_effect, 1);
    end();
    TEST_END();
}

int test_dlog_recorder_captures_trace_while_file_gets_the_chosen_level(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply_levels(1, DLOG_LVL_INFO, 1, 0);
    ASSERT_EQ(dlog_capture_level(DLOG_RENDER), (int)DLOG_TRACE);
    DLOG_EV(DLOG_RENDER, DLOG_INFO, 0, "kept.info");
    DLOG_EV(DLOG_RENDER, DLOG_TRACE, 0, "detail.trace");
    const char *t = logtext();
    ASSERT_NOT_NULL(t);
    if (t) {
        ASSERT_EQ(count_lines(t, "kept.info"), 1);
        ASSERT_EQ(count_lines(t, "detail.trace"), 0);       /* not in the file */
    }
    char rec[8192];
    size_t n = dlog_recorder_copy(rec, sizeof(rec) - 1);
    rec[n] = '\0';
    ASSERT_EQ(count_lines(rec, "detail.trace"), 1);          /* but in the recorder */
    ASSERT_EQ(count_lines(rec, "kept.info"), 1);
    end();
    TEST_END();
}

int test_dlog_recorder_off_means_capture_equals_file_level(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply_levels(1, DLOG_LVL_INFO, 0, 0);
    ASSERT_EQ(dlog_capture_level(DLOG_RENDER), (int)DLOG_INFO);
    DLOG_EV(DLOG_RENDER, DLOG_TRACE, 0, "detail.trace");
    DLOG_EV(DLOG_RENDER, DLOG_INFO, 0, "kept.info");
    char rec[4096];
    size_t n = dlog_recorder_copy(rec, sizeof(rec) - 1);
    rec[n] = '\0';
    ASSERT_EQ(count_lines(rec, "detail.trace"), 0);
    ASSERT_EQ(count_lines(rec, "kept.info"), 1);
    end();
    TEST_END();
}

int test_dlog_recorder_skips_categories_set_to_off(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = 1;
    c.recorder = 1;
    c.levels[DLOG_SSH] = DLOG_LVL_OFF;
    dlog_apply(&c);
    ASSERT_EQ(dlog_capture_level(DLOG_SSH), 0);
    ASSERT_EQ(dlog_capture_level(DLOG_AI), (int)DLOG_TRACE);
    end();
    TEST_END();
}

int test_dlog_off_mode_recorder_holds_errors_only(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    ASSERT_EQ(dlog_capture_level(DLOG_RENDER), (int)DLOG_WARN);
    DLOG_EV(DLOG_RENDER, DLOG_INFO, 0, "i");
    DLOG_EV(DLOG_RENDER, DLOG_WARN, 0, "w");
    char rec[4096];
    size_t n = dlog_recorder_copy(rec, sizeof(rec) - 1);
    rec[n] = '\0';
    ASSERT_EQ(count_lines(rec, " i"), 0);
    ASSERT_EQ(count_lines(rec, " w"), 1);
    end();
    TEST_END();
}

/* ---- line format ------------------------------------------------------------------ */

int test_dlog_line_prefix_layout(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply_levels(1, DLOG_LVL_TRACE, 0, 0);
    fk_advance(418);
    DLOG(DLOG_RENDER, DLOG_TRACE, 2, "poll.cooldown_skip", "poll_rc=%d dirty_rows=%d since_paint_ms=%u", 1, 73, 9u);
    const char *t = logtext();
    char l[400];
    ASSERT_TRUE(line_with(t, "poll.cooldown_skip", l, sizeof(l)));
    ASSERT_STR_EQ(l, "2026-10-17T14:03:10.418+01:00 TRACE render s2 ui poll.cooldown_skip poll_rc=1 dirty_rows=73 since_paint_ms=9");
    end();
    TEST_END();
}

int test_dlog_event_without_fields_has_no_trailing_space(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "bare");
    const char *t = logtext();
    char l[300];
    ASSERT_TRUE(line_with(t, "bare", l, sizeof(l)));
    ASSERT_STR_EQ(l, "2026-10-17T14:03:10.000+01:00 ERROR general - ui bare");
    end();
    TEST_END();
}

int test_dlog_thread_tag_is_printed_and_cut(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    dlog_set_thread_tag("ssh");
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "from.ssh");
    dlog_set_thread_tag("a-very-long-thread-name");
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "from.long");
    dlog_set_thread_tag(NULL);
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "from.none");
    dlog_set_thread_tag("ui");
    const char *t = logtext();
    char l[300];
    ASSERT_TRUE(line_with(t, "from.ssh", l, sizeof(l)));
    ASSERT_TRUE(strstr(l, " general - ssh from.ssh") != NULL);
    ASSERT_TRUE(line_with(t, "from.long", l, sizeof(l)));
    ASSERT_TRUE(strstr(l, " general - a-very- from.long") != NULL);
    ASSERT_TRUE(line_with(t, "from.none", l, sizeof(l)));
    ASSERT_TRUE(strstr(l, " general - ? from.none") != NULL);
    end();
    TEST_END();
}

int test_dlog_offsets_negative_zero_and_half_hour(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    g_fk.offset_min = -300;
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "neg");
    g_fk.offset_min = 0;
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "zero");
    g_fk.offset_min = 330;
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "half");
    const char *t = logtext();
    ASSERT_TRUE(strstr(t, "2026-10-17T08:03:10.000-05:00 ERROR general - ui neg") != NULL);
    ASSERT_TRUE(strstr(t, "2026-10-17T13:03:10.000+00:00 ERROR general - ui zero") != NULL);
    ASSERT_TRUE(strstr(t, "2026-10-17T18:33:10.000+05:30 ERROR general - ui half") != NULL);
    end();
    TEST_END();
}

static void body(char *out, size_t cap, const char *fmt_value_text)
{
    (void)dlog_format_body(out, cap, "e", "k=%s", fmt_value_text);
}

int test_dlog_format_plain_and_quoted_values(void)
{
    TEST_BEGIN();
    char b[256];
    body(b, sizeof(b), "plain");
    ASSERT_STR_EQ(b, "e k=plain");
    body(b, sizeof(b), "has space");
    ASSERT_STR_EQ(b, "e k=\"has space\"");
    body(b, sizeof(b), "a=b");
    ASSERT_STR_EQ(b, "e k=\"a=b\"");
    body(b, sizeof(b), "say \"hi\"");
    ASSERT_STR_EQ(b, "e k=\"say \\\"hi\\\"\"");
    body(b, sizeof(b), "C:\\Tools");
    ASSERT_STR_EQ(b, "e k=\"C:\\\\Tools\"");
    body(b, sizeof(b), "");
    ASSERT_STR_EQ(b, "e k=\"\"");
    body(b, sizeof(b), "path/with:colon,comma");
    ASSERT_STR_EQ(b, "e k=path/with:colon,comma");
    const char *no_str = NULL;
    (void)dlog_format_body(b, sizeof(b), "e", "k=%s", no_str);
    ASSERT_STR_EQ(b, "e k=-");
    TEST_END();
}

int test_dlog_format_control_bytes_and_invalid_utf8_are_hex_escaped(void)
{
    TEST_BEGIN();
    char b[256];
    body(b, sizeof(b), "a\nb");
    ASSERT_STR_EQ(b, "e k=\"a\\x0ab\"");
    body(b, sizeof(b), "tab\there");
    ASSERT_STR_EQ(b, "e k=\"tab\\x09here\"");
    body(b, sizeof(b), "esc\x1b[0m");
    ASSERT_STR_EQ(b, "e k=\"esc\\x1b[0m\"");
    body(b, sizeof(b), "del\x7f");
    ASSERT_STR_EQ(b, "e k=\"del\\x7f\"");
    body(b, sizeof(b), "bad\xff" "byte");
    ASSERT_STR_EQ(b, "e k=\"bad\\xffbyte\"");
    body(b, sizeof(b), "trunc\xe2\x82");            /* incomplete 3-byte sequence */
    ASSERT_STR_EQ(b, "e k=\"trunc\\xe2\\x82\"");
    body(b, sizeof(b), "over\xc0\x80long");          /* overlong NUL */
    ASSERT_STR_EQ(b, "e k=\"over\\xc0\\x80long\"");
    body(b, sizeof(b), "sur\xed\xa0\x80rogate");      /* UTF-16 surrogate */
    ASSERT_STR_EQ(b, "e k=\"sur\\xed\\xa0\\x80rogate\"");
    TEST_END();
}

int test_dlog_format_valid_utf8_passes_through_unquoted(void)
{
    TEST_BEGIN();
    char b[256];
    body(b, sizeof(b), "caf\xc3\xa9");                /* cafe with e-acute */
    ASSERT_STR_EQ(b, "e k=caf\xc3\xa9");
    body(b, sizeof(b), "\xe2\x82\xac" "5");           /* euro sign */
    ASSERT_STR_EQ(b, "e k=\xe2\x82\xac" "5");
    body(b, sizeof(b), "\xf0\x9f\x98\x80");           /* emoji, 4 bytes */
    ASSERT_STR_EQ(b, "e k=\xf0\x9f\x98\x80");
    body(b, sizeof(b), "caf\xc3\xa9 au lait");
    ASSERT_STR_EQ(b, "e k=\"caf\xc3\xa9 au lait\"");
    TEST_END();
}

int test_dlog_format_numbers_and_conversions(void)
{
    TEST_BEGIN();
    char b[512];
    (void)dlog_format_body(b, sizeof(b), "n", "a=%d b=%u c=%lld d=%zu e=%x f=%X g=%o", -5, 7u, -123456789012LL, (size_t)42, 255u, 255u, 8u);
    ASSERT_STR_EQ(b, "n a=-5 b=7 c=-123456789012 d=42 e=ff f=FF g=10");
    (void)dlog_format_body(b, sizeof(b), "n", "a=%5d b=%-5d| c=%05d d=%+d e=%#x", 42, 42, 42, 42, 255u);
    ASSERT_STR_EQ(b, "n a=   42 b=42   | c=00042 d=+42 e=0xff");
    (void)dlog_format_body(b, sizeof(b), "n", "a=%.2f b=%g c=%.0f", 3.14159, 0.5, 2.0);
    ASSERT_STR_EQ(b, "n a=3.14 b=0.5 c=2");
    (void)dlog_format_body(b, sizeof(b), "n", "a=%c b=%c p=%d%%", 'x', ' ', 50);
    ASSERT_STR_EQ(b, "n a=x b=\" \" p=50%");
    (void)dlog_format_body(b, sizeof(b), "n", "a=%ld b=%lu c=%hd d=%hhu e=%llu", -7L, 8UL, (short)-3, (unsigned char)200, 5ULL);
    ASSERT_STR_EQ(b, "n a=-7 b=8 c=-3 d=200 e=5");
    (void)dlog_format_body(b, sizeof(b), "n", "w=%*d s=%.*s t=%.3s", 4, 7, 2, "abcdef", "abcdef");
    ASSERT_STR_EQ(b, "n w=   7 s=ab t=abc");
    int x = 0;
    (void)dlog_format_body(b, sizeof(b), "n", "p=%p", (void *)&x);
    ASSERT_TRUE(strncmp(b, "n p=", 4) == 0 && strlen(b) > 5);
    TEST_END();
}

int test_dlog_format_event_name_is_sanitised(void)
{
    TEST_BEGIN();
    char b[256];
    (void)dlog_format_body(b, sizeof(b), "bad name\n=x", "k=%d", 1);
    ASSERT_STR_EQ(b, "bad_name__x k=1");
    (void)dlog_format_body(b, sizeof(b), "", "k=%d", 1);
    ASSERT_STR_EQ(b, "- k=1");
    (void)dlog_format_body(b, sizeof(b), NULL, "k=%d", 1);
    ASSERT_STR_EQ(b, "- k=1");
    TEST_END();
}

int test_dlog_format_cuts_at_the_cap_and_says_how_much(void)
{
    TEST_BEGIN();
    char big[6000];
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    char b[8192];
    size_t n = dlog_format_body(b, sizeof(b), "e", "k=%s", big);
    ASSERT_EQ(n, strlen(b));
    ASSERT_TRUE(n < (size_t)DLOG_LINE_MAX);
    ASSERT_TRUE(n > (size_t)DLOG_LINE_MAX - 40);
    ASSERT_TRUE(strncmp(b, "e k=aaaa", 8) == 0);
    ASSERT_STR_EQ(b + n - 12, " truncated=1");
    /* a value that fits is not marked */
    (void)dlog_format_body(b, sizeof(b), "e", "k=%s", "short");
    ASSERT_TRUE(strstr(b, "truncated") == NULL);
    TEST_END();
}

int test_dlog_format_cut_closes_the_quote_and_never_splits_utf8(void)
{
    TEST_BEGIN();
    char big[6000];
    for (size_t i = 0; i + 1 < sizeof(big); i++) big[i] = (i % 2) ? 'a' : ' ';
    big[sizeof(big) - 1] = '\0';
    char b[4096];
    (void)dlog_format_body(b, sizeof(b), "e", "k=%s", big);
    const char *tr = strstr(b, "\" truncated=1");
    ASSERT_NOT_NULL(tr);                                   /* quote closed before the trunc marker */
    char u[6000];
    size_t o = 0;
    for (; o + 3 < sizeof(u) - 1; o += 3) { u[o] = (char)0xE2; u[o + 1] = (char)0x82; u[o + 2] = (char)0xAC; }
    u[o] = '\0';
    (void)dlog_format_body(b, sizeof(b), "e", "k=%s", u);
    /* every byte >= 0x80 sits in a complete euro sign */
    const unsigned char *p = (const unsigned char *)strstr(b, "k=") + 2;
    int ok = 1;
    while (*p && !(p[0] == ' ' && p[1] == 't' && p[2] == 'r')) {
        if (p[0] == 0xE2) { if (p[1] != 0x82 || p[2] != 0xAC) ok = 0; p += 3; }
        else { ok = 0; p++; }
    }
    ASSERT_TRUE(ok);
    TEST_END();
}

int test_dlog_emitted_long_line_is_capped_with_newline(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    char big[6000];
    memset(big, 'z', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    DLOG(DLOG_GENERAL, DLOG_ERROR, 0, "huge", "v=%s", big);
    const char *t = logtext();
    char l[8192];
    ASSERT_TRUE(line_with(t, "huge", l, sizeof(l)));
    ASSERT_TRUE(strlen(l) < (size_t)DLOG_LINE_MAX);
    ASSERT_TRUE(strstr(l, " truncated=1") != NULL);
    ASSERT_TRUE(all_lines_complete(t));
    end();
    TEST_END();
}

int test_dlog_emit_fields_is_taken_verbatim(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    dlog_emit_fields(DLOG_GENERAL, DLOG_ERROR, 3, "pre", "a=\"x y\" b=2");
    const char *t = logtext();
    char l[300];
    ASSERT_TRUE(line_with(t, " pre ", l, sizeof(l)));
    ASSERT_TRUE(strstr(l, " ERROR general s3 ui pre a=\"x y\" b=2") != NULL);
    end();
    TEST_END();
}

/* ---- repeat suppression --------------------------------------------------------------- */

int test_dlog_identical_lines_collapse_into_one_and_a_repeat(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply_levels(1, DLOG_LVL_TRACE, 0, 0);
    for (int i = 0; i < 4; i++) DLOG(DLOG_RENDER, DLOG_TRACE, 2, "paint", "rows=%d", 73);
    fk_advance(7);
    DLOG(DLOG_RENDER, DLOG_TRACE, 2, "other", "x=%d", 1);
    const char *t = logtext();
    ASSERT_EQ(count_lines(t, " paint "), 1);
    char l[300];
    ASSERT_TRUE(line_with(t, " repeat ", l, sizeof(l)));
    ASSERT_TRUE(strstr(l, " TRACE render s2 ui repeat n=3 last=2026-10-17T14:03:10.000+01:00 of=paint") != NULL);
    ASSERT_EQ(count_lines(t, " other "), 1);
    /* order: paint, repeat, other */
    ASSERT_TRUE(strstr(t, " paint ") < strstr(t, " repeat "));
    ASSERT_TRUE(strstr(t, " repeat ") < strstr(t, " other "));
    end();
    TEST_END();
}

int test_dlog_a_differing_field_breaks_the_run(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply_levels(1, DLOG_LVL_TRACE, 0, 0);
    DLOG(DLOG_RENDER, DLOG_TRACE, 2, "paint", "rows=%d", 73);
    DLOG(DLOG_RENDER, DLOG_TRACE, 2, "paint", "rows=%d", 74);
    DLOG(DLOG_RENDER, DLOG_TRACE, 2, "paint", "rows=%d", 73);
    const char *t = logtext();
    ASSERT_EQ(count_lines(t, " paint "), 3);
    ASSERT_EQ(count_lines(t, " repeat "), 0);
    end();
    TEST_END();
}

int test_dlog_other_session_or_level_breaks_the_run(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply_levels(1, DLOG_LVL_TRACE, 0, 0);
    DLOG(DLOG_RENDER, DLOG_TRACE, 2, "paint", "rows=%d", 1);
    DLOG(DLOG_RENDER, DLOG_TRACE, 3, "paint", "rows=%d", 1);     /* other session */
    DLOG(DLOG_RENDER, DLOG_DEBUG, 3, "paint", "rows=%d", 1);     /* other level */
    const char *t = logtext();
    ASSERT_EQ(count_lines(t, " paint "), 3);
    end();
    TEST_END();
}

int test_dlog_runs_are_tracked_per_category(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply_levels(1, DLOG_LVL_TRACE, 0, 0);
    /* render repeats, with a term line in between: the term line is a different
     * category so it does not interrupt render's run */
    DLOG(DLOG_RENDER, DLOG_TRACE, 2, "paint", "rows=%d", 1);
    DLOG(DLOG_TERM, DLOG_TRACE, 2, "mode", "alt=%d", 1);
    DLOG(DLOG_RENDER, DLOG_TRACE, 2, "paint", "rows=%d", 1);
    DLOG(DLOG_RENDER, DLOG_TRACE, 2, "paint", "rows=%d", 1);
    const char *t = logtext();
    ASSERT_EQ(count_lines(t, " paint "), 1);
    ASSERT_EQ(count_lines(t, " mode "), 1);
    end();
    dlog_shutdown();
    TEST_END();
}

int test_dlog_repeat_is_flushed_after_five_seconds_by_tick(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply_levels(1, DLOG_LVL_TRACE, 0, 0);
    DLOG(DLOG_RENDER, DLOG_TRACE, 2, "paint", "rows=%d", 1);
    DLOG(DLOG_RENDER, DLOG_TRACE, 2, "paint", "rows=%d", 1);
    DLOG(DLOG_RENDER, DLOG_TRACE, 2, "paint", "rows=%d", 1);
    fk_advance(4000);
    dlog_tick();
    const char *t = logtext();
    ASSERT_EQ(count_lines(t, " repeat "), 0);                 /* not yet */
    fk_advance(1500);
    dlog_tick();
    t = logtext();
    ASSERT_EQ(count_lines(t, " repeat n=2 "), 1);
    /* the run restarts: more identical lines count again */
    DLOG(DLOG_RENDER, DLOG_TRACE, 2, "paint", "rows=%d", 1);
    DLOG(DLOG_RENDER, DLOG_TRACE, 2, "paint", "rows=%d", 1);
    fk_advance(6000);
    DLOG(DLOG_RENDER, DLOG_TRACE, 2, "paint", "rows=%d", 1);   /* the emitter flushes too */
    t = logtext();
    ASSERT_EQ(count_lines(t, " repeat n=3 "), 1);
    end();
    TEST_END();
}

int test_dlog_shutdown_flushes_a_pending_repeat(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DLOG(DLOG_GENERAL, DLOG_ERROR, 0, "same", "x=%d", 1);
    DLOG(DLOG_GENERAL, DLOG_ERROR, 0, "same", "x=%d", 1);
    DLOG(DLOG_GENERAL, DLOG_ERROR, 0, "same", "x=%d", 1);
    dlog_shutdown();
    const char *t = fk_text(FK_LOG_FILE);
    ASSERT_NOT_NULL(t);
    ASSERT_EQ(count_lines(t, " same "), 1);
    ASSERT_EQ(count_lines(t, " repeat n=2 "), 1);
    ASSERT_TRUE(all_lines_complete(t));
    end();
    TEST_END();
}

/* ---- redaction -------------------------------------------------------------------------- */

int test_dlog_sensitive_key_names(void)
{
    TEST_BEGIN();
    static const char *const bad[] = {
        "pass", "password", "passphrase", "PASSWORD", "user_pass", "secret", "client_secret",
        "token", "api_token", "Token", "key", "api_key", "apikey", "privkey", "auth",
        "authorization", "cookie", "set_cookie", "Cookie", "auth_header", "private_key_bytes"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        if (!dlog_key_is_sensitive(bad[i])) printf("  should be sensitive: %s\n", bad[i]);
        ASSERT_TRUE(dlog_key_is_sensitive(bad[i]));
    }
    static const char *const good[] = {
        "name", "rows", "cols", "host", "user", "event", "sid", "len", "bytes", "n",
        "poll_rc", "dirty_rows", "version", "pid", "dir", "mods", "keepalive", "provider"
    };
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++) {
        if (dlog_key_is_sensitive(good[i])) printf("  should not be sensitive: %s\n", good[i]);
        ASSERT_FALSE(dlog_key_is_sensitive(good[i]));
    }
    ASSERT_FALSE(dlog_key_is_sensitive(NULL));
    ASSERT_FALSE(dlog_key_is_sensitive(""));
    TEST_END();
}

int test_dlog_known_safe_names_are_not_redacted(void)
{
    TEST_BEGIN();
    /* counts and method names that the spec needs: tokens in/out, the auth method */
    static const char *const ok[] = {
        "auth_method", "auth_type", "auth_result", "key_name", "key_type", "key_mods",
        "tokens", "tokens_in", "tokens_out", "token_count", "input_tokens", "output_tokens", "max_tokens"
    };
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
        if (dlog_key_is_sensitive(ok[i])) printf("  allow-listed name is redacted: %s\n", ok[i]);
        ASSERT_FALSE(dlog_key_is_sensitive(ok[i]));
    }
    /* but a near miss is still redacted */
    ASSERT_TRUE(dlog_key_is_sensitive("auth_method_secret"));
    ASSERT_TRUE(dlog_key_is_sensitive("tokens_secret"));
    TEST_END();
}

int test_dlog_sensitive_fields_are_redacted_and_arguments_stay_aligned(void)
{
    TEST_BEGIN();
    char b[512];
    (void)dlog_format_body(b, sizeof(b), "login", "user=%s password=%s next=%d", "bob", "hunter2", 9);
    ASSERT_STR_EQ(b, "login user=bob password=[REDACTED] next=9");
    (void)dlog_format_body(b, sizeof(b), "x", "token=%d after=%s", 12345, "ok");
    ASSERT_STR_EQ(b, "x token=[REDACTED] after=ok");
    (void)dlog_format_body(b, sizeof(b), "x", "api_key=%s n=%lld", "sk-live-abc", 5LL);
    ASSERT_STR_EQ(b, "x api_key=[REDACTED] n=5");
    (void)dlog_format_body(b, sizeof(b), "x", "cookie=%.*s z=%c", 3, "abcdef", 'q');
    ASSERT_STR_EQ(b, "x cookie=[REDACTED] z=q");
    (void)dlog_format_body(b, sizeof(b), "x", "auth_method=%s tokens_in=%d", "publickey", 77);
    ASSERT_STR_EQ(b, "x auth_method=publickey tokens_in=77");
    TEST_END();
}

int test_dlog_redaction_never_lets_the_value_reach_the_file(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DLOG(DLOG_SSH, DLOG_ERROR, 1, "ssh.auth", "passphrase=%s", "correct horse battery staple");
    const char *t = logtext();
    ASSERT_TRUE(strstr(t, "correct") == NULL);
    ASSERT_TRUE(strstr(t, "passphrase=[REDACTED]") != NULL);
    end();
    TEST_END();
}

int test_dlog_content_is_dropped_unless_the_tickbox_is_on(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply_levels(1, DLOG_LVL_TRACE, 0, 0);
    ASSERT_FALSE(dlog_content_enabled());
    DLOG_CONTENT(DLOG_TERM, DLOG_TRACE, 2, "term.rx", "data", "secret-ish\r\n", 12);
    const char *t = logtext();
    ASSERT_EQ(count_lines(t, "term.rx"), 0);
    apply_levels(1, DLOG_LVL_TRACE, 0, 1);
    ASSERT_TRUE(dlog_content_enabled());
    DLOG_CONTENT(DLOG_TERM, DLOG_TRACE, 2, "term.rx", "data", "hello\r\n\x1b[0m", 11);
    t = logtext();
    char l[400];
    ASSERT_TRUE(line_with(t, "term.rx", l, sizeof(l)));
    ASSERT_TRUE(strstr(l, " TRACE term s2 ui term.rx data=\"hello\\r\\n\\e[0m\" len=11") != NULL);
    apply_levels(1, DLOG_LVL_TRACE, 0, 0);
    ASSERT_FALSE(dlog_content_enabled());
    end();
    TEST_END();
}

int test_dlog_content_below_the_category_level_is_dropped(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply_levels(1, DLOG_LVL_INFO, 0, 1);
    DLOG_CONTENT(DLOG_TERM, DLOG_TRACE, 0, "term.rx", "data", "abc", 3);
    const char *t = logtext();
    ASSERT_EQ(count_lines(t, "term.rx"), 0);
    end();
    TEST_END();
}

int test_dlog_content_is_cut_at_the_line_cap(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply_levels(1, DLOG_LVL_TRACE, 0, 1);
    char big[9000];
    memset(big, 'q', sizeof(big));
    DLOG_CONTENT(DLOG_TERM, DLOG_TRACE, 1, "term.rx", "data", big, sizeof(big));
    const char *t = logtext();
    char l[8192];
    ASSERT_TRUE(line_with(t, "term.rx", l, sizeof(l)));
    ASSERT_TRUE(strlen(l) < (size_t)DLOG_LINE_MAX);
    ASSERT_TRUE(strstr(l, "len=9000") != NULL);
    ASSERT_TRUE(strstr(l, " truncated=1") != NULL);
    end();
    TEST_END();
}

int test_dlog_secrets_are_scrubbed_from_content_not_from_fields(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply_levels(1, DLOG_LVL_TRACE, 0, 1);
    ASSERT_TRUE(dlog_secret_add("hunter2xyz"));
    DLOG_CONTENT(DLOG_TERM, DLOG_TRACE, 1, "term.rx", "data", "pw is hunter2xyz ok hunter2xyz", 31);
    const char *t = logtext();
    ASSERT_TRUE(strstr(t, "hunter2xyz") == NULL);
    ASSERT_TRUE(strstr(t, "pw is [REDACTED] ok [REDACTED]") != NULL);
    dlog_secret_remove("hunter2xyz");
    DLOG_CONTENT(DLOG_TERM, DLOG_TRACE, 1, "term.rx2", "data", "pw is hunter2xyz", 16);
    t = logtext();
    ASSERT_TRUE(strstr(t, "pw is hunter2xyz") != NULL);
    end();
    TEST_END();
}

int test_dlog_scrub_copy_cases(void)
{
    TEST_BEGIN();
    char out[128];
    dlog_secret_clear();
    ASSERT_TRUE(dlog_secret_add("abcdef"));
    size_t n = dlog_scrub_copy(out, sizeof(out), "xxabcdef", 8);          /* at the very end */
    ASSERT_STR_EQ(out, "xx[REDACTED]");
    ASSERT_EQ(n, strlen(out));
    n = dlog_scrub_copy(out, sizeof(out), "abcdefabcdef", 12);           /* back to back */
    ASSERT_STR_EQ(out, "[REDACTED][REDACTED]");
    n = dlog_scrub_copy(out, sizeof(out), "abcde", 5);                   /* prefix only: stays */
    ASSERT_STR_EQ(out, "abcde");
    n = dlog_scrub_copy(out, sizeof(out), "ab\0cdef", 7);                /* embedded NUL: length wins */
    ASSERT_EQ(n, (size_t)7);
    n = dlog_scrub_copy(out, 8, "abcdef-xyz", 10);                       /* dst too small */
    ASSERT_TRUE(n < 8);
    ASSERT_EQ(out[n], '\0');
    n = dlog_scrub_copy(out, sizeof(out), "", 0);
    ASSERT_EQ(n, (size_t)0);
    dlog_secret_clear();
    n = dlog_scrub_copy(out, sizeof(out), "abcdef", 6);
    ASSERT_STR_EQ(out, "abcdef");
    TEST_END();
}

int test_dlog_secret_table_limits(void)
{
    TEST_BEGIN();
    dlog_secret_clear();
    ASSERT_FALSE(dlog_secret_add(NULL));
    ASSERT_FALSE(dlog_secret_add(""));
    ASSERT_FALSE(dlog_secret_add("ab"));                    /* too short to scrub sensibly */
    char s[16];
    for (int i = 0; i < DLOG_SECRETS_MAX; i++) {
        (void)snprintf(s, sizeof(s), "secret-%d", i);
        ASSERT_TRUE(dlog_secret_add(s));
    }
    ASSERT_FALSE(dlog_secret_add("one-too-many"));
    ASSERT_TRUE(dlog_secret_add("secret-3"));               /* a duplicate is fine */
    dlog_secret_remove("secret-3");
    ASSERT_TRUE(dlog_secret_add("one-more-now"));           /* room again */
    dlog_secret_remove("not-there");                        /* harmless */
    dlog_secret_clear();
    char out[64];
    (void)dlog_scrub_copy(out, sizeof(out), "secret-1", 8);
    ASSERT_STR_EQ(out, "secret-1");
    TEST_END();
}

/* ---- dlog_escape_bytes -------------------------------------------------------------------- */

static size_t esc_all(const char *in, size_t n, char *out, size_t cap)
{
    DlogEscState st;
    dlog_esc_init(&st);
    size_t used = 0;
    (void)dlog_escape_bytes(&st, in, n, out, cap, &used);
    return used;
}

int test_dlog_escape_bytes_basic_escapes(void)
{
    TEST_BEGIN();
    char out[256];
    DlogEscState st;
    dlog_esc_init(&st);
    size_t used = 0;
    size_t o = dlog_escape_bytes(&st, "a\x1b[1mB\r\n\t\"\\\x01\x7f", 13, out, sizeof(out), &used);
    ASSERT_STR_EQ(out, "a\\e[1mB\\r\\n\\t\\\"\\\\\\x01\\x7f");
    ASSERT_EQ(o, strlen(out));
    ASSERT_EQ(used, (size_t)13);
    dlog_esc_init(&st);
    o = dlog_escape_bytes(&st, "ok\x00z", 4, out, sizeof(out), &used);
    ASSERT_STR_EQ(out, "ok\\x00z");
    TEST_END();
}

int test_dlog_escape_bytes_utf8_and_invalid(void)
{
    TEST_BEGIN();
    char out[256];
    (void)esc_all("caf\xc3\xa9", 5, out, sizeof(out));
    ASSERT_STR_EQ(out, "caf\xc3\xa9");
    (void)esc_all("a\xffz", 3, out, sizeof(out));
    ASSERT_STR_EQ(out, "a\\xffz");
    (void)esc_all("\xe2\x82", 2, out, sizeof(out));
    ASSERT_STR_EQ(out, "\\xe2\\x82");
    TEST_END();
}

int test_dlog_escape_bytes_stops_when_output_is_full(void)
{
    TEST_BEGIN();
    char in[400];
    memset(in, 'a', sizeof(in));
    char out[100];
    DlogEscState st;
    dlog_esc_init(&st);
    size_t used = 0;
    size_t o = dlog_escape_bytes(&st, in, sizeof(in), out, sizeof(out), &used);
    ASSERT_TRUE(used < sizeof(in));
    ASSERT_TRUE(o < sizeof(out));
    ASSERT_EQ(out[o], '\0');
    ASSERT_TRUE(o > 20);
    ASSERT_EQ(dlog_escape_bytes(&st, in, 5, out, 0, &used), (size_t)0);
    ASSERT_EQ(used, (size_t)0);
    ASSERT_EQ(dlog_escape_bytes(&st, NULL, 5, out, sizeof(out), &used), (size_t)0);
    TEST_END();
}

int test_dlog_escape_bytes_withholds_osc52_bel_and_st(void)
{
    TEST_BEGIN();
    char out[256];
    (void)esc_all("A\x1b]52;c;SGVsbG8=\x07" "B", 18, out, sizeof(out));
    ASSERT_STR_EQ(out, "A[osc52 bytes=10]B");           /* "c;SGVsbG8=" after the first ';' */
    (void)esc_all("\x1b]52;c;QUJD\x1b\\" "Z", 4 + 7 + 2 + 1, out, sizeof(out));
    ASSERT_TRUE(strstr(out, "QUJD") == NULL);
    ASSERT_TRUE(strstr(out, "[osc52 bytes=6]Z") != NULL);
    TEST_END();
}

int test_dlog_escape_bytes_osc52_split_at_every_position(void)
{
    TEST_BEGIN();
    const char msg[] = "pre\x1b]52;c;U0VDUkVU\x07post\x1b]52;c;WFla\x1b\\end";
    size_t n = sizeof(msg) - 1;
    char whole[512];
    (void)esc_all(msg, n, whole, sizeof(whole));
    ASSERT_TRUE(strstr(whole, "U0VDUkVU") == NULL);
    ASSERT_TRUE(strstr(whole, "WFla") == NULL);
    ASSERT_TRUE(strstr(whole, "[osc52 bytes=10]") != NULL);
    for (size_t k = 0; k <= n; k++) {
        DlogEscState st;
        dlog_esc_init(&st);
        char a[512], b[512];
        size_t used1 = 0, used2 = 0;
        size_t la = dlog_escape_bytes(&st, msg, k, a, sizeof(a), &used1);
        size_t lb = dlog_escape_bytes(&st, msg + k, n - k, b, sizeof(b), &used2);
        char joined[1024];
        memcpy(joined, a, la);
        memcpy(joined + la, b, lb + 1);
        if (strcmp(joined, whole) != 0) printf("  split at %zu: \"%s\" vs \"%s\"\n", k, joined, whole);
        ASSERT_STR_EQ(joined, whole);
    }
    TEST_END();
}

int test_dlog_escape_bytes_other_osc_and_near_misses_pass_through(void)
{
    TEST_BEGIN();
    char out[256];
    (void)esc_all("\x1b]0;title\x07", 10, out, sizeof(out));
    ASSERT_STR_EQ(out, "\\e]0;title\\x07");
    (void)esc_all("\x1b]53;x\x07", 7, out, sizeof(out));
    ASSERT_STR_EQ(out, "\\e]53;x\\x07");
    (void)esc_all("\x1b]5x", 4, out, sizeof(out));
    ASSERT_STR_EQ(out, "\\e]5x");
    (void)esc_all("\x1b\x1b]52;c;QQ==\x07", 13, out, sizeof(out));
    ASSERT_STR_EQ(out, "\\e[osc52 bytes=6]");
    TEST_END();
}

/* ---- rings ---------------------------------------------------------------------------------- */

int test_dlog_full_queue_drops_low_levels_and_counts_them(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(8192, 65536));
    apply_levels(1, DLOG_LVL_TRACE, 0, 0);
    for (int i = 0; i < 200; i++)
        DLOG(DLOG_RENDER, DLOG_TRACE, 1, "line", "i=%d pad=%s", i, "0123456789012345678901234567890123456789");
    DlogStatus st;
    dlog_status(&st);
    ASSERT_TRUE(st.dropped_total > 0);
    unsigned long long dropped = st.dropped_total;
    ASSERT_EQ(g_fk.sleeps, 0);                     /* producers never wait */
    const char *t = logtext();
    ASSERT_NOT_NULL(t);
    char l[300];
    ASSERT_TRUE(line_with(t, "dlog.dropped", l, sizeof(l)));
    ASSERT_TRUE(strstr(l, " WARN  general - ") != NULL);
    char want[64];
    (void)snprintf(want, sizeof(want), "n=%llu", dropped);
    ASSERT_TRUE(strstr(l, want) != NULL);
    int written = count_lines(t, " line ");
    ASSERT_EQ((unsigned long long)written + dropped, 200ULL);
    ASSERT_TRUE(all_lines_complete(t));
    /* once drained, lines flow again */
    DLOG(DLOG_RENDER, DLOG_TRACE, 1, "after", "x=%d", 1);
    t = logtext();
    ASSERT_EQ(count_lines(t, " after "), 1);
    end();
    TEST_END();
}

int test_dlog_errors_use_the_reserved_headroom_and_are_not_dropped(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(8192, 65536));
    apply_levels(1, DLOG_LVL_TRACE, 0, 0);
    for (int i = 0; i < 200; i++)                  /* far more than fits: the reserve stays free */
        DLOG(DLOG_RENDER, DLOG_TRACE, 1, "fill", "i=%d pad=%s", i, "0123456789012345678901234567890123456789");
    int wakes = g_fk.wakes;
    DLOG(DLOG_GENERAL, DLOG_ERROR, 0, "must.arrive", "x=%d", 1);
    DLOG(DLOG_GENERAL, DLOG_WARN, 0, "warn.arrives", "x=%d", 2);
    DLOG_EV(DLOG_GENERAL, DLOG_ISSUE, 0, "issue.arrives");
    ASSERT_TRUE(g_fk.wakes > wakes);               /* the writer was woken */
    ASSERT_EQ(g_fk.sleeps, 0);                     /* and the producer did not wait for it */
    const char *t = logtext();
    ASSERT_EQ(count_lines(t, "must.arrive"), 1);
    ASSERT_EQ(count_lines(t, "warn.arrives"), 1);
    ASSERT_EQ(count_lines(t, "issue.arrives"), 1);
    end();
    TEST_END();
}

int test_dlog_a_completely_full_queue_drops_even_errors_without_blocking(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(8192, 65536));
    apply_levels(1, DLOG_LVL_TRACE, 0, 0);
    for (int i = 0; i < 400; i++)                  /* errors have no reserve to respect: fill it all */
        DLOG(DLOG_GENERAL, DLOG_ERROR, 0, "e", "i=%d pad=%s", i, "0123456789012345678901234567890123456789");
    DlogStatus st0;
    dlog_status(&st0);
    ASSERT_TRUE(st0.dropped_total > 0);            /* no room is no room, but nobody waited */
    ASSERT_EQ(g_fk.sleeps, 0);
    unsigned before = g_fk.mono;
    DLOG(DLOG_GENERAL, DLOG_ERROR, 0, "lost", "x=%d", 1);
    ASSERT_EQ(g_fk.mono, before);
    DlogStatus st1;
    dlog_status(&st1);
    ASSERT_TRUE(st1.dropped_total > st0.dropped_total);
    const char *t = logtext();
    ASSERT_EQ(count_lines(t, "dlog.dropped") >= 1, 1);
    end();
    TEST_END();
}

int test_dlog_recorder_wraps_oldest_first_on_line_boundaries(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 8192));
    apply_levels(1, DLOG_LVL_TRACE, 1, 0);
    for (int i = 0; i < 400; i++)
        DLOG(DLOG_RENDER, DLOG_TRACE, 1, "seq", "n=%d", i);
    size_t need = dlog_recorder_size();
    ASSERT_TRUE(need > 0 && need <= 8192);
    char *rec = (char *)malloc(need + 1);
    ASSERT_NOT_NULL(rec);
    if (rec) {
        size_t n = dlog_recorder_copy(rec, need);
        ASSERT_EQ(n, need);
        rec[n] = '\0';
        ASSERT_TRUE(all_lines_complete(rec));
        int first = -1, prev = -1, ok = 1, lines = 0;
        for (char *p = rec; *p; ) {
            char *e = strchr(p, '\n');
            if (!e) break;
            *e = '\0';
            char *m = strstr(p, " n=");
            if (!m || strstr(p, "seq") == NULL) { ok = 0; break; }
            int v = atoi(m + 3);
            if (first < 0) first = v;
            if (prev >= 0 && v != prev + 1) ok = 0;
            prev = v;
            lines++;
            p = e + 1;
        }
        ASSERT_TRUE(ok);
        ASSERT_EQ(prev, 399);                       /* newest line is last */
        ASSERT_TRUE(first > 20);                    /* the oldest ones were overwritten */
        ASSERT_TRUE(lines > 20);
        free(rec);
    }
    ASSERT_EQ(dlog_recorder_copy(NULL, 0), need);   /* NULL buf = size query */
    end();
    TEST_END();
}

int test_dlog_recorder_copy_into_a_smaller_buffer_keeps_whole_lines(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply_levels(1, DLOG_LVL_TRACE, 1, 0);
    for (int i = 0; i < 20; i++) DLOG(DLOG_RENDER, DLOG_TRACE, 1, "seq", "n=%d", i);
    char small[300];
    size_t n = dlog_recorder_copy(small, sizeof(small) - 1);
    small[n] = '\0';
    ASSERT_TRUE(n > 0 && n < 300);
    ASSERT_TRUE(all_lines_complete(small));          /* never ends mid-line */
    ASSERT_TRUE(strstr(small, "n=19") != NULL);      /* it keeps the newest, not the oldest */
    end();
    TEST_END();
}

int test_dlog_one_lock_hold_per_emit_and_never_nested(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    g_fk.max_lock_depth = 0;
    DLOG(DLOG_GENERAL, DLOG_ERROR, 0, "x", "a=%d", 1);
    DLOG(DLOG_GENERAL, DLOG_ERROR, 0, "y", "a=%d", 2);
    (void)dlog_pump();
    dlog_tick();
    ASSERT_EQ(g_fk.max_lock_depth, 1);
    ASSERT_EQ(g_fk.lock_depth, 0);
    end();
    TEST_END();
}

/* ---- files: rotation, retention, fallback -------------------------------------------------------- */

static void emit_long(int n, const char *tag)
{
    char pad[1800];
    memset(pad, 'p', sizeof(pad) - 1);
    pad[sizeof(pad) - 1] = '\0';
    for (int i = 0; i < n; i++)
        DLOG(DLOG_GENERAL, DLOG_ERROR, 0, tag, "i=%d pad=%s", i, pad);
}

int test_dlog_rotation_continues_in_numbered_files_and_never_splits_a_line(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    for (int round = 0; round < 8; round++) {          /* ~1.9 MB at ~1.85 KB a line */
        emit_long(130, "big");
        (void)dlog_pump();
    }
    FkFile *f1 = fk_find(FK_LOG_FILE);
    FkFile *f2 = fk_find(FK_LOG_DIR "\\" FK_STEM ".2.log");
    ASSERT_NOT_NULL(f1);
    ASSERT_NOT_NULL(f2);
    if (f1 && f2) {
        ASSERT_TRUE(f1->len <= 1024u * 1024u);
        ASSERT_TRUE(f1->len > 1000u * 1024u);
        ASSERT_TRUE(f1->data[f1->len - 1] == '\n');
        ASSERT_TRUE(strncmp(f2->data, "2026-10-17T14:03:10.000+01:00 INFO  general - log log.continue prev=" FK_STEM ".log", 70) == 0);
        ASSERT_TRUE(strstr(f2->data, "app.start") != NULL);
        ASSERT_EQ(count_lines(f1->data, "big") + count_lines(f2->data, "big"), 8 * 130);
    }
    end();
    TEST_END();
}

int test_dlog_rotation_with_debugging_on_uses_the_configured_size(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = 1;
    c.max_file_mb = 2;
    dlog_apply(&c);
    for (int round = 0; round < 8; round++) { emit_long(130, "big"); (void)dlog_pump(); }
    FkFile *f1 = fk_find(FK_LOG_FILE);
    ASSERT_NOT_NULL(f1);
    ASSERT_NULL(fk_find(FK_LOG_DIR "\\" FK_STEM ".2.log"));   /* 1.9 MB fits in 2 MB */
    end();
    TEST_END();
}

int test_dlog_retention_removes_oldest_files_at_creation(void)
{
    TEST_BEGIN();
    fk_reset();
    g_fk.ndirs = 0;
    /* 8 old files of 1 MB each; the cap is 5 MB */
    char path[300];
    for (int i = 0; i < 8; i++) {
        (void)snprintf(path, sizeof(path), FK_LOG_DIR "\\nutshell-2026100%d-120000-p1.log", i + 1);
        fk_put_file(path, "old\n", 1024ull * 1024ull, 1000 + i);
    }
    fk_put_file(FK_LOG_DIR "\\notes.txt", "mine", 0, 1);
    (void)snprintf(g_fk.dirs[g_fk.ndirs++], DLOG_MAX_PATH, "%s", FK_LOG_DIR);
    ASSERT_TRUE(fk_start(0, 0));
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = 1;
    c.max_total_mb = 5;
    dlog_apply(&c);
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "first");
    (void)dlog_pump();
    unsigned long long total = 0;
    int ours = 0;
    for (int i = 0; i < g_fk.nfiles; i++) {
        if (strstr(g_fk.files[i].path, "nutshell-")) { total += g_fk.files[i].len; ours++; }
    }
    ASSERT_TRUE(total <= 5ull * 1024ull * 1024ull);
    ASSERT_TRUE(fk_find(FK_LOG_FILE) != NULL);                 /* the current file stays */
    ASSERT_NULL(fk_find(FK_LOG_DIR "\\nutshell-20261001-120000-p1.log"));   /* oldest gone */
    ASSERT_NOT_NULL(fk_find(FK_LOG_DIR "\\nutshell-20261008-120000-p1.log"));   /* newest old kept */
    ASSERT_NOT_NULL(fk_find(FK_LOG_DIR "\\notes.txt"));        /* foreign files untouched */
    ASSERT_TRUE(ours <= 6);
    end();
    TEST_END();
}

int test_dlog_retention_skips_a_file_another_instance_holds_open(void)
{
    TEST_BEGIN();
    fk_reset();
    char path[300];
    for (int i = 0; i < 4; i++) {
        (void)snprintf(path, sizeof(path), FK_LOG_DIR "\\nutshell-2026100%d-120000-p1.log", i + 1);
        fk_put_file(path, "old\n", 2ull * 1024ull * 1024ull, 1000 + i);
    }
    fk_find(FK_LOG_DIR "\\nutshell-20261001-120000-p1.log")->undeletable = 1;
    (void)snprintf(g_fk.dirs[g_fk.ndirs++], DLOG_MAX_PATH, "%s", FK_LOG_DIR);
    ASSERT_TRUE(fk_start(0, 0));
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = 1;
    c.max_total_mb = 5;
    dlog_apply(&c);
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "first");
    (void)dlog_pump();
    ASSERT_NOT_NULL(fk_find(FK_LOG_DIR "\\nutshell-20261001-120000-p1.log"));   /* stuck, skipped */
    ASSERT_NULL(fk_find(FK_LOG_DIR "\\nutshell-20261002-120000-p1.log"));       /* next oldest goes */
    end();
    TEST_END();
}

int test_dlog_unwritable_folder_falls_back_to_local_appdata_and_warns(void)
{
    TEST_BEGIN();
    fk_reset();
    (void)snprintf(g_fk.deny_prefix, sizeof(g_fk.deny_prefix), "C:\\Tools");
    ASSERT_TRUE(fk_start(0, 0));
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "needs.file");
    (void)dlog_pump();
    const char *fb = "C:\\Users\\me\\AppData\\Local\\Nutshell\\logs\\" FK_STEM ".log";
    const char *t = fk_text(fb);
    ASSERT_NOT_NULL(t);
    if (t) {
        char l[600];
        ASSERT_TRUE(line_with(t, "log.fallback", l, sizeof(l)));
        ASSERT_TRUE(strstr(l, " WARN  general - ") != NULL);
        ASSERT_TRUE(strstr(l, "Access is denied") != NULL);
        ASSERT_TRUE(strstr(l, "C:\\\\Users\\\\me\\\\AppData\\\\Local\\\\Nutshell\\\\logs") != NULL);
        ASSERT_EQ(count_lines(t, "needs.file"), 1);
    }
    DlogStatus st;
    dlog_status(&st);
    ASSERT_TRUE(st.file_open);
    ASSERT_FALSE(st.mem_only);
    ASSERT_TRUE(strstr(st.path, "C:\\Users\\me\\AppData\\Local\\Nutshell\\logs") == st.path);
    ASSERT_TRUE(strstr(st.note, "Access is denied") != NULL);
    end();
    TEST_END();
}

int test_dlog_nothing_writable_keeps_events_in_memory_only(void)
{
    TEST_BEGIN();
    fk_reset();
    (void)snprintf(g_fk.deny_prefix, sizeof(g_fk.deny_prefix), "C:\\");
    ASSERT_TRUE(fk_start(0, 0));
    DLOG(DLOG_GENERAL, DLOG_ERROR, 0, "nowhere", "x=%d", 1);
    (void)dlog_pump();
    ASSERT_EQ(g_fk.nfiles, 0);
    DlogStatus st;
    dlog_status(&st);
    ASSERT_TRUE(st.mem_only);
    ASSERT_FALSE(st.file_open);
    ASSERT_EQ(st.path[0], '\0');
    ASSERT_TRUE(st.note[0] != '\0');
    char rec[4096];
    size_t n = dlog_recorder_copy(rec, sizeof(rec) - 1);
    rec[n] = '\0';
    ASSERT_EQ(count_lines(rec, "nowhere"), 1);            /* the recorder still has it */
    ASSERT_EQ(count_lines(rec, "log.memory_only"), 1);    /* and says why */
    /* more lines keep working and do not retry the disk every time */
    int opens = g_fk.write_calls;
    DLOG(DLOG_GENERAL, DLOG_ERROR, 0, "again", "x=%d", 2);
    (void)dlog_pump();
    ASSERT_EQ(g_fk.write_calls, opens);
    end();
    TEST_END();
}

int test_dlog_write_failure_does_not_loop_or_lose_the_process(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "first");
    (void)dlog_pump();
    g_fk.fail_writes = 1;
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "second");
    (void)dlog_pump();                                   /* must return */
    g_fk.fail_writes = 0;
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "third");
    (void)dlog_pump();
    ASSERT_TRUE(dlog_is_started());
    end();
    TEST_END();
}

int test_dlog_folder_change_writes_log_moved_and_continues_in_the_new_one(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "one");
    (void)dlog_pump();
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = 1;
    (void)snprintf(c.dir, sizeof(c.dir), "D:\\elsewhere");
    dlog_apply(&c);
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "two");
    (void)dlog_pump();
    const char *old = fk_text(FK_LOG_FILE);
    ASSERT_NOT_NULL(old);
    ASSERT_EQ(count_lines(old, "log.moved"), 1);
    ASSERT_EQ(count_lines(old, " two"), 0);
    const char *nw = fk_text("D:\\elsewhere\\" FK_STEM ".2.log");
    ASSERT_NOT_NULL(nw);
    if (nw) {
        ASSERT_EQ(count_lines(nw, "log.continue"), 1);
        ASSERT_EQ(count_lines(nw, " two"), 1);
    }
    DlogStatus st;
    dlog_status(&st);
    ASSERT_TRUE(strstr(st.dir, "D:\\elsewhere") == st.dir);
    end();
    TEST_END();
}

int test_dlog_relative_folder_setting_resolves_against_the_exe_dir(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = 1;
    (void)snprintf(c.dir, sizeof(c.dir), "diag");
    dlog_apply(&c);
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "x");
    (void)dlog_pump();
    ASSERT_NOT_NULL(fk_find("C:\\Tools\\nutshell\\diag\\" FK_STEM ".log"));
    end();
    TEST_END();
}

int test_dlog_config_line_is_written_when_a_file_is_open_only(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply_levels(1, DLOG_LVL_INFO, 1, 0);                  /* no file yet: nothing is created */
    (void)dlog_pump();
    ASSERT_EQ(g_fk.nfiles, 0);
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "open.it");
    (void)dlog_pump();
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = 1;
    c.levels[DLOG_RENDER] = DLOG_LVL_TRACE;
    dlog_apply(&c);
    const char *t = logtext();
    char l[600];
    ASSERT_TRUE(line_with(t, "dlog.config", l, sizeof(l)));
    ASSERT_TRUE(strstr(l, "render:trace") != NULL);
    ASSERT_TRUE(strstr(l, "term:info") != NULL);
    ASSERT_TRUE(strstr(l, "enabled=1") != NULL);
    end();
    TEST_END();
}

int test_dlog_header_states_the_effective_levels(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = 1;
    c.levels[DLOG_RENDER] = DLOG_LVL_TRACE;
    c.levels[DLOG_TERM] = DLOG_LVL_DEBUG;
    c.include_content = 1;
    c.recorder = 0;
    dlog_apply(&c);
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "x");
    const char *t = logtext();
    char l[800];
    ASSERT_TRUE(line_with(t, "app.start", l, sizeof(l)));
    ASSERT_TRUE(strstr(l, "levels=render:trace,term:debug,ai:info,ssh:info,shell:info,general:info") != NULL);
    ASSERT_TRUE(strstr(l, "content=1") != NULL);
    ASSERT_TRUE(strstr(l, "recorder=0") != NULL);
    end();
    TEST_END();
}

int test_dlog_app_start_extra_fields_are_appended(void)
{
    TEST_BEGIN();
    fk_reset();
    DlogStartInfo si;
    memset(&si, 0, sizeof(si));
    si.exe_dir = "C:\\Tools\\nutshell";
    si.exe_path = "C:\\Tools\\nutshell\\nutshell.exe";
    si.version = "1.2.36";
    si.pid = 8124;
    si.extra = "windows=10.0.26300 dpi=144";
    ASSERT_TRUE(dlog_start_with(fk_platform(), &si));
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "x");
    const char *t = logtext();
    ASSERT_TRUE(strstr(t, "windows=10.0.26300 dpi=144") != NULL);
    end();
    TEST_END();
}

/* ---- the writer pump and flush --------------------------------------------------------------------- */

int test_dlog_flush_wait_in_synchronous_mode_drains(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "flushme");
    ASSERT_TRUE(dlog_flush_wait(100));
    ASSERT_EQ(count_lines(fk_text(FK_LOG_FILE), "flushme"), 1);
    ASSERT_TRUE(dlog_flush_wait(100));                      /* nothing queued: still fine */
    end();
    TEST_END();
}

int test_dlog_errors_and_issues_wake_the_writer_info_does_not(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply_levels(1, DLOG_LVL_TRACE, 0, 0);
    int w = g_fk.wakes;
    DLOG_EV(DLOG_RENDER, DLOG_TRACE, 0, "quiet");
    DLOG_EV(DLOG_RENDER, DLOG_INFO, 0, "quiet2");
    ASSERT_EQ(g_fk.wakes, w);
    DLOG_EV(DLOG_RENDER, DLOG_ERROR, 0, "loud");
    ASSERT_EQ(g_fk.wakes, w + 1);
    DLOG_EV(DLOG_RENDER, DLOG_ISSUE, 0, "loud2");
    ASSERT_EQ(g_fk.wakes, w + 2);
    end();
    TEST_END();
}

int test_dlog_shutdown_drains_and_closes_the_file(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "last.words");
    dlog_shutdown();
    FkFile *f = fk_find(FK_LOG_FILE);
    ASSERT_NOT_NULL(f);
    if (f) {
        ASSERT_FALSE(f->open);
        ASSERT_EQ(count_lines(f->data, "last.words"), 1);
    }
    end();
    TEST_END();
}

int test_dlog_crash_note_writes_and_drains_without_the_lock(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DLOG_EV(DLOG_GENERAL, DLOG_ERROR, 0, "before.crash");
    g_fk.try_lock_result = 0;                              /* the crashed thread held the lock */
    dlog_crash_note(0xC0000005ul, (const void *)(size_t)0x1234);
    g_fk.try_lock_result = 1;
    const char *t = fk_text(FK_LOG_FILE);
    ASSERT_NOT_NULL(t);
    if (t) {
        char l[300];
        ASSERT_TRUE(line_with(t, "app.crash", l, sizeof(l)));
        ASSERT_TRUE(strstr(l, " ERROR general - ") != NULL);
        ASSERT_TRUE(strstr(l, "code=0xc0000005") != NULL);
        ASSERT_EQ(count_lines(t, "before.crash"), 1);
        ASSERT_TRUE(all_lines_complete(t));
    }
    end();
    TEST_END();
}

/* ---- issue marks -------------------------------------------------------------------------------------- */

static DlogSnapshot snap(void)
{
    static unsigned char dirty[24];
    memset(dirty, 0, sizeof(dirty));
    memset(dirty, 1, 5);
    DlogSnapshot s;
    memset(&s, 0, sizeof(s));
    s.window.client_w = 1500; s.window.client_h = 900; s.window.dpi = 144;
    s.has_term = 1;
    s.term.sid = 2; s.term.kind = "ssh"; s.term.connected = 1; s.term.rows = 24; s.term.cols = 80;
    s.term.dirty = dirty; s.term.dirty_n = 24;
    s.nsessions = 1;
    s.sessions[0].sid = 1; s.sessions[0].kind = "local"; s.sessions[0].connected = 1;
    s.ai.policy = "read/none";
    return s;
}

int test_dlog_issue_mark_writes_mark_snapshot_and_the_recorder_dump(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    apply_levels(1, DLOG_LVL_INFO, 1, 0);
    DLOG(DLOG_RENDER, DLOG_TRACE, 2, "detail", "n=%d", 1);
    DLOG(DLOG_RENDER, DLOG_INFO, 2, "visible", "n=%d", 2);
    fk_advance(50);
    DlogSnapshot s = snap();
    DlogIssue iss;
    memset(&iss, 0, sizeof(iss));
    ASSERT_TRUE(dlog_issue_mark(&s, 0, &iss));
    ASSERT_EQ(iss.n, 1u);
    ASSERT_TRUE(iss.ok);
    ASSERT_STR_EQ(iss.recorder_path, FK_LOG_DIR "\\" FK_STEM ".issue1.log");
    ASSERT_EQ(iss.screenshot_path[0], '\0');

    const char *main_log = fk_text(FK_LOG_FILE);
    ASSERT_NOT_NULL(main_log);
    if (main_log) {
        char l[600];
        ASSERT_TRUE(line_with(main_log, "issue.mark", l, sizeof(l)));
        ASSERT_TRUE(strstr(l, " ISSUE general - ui issue.mark n=1 recorder=" FK_STEM ".issue1.log screenshot=none") != NULL);
        ASSERT_EQ(count_lines(main_log, "issue.window"), 1);
        ASSERT_EQ(count_lines(main_log, "issue.term"), 1);
        ASSERT_EQ(count_lines(main_log, "issue.render"), 1);
        ASSERT_EQ(count_lines(main_log, "issue.session"), 1);
        ASSERT_EQ(count_lines(main_log, "issue.ai"), 1);
        ASSERT_EQ(count_lines(main_log, "detail"), 0);        /* Trace only in the recorder */
        /* mark first, snapshot after */
        ASSERT_TRUE(strstr(main_log, "issue.mark") < strstr(main_log, "issue.window"));
    }
    const char *dump = fk_text(iss.recorder_path);
    ASSERT_NOT_NULL(dump);
    if (dump) {
        ASSERT_EQ(count_lines(dump, "detail"), 1);            /* the recorder has the Trace line */
        ASSERT_EQ(count_lines(dump, " visible "), 1);
        ASSERT_EQ(count_lines(dump, "issue.mark"), 1);
        ASSERT_EQ(count_lines(dump, "issue.ai"), 1);
        ASSERT_TRUE(strstr(dump, "detail") < strstr(dump, " visible "));        /* oldest first */
        ASSERT_TRUE(strstr(dump, " visible ") < strstr(dump, "issue.mark"));
        ASSERT_TRUE(strstr(dump, "issue.mark") < strstr(dump, "issue.ai"));
        ASSERT_EQ(count_lines(dump, "app.start"), 1);         /* its own header */
        ASSERT_TRUE(strstr(dump, "app.start") < strstr(dump, "detail"));
        ASSERT_TRUE(all_lines_complete(dump));
    }
    end();
    TEST_END();
}

int test_dlog_issue_numbers_increment_and_screenshot_name_follows_the_flag(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DlogIssue a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    ASSERT_TRUE(dlog_issue_mark(NULL, 0, &a));
    ASSERT_TRUE(dlog_issue_mark(NULL, 1, &b));
    ASSERT_EQ(a.n, 1u);
    ASSERT_EQ(b.n, 2u);
    ASSERT_STR_EQ(b.recorder_path, FK_LOG_DIR "\\" FK_STEM ".issue2.log");
    ASSERT_STR_EQ(b.screenshot_path, FK_LOG_DIR "\\" FK_STEM ".issue2.png");
    ASSERT_EQ(a.screenshot_path[0], '\0');
    const char *t = fk_text(FK_LOG_FILE);
    ASSERT_TRUE(t && strstr(t, "screenshot=" FK_STEM ".issue2.png") != NULL);
    DlogStatus st;
    dlog_status(&st);
    ASSERT_EQ(st.issues, 2u);
    end();
    TEST_END();
}

int test_dlog_issue_mark_works_with_debugging_off(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    DLOG_EV(DLOG_RENDER, DLOG_INFO, 0, "dropped.at.capture");
    DLOG_EV(DLOG_RENDER, DLOG_WARN, 0, "warn.kept");
    DlogSnapshot s = snap();
    DlogIssue iss;
    memset(&iss, 0, sizeof(iss));
    ASSERT_TRUE(dlog_issue_mark(&s, 0, &iss));
    const char *dump = fk_text(iss.recorder_path);
    ASSERT_NOT_NULL(dump);
    if (dump) {
        ASSERT_EQ(count_lines(dump, "dropped.at.capture"), 0);
        ASSERT_EQ(count_lines(dump, "warn.kept"), 1);
        ASSERT_EQ(count_lines(dump, "issue.mark"), 1);
    }
    end();
    TEST_END();
}

int test_dlog_issue_mark_with_nowhere_to_write_still_marks_the_recorder(void)
{
    TEST_BEGIN();
    fk_reset();
    (void)snprintf(g_fk.deny_prefix, sizeof(g_fk.deny_prefix), "C:\\");
    ASSERT_TRUE(fk_start(0, 0));
    DlogIssue iss;
    memset(&iss, 0, sizeof(iss));
    ASSERT_TRUE(dlog_issue_mark(NULL, 1, &iss));
    ASSERT_FALSE(iss.ok);
    ASSERT_EQ(iss.recorder_path[0], '\0');
    ASSERT_EQ(iss.screenshot_path[0], '\0');
    char rec[4096];
    size_t n = dlog_recorder_copy(rec, sizeof(rec) - 1);
    rec[n] = '\0';
    ASSERT_EQ(count_lines(rec, "issue.mark"), 1);
    ASSERT_TRUE(strstr(rec, "recorder=none") != NULL);
    end();
    TEST_END();
}

int test_dlog_issue_mark_not_started_and_null_out(void)
{
    TEST_BEGIN();
    DlogIssue iss;
    memset(&iss, 0, sizeof(iss));
    ASSERT_FALSE(dlog_issue_mark(NULL, 0, &iss));
    ASSERT_TRUE(begin(0, 0));
    ASSERT_TRUE(dlog_issue_mark(NULL, 0, NULL));           /* out is optional */
    end();
    TEST_END();
}

/* ---- configuration ------------------------------------------------------------------------------------ */

int test_dlog_config_defaults_match_the_spec(void)
{
    TEST_BEGIN();
    DlogConfig c;
    dlog_config_defaults(&c);
    ASSERT_EQ(c.enabled, 0);
    for (int i = 0; i < DLOG_CAT_COUNT; i++) ASSERT_EQ(c.levels[i], (unsigned char)DLOG_LVL_INFO);
    ASSERT_EQ(c.include_content, 0);
    ASSERT_EQ(c.screenshot, 0);
    ASSERT_EQ(c.recorder, 1);
    ASSERT_STR_EQ(c.dir, "logs");
    ASSERT_EQ(c.max_file_mb, 10);
    ASSERT_EQ(c.max_total_mb, 50);
    TEST_END();
}

int test_dlog_config_from_settings_copies_and_clamps(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.debug_enabled = 1;
    s.debug_levels[DLOG_RENDER] = DLOG_LVL_TRACE;
    s.debug_levels[DLOG_SHELL] = DLOG_LVL_OFF;
    s.debug_include_content = 1;
    s.debug_screenshot = 1;
    s.debug_recorder = 0;
    (void)snprintf(s.debug_log_dir, sizeof(s.debug_log_dir), "%s", "D:\\diag");
    s.debug_max_file_mb = 20;
    s.debug_max_total_mb = 99;
    DlogConfig c;
    dlog_config_from_settings(&s, &c);
    ASSERT_EQ(c.enabled, 1);
    ASSERT_EQ(c.levels[DLOG_RENDER], (unsigned char)DLOG_LVL_TRACE);
    ASSERT_EQ(c.levels[DLOG_SHELL], (unsigned char)DLOG_LVL_OFF);
    ASSERT_EQ(c.levels[DLOG_AI], (unsigned char)DLOG_LVL_INFO);
    ASSERT_EQ(c.include_content, 1);
    ASSERT_EQ(c.screenshot, 1);
    ASSERT_EQ(c.recorder, 0);
    ASSERT_STR_EQ(c.dir, "D:\\diag");
    ASSERT_EQ(c.max_file_mb, 20);
    ASSERT_EQ(c.max_total_mb, 99);
    s.debug_levels[DLOG_TERM] = 77;                       /* out of range -> default */
    dlog_config_from_settings(&s, &c);
    ASSERT_EQ(c.levels[DLOG_TERM], (unsigned char)DLOG_LVL_INFO);
    dlog_config_from_settings(NULL, &c);                  /* NULL = defaults */
    ASSERT_EQ(c.enabled, 0);
    TEST_END();
}

int test_dlog_apply_sets_the_flags_and_status(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    ASSERT_FALSE(dlog_enabled());
    ASSERT_FALSE(dlog_screenshot_enabled());
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = 1;
    c.screenshot = 1;
    dlog_apply(&c);
    ASSERT_TRUE(dlog_enabled());
    ASSERT_TRUE(dlog_screenshot_enabled());
    DlogStatus st;
    dlog_status(&st);
    ASSERT_TRUE(st.enabled);
    ASSERT_TRUE(st.started);
    ASSERT_STR_EQ(st.dir, FK_LOG_DIR);
    ASSERT_STR_EQ(st.stem, FK_STEM);
    end();
    TEST_END();
}

int test_dlog_configure_registers_the_api_key_as_a_secret(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    Settings s;
    config_default_settings(&s);
    s.debug_enabled = 1;
    s.debug_include_content = 1;
    for (int i = 0; i < DLOG_CAT_COUNT; i++) s.debug_levels[i] = DLOG_LVL_TRACE;
    (void)snprintf(s.ai_api_key, sizeof(s.ai_api_key), "sk-first-key-123");
    dlog_configure(&s);
    DLOG_CONTENT(DLOG_AI, DLOG_TRACE, 0, "ai.req", "body", "auth sk-first-key-123 end", 25);
    const char *t = logtext();
    ASSERT_TRUE(strstr(t, "sk-first-key-123") == NULL);
    ASSERT_TRUE(strstr(t, "auth [REDACTED] end") != NULL);
    /* changing the key replaces the old one rather than accumulating */
    (void)snprintf(s.ai_api_key, sizeof(s.ai_api_key), "sk-second-key-456");
    dlog_configure(&s);
    DLOG_CONTENT(DLOG_AI, DLOG_TRACE, 0, "ai.req2", "body", "x sk-second-key-456 y", 21);
    t = logtext();
    ASSERT_TRUE(strstr(t, "sk-second-key-456") == NULL);
    /* an empty key removes it */
    s.ai_api_key[0] = '\0';
    dlog_configure(&s);
    DLOG_CONTENT(DLOG_AI, DLOG_TRACE, 0, "ai.req3", "body", "x sk-second-key-456 y", 21);
    t = logtext();
    ASSERT_TRUE(strstr(t, "x sk-second-key-456 y") != NULL);
    end();
    TEST_END();
}

int test_dlog_shutdown_wipes_secrets(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(begin(0, 0));
    ASSERT_TRUE(dlog_secret_add("wipe-me-please"));
    dlog_shutdown();
    ASSERT_TRUE(begin(0, 0));
    char out[64];
    (void)dlog_scrub_copy(out, sizeof(out), "wipe-me-please", 14);
    ASSERT_STR_EQ(out, "wipe-me-please");               /* gone: nothing is scrubbed any more */
    end();
    TEST_END();
}

int test_dlog_fmt_value_standalone(void)
{
    TEST_BEGIN();
    char b[64];
    ASSERT_EQ(dlog_fmt_value(b, sizeof(b), "abc"), (size_t)3);
    ASSERT_STR_EQ(b, "abc");
    (void)dlog_fmt_value(b, sizeof(b), "a b");
    ASSERT_STR_EQ(b, "\"a b\"");
    (void)dlog_fmt_value(b, sizeof(b), NULL);
    ASSERT_STR_EQ(b, "-");
    (void)dlog_fmt_value(b, 6, "a b c d e f g");
    ASSERT_TRUE(strlen(b) < 6);                          /* cut, still terminated */
    ASSERT_EQ(dlog_fmt_value(b, 0, "x"), (size_t)0);
    TEST_END();
}
