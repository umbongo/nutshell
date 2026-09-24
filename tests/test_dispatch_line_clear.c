#include "test_framework.h"
#include "dispatch_line_clear.h"
#include "local_shell.h"

int test_dispatch_line_clear_local_gitbash_is_readline(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_line_clear_mode(SESSION_LOCAL, "Git bash"),
              DISPATCH_LINE_CLEAR_READLINE);
    TEST_END();
}

int test_dispatch_line_clear_local_msys2_is_readline(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_line_clear_mode(SESSION_LOCAL, "MSYS2"),
              DISPATCH_LINE_CLEAR_READLINE);
    TEST_END();
}

int test_dispatch_line_clear_local_powershell_is_none(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_line_clear_mode(SESSION_LOCAL, "PowerShell"),
              DISPATCH_LINE_CLEAR_NONE);
    TEST_END();
}

int test_dispatch_line_clear_local_cmd_is_none(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_line_clear_mode(SESSION_LOCAL, "cmd"),
              DISPATCH_LINE_CLEAR_NONE);
    TEST_END();
}

int test_dispatch_line_clear_local_custom_is_none(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_line_clear_mode(SESSION_LOCAL, "custom"),
              DISPATCH_LINE_CLEAR_NONE);
    TEST_END();
}

int test_dispatch_line_clear_local_empty_name_is_none(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_line_clear_mode(SESSION_LOCAL, ""),
              DISPATCH_LINE_CLEAR_NONE);
    TEST_END();
}

int test_dispatch_line_clear_local_null_name_is_none(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_line_clear_mode(SESSION_LOCAL, NULL),
              DISPATCH_LINE_CLEAR_NONE);
    TEST_END();
}

/* SSH always gets READLINE -- today's behaviour, kept regardless of
 * CmdPlatform (see the header's rationale): CMD_PLATFORM_UNKNOWN covers
 * plenty of ordinary undetected Linux hosts, and the network-device CLIs
 * this app classifies support Ctrl+E/Ctrl+U too. A remote Windows host
 * running PowerShell/cmd over OpenSSH is the one case this still gets
 * wrong -- an open item, since SSH has no Windows CmdPlatform to key off. */
int test_dispatch_line_clear_ssh_is_always_readline(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_line_clear_mode(SESSION_SSH, NULL),
              DISPATCH_LINE_CLEAR_READLINE);
    TEST_END();
}

/* local_shell_name is ignored for SESSION_SSH -- a stray value (even one
 * that would mean NONE for SESSION_LOCAL) must not change the decision. */
int test_dispatch_line_clear_ssh_ignores_local_shell_name(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_line_clear_mode(SESSION_SSH, "PowerShell"),
              DISPATCH_LINE_CLEAR_READLINE);
    ASSERT_EQ(dispatch_line_clear_mode(SESSION_SSH, "cmd"),
              DISPATCH_LINE_CLEAR_READLINE);
    ASSERT_EQ(dispatch_line_clear_mode(SESSION_SSH, "Git bash"),
              DISPATCH_LINE_CLEAR_READLINE);
    TEST_END();
}

/* Feed local_shell_kind_name()'s own output into dispatch_line_clear_mode()
 * for every local shell kind it knows, so a rename of a LocalShellKind
 * constant or its returned string cannot silently change the READLINE/NONE
 * mapping without a test failing -- these two functions currently agree by
 * construction (the "Git bash"/"MSYS2" string literals in
 * dispatch_line_clear.c happen to match local_shell_kind_name()'s), not by
 * a shared symbol. */
int test_dispatch_line_clear_matches_local_shell_kind_name(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_line_clear_mode(SESSION_LOCAL,
                                        local_shell_kind_name(SHELL_GITBASH)),
              DISPATCH_LINE_CLEAR_READLINE);
    ASSERT_EQ(dispatch_line_clear_mode(SESSION_LOCAL,
                                        local_shell_kind_name(SHELL_MSYS2)),
              DISPATCH_LINE_CLEAR_READLINE);
    ASSERT_EQ(dispatch_line_clear_mode(SESSION_LOCAL,
                                        local_shell_kind_name(SHELL_PWSH)),
              DISPATCH_LINE_CLEAR_NONE);
    ASSERT_EQ(dispatch_line_clear_mode(SESSION_LOCAL,
                                        local_shell_kind_name(SHELL_POWERSHELL)),
              DISPATCH_LINE_CLEAR_NONE);
    ASSERT_EQ(dispatch_line_clear_mode(SESSION_LOCAL,
                                        local_shell_kind_name(SHELL_CMD)),
              DISPATCH_LINE_CLEAR_NONE);
    TEST_END();
}

/* ---- dispatch_keystroke_too_recent() ----------------------------------- */

int test_dispatch_keystroke_too_recent_zero_elapsed(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_keystroke_too_recent(0), 1);
    TEST_END();
}

int test_dispatch_keystroke_too_recent_just_under_guard(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_keystroke_too_recent(DISPATCH_KEYSTROKE_GUARD_MS - 1), 1);
    TEST_END();
}

int test_dispatch_keystroke_too_recent_at_guard_is_not_recent(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_keystroke_too_recent(DISPATCH_KEYSTROKE_GUARD_MS), 0);
    TEST_END();
}

int test_dispatch_keystroke_too_recent_well_past_guard(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_keystroke_too_recent(60000), 0);
    TEST_END();
}

/* ---- dispatch_ambiguous_prompt_timed_out() ----------------------------- */

int test_dispatch_ambiguous_prompt_not_timed_out_at_zero(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_ambiguous_prompt_timed_out(0), 0);
    TEST_END();
}

int test_dispatch_ambiguous_prompt_not_timed_out_just_under_bound(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_ambiguous_prompt_timed_out(
                  DISPATCH_AMBIGUOUS_PROMPT_TIMEOUT_MS - 1), 0);
    TEST_END();
}

int test_dispatch_ambiguous_prompt_timed_out_at_bound(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_ambiguous_prompt_timed_out(
                  DISPATCH_AMBIGUOUS_PROMPT_TIMEOUT_MS), 1);
    TEST_END();
}

int test_dispatch_ambiguous_prompt_timed_out_well_past_bound(void) {
    TEST_BEGIN();
    ASSERT_EQ(dispatch_ambiguous_prompt_timed_out(600000), 1);
    TEST_END();
}
