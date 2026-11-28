#include "test_framework.h"
#include "settings_tab.h"

/* ---- settings_tab_order: visual order, skipping disabled and hidden ---- */

static SettingsTabItem item(int group, int x, int y, int enabled, int visible)
{
    SettingsTabItem it;
    it.group = group;
    it.x = x;
    it.y = y;
    it.enabled = enabled;
    it.visible = visible;
    return it;
}

int test_stab_order_top_to_bottom_then_left_to_right(void) {
    TEST_BEGIN();
    /* Created out of visual order on purpose. */
    SettingsTabItem it[4] = {
        item(STAB_GROUP_PAGE, 200, 60, 1, 1),   /* 0: row 2        */
        item(STAB_GROUP_PAGE, 300, 30, 1, 1),   /* 1: row 1, right */
        item(STAB_GROUP_PAGE, 200, 30, 1, 1),   /* 2: row 1, left  */
        item(STAB_GROUP_PAGE, 200, 90, 1, 1),   /* 3: row 3        */
    };
    int out[4];
    int n = settings_tab_order(it, 4, out);
    ASSERT_EQ(n, 4);
    ASSERT_EQ(out[0], 2);
    ASSERT_EQ(out[1], 1);
    ASSERT_EQ(out[2], 0);
    ASSERT_EQ(out[3], 3);
    TEST_END();
}

int test_stab_order_skips_disabled_and_hidden(void) {
    TEST_BEGIN();
    SettingsTabItem it[4] = {
        item(STAB_GROUP_PAGE, 0, 10, 1, 1),
        item(STAB_GROUP_PAGE, 0, 20, 0, 1),   /* disabled */
        item(STAB_GROUP_PAGE, 0, 30, 1, 0),   /* hidden   */
        item(STAB_GROUP_PAGE, 0, 40, 1, 1),
    };
    int out[4];
    int n = settings_tab_order(it, 4, out);
    ASSERT_EQ(n, 2);
    ASSERT_EQ(out[0], 0);
    ASSERT_EQ(out[1], 3);
    TEST_END();
}

int test_stab_order_groups_nav_page_buttons(void) {
    TEST_BEGIN();
    SettingsTabItem it[4] = {
        item(STAB_GROUP_BUTTONS, 500, 500, 1, 1),
        item(STAB_GROUP_PAGE, 200, 10, 1, 1),
        item(STAB_GROUP_NAV, 0, 400, 1, 1),   /* lower on screen, still first */
        item(STAB_GROUP_BUTTONS, 400, 500, 1, 1),
    };
    int out[4];
    int n = settings_tab_order(it, 4, out);
    ASSERT_EQ(n, 4);
    ASSERT_EQ(out[0], 2);
    ASSERT_EQ(out[1], 1);
    ASSERT_EQ(out[2], 3);
    ASSERT_EQ(out[3], 0);
    TEST_END();
}

int test_stab_order_ties_keep_creation_order(void) {
    TEST_BEGIN();
    SettingsTabItem it[3] = {
        item(STAB_GROUP_PAGE, 50, 50, 1, 1),
        item(STAB_GROUP_PAGE, 50, 50, 1, 1),
        item(STAB_GROUP_PAGE, 50, 50, 1, 1),
    };
    int out[3];
    ASSERT_EQ(settings_tab_order(it, 3, out), 3);
    ASSERT_EQ(out[0], 0);
    ASSERT_EQ(out[1], 1);
    ASSERT_EQ(out[2], 2);
    TEST_END();
}

int test_stab_order_degenerate_inputs(void) {
    TEST_BEGIN();
    int out[2];
    ASSERT_EQ(settings_tab_order(NULL, 3, out), 0);
    ASSERT_EQ(settings_tab_order(NULL, 0, out), 0);
    SettingsTabItem it[1] = { item(STAB_GROUP_PAGE, 0, 0, 1, 1) };
    ASSERT_EQ(settings_tab_order(it, 1, NULL), 0);
    ASSERT_EQ(settings_tab_order(it, 0, out), 0);
    ASSERT_EQ(settings_tab_order(it, -1, out), 0);
    SettingsTabItem none[1] = { item(STAB_GROUP_PAGE, 0, 0, 0, 1) };
    ASSERT_EQ(settings_tab_order(none, 1, out), 0);
    TEST_END();
}

/* ---- settings_tab_step ---- */

int test_stab_step_forward_backward_wrap(void) {
    TEST_BEGIN();
    int order[3] = { 4, 2, 7 };
    ASSERT_EQ(settings_tab_step(order, 3, 4, +1), 2);
    ASSERT_EQ(settings_tab_step(order, 3, 2, +1), 7);
    ASSERT_EQ(settings_tab_step(order, 3, 7, +1), 4);   /* wraps */
    ASSERT_EQ(settings_tab_step(order, 3, 7, -1), 2);
    ASSERT_EQ(settings_tab_step(order, 3, 4, -1), 7);   /* wraps */
    TEST_END();
}

int test_stab_step_unknown_current_enters_at_the_ends(void) {
    TEST_BEGIN();
    int order[3] = { 4, 2, 7 };
    ASSERT_EQ(settings_tab_step(order, 3, 99, +1), 4);
    ASSERT_EQ(settings_tab_step(order, 3, 99, -1), 7);
    ASSERT_EQ(settings_tab_step(order, 3, -1, +1), 4);
    TEST_END();
}

int test_stab_step_empty_and_single(void) {
    TEST_BEGIN();
    int order[1] = { 5 };
    ASSERT_EQ(settings_tab_step(order, 0, 5, +1), -1);
    ASSERT_EQ(settings_tab_step(NULL, 1, 5, +1), -1);
    ASSERT_EQ(settings_tab_step(order, 1, 5, +1), 5);
    ASSERT_EQ(settings_tab_step(order, 1, 5, -1), 5);
    TEST_END();
}
