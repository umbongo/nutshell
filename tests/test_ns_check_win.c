/* tests/test_ns_check_win.c -- Win32 tests for the themed checkbox painter
 * (src/ui/ns_check.c). Built only by `make wintest`. */

#ifdef _WIN32

#include <windows.h>
#include <commctrl.h>
#include "test_framework.h"
#include "ns_check.h"
#include "ns_check_geom.h"
#include "ns_tokens.h"
#include "ui_theme.h"
#include "icons.h"
#include "dpi_util.h"

static COLORREF cr_of(unsigned int rgb)
{
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

/* The harness's ns_tokens() hands back a static filled by an earlier test;
 * resolve it here so this file does not depend on running after it. */
static const ThemeTokens *tokens(void)
{
    ThemeTokens *tk = (ThemeTokens *)ns_tokens();
    ui_theme_resolve(ui_theme_get(0), tk);
    return tk;
}

static HWND make_parent(void)
{
    return CreateWindowExA(0, "STATIC", "chk", WS_OVERLAPPEDWINDOW,
                           0, 0, 400, 200, NULL, NULL,
                           GetModuleHandle(NULL), NULL);
}

static HWND make_check(HWND parent)
{
    return CreateWindowExA(0, "BUTTON", "Enable thing",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                           10, 10, 200, 28, parent, (HMENU)(UINT_PTR)101,
                           GetModuleHandle(NULL), NULL);
}

/* Paints `chk` with WM_PRINTCLIENT into a bitmap and samples pixels. */
typedef struct { HDC screen, mem; HBITMAP bmp; HGDIOBJ old; } Shot;

static int shot_take(Shot *s, HWND chk)
{
    RECT rc;
    GetClientRect(chk, &rc);
    s->screen = GetDC(NULL);
    s->mem = CreateCompatibleDC(s->screen);
    s->bmp = CreateCompatibleBitmap(s->screen, rc.right, rc.bottom);
    s->old = SelectObject(s->mem, s->bmp);
    SendMessage(chk, WM_PRINTCLIENT, (WPARAM)s->mem, PRF_CLIENT);
    return 1;
}

static void shot_free(Shot *s)
{
    SelectObject(s->mem, s->old);
    DeleteObject(s->bmp);
    DeleteDC(s->mem);
    ReleaseDC(NULL, s->screen);
}

int test_ns_check_win_check_state_round_trips(void)
{
    TEST_BEGIN();
    HWND top = make_parent();
    ASSERT_TRUE(top != NULL);
    HWND chk = make_check(top);
    ASSERT_TRUE(chk != NULL);
    ASSERT_TRUE(!ns_check_is_attached(chk));
    ns_check_attach(chk);
    ns_check_attach(chk);                        /* idempotent */
    ASSERT_TRUE(ns_check_is_attached(chk));
    ASSERT_EQ((int)SendMessage(chk, BM_GETCHECK, 0, 0), BST_UNCHECKED);
    SendMessage(chk, BM_SETCHECK, BST_CHECKED, 0);
    ASSERT_EQ((int)SendMessage(chk, BM_GETCHECK, 0, 0), BST_CHECKED);
    SendMessage(chk, BM_SETCHECK, BST_UNCHECKED, 0);
    ASSERT_EQ((int)SendMessage(chk, BM_GETCHECK, 0, 0), BST_UNCHECKED);
    DestroyWindow(top);
    TEST_END();
}

int test_ns_check_win_click_toggles_and_paints(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(ns_icons_init());                /* GDI+ for the painter */
    const ThemeTokens *tk = tokens();
    HWND top = make_parent();
    HWND chk = make_check(top);
    ns_check_attach(chk);
    ASSERT_TRUE(ns_check_is_attached(chk));
    ShowWindow(top, SW_SHOWNA);

    RECT rc;
    GetClientRect(chk, &rc);
    NsCheckLayout L;
    ns_check_layout(rc.right, rc.bottom, get_window_dpi(chk), &L);
    int fx = L.box_x + 3, fy = L.box_y + L.box_h / 2;    /* clear of the mark */
    float xy[6];
    ns_check_mark(L.box_x, L.box_y, L.box_w, xy);
    int mx = (int)((xy[0] + xy[2]) / 2.0f), my = (int)((xy[1] + xy[3]) / 2.0f);

    Shot s;
    shot_take(&s, chk);                          /* unchecked */
    ASSERT_TRUE(GetPixel(s.mem, fx, fy) == cr_of(tk->bg_primary.base));
    shot_free(&s);

    SendMessage(chk, BM_CLICK, 0, 0);
    ASSERT_EQ((int)SendMessage(chk, BM_GETCHECK, 0, 0), BST_CHECKED);
    shot_take(&s, chk);                          /* checked */
    ASSERT_TRUE(GetPixel(s.mem, fx, fy) == cr_of(tk->accent.base));
    ASSERT_TRUE(GetPixel(s.mem, mx, my) == cr_of(tk->accent.label));
    shot_free(&s);

    SendMessage(chk, BM_CLICK, 0, 0);
    ASSERT_EQ((int)SendMessage(chk, BM_GETCHECK, 0, 0), BST_UNCHECKED);

    /* A real mouse click toggles it too. */
    SendMessage(chk, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
    SendMessage(chk, WM_LBUTTONUP, 0, MAKELPARAM(5, 5));
    ASSERT_EQ((int)SendMessage(chk, BM_GETCHECK, 0, 0), BST_CHECKED);

    DestroyWindow(top);
    ns_icons_shutdown();
    TEST_END();
}

int test_ns_check_win_null_and_destroy_are_safe(void)
{
    TEST_BEGIN();
    ns_check_attach(NULL);
    ASSERT_TRUE(!ns_check_is_attached(NULL));
    HWND top = make_parent();
    HWND chk = make_check(top);
    ns_check_attach(chk);
    SendMessage(chk, WM_MOUSEMOVE, 0, MAKELPARAM(3, 3));
    SendMessage(chk, WM_MOUSEMOVE, 0, MAKELPARAM(-5, -5));   /* outside */
    SendMessage(chk, WM_MOUSELEAVE, 0, 0);
    EnableWindow(chk, FALSE);
    SendMessage(chk, WM_SETTEXT, 0, (LPARAM)"changed");
    DestroyWindow(top);                          /* frees the subclass data */
    ASSERT_TRUE(1);
    TEST_END();
}

#endif
