/* tests/test_notice_dlg_win.c -- Win32 tests for the themed notice dialog
 * (src/ui/notice_dlg.c). Built only by `make wintest`. A timer with no window
 * runs inside the dialog's modal loop and posts keys the way a user would. */

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_framework.h"
#include "notice_dlg.h"
#include "ui_theme.h"
#include "ns_tokens.h"
#include "ns_font.h"

#define CLS "Nutshell_Notice"

/* notice.c's upgrade builder reads the settings key table, which lives in
 * config/loader.c (the whole config stack). The dialog never builds an
 * upgrade notice here, so an empty table stands in. config/loader.c must not
 * join WIN_TEST_SRCS while this stub exists (duplicate symbol). */
const ConfigSettingKey *config_setting_keys(size_t *count)
{
    if (count) *count = 0;
    return NULL;
}

static const ThemeTokens *tokens(void)
{
    /* The harness's ns_tokens() hands back a static filled by an earlier
     * test; resolve it here so this file does not depend on running after
     * it. */
    ThemeTokens *tk = (ThemeTokens *)ns_tokens();
    ui_theme_resolve(ui_theme_get(0), tk);
    return tk;
}

static void sample(Notice *n, int tone, int with_action)
{
    memset(n, 0, sizeof(*n));
    n->tone = (NoticeTone)tone;
    n->action_page = -1;
    snprintf(n->title, sizeof(n->title), "Welcome to Nutshell 9.9.9");
    snprintf(n->opening, sizeof(n->opening), "Your configuration was updated:");
    snprintf(n->items[0].label, sizeof(n->items[0].label), "AI assistant");
    snprintf(n->items[0].value, sizeof(n->items[0].value), "off until you choose to turn it on");
    snprintf(n->items[1].label, sizeof(n->items[1].label), "Voice");
    snprintf(n->items[1].value, sizeof(n->items[1].value), "off");
    n->item_count = 2;
    snprintf(n->closing, sizeof(n->closing), "Everything else is just as you left it.");
    if (with_action) snprintf(n->action, sizeof(n->action), "Review in Settings");
}

/* ---- the scripted timer ---------------------------------------------- */

typedef enum {
    PLAN_READ_THEN_ESC,        /* read every control back, then Escape */
    PLAN_ENTER,                /* Enter on the default (OK) */
    PLAN_ESC,
    PLAN_TAB_ENTER,            /* Tab to the action, Enter */
    PLAN_TAB_CYCLE,            /* OK -> action -> OK, then Escape */
    PLAN_OWNER_STATE,          /* record the owner's enabled state, Escape */
    PLAN_CLOSE_BOX,            /* WM_CLOSE */
    PLAN_FIT,                  /* every child inside the client area, Escape */
    PLAN_CHILD_OWNER,          /* record the root's state and the notice's owner, Escape */
    PLAN_RECT,                 /* record the notice's window rectangle, Escape */
    PLAN_NEST_OUTER,           /* open an inner notice over this one (see below) */
    PLAN_NEST_INNER,           /* press the outer's OK, see it hidden, Escape */
    PLAN_REPEAT_ENTER          /* an auto-repeat Enter is ignored, then a real one */
} Plan;

static Plan g_plan;
static int  g_step;
static HWND g_dlg;
static HWND g_owner;
static char g_class[64], g_caption[256];
static char g_title[256], g_open[800], g_close[300];
static char g_label0[80], g_value0[200], g_label1[80], g_value1[200];
static int  g_has_action, g_has_ok, g_has_closing;
static HWND g_focus[4];
static HWND g_ok_h, g_act_h;
static int  g_owner_enabled_in_dlg = -1;
static int  g_fit_ok;
static HWND g_root;
static int  g_root_enabled_in_dlg = -1;
static int  g_child_enabled_in_dlg = -1;
static HWND g_notice_owner;
static RECT g_dlg_rect;
static HWND g_outer;
static int  g_outer_visible_after_ok = -1;
static int  g_repeat_left_open = -1;

static void key(HWND dlg, WPARAM vk)
{
    HWND f = GetFocus();
    PostMessageA((f && IsChild(dlg, f)) ? f : dlg, WM_KEYDOWN, vk, 0);
}

