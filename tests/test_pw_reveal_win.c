/* tests/test_pw_reveal_win.c -- Win32 tests for the hold-to-reveal eye
 * button helper (src/ui/pw_reveal.c). Built only by `make wintest`. */

#ifdef _WIN32

#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include "test_framework.h"
#include "pw_reveal.h"

typedef struct {
    HWND  top;
    HWND  edit;
    HWND  eye;
    WCHAR orig;
} PwFix;

static int fix_make(PwFix *f, const char *text)
{
    f->top = CreateWindowExA(0, "STATIC", "pwtest", WS_OVERLAPPEDWINDOW,
                             0, 0, 400, 200, NULL, NULL,
                             GetModuleHandle(NULL), NULL);
    if (!f->top) return 0;
    f->edit = CreateWindowExA(0, "EDIT", "",
                              WS_CHILD | WS_VISIBLE | ES_PASSWORD |
                              ES_AUTOHSCROLL | WS_BORDER,
                              10, 10, 200, 24, f->top, NULL,
                              GetModuleHandle(NULL), NULL);
    if (!f->edit) return 0;
    SetWindowTextA(f->edit, text);
    f->orig = (WCHAR)SendMessageW(f->edit, EM_GETPASSWORDCHAR, 0, 0);
    pw_reveal_attach(f->edit);
    f->eye = FindWindowExA(f->edit, NULL, "NutshellPwEye", NULL);
    return 1;
}

static void fix_free(PwFix *f)
{
    if (f->top) DestroyWindow(f->top);
    f->top = f->edit = f->eye = NULL;
}

