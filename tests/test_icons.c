/* tests/test_icons.c — Win32-only: renders every NsIconId into a
 * memory DC, counts non-background pixels, asserts each glyph
 * produces visible output.  Runs under Wine or on real Windows.
 * Excluded from the native Linux test build. */

#ifdef _WIN32

#include <windows.h>
#include "test_framework.h"
#include "icons.h"

/* Render `id` at `size` px into a memory DC filled with `bg` then
 * count pixels whose RGB distance from `bg` exceeds `thresh`. */
static int count_non_bg(NsIconId id, int size, COLORREF fg,
                        COLORREF bg, int thresh)
{
    HDC screen = GetDC(NULL);
    HDC mem    = CreateCompatibleDC(screen);

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth       = size;
    bmi.bmiHeader.biHeight      = -size;   /* top-down */
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(mem, bmp);

    RECT rc = { 0, 0, size, size };
    HBRUSH br = CreateSolidBrush(bg);
    FillRect(mem, &rc, br);
    DeleteObject(br);

    ns_icon_draw(mem, id, &rc, fg, 96);

    GdiFlush();

    int bg_r = GetRValue(bg), bg_g = GetGValue(bg), bg_b = GetBValue(bg);
    unsigned char *p = (unsigned char *)bits;
    int count = 0;
    for (int i = 0; i < size * size; i++) {
        int b = p[i * 4 + 0], g = p[i * 4 + 1], r = p[i * 4 + 2];
        int dr = r - bg_r, dg = g - bg_g, db = b - bg_b;
        if (dr * dr + dg * dg + db * db > thresh * thresh) count++;
    }

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    return count;
}

int test_icons_all_render_non_empty(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(ns_icons_init());

    for (int id = 0; id < NS_ICON_COUNT; id++) {
        int n = count_non_bg((NsIconId)id, 32, RGB(0, 0, 0),
                             RGB(255, 255, 255), 64);
        if (n <= 0) {
            printf("  icon id=%d rendered 0 non-bg pixels\n", id);
            _tf_local_fail = 1;
        }
    }

    ns_icons_shutdown();
    TEST_END();
}

int test_icons_render_at_multiple_sizes(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(ns_icons_init());

    int sizes[] = { 16, 32, 48 };
    for (int s = 0; s < 3; s++) {
        for (int id = 0; id < NS_ICON_COUNT; id++) {
            int n = count_non_bg((NsIconId)id, sizes[s], RGB(0, 0, 0),
                                 RGB(255, 255, 255), 64);
            if (n <= 0) {
                printf("  icon id=%d at %dpx rendered 0 px\n", id, sizes[s]);
                _tf_local_fail = 1;
            }
        }
    }

    ns_icons_shutdown();
    TEST_END();
}

#endif /* _WIN32 */