static void key_repeat(HWND dlg, WPARAM vk)
{
    HWND f = GetFocus();
    /* bit 30: the key was already down (auto-repeat) */
    PostMessageA((f && IsChild(dlg, f)) ? f : dlg, WM_KEYDOWN, vk, (LPARAM)1 << 30);
}

static void fit_check(HWND dlg)
{
    RECT cr;
    GetClientRect(dlg, &cr);
    g_fit_ok = 1;
    for (HWND c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        RECT r;
        GetWindowRect(c, &r);
        MapWindowPoints(NULL, dlg, (POINT *)&r, 2);
        if (r.left < 0 || r.top < 0 || r.right > cr.right || r.bottom > cr.bottom ||
            r.right < r.left || r.bottom < r.top)
            g_fit_ok = 0;
    }
}

static VOID CALLBACK notice_timer(HWND h, UINT m, UINT_PTR id, DWORD t)
{
    (void)h; (void)m; (void)t;
    if (g_plan == PLAN_NEST_INNER) {
        /* Two notices are up; the inner one is owned by the outer. Press the
         * outer's OK: it must vanish at once although the inner loop runs on. */
        HWND inner = NULL;
        for (HWND w = NULL; (w = FindWindowExA(NULL, w, CLS, NULL)) != NULL; )
            if (GetWindow(w, GW_OWNER) == g_outer) inner = w;
        if (!inner) { KillTimer(NULL, id); return; }
        if (g_step++ == 0) {
            PostMessageA(g_outer, WM_COMMAND, (WPARAM)NOTICE_ID_OK, 0);
        } else {
            g_outer_visible_after_ok = IsWindowVisible(g_outer) ? 1 : 0;
            KillTimer(NULL, id);
            PostMessageA(inner, WM_COMMAND, (WPARAM)NOTICE_ID_OK, 0);
        }
        return;
    }
    HWND dlg = FindWindowA(CLS, NULL);
    if (!dlg) { KillTimer(NULL, id); return; }
    g_dlg = dlg;
    g_ok_h = GetDlgItem(dlg, NOTICE_ID_OK);
    g_act_h = GetDlgItem(dlg, NOTICE_ID_ACTION);
    int step = g_step++;
    switch (g_plan) {
    case PLAN_READ_THEN_ESC:
        GetClassNameA(dlg, g_class, sizeof g_class);
        GetWindowTextA(dlg, g_caption, sizeof g_caption);
        GetDlgItemTextA(dlg, NOTICE_ID_TITLE, g_title, sizeof g_title);
        GetDlgItemTextA(dlg, NOTICE_ID_OPENING, g_open, sizeof g_open);
        GetDlgItemTextA(dlg, NOTICE_ID_CLOSING, g_close, sizeof g_close);
        GetDlgItemTextA(dlg, NOTICE_ID_LABEL0, g_label0, sizeof g_label0);
        GetDlgItemTextA(dlg, NOTICE_ID_VALUE0, g_value0, sizeof g_value0);
        GetDlgItemTextA(dlg, NOTICE_ID_LABEL0 + 1, g_label1, sizeof g_label1);
        GetDlgItemTextA(dlg, NOTICE_ID_VALUE0 + 1, g_value1, sizeof g_value1);
        g_has_action = g_act_h != NULL;
        g_has_ok = g_ok_h != NULL;
        g_has_closing = GetDlgItem(dlg, NOTICE_ID_CLOSING) != NULL;
        KillTimer(NULL, id);
        key(dlg, VK_ESCAPE);
        break;
    case PLAN_ENTER:
        KillTimer(NULL, id);
        key(dlg, VK_RETURN);
        break;
    case PLAN_ESC:
        KillTimer(NULL, id);
        key(dlg, VK_ESCAPE);
        break;
    case PLAN_TAB_ENTER:
        if (step == 0) { g_focus[0] = GetFocus(); key(dlg, VK_TAB); }
        else { g_focus[1] = GetFocus(); KillTimer(NULL, id); key(dlg, VK_RETURN); }
        break;
    case PLAN_TAB_CYCLE:
        if (step < 3) { g_focus[step] = GetFocus(); key(dlg, VK_TAB); }
        else { g_focus[3] = GetFocus(); KillTimer(NULL, id); key(dlg, VK_ESCAPE); }
        break;
    case PLAN_OWNER_STATE:
        g_owner_enabled_in_dlg = IsWindowEnabled(g_owner) ? 1 : 0;
        KillTimer(NULL, id);
        key(dlg, VK_ESCAPE);
        break;
    case PLAN_CLOSE_BOX:
        KillTimer(NULL, id);
        PostMessageA(dlg, WM_CLOSE, 0, 0);
        break;
    case PLAN_CHILD_OWNER:
        g_root_enabled_in_dlg = IsWindowEnabled(g_root) ? 1 : 0;
        g_child_enabled_in_dlg = IsWindowEnabled(g_owner) ? 1 : 0;
        g_notice_owner = GetWindow(dlg, GW_OWNER);
        KillTimer(NULL, id);
        key(dlg, VK_ESCAPE);
        break;
    case PLAN_NEST_INNER:   /* handled before the switch */
        break;
    case PLAN_RECT:
        GetWindowRect(dlg, &g_dlg_rect);
        KillTimer(NULL, id);
        key(dlg, VK_ESCAPE);
        break;
    case PLAN_NEST_OUTER: {
        KillTimer(NULL, id);
        g_outer = dlg;
        g_plan = PLAN_NEST_INNER;
        g_step = 0;
        SetTimer(NULL, 0, 100, notice_timer);
        Notice in;
        sample(&in, NOTICE_INFO, 0);
        (void)notice_dlg_show(dlg, &in, ui_theme_get(0));
        PostMessageA(dlg, WM_NULL, 0, 0);   /* wake the outer loop */
        break;
    }
    case PLAN_REPEAT_ENTER:
        if (step == 0) {
            key_repeat(dlg, VK_RETURN);
        } else {
            g_repeat_left_open = IsWindow(dlg) ? 1 : 0;
            KillTimer(NULL, id);
            key(dlg, VK_RETURN);
        }
        break;
    case PLAN_FIT:
        fit_check(dlg);
        KillTimer(NULL, id);
        key(dlg, VK_ESCAPE);
        break;
    }
}

