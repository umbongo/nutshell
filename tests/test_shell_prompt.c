#include "test_framework.h"
#include "shell_prompt.h"

int test_shell_prompt_dollar(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line("thomas@tompi:~$"), 1);
    TEST_END();
}

int test_shell_prompt_hash(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line("root@web-01:/var/log#"), 1);
    TEST_END();
}

int test_shell_prompt_percent(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line("zsh%"), 1);
    TEST_END();
}

int test_shell_prompt_windows_ps(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line("PS C:\\Users\\thoma>"), 1);
    TEST_END();
}

int test_shell_prompt_bracketed_user_host(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line("[user@host dir]$"), 1);
    TEST_END();
}

int test_shell_prompt_password_prompt_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line("[sudo] password for thomas:"), 0);
    TEST_END();
}

int test_shell_prompt_yn_prompt_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line("Do you want to continue? [Y/n]"), 0);
    TEST_END();
}

int test_shell_prompt_reading_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line("Reading package lists..."), 0);
    TEST_END();
}

int test_shell_prompt_bare_number_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line("1100"), 0);
    TEST_END();
}

int test_shell_prompt_bare_gt_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line(">"), 0);
    TEST_END();
}

int test_shell_prompt_more_pager_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line("--More--"), 0);
    TEST_END();
}

int test_shell_prompt_empty_string(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line(""), 0);
    TEST_END();
}

int test_shell_prompt_null(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line(NULL), 0);
    TEST_END();
}

int test_shell_prompt_trailing_spaces_trimmed(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line("thomas@tompi:~$   "), 1);
    TEST_END();
}

int test_shell_prompt_trailing_tabs_trimmed(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line("thomas@tompi:~$\t\t"), 1);
    TEST_END();
}

int test_shell_prompt_bare_gt_with_trailing_space_negative(void) {
    TEST_BEGIN();
    /* Trims down to a bare '>' -- still a continuation prompt, not a
     * primary prompt, regardless of the trailing whitespace. */
    ASSERT_EQ(shell_prompt_line(">   "), 0);
    TEST_END();
}

int test_shell_prompt_gt_with_content_positive(void) {
    TEST_BEGIN();
    /* Unlike a bare '>', a '>' preceded by other text on the line (a
     * genuine PS1-style prompt) is a positive match even with trailing
     * whitespace to trim. */
    ASSERT_EQ(shell_prompt_line("foo>   "), 1);
    TEST_END();
}

int test_shell_prompt_all_whitespace_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line("   \t  "), 0);
    TEST_END();
}

/* ---- shell_prompt_is_continuation() -------------------------------- */

int test_shell_prompt_is_continuation_bare_gt(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_continuation(">"), 1);
    TEST_END();
}

int test_shell_prompt_is_continuation_bare_gt_trailing_space(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_continuation("> "), 1);
    TEST_END();
}

int test_shell_prompt_is_continuation_dquote(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_continuation("dquote> "), 1);
    TEST_END();
}

int test_shell_prompt_is_continuation_cmdsubst(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_continuation("cmdsubst>"), 1);
    TEST_END();
}

int test_shell_prompt_is_continuation_heredoc(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_continuation("heredoc>"), 1);
    TEST_END();
}

int test_shell_prompt_is_continuation_then(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_continuation("then>"), 1);
    TEST_END();
}

int test_shell_prompt_is_continuation_foo_negative(void) {
    TEST_BEGIN();
    /* "foo" is not one of zsh's secondary-prompt words. */
    ASSERT_EQ(shell_prompt_is_continuation("foo>"), 0);
    TEST_END();
}

int test_shell_prompt_is_continuation_router_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_continuation("router>"), 0);
    TEST_END();
}

int test_shell_prompt_is_continuation_word_prefix_negative(void) {
    TEST_BEGIN();
    /* "mydquote" is not an exact match for "dquote" -- must be the whole
     * word before the '>', not merely a suffix of it. */
    ASSERT_EQ(shell_prompt_is_continuation("mydquote>"), 0);
    TEST_END();
}

int test_shell_prompt_is_continuation_dollar_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_continuation("$"), 0);
    TEST_END();
}

int test_shell_prompt_is_continuation_empty_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_continuation(""), 0);
    TEST_END();
}

int test_shell_prompt_is_continuation_null_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_continuation(NULL), 0);
    TEST_END();
}

/* ---- shell_prompt_line() deferring to shell_prompt_is_continuation() -- */

int test_shell_prompt_line_dquote_continuation_negative(void) {
    TEST_BEGIN();
    /* zsh's "dquote> " continuation prompt must not pass as a primary
     * prompt just because it ends in '>'. */
    ASSERT_EQ(shell_prompt_line("dquote> "), 0);
    TEST_END();
}

int test_shell_prompt_line_router_positive(void) {
    TEST_BEGIN();
    /* An ordinary prompt ending in '>' (not one of zsh's secondary-prompt
     * words) still passes, same as "foo>" already does. */
    ASSERT_EQ(shell_prompt_line("router>"), 1);
    TEST_END();
}
