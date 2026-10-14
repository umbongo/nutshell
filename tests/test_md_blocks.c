#include "test_framework.h"
#include "md_blocks.h"
#include <string.h>

/* md_code_blocks: fenced code block body spans, using the renderer's own
 * line classifier (spec 2026-10-13-command-detail-and-copy-design.md
 * section 3). */

static int span_eq(const char *text, MdCodeSpan s, const char *want)
{
    size_t n = strlen(want);
    return s.end >= s.start && s.end - s.start == n &&
           memcmp(text + s.start, want, n) == 0;
}

int test_md_blocks_none(void)
{
    TEST_BEGIN();
    const char *t = "plain text\n`inline` and more\n\nno fences here";
    MdCodeSpan sp[4];
    ASSERT_EQ(md_code_blocks(t, strlen(t), sp, 4), 0);
    ASSERT_EQ(md_code_blocks("", 0, sp, 4), 0);
    ASSERT_EQ(md_code_blocks(NULL, 0, sp, 4), 0);
    TEST_END();
}

int test_md_blocks_one(void)
{
    TEST_BEGIN();
    const char *t = "Run this:\n```\nls -la\n```\nDone.";
    MdCodeSpan sp[4];
    ASSERT_EQ(md_code_blocks(t, strlen(t), sp, 4), 1);
    ASSERT_TRUE(span_eq(t, sp[0], "ls -la"));
    TEST_END();
}

int test_md_blocks_multiline_body(void)
{
    TEST_BEGIN();
    const char *t = "```\none\ntwo\n\nfour\n```";
    MdCodeSpan sp[2];
    ASSERT_EQ(md_code_blocks(t, strlen(t), sp, 2), 1);
    ASSERT_TRUE(span_eq(t, sp[0], "one\ntwo\n\nfour"));

    /* Trailing blank lines before the closing fence are not part of the body. */
    const char *t2 = "```\nls\n\n```";
    ASSERT_EQ(md_code_blocks(t2, strlen(t2), sp, 2), 1);
    ASSERT_TRUE(span_eq(t2, sp[0], "ls"));
    const char *t3 = "```bash\nls -la\r\n\r\n```";
    ASSERT_EQ(md_code_blocks(t3, strlen(t3), sp, 2), 1);
    ASSERT_TRUE(span_eq(t3, sp[0], "ls -la"));
    const char *t4 = "```\n\n\n```";
    ASSERT_EQ(md_code_blocks(t4, strlen(t4), sp, 2), 1);
    ASSERT_EQ(sp[0].end, sp[0].start);
    const char *t5 = "```\nls\n\n\n";
    ASSERT_EQ(md_code_blocks(t5, strlen(t5), sp, 2), 1);
    ASSERT_TRUE(span_eq(t5, sp[0], "ls"));
    TEST_END();
}

int test_md_blocks_two(void)
{
    TEST_BEGIN();
    const char *t = "```\na\n```\ntext\n```\nb\nc\n```\n";
    MdCodeSpan sp[4];
    ASSERT_EQ(md_code_blocks(t, strlen(t), sp, 4), 2);
    ASSERT_TRUE(span_eq(t, sp[0], "a"));
    ASSERT_TRUE(span_eq(t, sp[1], "b\nc"));
    ASSERT_TRUE(sp[0].end < sp[1].start);
    TEST_END();
}

int test_md_blocks_language_tag(void)
{
    TEST_BEGIN();
    const char *t = "```bash\necho hi\n```";
    MdCodeSpan sp[2];
    ASSERT_EQ(md_code_blocks(t, strlen(t), sp, 2), 1);
    ASSERT_TRUE(span_eq(t, sp[0], "echo hi"));
    TEST_END();
}

