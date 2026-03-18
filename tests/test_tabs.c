#include "test_framework.h"
#include "tab_manager.h"
#include <string.h>

/* Convenience wrapper: call tabs_btn_tooltip_at with 96 DPI (no scaling) */
#define tabs_btn_tooltip_at_96(mx, cw) tabs_btn_tooltip_at((mx), (cw), 96)

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

/* =========================================================================
 * tabmgr_navigate — positive tests
 * ========================================================================= */

/* Navigate right from first → second. */
int test_tabmgr_navigate_right(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "A", (void *)1);
    tabmgr_add(&m, "B", (void *)2);
    tabmgr_add(&m, "C", (void *)3);
    tabmgr_set_active(&m, 0);
    ASSERT_EQ(tabmgr_navigate(&m, 1), 1);
    ASSERT_EQ(tabmgr_get_active(&m), 1);
    TEST_END();
}

/* Navigate left from second → first. */
int test_tabmgr_navigate_left(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "A", (void *)1);
    tabmgr_add(&m, "B", (void *)2);
    tabmgr_add(&m, "C", (void *)3);
    tabmgr_set_active(&m, 1);
    ASSERT_EQ(tabmgr_navigate(&m, -1), 0);
    ASSERT_EQ(tabmgr_get_active(&m), 0);
    TEST_END();
}

/* Wrap right: last → first. */
int test_tabmgr_navigate_wrap_right(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "A", (void *)1);
    tabmgr_add(&m, "B", (void *)2);
    tabmgr_add(&m, "C", (void *)3);
    tabmgr_set_active(&m, 2);
    ASSERT_EQ(tabmgr_navigate(&m, 1), 0);
    ASSERT_EQ(tabmgr_get_active(&m), 0);
    TEST_END();
}

/* Wrap left: first → last. */
int test_tabmgr_navigate_wrap_left(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "A", (void *)1);
    tabmgr_add(&m, "B", (void *)2);
    tabmgr_add(&m, "C", (void *)3);
    tabmgr_set_active(&m, 0);
    ASSERT_EQ(tabmgr_navigate(&m, -1), 2);
    ASSERT_EQ(tabmgr_get_active(&m), 2);
    TEST_END();
}

/* Single tab stays put. */
int test_tabmgr_navigate_single_tab(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "Only", (void *)1);
    ASSERT_EQ(tabmgr_navigate(&m, 1), 0);
    ASSERT_EQ(tabmgr_navigate(&m, -1), 0);
    TEST_END();
}

/* =========================================================================
 * tabmgr_navigate — negative / edge-case tests
 * ========================================================================= */

/* No tabs → returns -1. */
int test_tabmgr_navigate_no_tabs(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    ASSERT_EQ(tabmgr_navigate(&m, 1), -1);
    ASSERT_EQ(tabmgr_navigate(&m, -1), -1);
    TEST_END();
}

/* delta=0 → no change, returns current active. */
int test_tabmgr_navigate_zero_delta(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "A", (void *)1);
    tabmgr_add(&m, "B", (void *)2);
    tabmgr_set_active(&m, 1);
    ASSERT_EQ(tabmgr_navigate(&m, 0), 1);
    ASSERT_EQ(tabmgr_get_active(&m), 1);
    TEST_END();
}

/* =========================================================================
 * tabs_btn_tooltip_at — button tooltip text for hit-test positions
 * ========================================================================= */

/* [+] button occupies x=[4, 28). Clicking in that range → "Session Manager" */
int test_tabs_btn_tooltip_add(void)
{
    TEST_BEGIN();
    const char *tip = tabs_btn_tooltip_at_96(10, 400);
    ASSERT_NOT_NULL(tip);
    ASSERT_STR_EQ(tip, "Session Manager");
    /* Left edge */
    tip = tabs_btn_tooltip_at_96(4, 400);
    ASSERT_NOT_NULL(tip);
    ASSERT_STR_EQ(tip, "Session Manager");
    TEST_END();
}

/* Right-side buttons: with client_width=400:
 *   aiX=372, rightX=346, leftX=320 */