static int run(HWND owner, const Notice *n, Plan plan)
{
    tokens();
    g_plan = plan;
    g_step = 0;
    g_dlg = NULL;
    memset(g_focus, 0, sizeof g_focus);
    g_owner = owner;
    SetTimer(NULL, 0, 100, notice_timer);
    return notice_dlg_show(owner, n, ui_theme_get(0));
}

static int run_msg(HWND owner, NoticeMsgId id, Plan plan)
{
    tokens();
    g_plan = plan;
    g_step = 0;
    g_dlg = NULL;
    g_owner = owner;
    SetTimer(NULL, 0, 100, notice_timer);
    return notice_dlg_msg(owner, id, NULL, ui_theme_get(0));
}

/* ---- tests ------------------------------------------------------------ */

int test_notice_dlg_win_opens_with_class_caption_and_controls(void)
{
    TEST_BEGIN();
    Notice n;
    sample(&n, NOTICE_INFO, 1);
    g_class[0] = g_caption[0] = g_title[0] = g_open[0] = g_close[0] = '\0';
    g_label0[0] = g_value0[0] = g_label1[0] = g_value1[0] = '\0';
    ASSERT_EQ(run(NULL, &n, PLAN_READ_THEN_ESC), 0);
    ASSERT_STR_EQ(g_class, "Nutshell_Notice");
    ASSERT_STR_EQ(g_caption, "Welcome to Nutshell 9.9.9");
    ASSERT_STR_EQ(g_title, "Welcome to Nutshell 9.9.9");
    ASSERT_STR_EQ(g_open, "Your configuration was updated:");
    ASSERT_STR_EQ(g_label0, "AI assistant");
    ASSERT_STR_EQ(g_value0, "off until you choose to turn it on");
    ASSERT_STR_EQ(g_label1, "Voice");
    ASSERT_STR_EQ(g_value1, "off");
    ASSERT_STR_EQ(g_close, "Everything else is just as you left it.");
    ASSERT_TRUE(g_has_action);
    ASSERT_TRUE(g_has_ok);
    TEST_END();
}

