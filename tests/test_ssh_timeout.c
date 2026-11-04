#include "test_framework.h"
#include "ssh_timeout.h"

/* ---- Network-failure rail ---- */

int test_ssh_network_just_connected(void)
{
    TEST_BEGIN();
    /* now == last → 0 ms elapsed, well under threshold */
    ASSERT_FALSE(ssh_network_should_timeout(1000u, 1000u, 90000u));
    TEST_END();
}

int test_ssh_network_under_threshold(void)
{
    TEST_BEGIN();
    /* 89 s elapsed, 90 s threshold */
    ASSERT_FALSE(ssh_network_should_timeout(89000u, 0u, 90000u));
    TEST_END();
}

int test_ssh_network_over_threshold(void)
{
    TEST_BEGIN();
    /* 91 s elapsed, 90 s threshold */
    ASSERT_TRUE(ssh_network_should_timeout(91000u, 0u, 90000u));
    TEST_END();
}

int test_ssh_network_wraparound_under(void)
{
    TEST_BEGIN();
    /* last just before rollover, now just after → ~5 s elapsed */
    uint32_t last = 0xFFFFFF00u;
    uint32_t now  = 0x00001000u; /* (uint32_t)(now - last) == 4352 ms */
    ASSERT_FALSE(ssh_network_should_timeout(now, last, 90000u));
    TEST_END();
}

int test_ssh_network_wraparound_over(void)
{
    TEST_BEGIN();
    /* last well before rollover, now well after → ~120 s elapsed */
    uint32_t last = 0xFFFE0000u;
    uint32_t now  = 0x00020000u; /* delta ≈ 0x00040000 = 262 144 ms */
    ASSERT_TRUE(ssh_network_should_timeout(now, last, 90000u));
    TEST_END();
}

/* ---- User-idle rail ---- */

int test_ssh_idle_disabled_zero(void)
{
    TEST_BEGIN();
    ASSERT_FALSE(ssh_idle_should_timeout(0xFFFFFFFFu, 0u, 0));
    TEST_END();
}

int test_ssh_idle_disabled_negative(void)
{
    TEST_BEGIN();
    ASSERT_FALSE(ssh_idle_should_timeout(0xFFFFFFFFu, 0u, -5));
    TEST_END();
}

int test_ssh_idle_under_threshold(void)
{
    TEST_BEGIN();
    /* 179 minutes elapsed, threshold 180 */
    uint32_t now  = 179u * 60u * 1000u;
    uint32_t last = 0u;
    ASSERT_FALSE(ssh_idle_should_timeout(now, last, 180));
    TEST_END();
}

int test_ssh_idle_over_threshold(void)
{
    TEST_BEGIN();
    /* 181 minutes elapsed, threshold 180 */
    uint32_t now  = 181u * 60u * 1000u;
    uint32_t last = 0u;
    ASSERT_TRUE(ssh_idle_should_timeout(now, last, 180));
    TEST_END();
}

int test_ssh_idle_one_minute_under(void)
{
    TEST_BEGIN();
    /* 30 s elapsed, threshold 1 minute */
    ASSERT_FALSE(ssh_idle_should_timeout(30000u, 0u, 1));
    TEST_END();
}

int test_ssh_idle_one_minute_over(void)
{
    TEST_BEGIN();
    /* 70 s elapsed, threshold 1 minute */
    ASSERT_TRUE(ssh_idle_should_timeout(70000u, 0u, 1));
    TEST_END();
}

int test_ssh_network_at_threshold_exact(void)
{
    TEST_BEGIN();
    /* Exactly 90s elapsed, 90s threshold — strict > means false */
    ASSERT_FALSE(ssh_network_should_timeout(90000u, 0u, 90000u));
    TEST_END();
}

int test_ssh_idle_wraparound_over(void)
{
    TEST_BEGIN();
    /* last well before rollover, now well after → ~262 s elapsed,
     * threshold 1 minute → fires across the wrap. */
    uint32_t last = 0xFFFE0000u;
    uint32_t now  = 0x00020000u;
    ASSERT_TRUE(ssh_idle_should_timeout(now, last, 1));
    TEST_END();
}

