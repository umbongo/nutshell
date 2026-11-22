/* tests/test_ns_check_win.c -- Win32 tests for the themed checkbox painter
 * (src/ui/ns_check.c). Built only by `make wintest`. */

#ifdef _WIN32

#include <windows.h>
#include <commctrl.h>
#include "test_framework.h"
#include "ns_check.h"

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

int test_ns_check_win_check_state_round_trips(void)
{
    TEST_BEGIN();
    HWND top = make_parent();
    ASSERT_TRUE(top != NULL);
    HWND chk = make_check(top);
    ASSERT_TRUE(chk != NULL);
    ns_check_attach(chk);
    ns_check_attach(chk);                        /* idempotent */
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
    HWND top = make_parent();
    HWND chk = make_check(top);
    ns_check_attach(chk);
    ShowWindow(top, SW_SHOWNA);
    SendMessage(chk, BM_CLICK, 0, 0);
    ASSERT_EQ((int)SendMessage(chk, BM_GETCHECK, 0, 0), BST_CHECKED);
    SendMessage(chk, BM_CLICK, 0, 0);
    ASSERT_EQ((int)SendMessage(chk, BM_GETCHECK, 0, 0), BST_UNCHECKED);

    /* A real mouse click, posted the way the harness does it. */
    SendMessage(chk, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
    SendMessage(chk, WM_LBUTTONUP, 0, MAKELPARAM(5, 5));
    ASSERT_EQ((int)SendMessage(chk, BM_GETCHECK, 0, 0), BST_CHECKED);

    /* WM_PRINTCLIENT paints into our DC: the box area is not left blank. */
    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, 200, 28);
    HGDIOBJ old = SelectObject(mem, bmp);
    SetPixel(mem, 9, 14, RGB(1, 2, 3));
    SendMessage(chk, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT);
    ASSERT_TRUE(GetPixel(mem, 9, 14) != RGB(1, 2, 3));
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);

    ASSERT_EQ((int)SendMessage(chk, BM_GETCHECK, 0, 0), BST_CHECKED);
    DestroyWindow(top);
    TEST_END();
}

int test_ns_check_win_null_and_destroy_are_safe(void)
{
    TEST_BEGIN();
    ns_check_attach(NULL);
    HWND top = make_parent();
    HWND chk = make_check(top);
    ns_check_attach(chk);
    SendMessage(chk, WM_MOUSEMOVE, 0, MAKELPARAM(3, 3));
    SendMessage(chk, WM_MOUSELEAVE, 0, 0);
    EnableWindow(chk, FALSE);
    SendMessage(chk, WM_SETTEXT, 0, (LPARAM)"changed");
    DestroyWindow(top);                          /* frees the subclass data */
    ASSERT_TRUE(1);
    TEST_END();
}

#endif
