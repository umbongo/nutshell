#include "test_framework.h"
#include "telnet_session.h"
#include "term.h"
#include "term_extract.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX(s, T, lit) tn_session_rx((s), (T), NULL, (const unsigned char *)(lit), sizeof(lit) - 1u, 0)

static TnSession *mk(const char *user, const char *pass)
{
    TnSessionOpts o;
    memset(&o, 0, sizeof(o));
    o.username = user;
    o.password = pass;
    o.cols = 80;
    o.rows = 24;
    o.now_ms = 0;
    return tn_session_new(&o);
}

static void rx_at(TnSession *s, Terminal *t, const char *lit, size_t n, unsigned long ms)
{
    tn_session_rx(s, t, NULL, (const unsigned char *)lit, n, ms);
}
#define RXT(s, T, lit, ms) rx_at((s), (T), (lit), sizeof(lit) - 1u, (ms))

/* Returns the queue as a string (bytes, NUL-terminated) and consumes it. */
static const char *outq_str(TnSession *s)
{
    static char buf[1024];
    size_t n = 0;
    const unsigned char *q = tn_session_outq(s, &n);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1u;
    if (n) memcpy(buf, q, n);
    buf[n] = '\0';
    tn_session_outq_consume(s, n);
    return buf;
}

static int outq_is(TnSession *s, const void *exp, size_t n)
{
    size_t got = 0;
    const unsigned char *q = tn_session_outq(s, &got);
    int ok = got == n && (n == 0u || memcmp(q, exp, n) == 0);
    tn_session_outq_consume(s, got);
    return ok;
}
#define OUTQ_IS(s, lit) outq_is((s), (lit), sizeof(lit) - 1u)

static const char *screen(Terminal *t)
{
    static char buf[4096];
    term_extract_visible(t, buf, sizeof(buf));
    return buf;
}

int test_term_cursor_row_prompt_text_public(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    char buf[64];
    term_process(t, "Password: ", 10);
    ASSERT_EQ(term_cursor_row_prompt_text(t, buf, sizeof(buf)), 1);
    ASSERT_STR_EQ(buf, "Password: ");
    term_process(t, "\x1b[?1049h", 8);
    ASSERT_EQ(term_cursor_row_prompt_text(t, buf, sizeof(buf)), 0);
    term_free(t);
    TEST_END();
}

int test_tn_session_autologin_user_and_password(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("admin", "pw");
    RXT(s, t, "\xFF\xFB\x01Username: ", 0);
    ASSERT_TRUE(OUTQ_IS(s, "\xFF\xFD\x01"));
    tn_session_tick(s, t, NULL, 500);
    ASSERT_TRUE(OUTQ_IS(s, "admin\r\n"));
    RXT(s, t, "admin\r\nPassword: ", 600);
    tn_session_tick(s, t, NULL, 1000);
    ASSERT_TRUE(OUTQ_IS(s, "pw\r\n"));
    ASSERT_TRUE(strstr(screen(t), "Username: admin") != NULL);
    ASSERT_TRUE(strstr(screen(t), "pw") == NULL);
    ASSERT_EQ(tn_session_login_armed(s), 0);
    tn_session_free(s);
    term_free(t);
    TEST_END();
}

int test_tn_session_password_only_router(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("", "pw");
    RXT(s, t, "\r\nPassword: ", 0);
    tn_session_tick(s, t, NULL, 500);
    ASSERT_TRUE(OUTQ_IS(s, "pw\r\n"));
    tn_session_free(s);
    term_free(t);
    TEST_END();
}

int test_tn_session_no_login_device_never_gets_password_at_enable(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("admin", "pw");
    RXT(s, t, "Router>", 0);
    tn_session_tick(s, t, NULL, 500);
    ASSERT_TRUE(OUTQ_IS(s, ""));
    ASSERT_EQ(tn_session_user_write(s, "enable\r", 7), 7);
    RXT(s, t, "enable\r\nPassword: ", 600);
    tn_session_tick(s, t, NULL, 1000);
    ASSERT_TRUE(OUTQ_IS(s, "enable\r\n"));
    tn_session_free(s);
    term_free(t);
    TEST_END();
}

