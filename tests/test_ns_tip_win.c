/* tests/test_ns_tip_win.c -- Win32 tests for the themed tooltip helper
 * (src/ui/ns_tip.c). Built only by `make wintest`; win_runner.c runs them
 * under an activation context built from src/ui/nutshell.manifest, so they
 * get comctl32 v6 as the exe does. Colours and pixels are not checked:
 * whether the classic tip honours them is a Windows check. */

#ifdef _WIN32

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include "test_framework.h"
#include "ns_tip.h"
#include "ns_font.h"
#include "ns_scale.h"
#include "ui_theme.h"

#define TIP_DPI 192

typedef struct {
    HWND top, lbl, edit, btn, combo_list, combo_drop, tip;
} TipFix;

static int fix_make(TipFix *f)
{
    HINSTANCE hi = GetModuleHandle(NULL);
    memset(f, 0, sizeof(*f));
    f->top = CreateWindowExA(0, "STATIC", "tiptest", WS_OVERLAPPEDWINDOW,
                             0, 0, 400, 300, NULL, NULL, hi, NULL);
    if (!f->top) return 0;
    f->lbl = CreateWindowExA(0, "STATIC", "Label:",
                             WS_CHILD | WS_VISIBLE | SS_NOTIFY,
                             10, 10, 100, 20, f->top, NULL, hi, NULL);
    f->edit = CreateWindowExA(0, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER,
                              10, 40, 150, 24, f->top, NULL, hi, NULL);
    f->btn = CreateWindowExA(0, "BUTTON", "Go", WS_CHILD | WS_VISIBLE,
                             10, 70, 80, 24, f->top, NULL, hi, NULL);
    f->combo_list = CreateWindowExA(0, "COMBOBOX", "",
                                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                    10, 100, 150, 200, f->top, NULL, hi, NULL);
    f->combo_drop = CreateWindowExA(0, "COMBOBOX", "",
                                    WS_CHILD | WS_VISIBLE | CBS_DROPDOWN,
                                    10, 130, 150, 200, f->top, NULL, hi, NULL);
    if (!f->lbl || !f->edit || !f->btn || !f->combo_list || !f->combo_drop)
        return 0;
    EnableWindow(f->combo_list, FALSE);

    ThemeTokens tok;
    ui_theme_resolve(ui_theme_get(0), &tok);
    f->tip = ns_tip_create(f->top, TIP_DPI, ns_font(FONT_BODY, TIP_DPI), &tok);
    return f->tip != NULL;
}

static void fix_free(TipFix *f)
{
    if (f->top) DestroyWindow(f->top);   /* destroys the tip, its owned popup */
    if (f->tip && IsWindow(f->tip)) DestroyWindow(f->tip);
    memset(f, 0, sizeof(*f));
}

static int get_text(HWND tip, HWND hwnd, UINT_PTR id, char *buf, int n)
{
    TOOLINFOA ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.hwnd = hwnd;
    ti.uId = id;
    ti.lpszText = buf;
    buf[0] = '\0';
    SendMessageA(tip, TTM_GETTEXTA, (WPARAM)n, (LPARAM)&ti);
    return (int)strlen(buf);
}

int test_ns_tip_win_create_styles(void)
{
    TEST_BEGIN();
    TipFix f;
    ASSERT_TRUE(fix_make(&f));
    if (f.tip) {
        LONG st = GetWindowLongA(f.tip, GWL_STYLE);
        ASSERT_TRUE(st & TTS_NOPREFIX);
        ASSERT_TRUE(st & TTS_ALWAYSTIP);
        ASSERT_FALSE(st & TTS_BALLOON);
        ASSERT_EQ((int)SendMessage(f.tip, TTM_GETMAXTIPWIDTH, 0, 0),
                  ns_scale(360, 192));
        ASSERT_EQ((int)SendMessage(f.tip, TTM_GETDELAYTIME, TTDT_AUTOPOP, 0),
                  32767);
    }
    fix_free(&f);
    TEST_END();
}

int test_ns_tip_win_notify_static_hits_client(void)
{
    TEST_BEGIN();
    TipFix f;
    ASSERT_TRUE(fix_make(&f));
    if (f.lbl) {
        POINT pt = { 50, 20 };
        ClientToScreen(f.top, &pt);
        LRESULT hit = SendMessage(f.lbl, WM_NCHITTEST, 0,
                                  MAKELPARAM((WORD)(short)pt.x,
                                             (WORD)(short)pt.y));
        ASSERT_EQ((int)hit, HTCLIENT);
    }
    fix_free(&f);
    TEST_END();
}

int test_ns_tip_win_add_keeps_styles(void)
{
    TEST_BEGIN();
    TipFix f;
    ASSERT_TRUE(fix_make(&f));
    if (f.tip) {
        HWND w[5] = { f.lbl, f.edit, f.btn, f.combo_list, f.combo_drop };
        LONG st[5], ex[5];
        for (int i = 0; i < 5; i++) {
            st[i] = GetWindowLongA(w[i], GWL_STYLE);
            ex[i] = GetWindowLongA(w[i], GWL_EXSTYLE);
        }
        for (int i = 0; i < 5; i++)
            ASSERT_TRUE(ns_tip_add(f.tip, w[i], "a") != 0);
        for (int i = 0; i < 5; i++) {
            ASSERT_EQ(GetWindowLongA(w[i], GWL_STYLE), st[i]);
            ASSERT_EQ(GetWindowLongA(w[i], GWL_EXSTYLE), ex[i]);
        }
    }
    fix_free(&f);
    TEST_END();
}

