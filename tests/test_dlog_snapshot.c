/* tests/test_dlog_snapshot.c -- the issue snapshot formatter */
#include "test_framework.h"
#include "dlog_snapshot.h"
#include <string.h>
#include <stdio.h>

static void ranges(const char *flags_spec, char *out, size_t cap)
{
    /* flags_spec: a string of '0' / '1' per row */
    unsigned char f[256];
    int n = (int)strlen(flags_spec);
    for (int i = 0; i < n; i++) f[i] = (unsigned char)(flags_spec[i] == '1');
    (void)dlog_dirty_ranges(f, n, out, cap);
}

int test_dlog_dirty_ranges_none(void)
{
    TEST_BEGIN();
    char out[64];
    ranges("0000", out, sizeof(out));
    ASSERT_STR_EQ(out, "none");
    ranges("", out, sizeof(out));
    ASSERT_STR_EQ(out, "none");
    size_t n = dlog_dirty_ranges(NULL, 5, out, sizeof(out));
    ASSERT_STR_EQ(out, "none");
    ASSERT_EQ(n, (size_t)4);
    TEST_END();
}

int test_dlog_dirty_ranges_single_row(void)
{
    TEST_BEGIN();
    char out[64];
    ranges("00100", out, sizeof(out));
    ASSERT_STR_EQ(out, "2");
    ranges("1000", out, sizeof(out));
    ASSERT_STR_EQ(out, "0");
    ranges("0001", out, sizeof(out));
    ASSERT_STR_EQ(out, "3");
    TEST_END();
}

int test_dlog_dirty_ranges_contiguous_and_all(void)
{
    TEST_BEGIN();
    char out[64];
    ranges("0111100", out, sizeof(out));
    ASSERT_STR_EQ(out, "1-4");
    ranges("11111", out, sizeof(out));
    ASSERT_STR_EQ(out, "0-4");
    ranges("11", out, sizeof(out));
    ASSERT_STR_EQ(out, "0-1");
    TEST_END();
}

int test_dlog_dirty_ranges_scattered(void)
{
    TEST_BEGIN();
    char out[64];
    ranges("1110100110", out, sizeof(out));
    ASSERT_STR_EQ(out, "0-2,4,7-8");
    ranges("10101", out, sizeof(out));
    ASSERT_STR_EQ(out, "0,2,4");
    TEST_END();
}

int test_dlog_dirty_ranges_73_rows_like_the_stale_band(void)
{
    TEST_BEGIN();
    unsigned char f[80];
    memset(f, 0, sizeof(f));
    memset(f, 1, 73);
    char out[64];
    (void)dlog_dirty_ranges(f, 80, out, sizeof(out));
    ASSERT_STR_EQ(out, "0-72");
    TEST_END();
}

int test_dlog_dirty_ranges_truncates_with_ellipsis(void)
{
    TEST_BEGIN();
    unsigned char f[400];
    for (int i = 0; i < 400; i++) f[i] = (unsigned char)(i % 2 == 0);   /* 200 singles */
    char out[40];
    size_t n = dlog_dirty_ranges(f, 400, out, sizeof(out));
    ASSERT_EQ(n, strlen(out));
    ASSERT_TRUE(n < sizeof(out));
    ASSERT_TRUE(strlen(out) >= 3);
    ASSERT_STR_EQ(out + strlen(out) - 3, "...");
    ASSERT_TRUE(strncmp(out, "0,2,4,6", 7) == 0);
    char one[1];
    ASSERT_EQ(dlog_dirty_ranges(f, 400, one, 1), (size_t)0);
    ASSERT_EQ(dlog_dirty_ranges(f, 400, NULL, 0), (size_t)0);
    TEST_END();
}

static DlogSnapshot sample(void)
{
    static unsigned char dirty[24];
    memset(dirty, 0, sizeof(dirty));
    memset(dirty, 1, 5);
    DlogSnapshot s;
    memset(&s, 0, sizeof(s));
    s.window.client_w = 1500; s.window.client_h = 900; s.window.dpi = 144;
    s.window.tab_height = 36; s.window.left_margin = 8;
    s.window.ai_docked = 1; s.window.ai_width = 420;
    s.window.scrollbar_visible = 1; s.window.ms_since_last_paint = 9;
    s.window.cooldown_ms = 16; s.window.focus_hwnd = 0xABCDULL;
    s.has_term = 1;
    s.term.sid = 2; s.term.kind = "ssh"; s.term.connected = 1;
    s.term.rows = 24; s.term.cols = 80;
    s.term.lines_count = 500; s.term.lines_start = 12; s.term.lines_capacity = 10000;
    s.term.scrollback_offset = 3; s.term.cursor_row = 5; s.term.cursor_col = 9;
    s.term.cursor_visible = 1; s.term.alt_screen = 1; s.term.scroll_top = 0; s.term.scroll_bottom = 23;
    s.term.write_seq = 123456789ULL;
    s.term.dirty = dirty; s.term.dirty_n = 24;
    s.render.char_w = 9; s.render.char_h = 18; s.render.dispbuf_rows = 24; s.render.dispbuf_cols = 80;
    s.render.prev_cursor_row = 4; s.render.prev_cursor_col = 8;
    s.nsessions = 2;
    s.sessions[0].sid = 1; s.sessions[0].kind = "local"; s.sessions[0].connected = 1;
    s.sessions[0].dirty_rows = 0; s.sessions[0].alt_screen = 0;
    s.sessions[1].sid = 3; s.sessions[1].kind = "ssh"; s.sessions[1].connected = 0;
    s.sessions[1].dirty_rows = 7; s.sessions[1].alt_screen = 1;
    s.ai.panel_visible = 1; s.ai.stream_live = 0; s.ai.dispatch_batch = 2; s.ai.policy = "read/none";
    return s;
}