int test_tn_session_user_typing_switches_off(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("admin", "pw");
    ASSERT_EQ(tn_session_user_write(s, "a", 1), 1);
    RXT(s, t, "login: ", 0);
    tn_session_tick(s, t, NULL, 500);
    ASSERT_TRUE(OUTQ_IS(s, "a"));
    tn_session_free(s);
    term_free(t);
    TEST_END();
}

int test_tn_session_ask_and_answer(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("admin", "");
    RXT(s, t, "login: ", 0);
    tn_session_tick(s, t, NULL, 500);
    ASSERT_TRUE(OUTQ_IS(s, "admin\r\n"));
    RXT(s, t, "admin\r\nPassword: ", 600);
    tn_session_tick(s, t, NULL, 1000);
    ASSERT_EQ(tn_session_take_password_request(s), 1);
    ASSERT_EQ(tn_session_take_password_request(s), 0);
    ASSERT_EQ(tn_session_answer_password(s, t, "s3"), 1);
    ASSERT_TRUE(OUTQ_IS(s, "s3\r\n"));
    tn_session_tick(s, t, NULL, 1500);
    ASSERT_TRUE(strstr(screen(t), "s3") == NULL);
    tn_session_free(s);
    term_free(t);
    TEST_END();
}

int test_tn_session_ask_cancelled_sends_nothing(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("admin", "");
    RXT(s, t, "login: ", 0);
    tn_session_tick(s, t, NULL, 500);
    (void)outq_str(s);
    RXT(s, t, "admin\r\nPassword: ", 600);
    tn_session_tick(s, t, NULL, 1000);
    ASSERT_EQ(tn_session_take_password_request(s), 1);
    ASSERT_EQ(tn_session_answer_password(s, t, NULL), 0);
    ASSERT_TRUE(OUTQ_IS(s, ""));
    ASSERT_EQ(tn_session_login_armed(s), 0);
    tn_session_free(s);
    term_free(t);
    TEST_END();
}

int test_tn_session_port_check_never_logs_in(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("", "");
    RXT(s, t, "login: ", 0);
    tn_session_tick(s, t, NULL, 500);
    ASSERT_TRUE(OUTQ_IS(s, ""));
    tn_session_free(s);
    term_free(t);
    TEST_END();
}

int test_tn_session_local_echo_until_server_echoes(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("", "");
    tn_session_user_write(s, "HELO x\r", 7);
    tn_session_tick(s, t, NULL, 10);
    ASSERT_TRUE(strstr(screen(t), "HELO x") != NULL);
    (void)outq_str(s);
    RXT(s, t, "\xFF\xFB\x01", 20);
    ASSERT_TRUE(OUTQ_IS(s, "\xFF\xFD\x01"));
    tn_session_user_write(s, "abc", 3);
    tn_session_tick(s, t, NULL, 30);
    ASSERT_TRUE(strstr(screen(t), "abc") == NULL);
    (void)outq_str(s);
    RXT(s, t, "\xFF\xFC\x01", 40);
    (void)outq_str(s);
    tn_session_user_write(s, "z", 1);
    tn_session_tick(s, t, NULL, 50);
    ASSERT_TRUE(strstr(screen(t), "z") != NULL);
    tn_session_free(s);
    term_free(t);
    TEST_END();
}

int test_tn_session_local_echo_suppressed_at_password_prompt(void)
{
    TEST_BEGIN();
    char path[256];
    snprintf(path, sizeof(path), "%s/tn_session_log.txt", TEST_TMP_DIR);
    FILE *log = test_fopen_private(path);
    ASSERT_NOT_NULL(log);
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("", "");
    tn_session_rx(s, t, log, (const unsigned char *)"Password: ", 10, 0);
    tn_session_user_write(s, "hunter2\r", 8);
    tn_session_tick(s, t, log, 10);
    ASSERT_TRUE(strstr(screen(t), "hunter2") == NULL);
    ASSERT_TRUE(OUTQ_IS(s, "hunter2\r\n"));
    if (log) fclose(log);
    FILE *rd = fopen(path, "r");
    ASSERT_NOT_NULL(rd);
    if (rd) {
        char buf[512];
        size_t n = fread(buf, 1, sizeof(buf) - 1u, rd);
        buf[n] = '\0';
        ASSERT_TRUE(strstr(buf, "hunter2") == NULL);
        ASSERT_TRUE(strstr(buf, "Password:") != NULL);
        fclose(rd);
    }
    remove(path);
    tn_session_free(s);
    term_free(t);
    TEST_END();
}

