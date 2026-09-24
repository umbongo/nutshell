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

/* ---- PowerShell: "PS <path>> " is primary, PSReadLine's ">> " is not ---- */

int test_shell_prompt_ps_drive_root_positive(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line("PS C:\\>"), 1);
    ASSERT_EQ(shell_prompt_is_continuation("PS C:\\>"), 0);
    TEST_END();
}

int test_shell_prompt_ps_home_trailing_space_positive(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line("PS C:\\Users\\x> "), 1);
    ASSERT_EQ(shell_prompt_is_continuation("PS C:\\Users\\x> "), 0);
    TEST_END();
}

int test_shell_prompt_ps_nested_prompt_positive(void) {
    TEST_BEGIN();
    /* PowerShell's default prompt adds one '>' per nested prompt level
     * ($NestedPromptLevel), so "PS C:\>> " is a live primary prompt. Only a
     * ">>" with nothing before it is the continuation prompt. */
    ASSERT_EQ(shell_prompt_line("PS C:\\>> "), 1);
    ASSERT_EQ(shell_prompt_is_continuation("PS C:\\>> "), 0);
    TEST_END();
}

int test_shell_prompt_is_continuation_ps_double_gt(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_continuation(">>"), 1);
    ASSERT_EQ(shell_prompt_is_continuation(">> "), 1);
    ASSERT_EQ(shell_prompt_is_continuation(">>   "), 1);
    ASSERT_EQ(shell_prompt_is_continuation(" >>\t"), 1);
    TEST_END();
}

int test_shell_prompt_line_ps_double_gt_negative(void) {
    TEST_BEGIN();
    /* PSReadLine's (and the plain console host's) continuation prompt: the
     * dispatcher must not send the next command into it. */
    ASSERT_EQ(shell_prompt_line(">>"), 0);
    ASSERT_EQ(shell_prompt_line(">> "), 0);
    TEST_END();
}

int test_shell_prompt_triple_gt_unchanged(void) {
    TEST_BEGIN();
    /* ">>>" (Python's REPL prompt) and "> >" are not PowerShell's
     * continuation prompt and keep their earlier results. */
    ASSERT_EQ(shell_prompt_is_continuation(">>>"), 0);
    ASSERT_EQ(shell_prompt_line(">>>"), 1);
    ASSERT_EQ(shell_prompt_is_continuation("> >"), 0);
    TEST_END();
}

int test_shell_prompt_double_gt_change_keeps_existing_results(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line("router>"), 1);
    ASSERT_EQ(shell_prompt_is_continuation("router>"), 0);
    ASSERT_EQ(shell_prompt_line("foo>"), 1);
    ASSERT_EQ(shell_prompt_is_continuation("foo>"), 0);
    ASSERT_EQ(shell_prompt_line("dquote>"), 0);
    ASSERT_EQ(shell_prompt_is_continuation("dquote>"), 1);
    ASSERT_EQ(shell_prompt_line(">"), 0);
    ASSERT_EQ(shell_prompt_is_continuation(">"), 1);
    TEST_END();
}

/* ---- shell_prompt_line_unambiguous() ----------------------------------
 * The no-prefix dispatch safety check (dispatch_line_clear.h): true only
 * for a bare prompt with nothing else typed on the line. */

int test_shell_prompt_unambiguous_powershell_bare(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line_unambiguous("PS C:\\Users\\thoma>"), 1);
    TEST_END();
}

int test_shell_prompt_unambiguous_powershell_trailing_space(void) {
    TEST_BEGIN();
    /* PowerShell's real prompt has a trailing space after '>' -- trimmed
     * away before the terminator check, same as shell_prompt_line(). */
    ASSERT_EQ(shell_prompt_line_unambiguous("PS C:\\Users\\thoma> "), 1);
    TEST_END();
}

int test_shell_prompt_unambiguous_powershell_path_with_spaces_positive(void) {
    TEST_BEGIN();
    /* A directory name with spaces in it ("Program Files") must not be
     * mistaken for typed content paused before the terminator -- the
     * character right before '>' is 's', not whitespace, regardless of
     * what earlier in the line contains. */
    ASSERT_EQ(shell_prompt_line_unambiguous("PS C:\\Program Files> "), 1);
    TEST_END();
}

