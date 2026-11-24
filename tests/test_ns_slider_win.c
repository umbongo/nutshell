/* tests/test_ns_slider_win.c -- Win32 tests for the themed slider
 * (src/ui/ns_slider.c). Built only by `make wintest`. */

#ifdef _WIN32

#include <windows.h>
#include "test_framework.h"
#include "ns_slider.h"

static int g_changed;
static int g_last_id;

static LRESULT CALLBACK count_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_COMMAND && HIWORD(w) == NSS_CHANGED) {
        g_changed++;
        g_last_id = LOWORD(w);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static HWND make_parent(void)
{
    static int registered;
    HINSTANCE inst = GetModuleHandle(NULL);
    if (!registered) {
        WNDCLASSA wc;
        ZeroMemory(&wc, sizeof wc);
        wc.lpfnWndProc = count_proc;
        wc.hInstance = inst;
        wc.lpszClassName = "NsSliderTestParent";
        RegisterClassA(&wc);
        registered = 1;
    }
    g_changed = 0;
    g_last_id = 0;
    return CreateWindowExA(0, "NsSliderTestParent", "p", WS_OVERLAPPEDWINDOW,
                           0, 0, 400, 200, NULL, NULL, inst, NULL);
}

int test_ns_slider_win_keys_and_notify(void)
{
    TEST_BEGIN();
    HWND top = make_parent();
    ASSERT_TRUE(top != NULL);
    HWND sl = ns_slider_create(top, 4242, -12, 30, 0);
    ASSERT_TRUE(sl != NULL);
    MoveWindow(sl, 10, 10, 300, 24, TRUE);
    ASSERT_EQ(ns_slider_get(sl), 0);

    PostMessage(sl, WM_KEYDOWN, VK_RIGHT, 0);
    PostMessage(sl, WM_KEYDOWN, VK_RIGHT, 0);
    MSG m;
    while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessage(&m);
    ASSERT_EQ(ns_slider_get(sl), 2);
    ASSERT_EQ(g_changed, 2);
    ASSERT_EQ(g_last_id, 4242);

    PostMessage(sl, WM_KEYDOWN, VK_END, 0);
    while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessage(&m);
    ASSERT_EQ(ns_slider_get(sl), 30);
    ASSERT_EQ(g_changed, 3);

    /* No change at the end: no notification. */
    SendMessage(sl, WM_KEYDOWN, VK_END, 0);
    ASSERT_EQ(g_changed, 3);

    /* Programmatic set is silent and clamped. */
    ns_slider_set(sl, -99);
    ASSERT_EQ(ns_slider_get(sl), -12);
    ASSERT_EQ(g_changed, 3);

    /* A click/drag moves it: 300 px wide, track runs between the thumb radii. */
    SendMessage(sl, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(290, 10));
    ASSERT_TRUE(ns_slider_get(sl) > 25);
    ASSERT_EQ(g_changed, 4);
    SendMessage(sl, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(-50, 10));
    ASSERT_EQ(ns_slider_get(sl), -12);
    SendMessage(sl, WM_LBUTTONUP, 0, MAKELPARAM(-50, 10));

    /* Disabled: keys and clicks are ignored. */
    ns_slider_set(sl, 5);
    EnableWindow(sl, FALSE);
    int before = g_changed;
    SendMessage(sl, WM_KEYDOWN, VK_RIGHT, 0);
    SendMessage(sl, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(290, 10));
    ASSERT_EQ(ns_slider_get(sl), 5);
    ASSERT_EQ(g_changed, before);

    ASSERT_EQ(ns_slider_get(NULL), 0);
    DestroyWindow(top);
    TEST_END();
}

#endif /* _WIN32 */
