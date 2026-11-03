#include "test_framework.h"
#include "telnet_status.h"
#include <string.h>

int test_tn_status_lines_exact(void)
{
    TEST_BEGIN();
    char b[160];
    size_t n;
    n = tn_status_line(TN_LINE_CONNECTING, "192.168.1.1", 23, b, sizeof b);
    ASSERT_STR_EQ(b, "Connecting to 192.168.1.1:23");
    ASSERT_EQ(n, strlen(b));
    n = tn_status_line(TN_LINE_CONNECTED, "192.168.1.1", 23, b, sizeof b);
    ASSERT_STR_EQ(b, "Connected to 192.168.1.1:23 (telnet \xE2\x80\x94 not encrypted)");
    ASSERT_EQ(n, strlen(b));
    tn_status_line(TN_LINE_REFUSED, "192.168.1.1", 23, b, sizeof b);
    ASSERT_STR_EQ(b, "Connection refused by 192.168.1.1:23");
    tn_status_line(TN_LINE_TIMEOUT, "192.168.1.1", 23, b, sizeof b);
    ASSERT_STR_EQ(b, "Connection to 192.168.1.1:23 timed out");
    tn_status_line(TN_LINE_RESOLVE, "192.168.1.1", 23, b, sizeof b);
    ASSERT_STR_EQ(b, "Could not resolve host 192.168.1.1");
    tn_status_line(TN_LINE_OTHER, "192.168.1.1", 23, b, sizeof b);
    ASSERT_STR_EQ(b, "Could not connect to 192.168.1.1:23");
    tn_status_line(TN_LINE_CLOSED, "192.168.1.1", 23, b, sizeof b);
    ASSERT_STR_EQ(b, "Connection closed by 192.168.1.1:23");
    TEST_END();
}

int test_tn_status_ipv6_bracketed(void)
{
    TEST_BEGIN();
    char b[160];
    tn_status_line(TN_LINE_REFUSED, "::1", 2323, b, sizeof b);
    ASSERT_STR_EQ(b, "Connection refused by [::1]:2323");
    tn_status_line(TN_LINE_RESOLVE, "::1", 2323, b, sizeof b);
    ASSERT_STR_EQ(b, "Could not resolve host ::1");
    TEST_END();
}

int test_tn_status_truncates_safely(void)
{
    TEST_BEGIN();
    char b[16];
    size_t n = tn_status_line(TN_LINE_CONNECTING, "192.168.1.1", 23, b, 8);
    ASSERT_EQ(n, 7);
    ASSERT_EQ(b[7], 0);
    ASSERT_STR_EQ(b, "Connect");

    memset(b, 'x', sizeof b);
    n = tn_status_line(TN_LINE_CONNECTING, "h", 23, b, 0);
    ASSERT_EQ(n, 0);
    ASSERT_EQ(b[0], 'x');

    n = tn_status_line(TN_LINE_CONNECTING, "h", 23, b, 1);
    ASSERT_EQ(n, 0);
    ASSERT_EQ(b[0], 0);

    char big[160];
    tn_status_line(TN_LINE_REFUSED, NULL, 23, big, sizeof big);
    ASSERT_STR_EQ(big, "Connection refused by ?:23");

    /* a cut never leaves half of the em dash */
    char c[64];
    const char *full = "Connected to h:23 (telnet \xE2\x80\x94 not encrypted)";
    size_t dash = strlen("Connected to h:23 (telnet ");
    ASSERT_EQ(tn_status_line(TN_LINE_CONNECTED, "h", 23, c, dash + 2), dash);
    ASSERT_EQ(strncmp(c, full, dash), 0);
    ASSERT_EQ(tn_status_line(TN_LINE_CONNECTED, "h", 23, c, dash + 4), dash + 3);
    TEST_END();
}

int test_tn_conn_merge_precedence(void)
{
    TEST_BEGIN();
    ASSERT_EQ(tn_conn_merge(TN_CONN_REFUSED, TN_CONN_TIMEOUT), TN_CONN_REFUSED);
    ASSERT_EQ(tn_conn_merge(TN_CONN_TIMEOUT, TN_CONN_REFUSED), TN_CONN_REFUSED);
    ASSERT_EQ(tn_conn_merge(TN_CONN_TIMEOUT, TN_CONN_OTHER), TN_CONN_TIMEOUT);
    ASSERT_EQ(tn_conn_merge(TN_CONN_OTHER, TN_CONN_TIMEOUT), TN_CONN_TIMEOUT);
    ASSERT_EQ(tn_conn_merge(TN_CONN_OTHER, TN_CONN_RESOLVE), TN_CONN_OTHER);
    ASSERT_EQ(tn_conn_merge(TN_CONN_RESOLVE, TN_CONN_OTHER), TN_CONN_OTHER);
    ASSERT_EQ(tn_conn_merge(TN_CONN_RESOLVE, TN_CONN_RESOLVE), TN_CONN_RESOLVE);
    for (int o = TN_CONN_OK; o <= TN_CONN_RESOLVE; o++) {
        ASSERT_EQ(tn_conn_merge(TN_CONN_OK, (TnConnOutcome)o), TN_CONN_OK);
        ASSERT_EQ(tn_conn_merge((TnConnOutcome)o, TN_CONN_OK), TN_CONN_OK);
    }
    TEST_END();
}

int test_tn_line_for_outcome_maps_all(void)
{
    TEST_BEGIN();
    ASSERT_EQ(tn_line_for_outcome(TN_CONN_OK), TN_LINE_CONNECTED);
    ASSERT_EQ(tn_line_for_outcome(TN_CONN_REFUSED), TN_LINE_REFUSED);
    ASSERT_EQ(tn_line_for_outcome(TN_CONN_TIMEOUT), TN_LINE_TIMEOUT);
    ASSERT_EQ(tn_line_for_outcome(TN_CONN_OTHER), TN_LINE_OTHER);
    ASSERT_EQ(tn_line_for_outcome(TN_CONN_RESOLVE), TN_LINE_RESOLVE);
    TEST_END();
}