int test_tn_session_secret_write_escapes_iac(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("", "a\xFF" "b");
    RXT(s, t, "Password: ", 0);
    tn_session_tick(s, t, NULL, 500);
    ASSERT_TRUE(OUTQ_IS(s, "a\xFF\xFF" "b\r\n"));
    tn_session_free(s);
    term_free(t);
    TEST_END();
}

int test_tn_session_outq_consume_wipes(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("", "secret1");
    RXT(s, t, "Password: ", 0);
    tn_session_tick(s, t, NULL, 500);
    size_t n = 0;
    const unsigned char *q = tn_session_outq(s, &n);
    ASSERT_EQ((int)n, 9);
    ASSERT_TRUE(memcmp(q, "secret1\r\n", 9) == 0);
    tn_session_outq_consume(s, n);
    int zero = 1;
    for (size_t i = 0; i < n; i++) if (q[i] != 0) zero = 0;
    ASSERT_TRUE(zero);
    size_t left = 1;
    (void)tn_session_outq(s, &left);
    ASSERT_EQ((int)left, 0);
    tn_session_free(s);
    term_free(t);
    TEST_END();
}

int test_tn_session_outq_cap(void)
{
    TEST_BEGIN();
    TnSession *s = mk("", "");
    char *big = (char *)malloc(TN_OUTQ_MAX);
    ASSERT_NOT_NULL(big);
    if (big) {
        memset(big, 'x', TN_OUTQ_MAX);
        ASSERT_EQ(tn_session_user_write(s, big, TN_OUTQ_MAX), -1);
        size_t n = 1;
        (void)tn_session_outq(s, &n);
        ASSERT_EQ((int)n, 0);
        free(big);
    }
    tn_session_free(s);
    TEST_END();
}

int test_tn_session_resize_sends_naws_when_agreed(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("", "");
    RXT(s, t, "\xFF\xFD\x1F", 0);
    ASSERT_TRUE(OUTQ_IS(s, "\xFF\xFB\x1F\xFF\xFA\x1F\x00\x50\x00\x18\xFF\xF0"));
    tn_session_resize(s, 132, 50);
    ASSERT_TRUE(OUTQ_IS(s, "\xFF\xFA\x1F\x00\x84\x00\x32\xFF\xF0"));
    tn_session_free(s);
    term_free(t);
    TEST_END();
}

static int echo_is(TnEchoState *st, const char *in, size_t n, const char *exp)
{
    unsigned char out[64];
    size_t got = tn_echo_filter(st, (const unsigned char *)in, n, out, sizeof(out));
    return got == strlen(exp) && memcmp(out, exp, got) == 0;
}

int test_tn_echo_filter_rules(void)
{
    TEST_BEGIN();
    TnEchoState st = {0};
    ASSERT_TRUE(echo_is(&st, "ab\r", 3, "ab\r\n"));
    ASSERT_TRUE(echo_is(&st, "a\x7F", 2, "a\b \b"));
    ASSERT_TRUE(echo_is(&st, "\r\n", 2, "\r\n"));
    ASSERT_TRUE(echo_is(&st, "\x1B[A", 3, ""));
    ASSERT_TRUE(echo_is(&st, "\x1B", 1, ""));
    ASSERT_TRUE(echo_is(&st, "[1;5C", 5, ""));
    ASSERT_TRUE(echo_is(&st, "\x1BOPq", 4, "q"));
    ASSERT_TRUE(echo_is(&st, "\x01x", 2, "x"));
    ASSERT_TRUE(echo_is(&st, "\xC3\xA9", 2, "\xC3\xA9"));
    TEST_END();
}

