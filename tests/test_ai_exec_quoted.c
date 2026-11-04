#include "test_framework.h"
#include "ai_prompt.h"
#include <string.h>

/* Fence- and span-aware [EXEC] extraction: a marker the renderer draws as
 * code is never a command (spec 2026-11-04-guard-rails-design.md section 3). */

#define QS "Put each [EXEC]...[/EXEC] marker on its own line, never inside a " \
           "code block or inline code: a marker inside code is shown but not run."

int test_exec_quoted_fence_skipped_by_notes(void)
{
    TEST_BEGIN();
    const char *r = "Run:\n```\n[EXEC]rm -rf /tmp/x[/EXEC]\n```\n[EXEC]ls[/EXEC]";
    char cmds[4][1024];
    ASSERT_EQ(ai_extract_commands_notes(r, cmds, NULL, 4, NULL, NULL), 1);
    ASSERT_STR_EQ(cmds[0], "ls");
    TEST_END();
}

int test_exec_quoted_full_reports_both_in_order(void)
{
    TEST_BEGIN();
    const char *r = "Run:\n```\n[EXEC]rm -rf /tmp/x[/EXEC]\n```\n[EXEC]ls[/EXEC]";
    char cmds[4][1024];
    int quoted[4] = { -1, -1, -1, -1 };
    ASSERT_EQ(ai_extract_commands_full(r, cmds, NULL, quoted, 4, NULL, NULL), 2);
    ASSERT_STR_EQ(cmds[0], "rm -rf /tmp/x");
    ASSERT_EQ(quoted[0], 1);
    ASSERT_STR_EQ(cmds[1], "ls");
    ASSERT_EQ(quoted[1], 0);
    TEST_END();
}

int test_exec_quoted_unterminated_fence_runs_to_end(void)
{
    TEST_BEGIN();
    const char *r = "```\n[EXEC]a[/EXEC]\nmore\n[EXEC]b[/EXEC]";
    char cmds[4][1024];
    int quoted[4] = { 0, 0, 0, 0 };
    ASSERT_EQ(ai_extract_commands_notes(r, cmds, NULL, 4, NULL, NULL), 0);
    ASSERT_EQ(ai_extract_commands_full(r, cmds, NULL, quoted, 4, NULL, NULL), 2);
    ASSERT_EQ(quoted[0], 1);
    ASSERT_EQ(quoted[1], 1);
    TEST_END();
}

int test_exec_quoted_indented_fence_is_not_a_fence(void)
{
    TEST_BEGIN();
    const char *r = "  ```\n[EXEC]ls[/EXEC]\n  ```";
    char cmds[4][1024];
    ASSERT_EQ(ai_extract_commands_notes(r, cmds, NULL, 4, NULL, NULL), 1);
    ASSERT_STR_EQ(cmds[0], "ls");
    TEST_END();
}

int test_exec_quoted_marker_on_fence_line(void)
{
    TEST_BEGIN();
    const char *r = "```[EXEC]rm x[/EXEC]\n```\n";
    char cmds[4][1024];
    int quoted[4] = { 0, 0, 0, 0 };
    ASSERT_EQ(ai_extract_commands_notes(r, cmds, NULL, 4, NULL, NULL), 0);
    ASSERT_EQ(ai_extract_commands_full(r, cmds, NULL, quoted, 4, NULL, NULL), 1);
    ASSERT_EQ(quoted[0], 1);
    ASSERT_STR_EQ(cmds[0], "rm x");
    TEST_END();
}

int test_exec_quoted_crlf_fences(void)
{
    TEST_BEGIN();
    const char *r = "```\r\n[EXEC]ls[/EXEC]\r\n```\r\n[EXEC]pwd[/EXEC]";
    char cmds[4][1024];
    ASSERT_EQ(ai_extract_commands_notes(r, cmds, NULL, 4, NULL, NULL), 1);
    ASSERT_STR_EQ(cmds[0], "pwd");
    TEST_END();
}

int test_exec_quoted_inline_span(void)
{
    TEST_BEGIN();
    const char *r = "Try `[EXEC]ls[/EXEC]` here";
    char cmds[4][1024];
    int quoted[4] = { 0, 0, 0, 0 };
    ASSERT_EQ(ai_extract_commands_notes(r, cmds, NULL, 4, NULL, NULL), 0);
    ASSERT_EQ(ai_extract_commands_full(r, cmds, NULL, quoted, 4, NULL, NULL), 1);
    ASSERT_EQ(quoted[0], 1);
    TEST_END();
}

int test_exec_quoted_unpaired_backtick_opens_nothing(void)
{
    TEST_BEGIN();
    const char *r = "a ` b [EXEC]ls[/EXEC]";
    char cmds[4][1024];
    ASSERT_EQ(ai_extract_commands_notes(r, cmds, NULL, 4, NULL, NULL), 1);
    ASSERT_STR_EQ(cmds[0], "ls");
    TEST_END();
}

