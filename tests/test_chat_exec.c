/* tests/test_chat_exec.c -- chat_exec_split(): the [EXEC] segment split the
 * chat painter and the copier share (spec 2026-10-18 section 2). */
#include "test_framework.h"
#include "chat_exec.h"
#include "ai_prompt.h"
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

/* ---- quoted markers are prose (they render inside their own code) ---- */

int test_chat_exec_split_fenced_marker_is_prose(void)
{
    TEST_BEGIN();
    const char *t = "Run:\n```\n[EXEC]rm -rf /tmp/x[/EXEC]\n```\nDone.";
    ChatSeg s[4];
    int n = split_all(t, s, 4);
    ASSERT_EQ(n, 1);
    ASSERT_TRUE(seg_is(t, s[0], CHAT_SEG_PROSE, t));
    TEST_END();
}

int test_chat_exec_split_inline_code_marker_is_prose(void)
{
    TEST_BEGIN();
    const char *t = "Try `[EXEC]ls[/EXEC]` here";
    ChatSeg s[4];
    int n = split_all(t, s, 4);
    ASSERT_EQ(n, 1);
    ASSERT_TRUE(seg_is(t, s[0], CHAT_SEG_PROSE, t));
    TEST_END();
}

int test_chat_exec_split_marker_on_the_fence_line_is_prose(void)
{
    TEST_BEGIN();
    const char *t = "```[EXEC]ls[/EXEC]\nbody\n```\nafter";
    ChatSeg s[4];
    int n = split_all(t, s, 4);
    ASSERT_EQ(n, 1);
    ASSERT_TRUE(seg_is(t, s[0], CHAT_SEG_PROSE, t));
    TEST_END();
}

int test_chat_exec_split_crlf_fenced_marker_is_prose(void)
{
    TEST_BEGIN();
    const char *t = "Run:\r\n```\r\n[EXEC]rm x[/EXEC]\r\n```\r\nDone.";
    ChatSeg s[4];
    int n = split_all(t, s, 4);
    ASSERT_EQ(n, 1);
    ASSERT_TRUE(seg_is(t, s[0], CHAT_SEG_PROSE, t));
    TEST_END();
}

int test_chat_exec_split_unterminated_marker_in_a_fence_is_prose(void)
{
    TEST_BEGIN();
    const char *t = "```\n[EXEC]ls\n```\nafter";
    ChatSeg s[4];
    int n = split_all(t, s, 4);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(s[0].kind, CHAT_SEG_PROSE);
    TEST_END();
}

int test_chat_exec_split_live_marker_after_quoted_ones_still_splits(void)
{
    TEST_BEGIN();
    const char *t = "Example: `[EXEC]ls[/EXEC]`\n[EXEC]pwd[/EXEC]\nbye";
    ChatSeg s[4];
    int n = split_all(t, s, 4);
    ASSERT_EQ(n, 3);
    ASSERT_TRUE(seg_is(t, s[0], CHAT_SEG_PROSE, "Example: `[EXEC]ls[/EXEC]`"));
    ASSERT_TRUE(seg_is(t, s[1], CHAT_SEG_EXEC, "pwd"));
    ASSERT_TRUE(seg_is(t, s[2], CHAT_SEG_PROSE, "\nbye"));

    const char *u = "```\n[EXEC]a[/EXEC]\n```\n[EXEC]b[/EXEC]";
    n = split_all(u, s, 4);
    ASSERT_EQ(n, 2);
    ASSERT_TRUE(seg_is(u, s[0], CHAT_SEG_PROSE, "```\n[EXEC]a[/EXEC]\n```"));
    ASSERT_TRUE(seg_is(u, s[1], CHAT_SEG_EXEC, "b"));
    TEST_END();
}

int test_chat_exec_split_agrees_with_the_extractor(void)
{
    TEST_BEGIN();
    /* What the painter draws as a command is what ai_extract_commands_full()
     * would run, and the other way round. */
    const char *replies[] = {
        "a `[EXEC]x[/EXEC]` b\n[EXEC]one[/EXEC]\nc\n[EXEC]two[/EXEC]",
        "```\n[EXEC]q[/EXEC]\n```\n[EXEC]one[/EXEC]",
        "`[EXEC]` markers\n[EXEC]one[/EXEC]",
        "- item `[EXEC]q[/EXEC]`\n[EXEC]one[/EXEC]",
    };
    for (size_t i = 0; i < sizeof(replies) / sizeof(replies[0]); i++) {
        const char *t = replies[i];
        char cmds[8][1024];
        int nc = ai_extract_commands(t, cmds, 8);
        int pos = 0, ne = 0;
        ChatSeg sg;
        while (chat_exec_split(t, (int)strlen(t), &pos, &sg)) {
            if (sg.kind != CHAT_SEG_EXEC) continue;
            ASSERT_TRUE(ne < nc);
            ASSERT_EQ(sg.end - sg.start, (int)strlen(cmds[ne]));
            ASSERT_TRUE(memcmp(t + sg.start, cmds[ne],
                               (size_t)(sg.end - sg.start)) == 0);
            ne++;
        }
        ASSERT_EQ(ne, nc);
    }
    TEST_END();
}