int test_md_blocks_unterminated(void)
{
    TEST_BEGIN();
    const char *t = "intro\n```sh\nls\npwd\n";
    MdCodeSpan sp[2];
    ASSERT_EQ(md_code_blocks(t, strlen(t), sp, 2), 1);
    ASSERT_TRUE(span_eq(t, sp[0], "ls\npwd"));
    /* No trailing newline either. */
    const char *u = "```\nls";
    ASSERT_EQ(md_code_blocks(u, strlen(u), sp, 2), 1);
    ASSERT_TRUE(span_eq(u, sp[0], "ls"));
    TEST_END();
}

int test_md_blocks_empty_body(void)
{
    TEST_BEGIN();
    /* The live path can leave a fence opener when the first [EXEC] follows
     * it: "```bash\n" and nothing more. */
    const char *t = "text\n```bash\n";
    MdCodeSpan sp[2];
    ASSERT_EQ(md_code_blocks(t, strlen(t), sp, 2), 1);
    ASSERT_EQ(sp[0].start, sp[0].end);
    ASSERT_EQ(sp[0].start, strlen(t));
    /* Opener with no newline at all. */
    const char *u = "```bash";
    ASSERT_EQ(md_code_blocks(u, strlen(u), sp, 2), 1);
    ASSERT_EQ(sp[0].start, sp[0].end);
    /* Closed, adjacent fences and a body of one blank line. */
    const char *v = "```\n```\n```\n\n```";
    MdCodeSpan s4[4];
    ASSERT_EQ(md_code_blocks(v, strlen(v), s4, 4), 2);
    ASSERT_EQ(s4[0].start, s4[0].end);
    ASSERT_EQ(s4[1].start, s4[1].end);
    TEST_END();
}

int test_md_blocks_crlf(void)
{
    TEST_BEGIN();
    const char *t = "Run:\r\n```bash\r\nls -la\r\npwd\r\n```\r\nafter\r\n";
    MdCodeSpan sp[2];
    ASSERT_EQ(md_code_blocks(t, strlen(t), sp, 2), 1);
    ASSERT_TRUE(span_eq(t, sp[0], "ls -la\r\npwd"));
    TEST_END();
}

int test_md_blocks_indented_fence_not_counted(void)
{
    TEST_BEGIN();
    /* The renderer only treats ``` at column 0 as a fence. */
    const char *t = "text\n  ```\n  ls\n  ```\nmore";
    MdCodeSpan sp[2];
    ASSERT_EQ(md_code_blocks(t, strlen(t), sp, 2), 0);
    /* An indented fence inside a real block is just body text. */
    const char *u = "```\n  ```\nx\n```";
    ASSERT_EQ(md_code_blocks(u, strlen(u), sp, 2), 1);
    ASSERT_TRUE(span_eq(u, sp[0], "  ```\nx"));
    TEST_END();
}

int test_md_blocks_max_smaller_than_count_returns_full_count(void)
{
    TEST_BEGIN();
    const char *t = "```\na\n```\n```\nb\n```\n```\nc\n```";
    MdCodeSpan sp[2];
    memset(sp, 0, sizeof(sp));
    ASSERT_EQ(md_code_blocks(t, strlen(t), sp, 2), 3);
    ASSERT_TRUE(span_eq(t, sp[0], "a"));
    ASSERT_TRUE(span_eq(t, sp[1], "b"));
    /* Count only. */
    ASSERT_EQ(md_code_blocks(t, strlen(t), NULL, 0), 3);
    ASSERT_EQ(md_code_blocks(t, strlen(t), sp, 0), 3);
    TEST_END();
}

int test_md_blocks_respects_len_not_nul(void)
{
    TEST_BEGIN();
    /* The text is longer than `len`: only the first `len` bytes count. */
    const char *t = "```\nab\n```\n```\ncd\n```";
    MdCodeSpan sp[4];
    ASSERT_EQ(md_code_blocks(t, 10, sp, 4), 1);
    ASSERT_TRUE(span_eq(t, sp[0], "ab"));
    TEST_END();
}
