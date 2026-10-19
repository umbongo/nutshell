/* tests/test_chat_exec.c -- chat_exec_split(): the [EXEC] segment split the
 * chat painter and the copier share (spec 2026-10-18 section 2). */
#include "test_framework.h"
#include "chat_exec.h"
#include <string.h>

/* Collect every segment of `t` into segs (at most max); returns the count. */
static int split_all(const char *t, ChatSeg *segs, int max)
{
    int pos = 0, n = 0;
    ChatSeg s;
    while (chat_exec_split(t, (int)strlen(t), &pos, &s)) {
        if (n < max) segs[n] = s;
        n++;
        if (n > 64) break;   /* a stuck iterator fails the test, not the run */
    }
    return n;
}

static int seg_is(const char *t, ChatSeg s, ChatSegKind kind, const char *want)
{
    int n = (int)strlen(want);
    return s.kind == kind && s.end - s.start == n &&
           memcmp(t + s.start, want, (size_t)n) == 0;
}

int test_chat_exec_split_no_marker_is_one_prose_segment(void)
{
    TEST_BEGIN();
    const char *t = "Hello\n\n**world**\n";
    ChatSeg s[4];
    int n = split_all(t, s, 4);
    ASSERT_EQ(n, 1);
    ASSERT_TRUE(seg_is(t, s[0], CHAT_SEG_PROSE, "Hello\n\n**world**\n"));
    TEST_END();
}

int test_chat_exec_split_command_between_prose(void)
{
    TEST_BEGIN();
    const char *t = "Run this:\n\n[EXEC]ls -la[/EXEC]\nDone.";
    ChatSeg s[4];
    int n = split_all(t, s, 4);
    ASSERT_EQ(n, 3);
    ASSERT_TRUE(seg_is(t, s[0], CHAT_SEG_PROSE, "Run this:"));
    ASSERT_TRUE(seg_is(t, s[1], CHAT_SEG_EXEC, "ls -la"));
    ASSERT_TRUE(seg_is(t, s[2], CHAT_SEG_PROSE, "\nDone."));
    TEST_END();
}

int test_chat_exec_split_crlf_before_marker_is_trimmed(void)
{
    TEST_BEGIN();
    const char *t = "a\r\n\r\n[EXEC]x[/EXEC]";
    ChatSeg s[4];
    int n = split_all(t, s, 4);
    ASSERT_EQ(n, 2);
    ASSERT_TRUE(seg_is(t, s[0], CHAT_SEG_PROSE, "a"));
    ASSERT_TRUE(seg_is(t, s[1], CHAT_SEG_EXEC, "x"));
    TEST_END();
}

int test_chat_exec_split_only_newlines_before_marker_give_no_prose(void)
{
    TEST_BEGIN();
    const char *t = "\n\n[EXEC]pwd[/EXEC]";
    ChatSeg s[4];
    int n = split_all(t, s, 4);
    ASSERT_EQ(n, 1);
    ASSERT_TRUE(seg_is(t, s[0], CHAT_SEG_EXEC, "pwd"));
    TEST_END();
}

int test_chat_exec_split_unterminated_keeps_the_marker(void)
{
    TEST_BEGIN();
    const char *t = "Try [EXEC]uname -a";
    ChatSeg s[4];
    int n = split_all(t, s, 4);
    ASSERT_EQ(n, 2);
    ASSERT_TRUE(seg_is(t, s[0], CHAT_SEG_PROSE, "Try "));
    ASSERT_TRUE(seg_is(t, s[1], CHAT_SEG_EXEC, "[EXEC]uname -a"));
    TEST_END();
}

int test_chat_exec_split_empty_and_adjacent_commands(void)
{
    TEST_BEGIN();
    const char *t = "[EXEC][/EXEC][EXEC]a[/EXEC][EXEC]b[/EXEC]";
    ChatSeg s[4];
    int n = split_all(t, s, 4);
    ASSERT_EQ(n, 3);
    ASSERT_EQ(s[0].kind, CHAT_SEG_EXEC);
    ASSERT_EQ(s[0].start, s[0].end);
    ASSERT_TRUE(seg_is(t, s[1], CHAT_SEG_EXEC, "a"));
    ASSERT_TRUE(seg_is(t, s[2], CHAT_SEG_EXEC, "b"));
    TEST_END();
}

int test_chat_exec_split_stray_close_is_prose(void)
{
    TEST_BEGIN();
    const char *t = "x [/EXEC] y";
    ChatSeg s[4];
    int n = split_all(t, s, 4);
    ASSERT_EQ(n, 1);
    ASSERT_TRUE(seg_is(t, s[0], CHAT_SEG_PROSE, "x [/EXEC] y"));
    TEST_END();
}

int test_chat_exec_split_respects_len_and_null(void)
{
    TEST_BEGIN();
    const char *t = "ab[EXEC]cd[/EXEC]";
    int pos = 0;
    int r;
    ChatSeg s;
    /* len 4 cuts the marker short: the whole prefix is prose */
    r = chat_exec_split(t, 4, &pos, &s);
    ASSERT_EQ(r, 1);
    ASSERT_EQ(s.kind, CHAT_SEG_PROSE);
    ASSERT_EQ(s.start, 0);
    ASSERT_EQ(s.end, 4);
    r = chat_exec_split(t, 4, &pos, &s);
    ASSERT_EQ(r, 0);
    pos = 0;
    r = chat_exec_split(NULL, 4, &pos, &s);
    ASSERT_EQ(r, 0);
    r = chat_exec_split(t, 0, &pos, &s);
    ASSERT_EQ(r, 0);
    r = chat_exec_split(t, 4, NULL, &s);
    ASSERT_EQ(r, 0);
    TEST_END();
}