int test_notice_dlg_win_no_action_button_when_action_empty(void)
{
    TEST_BEGIN();
    Notice n;
    sample(&n, NOTICE_WARNING, 0);
    n.closing[0] = '\0';
    n.item_count = 0;
    g_has_action = 1;
    g_has_ok = 0;
    g_has_closing = 1;
    ASSERT_EQ(run(NULL, &n, PLAN_READ_THEN_ESC), 0);
    ASSERT_FALSE(g_has_action);
    ASSERT_TRUE(g_has_ok);
    ASSERT_FALSE(g_has_closing);
    TEST_END();
}

int test_notice_dlg_win_enter_on_ok_returns_zero_and_window_is_gone(void)
{
    TEST_BEGIN();
    Notice n;
    sample(&n, NOTICE_INFO, 1);
    ASSERT_EQ(run(NULL, &n, PLAN_ENTER), 0);
    ASSERT_NOT_NULL(g_dlg);
    ASSERT_FALSE(IsWindow(g_dlg));
    ASSERT_NULL(FindWindowA(CLS, NULL));
    TEST_END();
}

int test_notice_dlg_win_escape_returns_zero(void)
{
    TEST_BEGIN();
    Notice n;
    sample(&n, NOTICE_INFO, 1);
    ASSERT_EQ(run(NULL, &n, PLAN_ESC), 0);
    ASSERT_FALSE(IsWindow(g_dlg));
    TEST_END();
}

int test_notice_dlg_win_close_box_returns_zero(void)
{
    TEST_BEGIN();
    Notice n;
    sample(&n, NOTICE_INFO, 1);
    ASSERT_EQ(run(NULL, &n, PLAN_CLOSE_BOX), 0);
    ASSERT_FALSE(IsWindow(g_dlg));
    TEST_END();
}

int test_notice_dlg_win_enter_on_focused_action_returns_one(void)
{
    TEST_BEGIN();
    Notice n;
    sample(&n, NOTICE_INFO, 1);
    ASSERT_EQ(run(NULL, &n, PLAN_TAB_ENTER), 1);
    ASSERT_TRUE(g_focus[0] != NULL && g_focus[0] == g_ok_h);   /* OK starts focused */
    ASSERT_TRUE(g_focus[1] != NULL && g_focus[1] == g_act_h);
    ASSERT_FALSE(IsWindow(g_dlg));
    TEST_END();
}

int test_notice_dlg_win_tab_moves_ok_action_ok(void)
{
    TEST_BEGIN();
    Notice n;
    sample(&n, NOTICE_INFO, 1);
    ASSERT_EQ(run(NULL, &n, PLAN_TAB_CYCLE), 0);
    ASSERT_TRUE(g_ok_h != NULL && g_act_h != NULL);
    ASSERT_TRUE(g_focus[0] == g_ok_h);
    ASSERT_TRUE(g_focus[1] == g_act_h);
    ASSERT_TRUE(g_focus[2] == g_ok_h);
    ASSERT_TRUE(g_focus[3] == g_act_h);   /* the third Tab moves on again */
    TEST_END();
}

int test_notice_dlg_win_tab_with_only_ok_stays_on_ok(void)
{
    TEST_BEGIN();
    Notice n;
    sample(&n, NOTICE_INFO, 0);
    ASSERT_EQ(run(NULL, &n, PLAN_TAB_CYCLE), 0);
    ASSERT_TRUE(g_ok_h != NULL);
    for (int i = 0; i < 4; i++) ASSERT_TRUE(g_focus[i] == g_ok_h);
    TEST_END();
}

