/* tests/test_winhttp_tls.c -- the TLS 1.2 fail-closed decision of
 * ns_winhttp_open(), tested through a fake backend (Wine never refuses the
 * option, so the real call cannot show it). */

#include "test_framework.h"
#include "winhttp_open.h"
#include <string.h>

typedef struct {
    int accept_both;     /* accept 1.2 | 1.3 */
    int accept_12;       /* accept 1.2 alone */
    int calls;
    unsigned long seen[4];
    int closes;
    void *closed;
} FakeTls;

static int fake_set(void *session, unsigned long flags, void *user)
{
    FakeTls *f = (FakeTls *)user;
    (void)session;
    if (f->calls < 4) f->seen[f->calls] = flags;
    f->calls++;
    if (flags == (NS_TLS_FLAG_TLS1_2 | NS_TLS_FLAG_TLS1_3)) return f->accept_both;
    if (flags == NS_TLS_FLAG_TLS1_2) return f->accept_12;
    return 1;   /* any other set would be a weaker-protocol path: caught by seen[] */
}

static void fake_close(void *session, void *user)
{
    FakeTls *f = (FakeTls *)user;
    f->closes++;
    f->closed = session;
}

static NsTlsOps ops_for(FakeTls *f)
{
    NsTlsOps o = { fake_set, fake_close, f };
    return o;
}

int test_tls_enforce_accepts_1_2_and_1_3(void)
{
    TEST_BEGIN();
    FakeTls f; memset(&f, 0, sizeof(f)); f.accept_both = 1;
    NsTlsOps o = ops_for(&f);
    int s = 0;
    ASSERT_EQ(ns_tls_enforce(&s, &o), 1);
    ASSERT_EQ(f.calls, 1);
    ASSERT_EQ(f.closes, 0);
    TEST_END();
}

int test_tls_enforce_falls_back_to_1_2_alone(void)
{
    TEST_BEGIN();
    FakeTls f; memset(&f, 0, sizeof(f)); f.accept_12 = 1;
    NsTlsOps o = ops_for(&f);
    int s = 0;
    ASSERT_EQ(ns_tls_enforce(&s, &o), 1);
    ASSERT_EQ(f.calls, 2);
    ASSERT_TRUE(f.seen[1] == NS_TLS_FLAG_TLS1_2);
    ASSERT_EQ(f.closes, 0);
    TEST_END();
}

int test_tls_enforce_fails_closed_and_closes_once(void)
{
    TEST_BEGIN();
    FakeTls f; memset(&f, 0, sizeof(f));   /* refuses everything */
    NsTlsOps o = ops_for(&f);
    int s = 0;
    ASSERT_EQ(ns_tls_enforce(&s, &o), 0);
    ASSERT_EQ(f.calls, 2);                 /* only 1.2|1.3, then 1.2: nothing weaker */
    ASSERT_TRUE(f.seen[0] == (NS_TLS_FLAG_TLS1_2 | NS_TLS_FLAG_TLS1_3));
    ASSERT_TRUE(f.seen[1] == NS_TLS_FLAG_TLS1_2);
    ASSERT_EQ(f.closes, 1);
    ASSERT_TRUE(f.closed == (void *)&s);
    TEST_END();
}

int test_tls_enforce_rejects_bad_arguments_without_closing(void)
{
    TEST_BEGIN();
    FakeTls f; memset(&f, 0, sizeof(f));
    NsTlsOps o = ops_for(&f);
    int s = 0;
    ASSERT_EQ(ns_tls_enforce(NULL, &o), 0);
    ASSERT_EQ(ns_tls_enforce(&s, NULL), 0);
    ASSERT_EQ(f.closes, 0);
    TEST_END();
}

int test_winhttp_failure_text_says_why(void)
{
    TEST_BEGIN();
    char buf[200];
    ns_winhttp_failure_text(NS_ERR_TLS_UNAVAILABLE, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "secure connection could not be set up") != NULL);
    ASSERT_TRUE(strstr(buf, "TLS 1.2") != NULL);
    ns_winhttp_failure_text(5, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "WinHttpOpen failed: 5");
    /* A short buffer is cut, never overrun. */
    char tiny[8];
    ns_winhttp_failure_text(NS_ERR_TLS_UNAVAILABLE, tiny, sizeof(tiny));
    ASSERT_EQ((int)strlen(tiny), 7);
    TEST_END();
}
