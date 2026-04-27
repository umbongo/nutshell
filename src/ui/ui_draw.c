/* src/ui/ui_draw.c — GDI helpers: chips, pulses, alpha blending. */

#ifdef _WIN32

#include "ui_draw.h"
#include <math.h>
#include <string.h>

/* Blend fg toward bg by alpha (1.0 = fg, 0.0 = bg). */
COLORREF rgb_alpha(COLORREF fg, COLORREF bg, float alpha)
{
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    BYTE fr = GetRValue(fg), fgn = GetGValue(fg), fb = GetBValue(fg);
    BYTE br = GetRValue(bg), bgn = GetGValue(bg), bb = GetBValue(bg);
    BYTE r = (BYTE)((float)fr * alpha + (float)br * (1.0f - alpha));
    BYTE g = (BYTE)((float)fgn * alpha + (float)bgn * (1.0f - alpha));
    BYTE b = (BYTE)((float)fb * alpha + (float)bb * (1.0f - alpha));
    return RGB(r, g, b);
}

void draw_chip(HDC hdc, const RECT *rc, COLORREF bg, COLORREF fg,
               HFONT font, const char *text)
{
    if (!hdc || !rc) return;

    int radius = (rc->bottom - rc->top) / 2;
    if (radius < 2) radius = 2;

    HBRUSH br = CreateSolidBrush(bg);
    HPEN   pen = CreatePen(PS_NULL, 0, 0);
    HBRUSH oldbr = (HBRUSH)SelectObject(hdc, br);
    HPEN   oldpen = (HPEN)SelectObject(hdc, pen);

    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom,
              radius * 2, radius * 2);

    SelectObject(hdc, oldbr);
    SelectObject(hdc, oldpen);
    DeleteObject(br);
    DeleteObject(pen);

    if (!text || !*text) return;

    HFONT oldfont = NULL;
    if (font) oldfont = (HFONT)SelectObject(hdc, font);

    int old_bk = SetBkMode(hdc, TRANSPARENT);
    COLORREF old_tc = SetTextColor(hdc, fg);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (wlen > 0) {
        wchar_t stack_buf[128];
        wchar_t *wbuf = (wlen <= (int)(sizeof stack_buf / sizeof stack_buf[0]))
                        ? stack_buf
                        : (wchar_t *)LocalAlloc(LMEM_FIXED,
                                                (SIZE_T)wlen * sizeof(wchar_t));
        if (wbuf) {
            MultiByteToWideChar(CP_UTF8, 0, text, -1, wbuf, wlen);
            RECT tr = *rc;
            DrawTextW(hdc, wbuf, -1, &tr,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (wbuf != stack_buf) LocalFree(wbuf);
        }
    }

    SetBkMode(hdc, old_bk);
    SetTextColor(hdc, old_tc);
    if (oldfont) SelectObject(hdc, oldfont);
}

void draw_status_pulse(HDC hdc, const RECT *rc, COLORREF colour, float phase)
{
    if (!hdc || !rc) return;

    float mod = (sinf(phase) + 1.0f) * 0.5f;   /* [0, 1] */
    int pad = 3 + (int)(mod * 4.0f);           /* 3..7 px outer offset */

    RECT r = { rc->left - pad, rc->top - pad,
               rc->right + pad, rc->bottom + pad };
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;

    /* 2 px ring — draw two circles, outer colour and inner cut-out.
     * Use a pen for the ring so we don't need alpha blending. */
    HPEN pen = CreatePen(PS_SOLID, 1, colour);
    HPEN oldpen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldbr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    Ellipse(hdc, r.left, r.top, r.right, r.bottom);

    SelectObject(hdc, oldpen);
    SelectObject(hdc, oldbr);
    DeleteObject(pen);
}

#endif /* _WIN32 */
