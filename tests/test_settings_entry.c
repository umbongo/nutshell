#include "test_framework.h"
#include "search_providers.h"
#include "settings_entry.h"

int test_search_providers_table(void) {
    TEST_BEGIN();
    ASSERT_EQ(search_provider_count(), 4);
    ASSERT_STR_EQ(search_provider_label(1), "DuckDuckGo (API)");
    ASSERT_STR_EQ(search_provider_value(2), "duckduckgo-html");
    ASSERT_NULL(search_provider_label(4)); ASSERT_NULL(search_provider_value(-1));
    for (int i = 0; i < 4; i++) ASSERT_EQ(search_provider_index(search_provider_value(i)), i);
    TEST_END();
}

int test_search_provider_index_case_and_unknown(void) {
    TEST_BEGIN();
    ASSERT_EQ(search_provider_index("DuckDuckGo-API"), 1);
    ASSERT_EQ(search_provider_index("CUSTOM"), 3);
    ASSERT_EQ(search_provider_index("bing"), -1);
    ASSERT_EQ(search_provider_index(""), -1);
    ASSERT_EQ(search_provider_index(NULL), -1);
    TEST_END();
}

int test_settings_entry_or_keep_range(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_entry_or_keep(1, 1u, 1, 20, 0, 7), 1);
    ASSERT_EQ(settings_entry_or_keep(1, 20u, 1, 20, 0, 7), 20);
    ASSERT_EQ(settings_entry_or_keep(1, 9u, 1, 20, 0, 7), 9);
    ASSERT_EQ(settings_entry_or_keep(1, 0u, 1, 20, 0, 7), 7);
    ASSERT_EQ(settings_entry_or_keep(1, 21u, 1, 20, 0, 7), 7);
    ASSERT_EQ(settings_entry_or_keep(0, 9u, 1, 20, 0, 7), 7);
    TEST_END();
}

int test_settings_entry_or_keep_zero_ok(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_entry_or_keep(1, 0u, 15, 3600, 1, 60), 0);
    ASSERT_EQ(settings_entry_or_keep(1, 1u, 15, 3600, 1, 60), 60);
    ASSERT_EQ(settings_entry_or_keep(1, 3601u, 15, 3600, 1, 60), 60);
    TEST_END();
}

int test_settings_entry_or_keep_huge_unsigned(void) {
    TEST_BEGIN();
    ASSERT_EQ(settings_entry_or_keep(1, 3000000000u, 15, 3600, 1, 60), 60);
    ASSERT_EQ(settings_entry_or_keep(1, 4294967295u, 0, 10080, 1, 5), 5);
    TEST_END();
}

int test_settings_scrollback_for_new_tab(void) {
    TEST_BEGIN();
    Settings s; config_default_settings(&s);
    ASSERT_EQ(settings_scrollback_for_new_tab(NULL), 10000);
    ASSERT_EQ(settings_scrollback_for_new_tab(&s), 10000);
    int in[]  = { 99, 100, 50000, 50001, -5 };
    int out[] = { 100, 100, 50000, 50000, 100 };
    for (int i = 0; i < 5; i++) { s.scrollback_lines = in[i];
        ASSERT_EQ(settings_scrollback_for_new_tab(&s), out[i]); }
    TEST_END();
}

int test_settings_scrollback_agrees_with_validate(void) {
    TEST_BEGIN();
    int in[] = { 99, 50001 };
    for (int i = 0; i < 2; i++) { Settings s; config_default_settings(&s);
        s.scrollback_lines = in[i]; int want = settings_scrollback_for_new_tab(&s);
        settings_validate(&s); ASSERT_EQ(s.scrollback_lines, want); }
    TEST_END();
}