int test_exec_quoted_span_does_not_cross_lines(void)
{
    TEST_BEGIN();
    const char *r = "a `b\n[EXEC]ls[/EXEC] c`";
    char cmds[4][1024];
    ASSERT_EQ(ai_extract_commands_notes(r, cmds, NULL, 4, NULL, NULL), 1);
    ASSERT_STR_EQ(cmds[0], "ls");
    TEST_END();
}

int test_exec_live_command_with_backticks(void)
{
    TEST_BEGIN();
    const char *r = "[EXEC]echo `date`[/EXEC]";
    char cmds[4][1024];
    ASSERT_EQ(ai_extract_commands_notes(r, cmds, NULL, 4, NULL, NULL), 1);
    ASSERT_STR_EQ(cmds[0], "echo `date`");
    TEST_END();
}

int test_exec_quoted_same_checks_apply(void)
{
    TEST_BEGIN();
    char cmds[4][1024];
    int quoted[4];
    int rej = 0, rej_long = 0;

    /* A control character in a quoted marker is rejected like a live one. */
    ASSERT_EQ(ai_extract_commands_full("`[EXEC]a\x1b[/EXEC]`", cmds, NULL,
                                       quoted, 4, &rej, &rej_long), 0);
    ASSERT_EQ(rej, 1);
    ASSERT_EQ(rej_long, 0);

    /* So is a quoted 1024-byte payload. */
    char big[1100];
    memset(big, 0, sizeof(big));
    strcpy(big, "`[EXEC]");
    memset(big + strlen(big), 'a', 1024);
    strcat(big, "[/EXEC]`");
    rej = 0; rej_long = 0;
    ASSERT_EQ(ai_extract_commands_full(big, cmds, NULL, quoted, 4,
                                       &rej, &rej_long), 0);
    ASSERT_EQ(rej, 0);
    ASSERT_EQ(rej_long, 1);

    /* With quoted == NULL a quoted marker is skipped before any check. */
    rej = -1; rej_long = -1;
    ASSERT_EQ(ai_extract_commands_full("`[EXEC]a\x1b[/EXEC]`", cmds, NULL,
                                       NULL, 4, &rej, &rej_long), 0);
    ASSERT_EQ(rej, 0);
    ASSERT_EQ(rej_long, 0);
    rej = -1; rej_long = -1;
    ASSERT_EQ(ai_extract_commands_full(big, cmds, NULL, NULL, 4,
                                       &rej, &rej_long), 0);
    ASSERT_EQ(rej, 0);
    ASSERT_EQ(rej_long, 0);
    TEST_END();
}

int test_exec_quoted_notes_align(void)
{
    TEST_BEGIN();
    const char *r = "Explain\n`[EXEC]x[/EXEC]`\nThen\n[EXEC]ls[/EXEC]";
    char cmds[4][1024];
    char notes[4][CMD_NOTE_MAX];
    int quoted[4] = { 0, 0, 0, 0 };
    ASSERT_EQ(ai_extract_commands_full(r, cmds, notes, quoted, 4, NULL, NULL), 2);
    ASSERT_EQ(quoted[0], 1);
    ASSERT_STR_EQ(notes[1], "Then");
    ASSERT_STR_EQ(cmds[1], "ls");
    TEST_END();
}

int test_exec_quoted_takes_slots(void)
{
    TEST_BEGIN();
    const char *r = "`[EXEC]a[/EXEC]` `[EXEC]b[/EXEC]` `[EXEC]c[/EXEC]` "
                    "[EXEC]ls[/EXEC]";
    char cmds[2][1024];
    int quoted[2];
    ASSERT_EQ(ai_extract_commands_full(r, cmds, NULL, quoted, 2, NULL, NULL), 2);
    ASSERT_EQ(quoted[0], 1);
    ASSERT_EQ(quoted[1], 1);
    TEST_END();
}

int test_extract_command_skips_quoted(void)
{
    TEST_BEGIN();
    char cmd[1024];
    ASSERT_EQ(ai_extract_command("`[EXEC]rm x[/EXEC]` then [EXEC]ls[/EXEC]",
                                 cmd, sizeof(cmd)), 1);
    ASSERT_STR_EQ(cmd, "ls");
    ASSERT_EQ(ai_extract_command("`[EXEC]rm x[/EXEC]`", cmd, sizeof(cmd)), 0);
    TEST_END();
}

