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