int test_shell_prompt_unambiguous_powershell_drive_root_positive(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line_unambiguous("PS C:\\> "), 1);
    TEST_END();
}

int test_shell_prompt_unambiguous_powershell_typed_redirect_negative(void) {
    TEST_BEGIN();
    /* The bug case: "ls -la >" ends in '>' too, but it's the redirection
     * operator mid-command, with a space right before it -- not a prompt. */
    ASSERT_EQ(shell_prompt_line_unambiguous("PS C:\\Users\\thoma> ls -la >"), 0);
    /* shell_prompt_line() itself still reads this row as prompt-shaped --
     * that is exactly the gap this stricter check closes. */
    ASSERT_EQ(shell_prompt_line("PS C:\\Users\\thoma> ls -la >"), 1);
    TEST_END();
}

int test_shell_prompt_unambiguous_cmd_bare(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line_unambiguous("C:\\Users\\thoma>"), 1);
    TEST_END();
}

int test_shell_prompt_unambiguous_cmd_typed_redirect_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line_unambiguous("C:\\Users\\thoma>dir >"), 0);
    TEST_END();
}

int test_shell_prompt_unambiguous_bash_bare(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line_unambiguous("thomas@tompi:~$"), 1);
    TEST_END();
}

int test_shell_prompt_unambiguous_bash_typed_text_negative(void) {
    TEST_BEGIN();
    /* "echo hi #" ends in '#', one of shell_prompt_line()'s terminator
     * characters, but it is a shell comment the user is mid-typing, not a
     * root prompt -- the space right before the '#' is what this guards
     * against, regardless of shell family. */
    ASSERT_EQ(shell_prompt_line_unambiguous("thomas@tompi:~$ echo hi #"), 0);
    TEST_END();
}

int test_shell_prompt_unambiguous_single_char_prompt_positive(void) {
    TEST_BEGIN();
    /* Nothing precedes the terminator -- not "preceded by whitespace",
     * so this passes. */
    ASSERT_EQ(shell_prompt_line_unambiguous("$"), 1);
    TEST_END();
}

int test_shell_prompt_unambiguous_tab_before_terminator_negative(void) {
    TEST_BEGIN();
    /* A tab directly before the terminator is rejected the same as a
     * space. */
    ASSERT_EQ(shell_prompt_line_unambiguous("PS C:\\Users\\thoma>\t>"), 0);
    TEST_END();
}

int test_shell_prompt_unambiguous_non_prompt_negative(void) {
    TEST_BEGIN();
    /* Anything shell_prompt_line() already rejects stays rejected. */
    ASSERT_EQ(shell_prompt_line_unambiguous("Reading package lists..."), 0);
    TEST_END();
}

int test_shell_prompt_unambiguous_continuation_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line_unambiguous("dquote>"), 0);
    ASSERT_EQ(shell_prompt_line_unambiguous(">"), 0);
    TEST_END();
}

int test_shell_prompt_unambiguous_empty_and_null_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_line_unambiguous(""), 0);
    ASSERT_EQ(shell_prompt_line_unambiguous(NULL), 0);
    TEST_END();
}

/* ---- shell_prompt_is_windows() -----------------------------------------
 * Used by dispatch_line_clear_mode() (dispatch_line_clear.h) to force no
 * clear-line prefix at a live PowerShell/cmd prompt regardless of session
 * kind -- an SSH session to a Windows host, or a pwsh/cmd nested inside a
 * local Git bash/MSYS2 session. */

int test_shell_prompt_is_windows_powershell_user_path(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_windows("PS C:\\Users\\thoma> "), 1);
    TEST_END();
}

int test_shell_prompt_is_windows_powershell_drive_root(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_windows("PS C:\\> "), 1);
    TEST_END();
}

int test_shell_prompt_is_windows_powershell_path_with_spaces(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_windows("PS C:\\Program Files> "), 1);
    TEST_END();
}

