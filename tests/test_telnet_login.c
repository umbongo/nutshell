#include "test_framework.h"
#include "telnet_login.h"
#include <stddef.h>

static TnLoginAction step(TnLogin *l, const char *line, unsigned long t)
{
    tn_login_on_rx(l, t);
    return tn_login_step(l, line, t + 300u);
}

int test_tn_line_prompt_forms(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(tn_line_is_login_prompt("login:"));
    ASSERT_TRUE(tn_line_is_login_prompt("Login: "));
    ASSERT_TRUE(tn_line_is_login_prompt("Username:"));
    ASSERT_TRUE(tn_line_is_login_prompt("USER NAME:  "));
    ASSERT_TRUE(tn_line_is_login_prompt("user:"));
    ASSERT_TRUE(tn_line_is_login_prompt("router login:"));
    ASSERT_TRUE(tn_line_is_login_prompt("login:\t "));
    ASSERT_TRUE(tn_line_is_password_prompt("Password:"));
    ASSERT_TRUE(tn_line_is_password_prompt("password: "));
    ASSERT_TRUE(tn_line_is_password_prompt("Passcode:"));
    ASSERT_TRUE(!tn_line_is_login_prompt("Last login: Tue Nov 3"));
    ASSERT_TRUE(!tn_line_is_password_prompt("password"));
    ASSERT_TRUE(!tn_line_is_login_prompt("username: admin"));
    ASSERT_TRUE(!tn_line_is_login_prompt(""));
    ASSERT_TRUE(!tn_line_is_password_prompt(""));
    ASSERT_TRUE(!tn_line_is_device_prompt(""));
    ASSERT_TRUE(!tn_line_is_login_prompt(NULL));
    ASSERT_TRUE(!tn_line_is_password_prompt(NULL));
    ASSERT_TRUE(!tn_line_is_device_prompt(NULL));
    ASSERT_TRUE(tn_line_is_device_prompt("Router>"));
    ASSERT_TRUE(tn_line_is_device_prompt("switch#"));
    ASSERT_TRUE(tn_line_is_device_prompt("user@host:~$ "));
    ASSERT_TRUE(!tn_line_is_device_prompt("Password:"));
    TEST_END();
}

int test_tn_login_user_then_password(void)
{
    TEST_BEGIN();
    TnLogin l;
    tn_login_init(&l, 1, 1, 0);
    ASSERT_TRUE(tn_login_armed(&l));
    ASSERT_EQ((int)step(&l, "Username:", 1000), (int)TN_LOGIN_SEND_USER);
    ASSERT_EQ((int)step(&l, "Username: admin", 2000), (int)TN_LOGIN_NONE);
    ASSERT_EQ((int)step(&l, "Password:", 3000), (int)TN_LOGIN_SEND_PASSWORD);
    ASSERT_EQ(tn_login_armed(&l), 0);
    TEST_END();
}

int test_tn_login_password_only_device(void)
{
    TEST_BEGIN();
    TnLogin l;
    tn_login_init(&l, 1, 1, 0);
    ASSERT_EQ((int)step(&l, "Password:", 1000), (int)TN_LOGIN_SEND_PASSWORD);
    tn_login_init(&l, 0, 1, 0);
    ASSERT_EQ((int)step(&l, "Password:", 1000), (int)TN_LOGIN_SEND_PASSWORD);
    ASSERT_EQ(tn_login_armed(&l), 0);
    TEST_END();
}

int test_tn_login_login_prompt_without_username_switches_off(void)
{
    TEST_BEGIN();
    TnLogin l;
    tn_login_init(&l, 0, 1, 0);
    ASSERT_EQ((int)step(&l, "login:", 1000), (int)TN_LOGIN_NONE);
    ASSERT_EQ(tn_login_armed(&l), 0);
    ASSERT_EQ((int)step(&l, "Password:", 2000), (int)TN_LOGIN_NONE);
    TEST_END();
}