static void press(HWND eye)
{
    SendMessageW(eye, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
}

static void release_at(HWND eye, int x, int y)
{
    SendMessageW(eye, WM_LBUTTONUP, 0, MAKELPARAM((WORD)(short)x, (WORD)(short)y));
}

static WCHAR pwchar(HWND edit)
{
    return (WCHAR)SendMessageW(edit, EM_GETPASSWORDCHAR, 0, 0);
}

int test_pw_reveal_win_press_shows_release_restores_mask(void)
{
    TEST_BEGIN();
    PwFix f;
    ASSERT_TRUE(fix_make(&f, "s3cret"));
    ASSERT_NOT_NULL(f.eye);
    if (f.eye) {
        press(f.eye);
        ASSERT_EQ(pwchar(f.edit), 0);
        ASSERT_EQ(pw_reveal_is_revealed(f.edit), 1);
        release_at(f.eye, 2, 2);
        ASSERT_EQ(pwchar(f.edit), f.orig);
        ASSERT_EQ(pw_reveal_is_revealed(f.edit), 0);
        ASSERT_TRUE(pw_reveal_attach(f.edit) == f.eye);
    }
    fix_free(&f);
    TEST_END();
}

int test_pw_reveal_win_text_caret_selection_unchanged(void)
{
    TEST_BEGIN();
    PwFix f;
    ASSERT_TRUE(fix_make(&f, "s3cret"));
    ASSERT_NOT_NULL(f.eye);
    if (f.eye) {
        char buf[32];
        DWORD s = 0, e = 0;
        SendMessageW(f.edit, EM_SETSEL, 2, 4);
        press(f.eye);
        GetWindowTextA(f.edit, buf, sizeof buf);
        ASSERT_STR_EQ(buf, "s3cret");
        SendMessageW(f.edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
        ASSERT_EQ(s, 2);
        ASSERT_EQ(e, 4);
        release_at(f.eye, 2, 2);
        GetWindowTextA(f.edit, buf, sizeof buf);
        ASSERT_STR_EQ(buf, "s3cret");
        SendMessageW(f.edit, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
        ASSERT_EQ(s, 2);
        ASSERT_EQ(e, 4);
    }
    fix_free(&f);
    TEST_END();
}

int test_pw_reveal_win_capture_lost_masks(void)
{
    TEST_BEGIN();
    PwFix f;
    ASSERT_TRUE(fix_make(&f, "s3cret"));
    ASSERT_NOT_NULL(f.eye);
    if (f.eye) {
        press(f.eye);
        SendMessageW(f.eye, WM_CAPTURECHANGED, 0, 0);
        ASSERT_EQ(pwchar(f.edit), f.orig);
        press(f.eye);
        ASSERT_EQ(pwchar(f.edit), 0);
        SendMessageW(f.eye, WM_CANCELMODE, 0, 0);
        ASSERT_EQ(pwchar(f.edit), f.orig);
    }
    fix_free(&f);
    TEST_END();
}

int test_pw_reveal_win_release_outside_masks(void)
{
    TEST_BEGIN();
    PwFix f;
    ASSERT_TRUE(fix_make(&f, "s3cret"));
    ASSERT_NOT_NULL(f.eye);
    if (f.eye) {
        press(f.eye);
        ASSERT_EQ(pwchar(f.edit), 0);
        release_at(f.eye, -50, -50);
        ASSERT_EQ(pwchar(f.edit), f.orig);
    }
    fix_free(&f);
    TEST_END();
}

int test_pw_reveal_win_drag_off_masks_drag_back_reveals(void)
{
    TEST_BEGIN();
    PwFix f;
    ASSERT_TRUE(fix_make(&f, "s3cret"));
    ASSERT_NOT_NULL(f.eye);
    if (f.eye) {
        press(f.eye);
        SendMessageW(f.eye, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM((WORD)(short)-50, 2));
        ASSERT_EQ(pwchar(f.edit), f.orig);
        SendMessageW(f.eye, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(2, 2));
        ASSERT_EQ(pwchar(f.edit), 0);
        release_at(f.eye, 2, 2);
        ASSERT_EQ(pwchar(f.edit), f.orig);
    }
    fix_free(&f);
    TEST_END();
}

int test_pw_reveal_win_mask_all_masks_descendants(void)
{
    TEST_BEGIN();
    PwFix f;
    ASSERT_TRUE(fix_make(&f, "s3cret"));
    ASSERT_NOT_NULL(f.eye);
    if (f.eye) {
        press(f.eye);
        ASSERT_EQ(pwchar(f.edit), 0);
        pw_reveal_mask_all(f.top);
        ASSERT_EQ(pwchar(f.edit), f.orig);
        pw_reveal_mask_all(NULL);
    }
    fix_free(&f);
    TEST_END();
}

int test_pw_reveal_win_emptied_while_revealed_masks(void)
{
    TEST_BEGIN();
    PwFix f;
    ASSERT_TRUE(fix_make(&f, "s3cret"));
    ASSERT_NOT_NULL(f.eye);
    if (f.eye) {
        press(f.eye);
        ASSERT_EQ(pwchar(f.edit), 0);
        SetWindowTextA(f.edit, "");
        ASSERT_EQ(pw_reveal_is_revealed(f.edit), 0);
        ASSERT_EQ(pwchar(f.edit), f.orig);
    }
    fix_free(&f);
    TEST_END();
}

int test_pw_reveal_win_empty_field_never_reveals(void)
{
    TEST_BEGIN();
    PwFix f;
    ASSERT_TRUE(fix_make(&f, "s3cret"));
    ASSERT_NOT_NULL(f.eye);
    if (f.eye) {
        SetWindowTextA(f.edit, "");
        press(f.eye);
        ASSERT_EQ(pwchar(f.edit), f.orig);
        release_at(f.eye, 2, 2);
    }
    fix_free(&f);
    TEST_END();
}

static int clip_text_is(HWND owner, const char *want)
{
    int ok = 0;
    if (!OpenClipboard(owner)) return 0;
    HANDLE h = GetClipboardData(CF_TEXT);
    if (h) {
        const char *p = (const char *)GlobalLock(h);
        if (p) { ok = strcmp(p, want) == 0; GlobalUnlock(h); }
    }
    CloseClipboard();
    return ok;
}

int test_pw_reveal_win_copy_blocked_while_revealed(void)
{
    TEST_BEGIN();
    PwFix f;
    ASSERT_TRUE(fix_make(&f, "s3cret"));
    ASSERT_NOT_NULL(f.eye);
    if (f.eye) {
        const char *sentinel = "sentinel";
        size_t n = strlen(sentinel) + 1;
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n);
        ASSERT_NOT_NULL(g);
        if (g && OpenClipboard(f.top)) {
            char *p = (char *)GlobalLock(g);
            memcpy(p, sentinel, n);
            GlobalUnlock(g);
            EmptyClipboard();
            SetClipboardData(CF_TEXT, g);
            CloseClipboard();
        }
        ASSERT_TRUE(clip_text_is(f.top, sentinel));
        SendMessageW(f.edit, EM_SETSEL, 0, (LPARAM)-1);
        press(f.eye);
        SendMessageW(f.edit, WM_COPY, 0, 0);
        release_at(f.eye, 2, 2);
        ASSERT_TRUE(clip_text_is(f.top, sentinel));
    }
    fix_free(&f);
    TEST_END();
}

int test_pw_reveal_win_eye_tracks_resize_and_margin(void)
{
    TEST_BEGIN();
    PwFix f;
    ASSERT_TRUE(fix_make(&f, "s3cret"));
    ASSERT_NOT_NULL(f.eye);
    if (f.eye) {
        RECT er, cr;
        SetWindowPos(f.edit, NULL, 0, 0, 320, 30, SWP_NOMOVE | SWP_NOZORDER);
        GetClientRect(f.edit, &cr);
        GetWindowRect(f.eye, &er);
        MapWindowPoints(NULL, f.edit, (POINT *)&er, 2);
        ASSERT_EQ(er.right, cr.right);
        ASSERT_EQ(er.top, 0);
        ASSERT_EQ(er.bottom, cr.bottom);
        LRESULT m = SendMessageW(f.edit, EM_GETMARGINS, 0, 0);
        ASSERT_TRUE((int)HIWORD(m) >= (int)(er.right - er.left));
    }
    fix_free(&f);
    TEST_END();
}

int test_pw_reveal_win_destroy_while_revealed_is_clean(void)
{
    TEST_BEGIN();
    PwFix f;
    ASSERT_TRUE(fix_make(&f, "s3cret"));
    ASSERT_NOT_NULL(f.eye);
    if (f.eye) {
        press(f.eye);
        DestroyWindow(f.edit);
        ASSERT_EQ(IsWindow(f.eye), 0);
        HWND e2 = CreateWindowExA(0, "EDIT", "abc",
                                  WS_CHILD | WS_VISIBLE | ES_PASSWORD,
                                  10, 10, 200, 24, f.top, NULL,
                                  GetModuleHandle(NULL), NULL);
        ASSERT_NOT_NULL(e2);
        ASSERT_NOT_NULL(pw_reveal_attach(e2));
    }
    fix_free(&f);
    TEST_END();
}

#endif /* _WIN32 */
