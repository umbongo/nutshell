#include "test_framework.h"
#include "tab_manager.h"
#include <string.h>

/* ---- tabmgr_init --------------------------------------------------------- */

int test_tabmgr_init(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    ASSERT_EQ(tabmgr_count(&m), 0);
    ASSERT_EQ(tabmgr_get_active(&m), -1);
    TEST_END();
}

/* ---- tabmgr_add: first tab becomes active -------------------------------- */

int test_tabmgr_add_first(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    int idx = tabmgr_add(&m, "Tab 1", (void *)1);
    ASSERT_EQ(idx, 0);
    ASSERT_EQ(tabmgr_count(&m), 1);
    ASSERT_EQ(tabmgr_get_active(&m), 0);
    TEST_END();
}

/* ---- tabmgr_add: multiple tabs each get unique id ----------------------- */

int test_tabmgr_add_multiple(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "A", (void *)1);
    tabmgr_add(&m, "B", (void *)2);
    tabmgr_add(&m, "C", (void *)3);
    ASSERT_EQ(tabmgr_count(&m), 3);
    int id0 = tabmgr_get_id(&m, 0);
    int id1 = tabmgr_get_id(&m, 1);
    int id2 = tabmgr_get_id(&m, 2);
    ASSERT_TRUE(id0 != id1);
    ASSERT_TRUE(id1 != id2);
    ASSERT_TRUE(id0 != id2);
    TEST_END();
}

/* ---- tabmgr_set_active: switch to tab 1 of 3 ---------------------------- */

int test_tabmgr_switch(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    void *d0 = (void *)10, *d1 = (void *)20, *d2 = (void *)30;
    tabmgr_add(&m, "A", d0);
    tabmgr_add(&m, "B", d1);
    tabmgr_add(&m, "C", d2);
    tabmgr_set_active(&m, 1);
    ASSERT_EQ(tabmgr_get_active(&m), 1);
    ASSERT_EQ(tabmgr_get_user_data(&m, 1), d1);
    TEST_END();
}

/* ---- tabmgr_remove: close middle tab; remaining data intact ------------- */

int test_tabmgr_remove_middle(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    void *d0 = (void *)10, *d1 = (void *)20, *d2 = (void *)30;
    tabmgr_add(&m, "A", d0);
    tabmgr_add(&m, "B", d1);
    tabmgr_add(&m, "C", d2);
    tabmgr_remove(&m, 1);
    ASSERT_EQ(tabmgr_count(&m), 2);
    ASSERT_EQ(tabmgr_get_user_data(&m, 0), d0);
    ASSERT_EQ(tabmgr_get_user_data(&m, 1), d2);
    TEST_END();
}

/* ---- tabmgr_remove: close only tab → count 0, active -1 ---------------- */

int test_tabmgr_remove_last_tab(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "A", (void *)1);
    tabmgr_remove(&m, 0);
    ASSERT_EQ(tabmgr_count(&m), 0);
    ASSERT_EQ(tabmgr_get_active(&m), -1);
    TEST_END();
}

/* ---- tabmgr_set_active: invalid index leaves state unchanged ------------ */

int test_tabmgr_switch_invalid(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "A", (void *)1);
    tabmgr_add(&m, "B", (void *)2);
    tabmgr_add(&m, "C", (void *)3);
    tabmgr_set_active(&m, 0);
    tabmgr_set_active(&m, 99); /* invalid — must be ignored */
    ASSERT_EQ(tabmgr_get_active(&m), 0);
    TEST_END();
}

/* ---- tabmgr_remove: invalid index must not crash, count unchanged ------- */

int test_tabmgr_remove_invalid(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "A", (void *)1);
    tabmgr_remove(&m, 99);
    ASSERT_EQ(tabmgr_count(&m), 1);
    TEST_END();
}

/* ---- tabmgr_get_active / get_user_data with no tabs --------------------- */

int test_tabmgr_active_no_tabs(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    ASSERT_EQ(tabmgr_get_active(&m), -1);
    ASSERT_EQ(tabmgr_get_user_data(&m, 0), NULL);
    TEST_END();
}

/* ---- tabmgr_add: 33rd tab at TABS_MAX returns -1 ------------------------ */

int test_tabmgr_max_capacity(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    for (int i = 0; i < TABS_MAX; i++) {
        int idx = tabmgr_add(&m, "T", NULL);
        ASSERT_EQ(idx, i);
    }
    ASSERT_EQ(tabmgr_count(&m), TABS_MAX);
    int over = tabmgr_add(&m, "Over", NULL);
    ASSERT_EQ(over, -1);
    ASSERT_EQ(tabmgr_count(&m), TABS_MAX);
    TEST_END();
}

/* ---- tabmgr_remove: closing the active tab moves active to previous ----- */

int test_tabmgr_close_active_tab(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "A", (void *)1);
    tabmgr_add(&m, "B", (void *)2);
    tabmgr_add(&m, "C", (void *)3);
    tabmgr_set_active(&m, 1);
    tabmgr_remove(&m, 1);
    ASSERT_EQ(tabmgr_count(&m), 2);
    ASSERT_EQ(tabmgr_get_active(&m), 0); /* moved to previous */
    TEST_END();
}

/* ---- New tab after close gets a fresh id (no collision) ----------------- */

int test_tabmgr_reopen_no_id_collision(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "A", (void *)1);
    tabmgr_add(&m, "B", (void *)2);
    tabmgr_add(&m, "C", (void *)3);
    int old_id = tabmgr_get_id(&m, 1);
    tabmgr_remove(&m, 1);
    /* After remove(1): [A, C] → add D → [A, C, D] */
    tabmgr_add(&m, "D", (void *)4);
    ASSERT_EQ(tabmgr_count(&m), 3);
    int new_id = tabmgr_get_id(&m, 2);
    ASSERT_TRUE(new_id != old_id);
    TEST_END();
}

/* ---- tabmgr_set_status: independent per tab ----------------------------- */

int test_tabmgr_status_independence(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "A", (void *)1);
    tabmgr_add(&m, "B", (void *)2);
    tabmgr_set_status(&m, 0, TAB_CONNECTED);
    ASSERT_EQ(tabmgr_get_status(&m, 0), TAB_CONNECTED);
    ASSERT_EQ(tabmgr_get_status(&m, 1), TAB_IDLE);
    TEST_END();
}

/* ---- tabmgr_find: lookup by user_data pointer --------------------------- */

int test_tabmgr_find(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    void *d0 = (void *)100, *d1 = (void *)200, *d2 = (void *)300;
    tabmgr_add(&m, "A", d0);
    tabmgr_add(&m, "B", d1);
    tabmgr_add(&m, "C", d2);
    ASSERT_EQ(tabmgr_find(&m, d1), 1);
    ASSERT_EQ(tabmgr_find(&m, d2), 2);
    ASSERT_EQ(tabmgr_find(&m, (void *)999), -1);
    TEST_END();
}