int test_tn_login_username_without_password_asks(void)
{
    TEST_BEGIN();
    TnLogin l;
    tn_login_init(&l, 1, 0, 0);
    ASSERT_EQ((int)step(&l, "login:", 1000), (int)TN_LOGIN_SEND_USER);
    ASSERT_EQ((int)step(&l, "Password:", 2000), (int)TN_LOGIN_ASK_PASSWORD);
    ASSERT_EQ((int)l.phase, (int)TN_LOGIN_ASKING);
    ASSERT_EQ((int)step(&l, "Password:", 3000), (int)TN_LOGIN_NONE);
    tn_login_ask_done(&l);
    ASSERT_EQ(tn_login_armed(&l), 0);
    TEST_END();
}

int test_tn_login_neither_is_a_port_check(void)
{
    TEST_BEGIN();
    TnLogin l;
    tn_login_init(&l, 0, 0, 0);
    ASSERT_EQ(tn_login_armed(&l), 0);
    ASSERT_EQ((int)step(&l, "login:", 1000), (int)TN_LOGIN_NONE);
    TEST_END();
}

int test_tn_login_waits_for_settle(void)
{
    TEST_BEGIN();
    TnLogin l;
    tn_login_init(&l, 1, 1, 0);
    tn_login_on_rx(&l, 1000);
    ASSERT_EQ((int)tn_login_step(&l, "login:", 1299), (int)TN_LOGIN_NONE);
    ASSERT_EQ((int)tn_login_step(&l, "login:", 1300), (int)TN_LOGIN_SEND_USER);
    TEST_END();
}

int test_tn_login_needs_fresh_output(void)
{
    TEST_BEGIN();
    TnLogin l;
    tn_login_init(&l, 1, 1, 0);
    ASSERT_EQ((int)step(&l, "login:", 1000), (int)TN_LOGIN_SEND_USER);
    ASSERT_EQ((int)tn_login_step(&l, "login:", 5000), (int)TN_LOGIN_NONE);
    ASSERT_EQ((int)l.phase, (int)TN_LOGIN_USER_SENT);
    TEST_END();
}

int test_tn_login_user_input_switches_off(void)
{
    TEST_BEGIN();
    TnLogin l;
    tn_login_init(&l, 1, 1, 0);
    tn_login_on_user_input(&l);
    ASSERT_EQ((int)step(&l, "login:", 1000), (int)TN_LOGIN_NONE);
    ASSERT_EQ(tn_login_armed(&l), 0);
    TEST_END();
}

int test_tn_login_device_prompt_switches_off(void)
{
    TEST_BEGIN();
    TnLogin l;
    tn_login_init(&l, 1, 1, 0);
    ASSERT_EQ((int)step(&l, "Router>", 1000), (int)TN_LOGIN_NONE);
    ASSERT_EQ(tn_login_armed(&l), 0);
    ASSERT_EQ((int)step(&l, "Password:", 2000), (int)TN_LOGIN_NONE);
    TEST_END();
}

int test_tn_login_failure_reprompt_after_password_does_nothing(void)
{
    TEST_BEGIN();
    TnLogin l;
    tn_login_init(&l, 1, 1, 0);
    ASSERT_EQ((int)step(&l, "login:", 1000), (int)TN_LOGIN_SEND_USER);
    ASSERT_EQ((int)step(&l, "Password:", 2000), (int)TN_LOGIN_SEND_PASSWORD);
    ASSERT_EQ((int)step(&l, "Username:", 3000), (int)TN_LOGIN_NONE);
    TEST_END();
}

int test_tn_login_second_login_before_password_switches_off(void)
{
    TEST_BEGIN();
    TnLogin l;
    tn_login_init(&l, 1, 1, 0);
    ASSERT_EQ((int)step(&l, "login:", 1000), (int)TN_LOGIN_SEND_USER);
    ASSERT_EQ((int)step(&l, "Username:", 2000), (int)TN_LOGIN_NONE);
    ASSERT_EQ(tn_login_armed(&l), 0);
    TEST_END();
}