int test_ssh_idle_huge_timeout_clamps(void)
{
    TEST_BEGIN();
    /* 100 000 minutes * 60 000 = 6e9 > 2^32 → clamped to 0xFFFFFFFF.
     * With now < clamped threshold, comparison is false. */
    ASSERT_FALSE(ssh_idle_should_timeout(0x80000000u, 0u, 100000));
    /* And for completeness: a delta that does exceed the clamp value
     * is impossible in 32-bit space — so the clamp behaves as
     * "effectively never times out within one tick window". */
    TEST_END();
}

/* ---- Connection-setup timeout wording ---- */

int test_ssh_setup_timeout_text_stages(void)
{
    TEST_BEGIN();
    char buf[256];
    size_t n = ssh_setup_timeout_text(SSH_STAGE_HANDSHAKE, "example.com", 22,
                                      15000, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Timed out after 15 s waiting for example.com:22 "
                       "during the SSH handshake.");
    ASSERT_EQ((int)n, (int)strlen(buf));

    n = ssh_setup_timeout_text(SSH_STAGE_AUTH, "example.com", 22,
                               15000, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Timed out after 15 s waiting for example.com:22 "
                       "during authentication.");
    ASSERT_EQ((int)n, (int)strlen(buf));

    n = ssh_setup_timeout_text(SSH_STAGE_SESSION, "example.com", 22,
                               15000, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "Timed out after 15 s waiting for example.com:22 "
                       "while opening the session.");
    ASSERT_EQ((int)n, (int)strlen(buf));
    TEST_END();
}

int test_ssh_setup_timeout_text_rounds_up(void)
{
    TEST_BEGIN();
    char buf[256];
    ASSERT_TRUE(ssh_setup_timeout_text(SSH_STAGE_HANDSHAKE, "h", 22, 300,
                                       buf, sizeof(buf)) > 0);
    ASSERT_TRUE(strstr(buf, "after 1 s") != NULL);
    ASSERT_TRUE(ssh_setup_timeout_text(SSH_STAGE_HANDSHAKE, "h", 22, 1000,
                                       buf, sizeof(buf)) > 0);
    ASSERT_TRUE(strstr(buf, "after 1 s") != NULL);
    ASSERT_TRUE(ssh_setup_timeout_text(SSH_STAGE_HANDSHAKE, "h", 22, 1001,
                                       buf, sizeof(buf)) > 0);
    ASSERT_TRUE(strstr(buf, "after 2 s") != NULL);
    TEST_END();
}

int test_ssh_setup_timeout_text_ipv6_host(void)
{
    TEST_BEGIN();
    char buf[256];
    ASSERT_TRUE(ssh_setup_timeout_text(SSH_STAGE_AUTH, "::1", 2222, 15000,
                                       buf, sizeof(buf)) > 0);
    ASSERT_TRUE(strstr(buf, "waiting for [::1]:2222 ") != NULL);
    /* A host that is already bracketed is not wrapped twice. */
    ASSERT_TRUE(ssh_setup_timeout_text(SSH_STAGE_AUTH, "[::1]", 2222, 15000,
                                       buf, sizeof(buf)) > 0);
    ASSERT_TRUE(strstr(buf, "waiting for [::1]:2222 ") != NULL);
    TEST_END();
}

int test_ssh_setup_timeout_text_small_buf(void)
{
    TEST_BEGIN();
    char buf[10];
    memset(buf, 'x', sizeof(buf));
    ASSERT_EQ((int)ssh_setup_timeout_text(SSH_STAGE_HANDSHAKE, "example.com",
                                          22, 15000, buf, sizeof(buf)), 0);
    ASSERT_EQ((int)buf[sizeof(buf) - 1], 0);
    ASSERT_TRUE(strlen(buf) < sizeof(buf));
    /* cap 0 and a NULL buffer are refused without writing. */
    ASSERT_EQ((int)ssh_setup_timeout_text(SSH_STAGE_AUTH, "h", 22, 1000,
                                          buf, 0), 0);
    ASSERT_EQ((int)ssh_setup_timeout_text(SSH_STAGE_AUTH, "h", 22, 1000,
                                          NULL, 64), 0);
    TEST_END();
}