int test_shell_prompt_is_windows_pwsh_on_unix_path(void) {
    TEST_BEGIN();
    /* pwsh running on a POSIX host still shows "PS " -- must still get
     * NONE, since it is PSReadLine either way. */
    ASSERT_EQ(shell_prompt_is_windows("PS /home/thoma> "), 1);
    TEST_END();
}

int test_shell_prompt_is_windows_powershell_remoting_prefix(void) {
    TEST_BEGIN();
    /* Enter-PSSession prepends "[host]: " to every prompt line. */
    ASSERT_EQ(shell_prompt_is_windows("[web01]: PS C:\\Users\\admin> "), 1);
    TEST_END();
}

int test_shell_prompt_is_windows_powershell_remoting_prefix_fqdn(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_windows("[web01.corp.example.com]: PS C:\\> "), 1);
    TEST_END();
}

int test_shell_prompt_is_windows_cmd_bare(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_windows("C:\\Users\\thoma>"), 1);
    TEST_END();
}

int test_shell_prompt_is_windows_cmd_drive_root(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_windows("C:\\>"), 1);
    TEST_END();
}

int test_shell_prompt_is_windows_cmd_customized_space_before_terminator(void) {
    TEST_BEGIN();
    /* "prompt $P $G" renders as "C:\x >" -- a space before the '>' is
     * still a legitimate cmd prompt shape; this function only decides the
     * prefix, not whether the line is safe to send to. */
    ASSERT_EQ(shell_prompt_is_windows("C:\\x >"), 1);
    TEST_END();
}

int test_shell_prompt_is_windows_typed_redirect_still_matches(void) {
    TEST_BEGIN();
    /* Deliberately over-matches a command paused mid-line: this function
     * only decides which prefix to send (always NONE here either way),
     * never whether the line is safe to send to --
     * term_at_unambiguous_prompt() is what must (and does, separately)
     * reject this row for sending. */
    ASSERT_EQ(shell_prompt_is_windows("PS C:\\Users\\thoma> ls -la >"), 1);
    ASSERT_EQ(shell_prompt_line_unambiguous("PS C:\\Users\\thoma> ls -la >"), 0);
    TEST_END();
}

int test_shell_prompt_is_windows_bash_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_windows("thomas@tompi:~$ "), 0);
    TEST_END();
}

int test_shell_prompt_is_windows_bash_space_before_terminator_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_windows("user@host ~ $"), 0);
    TEST_END();
}

int test_shell_prompt_is_windows_zsh_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_windows("host%"), 0);
    TEST_END();
}

int test_shell_prompt_is_windows_cisco_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_windows("Router>"), 0);
    TEST_END();
}

int test_shell_prompt_is_windows_comware_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_windows("<Comware>"), 0);
    TEST_END();
}

int test_shell_prompt_is_windows_mikrotik_negative(void) {
    TEST_BEGIN();
    /* A bracketed device prompt that is not the "[host]: " remoting
     * shape -- must not be mistaken for one. */
    ASSERT_EQ(shell_prompt_is_windows("[admin@MikroTik] >"), 0);
    TEST_END();
}

int test_shell_prompt_is_windows_arubaos_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_windows("(ArubaOS) #"), 0);
    TEST_END();
}

int test_shell_prompt_is_windows_posix_path_without_ps_token_negative(void) {
    TEST_BEGIN();
    /* Git bash's own path-only prompt style -- no "PS " token, so this
     * must not be read as pwsh-on-Unix. */
    ASSERT_EQ(shell_prompt_is_windows("/c/Users/thoma>"), 0);
    TEST_END();
}

int test_shell_prompt_is_windows_powershell_continuation_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_windows(">> "), 0);
    TEST_END();
}

int test_shell_prompt_is_windows_empty_and_null_negative(void) {
    TEST_BEGIN();
    ASSERT_EQ(shell_prompt_is_windows(""), 0);
    ASSERT_EQ(shell_prompt_is_windows(NULL), 0);
    TEST_END();
}