int test_response_split_ignores_quoted(void)
{
    TEST_BEGIN();
    char pre[512], post[512];
    const char *r = "Intro\n```\n[EXEC]a[/EXEC]\n```\nThen\n[EXEC]ls[/EXEC]\nAfter";
    ASSERT_EQ(ai_response_split(r, pre, sizeof(pre), post, sizeof(post)), 1);
    ASSERT_TRUE(strstr(pre, "[EXEC]a[/EXEC]") != NULL);
    ASSERT_TRUE(strstr(pre, "Then") != NULL);
    ASSERT_TRUE(strstr(post, "After") != NULL);

    const char *all = "Intro `[EXEC]a[/EXEC]` and\n```\n[EXEC]b[/EXEC]\n```\nend";
    ASSERT_EQ(ai_response_split(all, pre, sizeof(pre), post, sizeof(post)), 0);
    ASSERT_STR_EQ(pre, all);
    TEST_END();
}

int test_system_prompt_quoted_marker_sentence(void)
{
    TEST_BEGIN();
    char buf[4096];
    ai_build_system_prompt(buf, sizeof(buf), NULL, NULL, NULL, SESSION_SSH, NULL);
    ASSERT_TRUE(strstr(buf, QS) != NULL);
    ai_build_system_prompt(buf, sizeof(buf), NULL, NULL, NULL, SESSION_LOCAL, "bash");
    ASSERT_TRUE(strstr(buf, QS) != NULL);
    ai_build_system_prompt(buf, sizeof(buf), NULL, NULL, NULL, SESSION_LOCAL, "PowerShell");
    ASSERT_TRUE(strstr(buf, QS) != NULL);
    TEST_END();
}

int test_exec_quoted_inline_span_after_italic_swallows_backtick(void)
{
    TEST_BEGIN();
    /* The renderer lets the italic span "*a `b*" take the first backtick, so
     * the two backticks around the marker are the code span it draws. */
    const char *r = "*a `b* x `[EXEC]ls[/EXEC]`";
    char cmds[4][1024];
    int quoted[4] = { 0, 0, 0, 0 };
    ASSERT_EQ(ai_extract_commands_notes(r, cmds, NULL, 4, NULL, NULL), 0);
    ASSERT_EQ(ai_extract_commands_full(r, cmds, NULL, quoted, 4, NULL, NULL), 1);
    ASSERT_EQ(quoted[0], 1);
    TEST_END();
}

int test_exec_quoted_list_item_lines(void)
{
    TEST_BEGIN();
    char cmds[4][1024];
    int quoted[4] = { 0, 0, 0, 0 };

    /* The renderer parses a list item from after "* ", so the marker is not
     * read as italic. */
    ASSERT_EQ(ai_extract_commands_notes("* `[EXEC]ls[/EXEC]`", cmds, NULL, 4,
                                        NULL, NULL), 0);
    ASSERT_EQ(ai_extract_commands_full("* `[EXEC]ls[/EXEC]`", cmds, NULL,
                                       quoted, 4, NULL, NULL), 1);
    ASSERT_EQ(quoted[0], 1);

    ASSERT_EQ(ai_extract_commands_notes("* a [EXEC]ls[/EXEC]", cmds, NULL, 4,
                                        NULL, NULL), 1);
    ASSERT_STR_EQ(cmds[0], "ls");
    ASSERT_EQ(ai_extract_commands_notes("1. `[EXEC]ls[/EXEC]`", cmds, NULL, 4,
                                        NULL, NULL), 0);
    ASSERT_EQ(ai_extract_commands_notes("## `[EXEC]ls[/EXEC]`", cmds, NULL, 4,
                                        NULL, NULL), 0);
    TEST_END();
}

int test_exec_quoted_table_cell_spans(void)
{
    TEST_BEGIN();
    char cmds[4][1024];
    /* Each cell is parsed on its own: a backtick in one cell does not pair
     * with one in the next, and a span inside a cell is code. */
    ASSERT_EQ(ai_extract_commands_notes("| `[EXEC]ls[/EXEC]` | b |", cmds, NULL,
                                        4, NULL, NULL), 0);
    ASSERT_EQ(ai_extract_commands_notes("| a ` | [EXEC]ls[/EXEC] ` |", cmds, NULL,
                                        4, NULL, NULL), 1);
    TEST_END();
}

int test_exec_quoted_opener_without_own_closer_is_not_a_marker(void)
{
    TEST_BEGIN();
    /* The first opener's closer lies past another opener: it is prose, and
     * the real marker after it still runs. */
    const char *r = "`[EXEC]` markers:\n[EXEC]ls[/EXEC]";
    char cmds[4][1024];
    ASSERT_EQ(ai_extract_commands_notes(r, cmds, NULL, 4, NULL, NULL), 1);
    ASSERT_STR_EQ(cmds[0], "ls");
    char cmd[1024];
    ASSERT_EQ(ai_extract_command(r, cmd, sizeof(cmd)), 1);
    ASSERT_STR_EQ(cmd, "ls");
    TEST_END();
}
