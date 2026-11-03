#include "test_framework.h"
#include "profile_kind.h"
#include <stddef.h>

int test_profile_kind_of_maps_strings(void)
{
    TEST_BEGIN();
    ASSERT_EQ(profile_kind_of("ssh"), PROFILE_KIND_SSH);
    ASSERT_EQ(profile_kind_of(""), PROFILE_KIND_SSH);
    ASSERT_EQ(profile_kind_of(NULL), PROFILE_KIND_SSH);
    ASSERT_EQ(profile_kind_of("bogus"), PROFILE_KIND_SSH);
    ASSERT_EQ(profile_kind_of("local"), PROFILE_KIND_LOCAL);
    ASSERT_EQ(profile_kind_of("telnet"), PROFILE_KIND_TELNET);
    ASSERT_EQ(profile_kind_of("Telnet"), PROFILE_KIND_SSH);
    TEST_END();
}

int test_profile_kind_names_and_ports(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(profile_kind_name(PROFILE_KIND_SSH), "ssh");
    ASSERT_STR_EQ(profile_kind_name(PROFILE_KIND_LOCAL), "local");
    ASSERT_STR_EQ(profile_kind_name(PROFILE_KIND_TELNET), "telnet");
    ASSERT_EQ(profile_kind_default_port(PROFILE_KIND_SSH), 22);
    ASSERT_EQ(profile_kind_default_port(PROFILE_KIND_LOCAL), 0);
    ASSERT_EQ(profile_kind_default_port(PROFILE_KIND_TELNET), 23);
    ASSERT_EQ(profile_kind_session(PROFILE_KIND_SSH), SESSION_SSH);
    ASSERT_EQ(profile_kind_session(PROFILE_KIND_LOCAL), SESSION_LOCAL);
    ASSERT_EQ(profile_kind_session(PROFILE_KIND_TELNET), SESSION_TELNET);
    TEST_END();
}

int test_profile_kind_swap_port(void)
{
    TEST_BEGIN();
    ASSERT_EQ(profile_kind_swap_port(22, PROFILE_KIND_SSH, PROFILE_KIND_TELNET), 23);
    ASSERT_EQ(profile_kind_swap_port(23, PROFILE_KIND_TELNET, PROFILE_KIND_SSH), 22);
    ASSERT_EQ(profile_kind_swap_port(2323, PROFILE_KIND_SSH, PROFILE_KIND_TELNET), 2323);
    ASSERT_EQ(profile_kind_swap_port(22, PROFILE_KIND_SSH, PROFILE_KIND_SSH), 22);
    ASSERT_EQ(profile_kind_swap_port(23, PROFILE_KIND_TELNET, PROFILE_KIND_LOCAL), 23);
    TEST_END();
}

int test_session_kind_name_covers_telnet(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(session_kind_name(SESSION_SSH), "ssh");
    ASSERT_STR_EQ(session_kind_name(SESSION_LOCAL), "local");
    ASSERT_STR_EQ(session_kind_name(SESSION_TELNET), "telnet");
    ASSERT_STR_EQ(session_kind_name((SessionKind)99), "ssh");
    TEST_END();
}