int test_notice_dlg_win_owner_enable_state_is_restored(void)
{
    TEST_BEGIN();
    Notice n;
    sample(&n, NOTICE_INFO, 1);
    HWND owner = CreateWindowExA(0, "STATIC", "owner", WS_OVERLAPPEDWINDOW,
                                 0, 0, 500, 300, NULL, NULL, GetModuleHandle(NULL), NULL);
    ASSERT_NOT_NULL(owner);
    if (owner) {
        /* An enabled owner is disabled while it is up and enabled after. */
        g_owner_enabled_in_dlg = -1;
        ASSERT_EQ(run(owner, &n, PLAN_OWNER_STATE), 0);
        ASSERT_EQ(g_owner_enabled_in_dlg, 0);
        ASSERT_TRUE(IsWindowEnabled(owner));
        /* An owner that was already disabled (nested in another modal)
         * stays disabled. */
        EnableWindow(owner, FALSE);
        ASSERT_EQ(run(owner, &n, PLAN_ESC), 0);
        ASSERT_FALSE(IsWindowEnabled(owner));
        DestroyWindow(owner);
    }
    TEST_END();
}

int test_notice_dlg_win_child_owner_disables_root(void)
{
    TEST_BEGIN();
    Notice n;
    sample(&n, NOTICE_INFO, 0);
    HWND top = CreateWindowExA(0, "STATIC", "top", WS_OVERLAPPEDWINDOW,
                               0, 0, 500, 300, NULL, NULL, GetModuleHandle(NULL), NULL);
    ASSERT_NOT_NULL(top);
    if (top) {
        HWND child = CreateWindowExA(0, "STATIC", "child", WS_CHILD | WS_VISIBLE,
                                     0, 0, 100, 100, top, NULL, GetModuleHandle(NULL), NULL);
        ASSERT_NOT_NULL(child);
        if (child) {
            g_root = top;
            g_root_enabled_in_dlg = g_child_enabled_in_dlg = -1;
            g_notice_owner = NULL;
            ASSERT_EQ(run(child, &n, PLAN_CHILD_OWNER), 0);
            ASSERT_EQ(g_root_enabled_in_dlg, 0);
            ASSERT_TRUE(g_notice_owner == top);
            ASSERT_TRUE(IsWindowEnabled(top));
            /* A child's own WS_DISABLED is never set (only the root is), so
             * it reports enabled even while the root is disabled. */
            ASSERT_EQ(g_child_enabled_in_dlg, 1);
            ASSERT_TRUE(IsWindowEnabled(child));
        }
        DestroyWindow(top);
    }
    TEST_END();
}

int test_notice_dlg_win_msg_shows_table_text(void)
{
    TEST_BEGIN();
    g_caption[0] = g_open[0] = 0;
    g_class[0] = 0;
    ASSERT_EQ(run_msg(NULL, NOTICE_MSG_TAB_LIMIT, PLAN_READ_THEN_ESC), 0);
    ASSERT_STR_EQ(g_class, CLS);
    ASSERT_STR_EQ(g_caption, "Tab Limit");
    ASSERT_STR_EQ(g_open, "Maximum number of tabs reached.");
    ASSERT_NULL(FindWindowA(CLS, NULL));
    TEST_END();
}

int test_notice_dlg_win_msg_bad_id_shows_nothing(void)
{
    TEST_BEGIN();
    /* The close timer makes a regression that shows a window fail, not hang. */
    tokens();
    g_plan = PLAN_READ_THEN_ESC;
    g_step = 0;
    SetTimer(NULL, 0, 100, notice_timer);
    ASSERT_EQ(notice_dlg_msg(NULL, NOTICE_MSG_COUNT, NULL, ui_theme_get(0)), 0);
    ASSERT_EQ(g_step, 0);   /* the timer never found a window */
    ASSERT_NULL(FindWindowA(CLS, NULL));
    TEST_END();
}

