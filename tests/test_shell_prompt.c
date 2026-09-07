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