int test_tn_login_timeout(void)
{
    TEST_BEGIN();
    TnLogin l;
    tn_login_init(&l, 1, 1, 0);
    tn_login_on_rx(&l, 19000);
    ASSERT_EQ((int)tn_login_step(&l, "x", 20000), (int)TN_LOGIN_NONE);
    ASSERT_EQ(tn_login_armed(&l), 0);
    /* sending the username restarts the clock: deadline 15300 + 20000 */
    tn_login_init(&l, 1, 1, 0);
    ASSERT_EQ((int)step(&l, "login:", 15000), (int)TN_LOGIN_SEND_USER);
    ASSERT_EQ((int)step(&l, "Password:", 30000), (int)TN_LOGIN_SEND_PASSWORD);
    TEST_END();
}

int test_tn_login_null_cursor_line_is_ignored(void)
{
    TEST_BEGIN();
    TnLogin l;
    tn_login_init(&l, 1, 1, 0);
    ASSERT_EQ((int)step(&l, NULL, 1000), (int)TN_LOGIN_NONE);
    ASSERT_EQ((int)l.phase, (int)TN_LOGIN_ARMED);
    TEST_END();
}

/* The tick is a 32-bit millisecond counter that wraps every 49.7 days; the
 * callers pass values already truncated to 32 bits. */
#define TICK32(x) ((unsigned long)((x) & 0xFFFFFFFFul))

int test_tn_login_deadline_across_tick_wrap(void)
{
    TEST_BEGIN();
    const unsigned long t0 = 0xFFFFFF00ul;       /* 256 ms before the wrap */
    TnLogin l;

    /* Just before the deadline, after the wrap: still armed, not early. */
    tn_login_init(&l, 1, 1, t0);
    ASSERT_EQ((int)tn_login_step(&l, "x", TICK32(t0 + 19999u)), (int)TN_LOGIN_NONE);
    ASSERT_EQ(tn_login_armed(&l), 1);
    /* Exactly at the deadline: it fires, once. */
    ASSERT_EQ((int)tn_login_step(&l, "x", TICK32(t0 + 20000u)), (int)TN_LOGIN_NONE);
    ASSERT_EQ(tn_login_armed(&l), 0);
    ASSERT_EQ((int)tn_login_step(&l, "login:", TICK32(t0 + 20001u)), (int)TN_LOGIN_NONE);
    ASSERT_EQ(tn_login_armed(&l), 0);

    /* Past the deadline after the wrap. */
    tn_login_init(&l, 1, 1, t0);
    ASSERT_EQ((int)tn_login_step(&l, "x", TICK32(t0 + 30000u)), (int)TN_LOGIN_NONE);
    ASSERT_EQ(tn_login_armed(&l), 0);

    /* Before the wrap, well inside the window: armed. */
    tn_login_init(&l, 1, 1, t0);
    ASSERT_EQ((int)tn_login_step(&l, "x", t0 + 100u), (int)TN_LOGIN_NONE);
    ASSERT_EQ(tn_login_armed(&l), 1);
    TEST_END();
}

int test_tn_login_settle_across_tick_wrap(void)
{
    TEST_BEGIN();
    const unsigned long t0 = 0xFFFFFFF0ul;
    TnLogin l;
    tn_login_init(&l, 1, 1, t0);
    tn_login_on_rx(&l, t0);
    /* 100 ms later (after the wrap): still settling. */
    ASSERT_EQ((int)tn_login_step(&l, "login:", TICK32(t0 + 100u)), (int)TN_LOGIN_NONE);
    /* 300 ms later: settled, the user name goes out. */
    ASSERT_EQ((int)tn_login_step(&l, "login:", TICK32(t0 + 300u)), (int)TN_LOGIN_SEND_USER);
    /* The restarted deadline is also wrap-safe. */
    ASSERT_EQ((int)tn_login_step(&l, "x", TICK32(t0 + 300u + 19999u)), (int)TN_LOGIN_NONE);
    ASSERT_EQ(tn_login_armed(&l), 1);
    ASSERT_EQ((int)tn_login_step(&l, "x", TICK32(t0 + 300u + 20000u)), (int)TN_LOGIN_NONE);
    ASSERT_EQ(tn_login_armed(&l), 0);
    TEST_END();
}