int test_ns_tip_win_count_and_text(void)
{
    TEST_BEGIN();
    TipFix f;
    ASSERT_TRUE(fix_make(&f));
    if (f.tip) {
        char buf[64];
        RECT r = { 5, 5, 50, 25 };
        ASSERT_TRUE(ns_tip_add(f.tip, f.lbl, "a") != 0);
        ASSERT_TRUE(ns_tip_add(f.tip, f.edit, "bb") != 0);
        ASSERT_TRUE(ns_tip_add(f.tip, f.btn, "ccc") != 0);
        ASSERT_TRUE(ns_tip_add(f.tip, f.combo_list, "dddd") != 0);
        ASSERT_TRUE(ns_tip_add(f.tip, f.combo_drop, "eeeee") != 0);
        ASSERT_TRUE(ns_tip_add_combo(f.tip, f.combo_drop, "ffffff") != 0);
        ASSERT_TRUE(ns_tip_add_rect(f.tip, f.top, 7001, &r, "g") != 0);
        ASSERT_EQ((int)SendMessage(f.tip, TTM_GETTOOLCOUNT, 0, 0), 7);

        get_text(f.tip, f.top, (UINT_PTR)f.lbl, buf, 64);
        ASSERT_STR_EQ(buf, "a");
        get_text(f.tip, f.top, (UINT_PTR)f.edit, buf, 64);
        ASSERT_STR_EQ(buf, "bb");
        get_text(f.tip, f.top, (UINT_PTR)f.btn, buf, 64);
        ASSERT_STR_EQ(buf, "ccc");
        get_text(f.tip, f.top, (UINT_PTR)f.combo_list, buf, 64);
        ASSERT_STR_EQ(buf, "dddd");
        get_text(f.tip, f.top, (UINT_PTR)f.combo_drop, buf, 64);
        ASSERT_STR_EQ(buf, "eeeee");
        get_text(f.tip, f.top, 7001, buf, 64);
        ASSERT_STR_EQ(buf, "g");

        COMBOBOXINFO cbi;
        memset(&cbi, 0, sizeof(cbi));
        cbi.cbSize = sizeof(cbi);
        ASSERT_TRUE(GetComboBoxInfo(f.combo_drop, &cbi));
        ASSERT_NOT_NULL(cbi.hwndItem);
        get_text(f.tip, f.combo_drop, (UINT_PTR)cbi.hwndItem, buf, 64);
        ASSERT_STR_EQ(buf, "ffffff");

        /* A CBS_DROPDOWNLIST combo has no item edit. */
        ASSERT_EQ(ns_tip_add_combo(f.tip, f.combo_list, "x"), 0);
        ASSERT_EQ((int)SendMessage(f.tip, TTM_GETTOOLCOUNT, 0, 0), 7);
    }
    fix_free(&f);
    TEST_END();
}

int test_ns_tip_win_rect_tool_moves(void)
{
    TEST_BEGIN();
    TipFix f;
    ASSERT_TRUE(fix_make(&f));
    if (f.tip) {
        RECT r = { 5, 5, 50, 25 };
        RECT to = { 20, 30, 120, 52 };
        TOOLINFOA ti;
        ASSERT_TRUE(ns_tip_add_rect(f.tip, f.top, 7001, &r, "g") != 0);

        ns_tip_move_rect(f.tip, f.top, 7001, &to);
        memset(&ti, 0, sizeof(ti));
        ti.cbSize = sizeof(ti);
        ti.hwnd = f.top;
        ti.uId = 7001;
        ASSERT_TRUE(SendMessageA(f.tip, TTM_GETTOOLINFOA, 0, (LPARAM)&ti) != 0);
        ASSERT_EQ((int)ti.rect.left, 20);
        ASSERT_EQ((int)ti.rect.top, 30);
        ASSERT_EQ((int)ti.rect.right, 120);
        ASSERT_EQ((int)ti.rect.bottom, 52);

        ns_tip_move_rect(f.tip, f.top, 7001, NULL);
        memset(&ti, 0, sizeof(ti));
        ti.cbSize = sizeof(ti);
        ti.hwnd = f.top;
        ti.uId = 7001;
        ASSERT_TRUE(SendMessageA(f.tip, TTM_GETTOOLINFOA, 0, (LPARAM)&ti) != 0);
        ASSERT_EQ((int)ti.rect.left, 0);
        ASSERT_EQ((int)ti.rect.top, 0);
        ASSERT_EQ((int)ti.rect.right, 0);
        ASSERT_EQ((int)ti.rect.bottom, 0);
    }
    fix_free(&f);
    TEST_END();
}

#endif /* _WIN32 */