int test_tn_session_iac_split_across_rx(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("", "");
    RXT(s, t, "\xFF", 0);
    RXT(s, t, "\xFB\x01ok", 0);
    ASSERT_TRUE(strstr(screen(t), "ok") != NULL);
    ASSERT_TRUE(strstr(screen(t), "\xFF") == NULL);
    ASSERT_TRUE(OUTQ_IS(s, "\xFF\xFD\x01"));
    tn_session_free(s);
    term_free(t);
    TEST_END();
}

int test_tn_session_stale_box_answer_is_dropped(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("admin", "");
    RXT(s, t, "login: ", 0);
    tn_session_tick(s, t, NULL, 500);
    (void)outq_str(s);
    RXT(s, t, "admin\r\nPassword: ", 600);
    tn_session_tick(s, t, NULL, 1000);
    ASSERT_EQ(tn_session_take_password_request(s), 1);
    /* The device timed out while the box was open and asked for a login again. */
    RXT(s, t, "\r\nlogin: ", 1100);
    ASSERT_EQ(tn_session_answer_password(s, t, "hunter2"), 0);
    ASSERT_TRUE(OUTQ_IS(s, ""));
    ASSERT_TRUE(strstr(screen(t), "hunter2") == NULL);
    ASSERT_TRUE(strstr(screen(t), "Password not sent: the device is no longer asking for it") != NULL);
    ASSERT_EQ(tn_session_login_armed(s), 0);
    tn_session_free(s);
    term_free(t);
    TEST_END();
}

int test_tn_session_stale_box_answer_at_device_prompt_is_dropped(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("admin", "");
    RXT(s, t, "login: ", 0);
    tn_session_tick(s, t, NULL, 500);
    (void)outq_str(s);
    RXT(s, t, "admin\r\nPassword: ", 600);
    tn_session_tick(s, t, NULL, 1000);
    ASSERT_EQ(tn_session_take_password_request(s), 1);
    RXT(s, t, "\r\nrouter> ", 1100);
    ASSERT_EQ(tn_session_answer_password(s, t, "hunter2"), 0);
    ASSERT_TRUE(OUTQ_IS(s, ""));
    ASSERT_TRUE(strstr(screen(t), "hunter2") == NULL);
    tn_session_free(s);
    term_free(t);
    TEST_END();
}

int test_tn_session_password_prompt_still_shown_after_other_text(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("admin", "");
    RXT(s, t, "login: ", 0);
    tn_session_tick(s, t, NULL, 500);
    (void)outq_str(s);
    RXT(s, t, "admin\r\nPassword: ", 600);
    tn_session_tick(s, t, NULL, 1000);
    ASSERT_EQ(tn_session_take_password_request(s), 1);
    RXT(s, t, "\r\nPassword: ", 1100);   /* re-asked: still a password prompt */
    ASSERT_EQ(tn_session_answer_password(s, t, "s3"), 1);
    ASSERT_TRUE(OUTQ_IS(s, "s3\r\n"));
    tn_session_free(s);
    term_free(t);
    TEST_END();
}

int test_tn_session_echo_flushed_before_rx_hides_password(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("", "");
    RXT(s, t, "Password: ", 0);
    tn_session_user_write(s, "pw\r", 3);
    /* The transport's order: flush echo, receive, tick. */
    tn_session_flush_echo(s, t, NULL);
    RXT(s, t, "\r\nlogin: ", 10);
    tn_session_tick(s, t, NULL, 20);
    ASSERT_TRUE(strstr(screen(t), "pw") == NULL);
    ASSERT_TRUE(OUTQ_IS(s, "pw\r\n"));
    tn_session_free(s);
    term_free(t);
    TEST_END();
}

int test_tn_session_echo_precedes_server_reply(void)
{
    TEST_BEGIN();
    Terminal *t = term_init(24, 80, 100);
    TnSession *s = mk("", "");
    tn_session_user_write(s, "ls", 2);
    tn_session_flush_echo(s, t, NULL);
    RXT(s, t, "\r\nREPLY", 10);
    const char *scr = screen(t);
    const char *e = strstr(scr, "ls");
    const char *r = strstr(scr, "REPLY");
    ASSERT_TRUE(e != NULL && r != NULL && e < r);
    tn_session_free(s);
    term_free(t);
    TEST_END();
}