int test_dlog_snapshot_format_order_and_count(void)
{
    TEST_BEGIN();
    DlogSnapshot s = sample();
    DlogSnapLine lines[16];
    size_t n = dlog_snapshot_format(&s, lines, 16);
    ASSERT_EQ(n, (size_t)6);           /* window, term, render, 2 sessions, ai */
    ASSERT_STR_EQ(lines[0].event, "issue.window");
    ASSERT_STR_EQ(lines[1].event, "issue.term");
    ASSERT_STR_EQ(lines[2].event, "issue.render");
    ASSERT_STR_EQ(lines[3].event, "issue.session");
    ASSERT_STR_EQ(lines[4].event, "issue.session");
    ASSERT_STR_EQ(lines[5].event, "issue.ai");
    ASSERT_EQ(lines[1].sid, 2u);
    ASSERT_EQ(lines[3].sid, 1u);
    ASSERT_EQ(lines[4].sid, 3u);
    ASSERT_EQ(lines[0].sid, 0u);
    TEST_END();
}

int test_dlog_snapshot_format_fields(void)
{
    TEST_BEGIN();
    DlogSnapshot s = sample();
    DlogSnapLine lines[16];
    (void)dlog_snapshot_format(&s, lines, 16);
    ASSERT_TRUE(strstr(lines[0].fields, "client=1500x900") != NULL);
    ASSERT_TRUE(strstr(lines[0].fields, "dpi=144") != NULL);
    ASSERT_TRUE(strstr(lines[0].fields, "ai_docked=1") != NULL);
    ASSERT_TRUE(strstr(lines[0].fields, "since_paint_ms=9") != NULL);
    ASSERT_TRUE(strstr(lines[0].fields, "focus=0xabcd") != NULL);
    ASSERT_TRUE(strstr(lines[1].fields, "kind=ssh") != NULL);
    ASSERT_TRUE(strstr(lines[1].fields, "grid=24x80") != NULL);
    ASSERT_TRUE(strstr(lines[1].fields, "alt=1") != NULL);
    ASSERT_TRUE(strstr(lines[1].fields, "cursor=5,9") != NULL);
    ASSERT_TRUE(strstr(lines[1].fields, "region=0-23") != NULL);
    ASSERT_TRUE(strstr(lines[1].fields, "write_seq=123456789") != NULL);
    ASSERT_TRUE(strstr(lines[1].fields, "dirty=0-4") != NULL);
    ASSERT_TRUE(strstr(lines[2].fields, "char=9x18") != NULL);
    ASSERT_TRUE(strstr(lines[2].fields, "prev_cursor=4,8") != NULL);
    ASSERT_TRUE(strstr(lines[3].fields, "kind=local") != NULL);
    ASSERT_TRUE(strstr(lines[4].fields, "dirty_rows=7") != NULL);
    ASSERT_TRUE(strstr(lines[5].fields, "policy=read/none") != NULL);
    ASSERT_TRUE(strstr(lines[5].fields, "dispatch_batch=2") != NULL);
    TEST_END();
}

int test_dlog_snapshot_format_no_active_term_and_no_dirty_array(void)
{
    TEST_BEGIN();
    DlogSnapshot s = sample();
    s.has_term = 0;
    s.nsessions = 0;
    DlogSnapLine lines[16];
    size_t n = dlog_snapshot_format(&s, lines, 16);
    ASSERT_EQ(n, (size_t)3);           /* window, render, ai */
    ASSERT_STR_EQ(lines[1].event, "issue.render");
    s.has_term = 1;
    s.term.dirty = NULL;
    n = dlog_snapshot_format(&s, lines, 16);
    ASSERT_EQ(n, (size_t)4);
    ASSERT_TRUE(strstr(lines[1].fields, "dirty=none") != NULL);
    TEST_END();
}

int test_dlog_snapshot_format_quotes_odd_strings_and_null_kind(void)
{
    TEST_BEGIN();
    DlogSnapshot s = sample();
    s.term.kind = "my kind";
    s.ai.policy = NULL;
    DlogSnapLine lines[16];
    (void)dlog_snapshot_format(&s, lines, 16);
    ASSERT_TRUE(strstr(lines[1].fields, "kind=\"my kind\"") != NULL);
    ASSERT_TRUE(strstr(lines[5].fields, "policy=-") != NULL);
    TEST_END();
}

int test_dlog_snapshot_format_respects_max_and_null(void)
{
    TEST_BEGIN();
    DlogSnapshot s = sample();
    DlogSnapLine lines[16];
    ASSERT_EQ(dlog_snapshot_format(&s, lines, 2), (size_t)2);
    ASSERT_EQ(dlog_snapshot_format(&s, lines, 0), (size_t)0);
    ASSERT_EQ(dlog_snapshot_format(NULL, lines, 16), (size_t)0);
    ASSERT_EQ(dlog_snapshot_format(&s, NULL, 16), (size_t)0);
    TEST_END();
}

int test_dlog_snapshot_format_session_overflow_is_clamped(void)
{
    TEST_BEGIN();
    DlogSnapshot s = sample();
    s.nsessions = 9999;                /* garbage count must not overrun the array */
    DlogSnapLine lines[64];
    size_t n = dlog_snapshot_format(&s, lines, 64);
    ASSERT_TRUE(n <= 3 + DLOG_SNAP_MAX_SESSIONS + 1);
    s.nsessions = -3;
    n = dlog_snapshot_format(&s, lines, 64);
    ASSERT_EQ(n, (size_t)4);           /* window, term, render, ai */
    TEST_END();
}