int test_notice_dlg_win_minimized_owner_centres_on_normal_position(void)
{
    TEST_BEGIN();
    Notice n;
    sample(&n, NOTICE_INFO, 0);
    HWND owner = CreateWindowExA(0, "STATIC", "owner", WS_OVERLAPPEDWINDOW,
                                 100, 100, 500, 300, NULL, NULL, GetModuleHandle(NULL), NULL);
    ASSERT_NOT_NULL(owner);
    if (owner) {
        WINDOWPLACEMENT wp;
        memset(&wp, 0, sizeof wp);
        wp.length = sizeof wp;
        wp.showCmd = SW_SHOWMINIMIZED;
        wp.rcNormalPosition.left = 100;  wp.rcNormalPosition.top = 100;
        wp.rcNormalPosition.right = 600; wp.rcNormalPosition.bottom = 400;
        SetWindowPlacement(owner, &wp);
        ASSERT_TRUE(IsIconic(owner));
        memset(&g_dlg_rect, 0, sizeof g_dlg_rect);
        ASSERT_EQ(run(owner, &n, PLAN_RECT), 0);
        MONITORINFO mi;
        memset(&mi, 0, sizeof mi);
        mi.cbSize = sizeof mi;
        ASSERT_TRUE(GetMonitorInfoA(MonitorFromRect(&wp.rcNormalPosition,
                                                    MONITOR_DEFAULTTONEAREST), &mi));
        ASSERT_TRUE(g_dlg_rect.left >= mi.rcWork.left && g_dlg_rect.top >= mi.rcWork.top);
        ASSERT_TRUE(g_dlg_rect.right <= mi.rcWork.right && g_dlg_rect.bottom <= mi.rcWork.bottom);
        int cx = (g_dlg_rect.left + g_dlg_rect.right) / 2;
        int cy = (g_dlg_rect.top + g_dlg_rect.bottom) / 2;
        ASSERT_TRUE(abs(cx - 350) <= 4);
        ASSERT_TRUE(abs(cy - 250) <= 4);
        DestroyWindow(owner);
    }
    TEST_END();
}

int test_notice_dlg_win_close_hides_before_inner_loop_ends(void)
{
    TEST_BEGIN();
    Notice n;
    sample(&n, NOTICE_INFO, 0);
    g_outer_visible_after_ok = -1;
    ASSERT_EQ(run(NULL, &n, PLAN_NEST_OUTER), 0);
    ASSERT_EQ(g_outer_visible_after_ok, 0);
    ASSERT_NULL(FindWindowA(CLS, NULL));
    TEST_END();
}

int test_notice_dlg_win_held_enter_does_not_dismiss(void)
{
    TEST_BEGIN();
    Notice n;
    sample(&n, NOTICE_INFO, 1);
    g_repeat_left_open = -1;
    ASSERT_EQ(run(NULL, &n, PLAN_REPEAT_ENTER), 0);   /* the real Enter closed it: OK */
    ASSERT_EQ(g_repeat_left_open, 1);
    TEST_END();
}

int test_notice_dlg_win_wide_label_and_long_text_stay_inside(void)
{
    TEST_BEGIN();
    Notice n;
    sample(&n, NOTICE_WARNING, 1);
    memset(n.items[0].label, 'W', 63);
    n.items[0].label[63] = '\0';
    memset(n.items[0].value, 'v', 159);
    n.items[0].value[159] = '\0';
    n.item_count = 1;
    g_fit_ok = 0;
    ASSERT_EQ(run(NULL, &n, PLAN_FIT), 0);
    ASSERT_TRUE(g_fit_ok);
    TEST_END();
}

/* ns_font_bold: a different, cached handle from the body font, FW_BOLD or
 * heavier, at the same height. */
int test_ns_font_win_bold_is_cached_and_bold(void)
{
    TEST_BEGIN();
    int dpis[2] = { 96, 192 };
    for (int i = 0; i < 2; i++) {
        HFONT body = ns_font(FONT_BODY, dpis[i]);
        HFONT bold = ns_font_bold(FONT_BODY, dpis[i]);
        ASSERT_NOT_NULL(body);
        ASSERT_NOT_NULL(bold);
        ASSERT_TRUE(bold != body);
        ASSERT_TRUE(bold == ns_font_bold(FONT_BODY, dpis[i]));
        LOGFONTA lb, lr;
        ASSERT_TRUE(GetObjectA((HGDIOBJ)bold, (int)sizeof lb, &lb) > 0);
        ASSERT_TRUE(GetObjectA((HGDIOBJ)body, (int)sizeof lr, &lr) > 0);
        ASSERT_TRUE(lb.lfWeight >= FW_BOLD);
        ASSERT_EQ(lb.lfHeight, lr.lfHeight);
    }
    ASSERT_TRUE(ns_font_bold(FONT_MONO, 96) != ns_font(FONT_MONO, 96));
    TEST_END();
}

#endif /* _WIN32 */