int test_tabs_btn_tooltip_ai(void)
{
    TEST_BEGIN();
    const char *tip = tabs_btn_tooltip_at_96(380, 400);
    ASSERT_NOT_NULL(tip);
    /* Tooltip is now multiline; check it starts with "AI Assist" */
    ASSERT_EQ(strncmp(tip, "AI Assist", 9), 0);
    TEST_END();
}

int test_tabs_btn_tooltip_prev(void)
{
    TEST_BEGIN();
    const char *tip = tabs_btn_tooltip_at_96(325, 400);
    ASSERT_NOT_NULL(tip);
    ASSERT_STR_EQ(tip, "Previous tab");
    TEST_END();
}

int test_tabs_btn_tooltip_next(void)
{
    TEST_BEGIN();
    const char *tip = tabs_btn_tooltip_at_96(350, 400);
    ASSERT_NOT_NULL(tip);
    ASSERT_STR_EQ(tip, "Next tab");
    TEST_END();
}

/* Position between buttons → NULL */
int test_tabs_btn_tooltip_gap(void)
{
    TEST_BEGIN();
    /* Between [+] and tabs area, but past [+] button */
    const char *tip = tabs_btn_tooltip_at_96(200, 400);
    ASSERT_NULL(tip);
    TEST_END();
}

/* Edge: x just past [+] button → NULL */
int test_tabs_btn_tooltip_past_add(void)
{
    TEST_BEGIN();
    /* BTN_SIZE=24, so [+] goes from 4 to 28. x=29 is past it. */
    const char *tip = tabs_btn_tooltip_at_96(29, 400);
    ASSERT_NULL(tip);
    TEST_END();
}

/* =========================================================================
 * Status dot click — action depends on tab status
 * ========================================================================= */

/* Connected tab: status click should report TAB_CONNECTED */
int test_tabmgr_status_click_connected(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "Server", (void *)1);
    tabmgr_set_status(&m, 0, TAB_CONNECTED);
    /* Simulate what the click handler does: read status to decide action */
    TabStatus s = tabmgr_get_status(&m, 0);
    ASSERT_EQ(s, TAB_CONNECTED);
    TEST_END();
}

/* Disconnected tab: status click should report TAB_DISCONNECTED */
int test_tabmgr_status_click_disconnected(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "Server", (void *)1);
    tabmgr_set_status(&m, 0, TAB_DISCONNECTED);
    TabStatus s = tabmgr_get_status(&m, 0);
    ASSERT_EQ(s, TAB_DISCONNECTED);
    TEST_END();
}

/* Connecting tab: status click should report TAB_CONNECTING (no action) */
int test_tabmgr_status_click_connecting(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "Server", (void *)1);
    tabmgr_set_status(&m, 0, TAB_CONNECTING);
    TabStatus s = tabmgr_get_status(&m, 0);
    ASSERT_EQ(s, TAB_CONNECTING);
    TEST_END();
}

/* Idle tab: status click should report TAB_IDLE (no action) */
int test_tabmgr_status_click_idle(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "Server", (void *)1);
    TabStatus s = tabmgr_get_status(&m, 0);
    ASSERT_EQ(s, TAB_IDLE);
    TEST_END();
}

/* Status click on correct tab among multiple */
int test_tabmgr_status_click_correct_tab(void)
{
    TEST_BEGIN();
    TabManager m;
    tabmgr_init(&m);
    tabmgr_add(&m, "A", (void *)1);
    tabmgr_add(&m, "B", (void *)2);
    tabmgr_add(&m, "C", (void *)3);
    tabmgr_set_status(&m, 0, TAB_CONNECTED);
    tabmgr_set_status(&m, 1, TAB_DISCONNECTED);
    tabmgr_set_status(&m, 2, TAB_CONNECTING);
    /* Each tab has independent status */
    ASSERT_EQ(tabmgr_get_status(&m, 0), TAB_CONNECTED);
    ASSERT_EQ(tabmgr_get_status(&m, 1), TAB_DISCONNECTED);
    ASSERT_EQ(tabmgr_get_status(&m, 2), TAB_CONNECTING);
    TEST_END();
}
