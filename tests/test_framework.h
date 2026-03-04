#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>

extern int _tf_failed;
extern int _tf_run;

#define TEST_BEGIN() int _tf_local_fail = 0; printf("[RUN ] %s\n", __func__)

#define TEST_END() \
    do { \
        _tf_run++; \
        if (_tf_local_fail == 0) { \
            printf("[PASS] %s\n", __func__); \
            return 0; \
        } else { \
            printf("[FAIL] %s\n", __func__); \
            _tf_failed++; \
            return 1; \
        } \
    } while(0)

#define ASSERT_TRUE(cond) if (!(cond)) { printf("  Assertion failed: %s\n", #cond); _tf_local_fail = 1; }
#define ASSERT_FALSE(cond) if (cond) { printf("  Assertion failed: !(%s)\n", #cond); _tf_local_fail = 1; }
#define ASSERT_EQ(a, b) if ((a) != (b)) { printf("  Assertion failed: %s == %s (%lld != %lld)\n", #a, #b, (long long)(uintptr_t)(a), (long long)(uintptr_t)(b)); _tf_local_fail = 1; }
#define ASSERT_STR_EQ(a, b) if (strcmp(a, b) != 0) { printf("  Assertion failed: strcmp(%s, %s) == 0 (\"%s\" != \"%s\")\n", #a, #b, a, b); _tf_local_fail = 1; }
#define ASSERT_NULL(ptr) if ((ptr) != NULL) { printf("  Assertion failed: %s == NULL\n", #ptr); _tf_local_fail = 1; }
#define ASSERT_NOT_NULL(ptr) if ((ptr) == NULL) { printf("  Assertion failed: %s != NULL\n", #ptr); _tf_local_fail = 1; }

#endif