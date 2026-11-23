#include "test_framework.h"
#include "config.h"
#include "opt_feature.h"
#include <string.h>

int test_opt_feature_table_ai_first(void)
{
    TEST_BEGIN();
    size_t n = 0;
    const OptFeatureInfo *t = opt_feature_table(&n);
    ASSERT_NOT_NULL(t);
    ASSERT_EQ(n, (size_t)OPT_COUNT);
    ASSERT_NOT_NULL(opt_feature_table(NULL));
    if (t != NULL && n >= 1u) {
        ASSERT_EQ(t[0].id, OPT_AI);
        ASSERT_STR_EQ(t[0].json_key, "ai_enabled");
        ASSERT_NOT_NULL(t[0].name);
        ASSERT_EQ(t[0].requires, -1);
        for (size_t i = 0; i < n; i++) ASSERT_EQ((size_t)t[i].id, i);
    }
    TEST_END();
}

int test_opt_feature_enabled(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_EQ(opt_feature_enabled(&s, OPT_AI), 0);
    s.ai_enabled = 1;
    ASSERT_EQ(opt_feature_enabled(&s, OPT_AI), 1);
    ASSERT_EQ(opt_feature_enabled(NULL, OPT_AI), 0);
    ASSERT_EQ(opt_feature_enabled(&s, OPT_COUNT), 0);
    ASSERT_EQ(opt_feature_enabled(&s, (OptFeature)-1), 0);
    TEST_END();
}

int test_opt_feature_blocked_reason(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_STR_EQ(opt_feature_blocked_reason(&s, OPT_AI), "Needs the AI assistant");
    ASSERT_NULL(opt_feature_blocked_reason(&s, -1));
    s.ai_enabled = 1;
    ASSERT_NULL(opt_feature_blocked_reason(&s, OPT_AI));
    ASSERT_NULL(opt_feature_blocked_reason(&s, -1));
    ASSERT_STR_EQ(opt_feature_blocked_reason(NULL, OPT_AI), "Needs the AI assistant");
    TEST_END();
}
