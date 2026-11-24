/* tests/test_ns_draw_level_meter_win.c -- Win32 test for the zoned level
 * meter painter (ns_draw_level_meter in src/ui/ns_draw.c): paints into a
 * memory DC and samples pixels for the zone bands, the level fill and the
 * peak-hold tick. Built only by `make wintest`. */

#ifdef _WIN32

#include <windows.h>
#include "test_framework.h"
#include "ns_draw.h"
#include "ns_tokens.h"
#include "ui_theme.h"
#include "voice_level.h"

static COLORREF cr_of(unsigned int rgb)
{
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

static COLORREF band(unsigned int intent, const ThemeTokens *t)
{
    return rgb_alpha(cr_of(intent), cr_of(t->raised.base), 0.35f);
}

int test_ns_draw_level_meter_win_zones_fill_and_hold(void)
{
    TEST_BEGIN();
    ThemeTokens *tk = (ThemeTokens *)ns_tokens();
    ui_theme_resolve(ui_theme_get(0), tk);

    HDC screen = GetDC(NULL);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, 220, 40);
    HGDIOBJ old = SelectObject(dc, bmp);
    RECT all = { 0, 0, 220, 40 };
    HBRUSH bg = CreateSolidBrush(RGB(1, 2, 3));
    FillRect(dc, &all, bg);

    double zf[4] = { voice_meter_frac(VOICE_LEVEL_SILENT_DB),
                     voice_meter_frac(VOICE_LEVEL_GOOD_LO_DB),
                     voice_meter_frac(VOICE_LEVEL_GOOD_HI_DB),
                     voice_meter_frac(VOICE_LEVEL_LOUD_DB) };
    RECT bar = { 10, 10, 210, 30 };   /* 200 x 20; frac f is x = 10 + 200 f */
    const int top = 11, mid = 20;

    /* Empty, no tick: every row shows the bands, the fill is absent. */
    ns_draw_level_meter(dc, &bar, 0.0, 0.0, zf, tk);
    ASSERT_EQ((int)GetPixel(dc, 110, mid), (int)band(tk->warning.base, tk));
    ASSERT_EQ((int)GetPixel(dc, 160, mid), (int)band(tk->success.base, tk));
    ASSERT_EQ((int)GetPixel(dc, 195, top), (int)band(tk->warning.base, tk));
    ASSERT_EQ((int)GetPixel(dc, 207, top), (int)band(tk->danger.base, tk));
    ASSERT_TRUE((int)band(tk->success.base, tk) != (int)band(tk->warning.base, tk));

    /* A good level (0.8) with the tick at 0.9: success fill, tick on top. */
    FillRect(dc, &all, bg);
    ns_draw_level_meter(dc, &bar, 0.8, 0.9, zf, tk);
    ASSERT_EQ((int)GetPixel(dc, 110, mid), (int)cr_of(tk->success.base));   /* the whole fill takes the zone's intent */
    ASSERT_EQ((int)GetPixel(dc, 160, mid), (int)cr_of(tk->success.base));
    ASSERT_EQ((int)GetPixel(dc, 160, top), (int)band(tk->success.base, tk)); /* band above the inset fill */
    ASSERT_EQ((int)GetPixel(dc, 190, mid), (int)cr_of(tk->text_main));       /* hold tick at 0.9 */
    ASSERT_EQ((int)GetPixel(dc, 172, mid), (int)band(tk->success.base, tk)); /* past the fill */

    /* A loud level (0.99): danger fill. */
    FillRect(dc, &all, bg);
    ns_draw_level_meter(dc, &bar, 0.99, 0.0, zf, tk);
    ASSERT_EQ((int)GetPixel(dc, 207, mid), (int)cr_of(tk->danger.base));
    ASSERT_EQ((int)GetPixel(dc, 150, mid), (int)cr_of(tk->danger.base));   /* one colour end to end */

    /* Nothing is painted outside the bar. */
    ASSERT_EQ((int)GetPixel(dc, 5, mid), (int)RGB(1, 2, 3));
    ASSERT_EQ((int)GetPixel(dc, 215, mid), (int)RGB(1, 2, 3));

    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteObject(bg);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
    TEST_END();
}

#endif
