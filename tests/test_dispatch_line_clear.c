#include "test_framework.h"
#include "dispatch_line_clear.h"

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
