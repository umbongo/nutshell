#include "test_framework.h"
#include "profile.h"
#include <string.h>
#include <stdio.h>

int test_profile_struct(void) {
    TEST_BEGIN();
    Profile p;
    memset(&p, 0, sizeof(p));
    
    snprintf(p.name, sizeof(p.name), "Test Session");
    snprintf(p.host, sizeof(p.host), "127.0.0.1");
    p.port = 2222;
    p.auth_type = AUTH_KEY;
    
    ASSERT_EQ(strcmp(p.name, "Test Session"), 0);
    ASSERT_EQ(strcmp(p.host, "127.0.0.1"), 0);
    ASSERT_EQ(p.port, 2222);
    ASSERT_EQ((int)p.auth_type, (int)AUTH_KEY);
    
    TEST_END();
}