/* src/ui/ns_draw.c — the one drawing module: rounded rects, chips, pulses,
 * cards, buttons, icon+label, separators and focus rings. GDI+ where
 * anti-aliasing matters (rounded-rect fill/stroke), plain GDI otherwise.
 * See ns_draw.h and docs/superpowers/specs/2026-09-07-design-system-
 * foundation-design.md section 3.
 */

#ifdef _WIN32

#include "ns_draw.h"
#include "ns_scale.h"
#include "ns_type.h"
#include <gdiplus.h>
#include <math.h>
#include <string.h>

/* ── Colour helpers ─────────────────────────────────────────────────── */

/* Theme tokens are packed 0x00RRGGBB; GDI wants 0x00BBGGRR. */
#define NS_CR(rgb) RGB(((rgb) >> 16) & 0xFF, ((rgb) >> 8) & 0xFF, (rgb) & 0xFF)

static ARGB argb_of(COLORREF c, BYTE a)
{
    return ((ARGB)a << 24)
         | ((ARGB)GetRValue(c) << 16)
         | ((ARGB)GetGValue(c) <<  8)
         | ((ARGB)GetBValue(c));
}

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

/* ── UTF-8 text draw (shared by ns_draw_chip / ns_draw_button /
 *    ns_draw_icon_label) ──────────────────────────────────────────── */

static void draw_utf8_text(HDC hdc, const RECT *rc, const char *text,
                            UINT flags)
{
    if (!text || !*text) return;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (wlen <= 0) return;

    wchar_t stack_buf[128];
    wchar_t *wbuf = (wlen <= (int)(sizeof stack_buf / sizeof stack_buf[0]))
                    ? stack_buf
                    : (wchar_t *)LocalAlloc(LMEM_FIXED,
                                            (SIZE_T)wlen * sizeof(wchar_t));
    if (!wbuf) return;

    MultiByteToWideChar(CP_UTF8, 0, text, -1, wbuf, wlen);
    RECT tr = *rc;
    DrawTextW(hdc, wbuf, -1, &tr, flags);
    if (wbuf != stack_buf) LocalFree(wbuf);
}

/* ── Rounded-rect path + fill/stroke (GDI+, anti-aliased) ──────────── */

static void build_round_rect_path(GpPath *path, float left, float top,
                                   float right, float bottom, float radius)
{
    float w = right - left;
    float h = bottom - top;
    float d = radius * 2.0f;
    if (d < 0.0f) d = 0.0f;
    if (d > w) d = w;
    if (d > h) d = h;

    GdipAddPathArc(path, left, top, d, d, 180.0f, 90.0f);
    GdipAddPathArc(path, right - d, top, d, d, 270.0f, 90.0f);
    GdipAddPathArc(path, right - d, bottom - d, d, d, 0.0f, 90.0f);
    GdipAddPathArc(path, left, bottom - d, d, d, 90.0f, 90.0f);
    GdipClosePathFigure(path);
}

void ns_draw_round_fill(HDC hdc, const RECT *rc, int radius_px,
                         COLORREF fill, BYTE alpha)
{
    if (!hdc || !rc) return;
    if (radius_px < 0) radius_px = 0;

    GpGraphics *g = NULL;
    if (GdipCreateFromHDC(hdc, &g) != 0) return;
    GdipSetSmoothingMode(g, SmoothingModeAntiAlias);

    GpPath *path = NULL;
    GdipCreatePath(FillModeAlternate, &path);
    build_round_rect_path(path, (float)rc->left, (float)rc->top,
                          (float)rc->right, (float)rc->bottom,
                          (float)radius_px);

    GpSolidFill *brush = NULL;
    GdipCreateSolidFill(argb_of(fill, alpha), &brush);
    GdipFillPath(g, (GpBrush *)brush, path);

    GdipDeleteBrush((GpBrush *)brush);
    GdipDeletePath(path);
    GdipDeleteGraphics(g);
}

void ns_draw_round_stroke(HDC hdc, const RECT *rc, int radius_px,
                           COLORREF stroke, int width_px)
{
    if (!hdc || !rc) return;
    if (radius_px < 0) radius_px = 0;
    if (width_px < 1) width_px = 1;

    GpGraphics *g = NULL;
    if (GdipCreateFromHDC(hdc, &g) != 0) return;
    GdipSetSmoothingMode(g, SmoothingModeAntiAlias);

    GpPath *path = NULL;
    GdipCreatePath(FillModeAlternate, &path);
    build_round_rect_path(path, (float)rc->left, (float)rc->top,
                          (float)rc->right, (float)rc->bottom,
                          (float)radius_px);

    GpPen *pen = NULL;
    GdipCreatePen1(argb_of(stroke, 255), (REAL)width_px, UnitPixel, &pen);
    GdipDrawPath(g, pen, path);

    GdipDeletePen(pen);
    GdipDeletePath(path);
    GdipDeleteGraphics(g);
}

/* ── Card ───────────────────────────────────────────────────────────── */

void ns_draw_card(HDC hdc, const RECT *rc, const ThemeTokens *tokens,
                   int dpi)
{
    if (!hdc || !rc || !tokens) return;

    int radius = ns_scale(R_CARD, dpi);
    ns_draw_round_fill(hdc, rc, radius, NS_CR(tokens->raised.base), 255);
    ns_draw_round_stroke(hdc, rc, radius, NS_CR(tokens->border),
                         STROKE_HAIRLINE);
}

/* ── Button ─────────────────────────────────────────────────────────── */

void ns_draw_button(HDC hdc, const RECT *rc, const ThemeSurface *surface,
                     NsBtnState state, int focused, const char *label,
                     HFONT font, int dpi)
{
    if (!hdc || !rc || !surface) return;

    unsigned int fill_rgb;
    switch (state) {
    case NS_BTN_HOVER:    fill_rgb = surface->hover;    break;
    case NS_BTN_PRESSED:  fill_rgb = surface->pressed;  break;
    case NS_BTN_DISABLED: fill_rgb = surface->disabled; break;
    case NS_BTN_REST:
    default:              fill_rgb = surface->base;     break;
    }

    int radius = ns_scale(R_CTRL, dpi);
    COLORREF fill_cr = NS_CR(fill_rgb);
    ns_draw_round_fill(hdc, rc, radius, fill_cr, 255);

    COLORREF label_cr = NS_CR(surface->label);
    if (state == NS_BTN_DISABLED)
        label_cr = rgb_alpha(label_cr, fill_cr, 0.5f);

    if (label && *label) {
        HFONT old_font = font ? (HFONT)SelectObject(hdc, font) : NULL;
        int old_bk = SetBkMode(hdc, TRANSPARENT);
        COLORREF old_tc = SetTextColor(hdc, label_cr);

        draw_utf8_text(hdc, rc, label,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SetBkMode(hdc, old_bk);
        SetTextColor(hdc, old_tc);
        if (old_font) SelectObject(hdc, old_font);
    }

    if (focused)
        ns_draw_focus_ring(hdc, rc, NS_CR(surface->label), dpi);
}

/* ── Icon + label ───────────────────────────────────────────────────── */

void ns_draw_icon_label(HDC hdc, const RECT *rc, NsIconId icon,
                         const char *label, COLORREF colour, HFONT font,
                         int dpi)
{
    if (!hdc || !rc) return;

    int icon_sz = ns_scale(SZ_ICON, dpi);
    int gap = ns_scale(SP_SM, dpi);

    RECT icon_rc;
    icon_rc.left = rc->left;
    icon_rc.top = rc->top + ((rc->bottom - rc->top) - icon_sz) / 2;
    icon_rc.right = icon_rc.left + icon_sz;
    icon_rc.bottom = icon_rc.top + icon_sz;
    ns_icon_draw(hdc, icon, &icon_rc, colour, (UINT)dpi);

    if (label && *label) {
        RECT label_rc = *rc;
        label_rc.left = icon_rc.right + gap;

        HFONT old_font = font ? (HFONT)SelectObject(hdc, font) : NULL;
        int old_bk = SetBkMode(hdc, TRANSPARENT);
        COLORREF old_tc = SetTextColor(hdc, colour);

        draw_utf8_text(hdc, &label_rc, label,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SetBkMode(hdc, old_bk);
        SetTextColor(hdc, old_tc);
        if (old_font) SelectObject(hdc, old_font);
    }
}

/* ── Separator ──────────────────────────────────────────────────────── */

void ns_draw_separator(HDC hdc, int x1, int x2, int y, COLORREF colour)
{
    if (!hdc) return;

    RECT rc = { x1, y, x2, y + STROKE_HAIRLINE };
    HBRUSH br = CreateSolidBrush(colour);
    FillRect(hdc, &rc, br);
    DeleteObject(br);
}

/* ── Focus ring ─────────────────────────────────────────────────────── */

void ns_draw_focus_ring(HDC hdc, const RECT *rc, COLORREF colour, int dpi)
{
    if (!hdc || !rc) return;

    RECT inset = { rc->left + 1, rc->top + 1, rc->right - 1, rc->bottom - 1 };
    ns_draw_round_stroke(hdc, &inset, ns_scale(R_CTRL, dpi), colour,
                         STROKE_RULE);
}

/* ── Chip ───────────────────────────────────────────────────────────── */

void ns_draw_chip(HDC hdc, const RECT *rc, COLORREF bg, COLORREF fg,
                   HFONT font, const char *text)
{
    if (!hdc || !rc) return;

    int radius = (rc->bottom - rc->top) / 2;
    ns_draw_round_fill(hdc, rc, radius, bg, 255);

    if (!text || !*text) return;

    HFONT oldfont = font ? (HFONT)SelectObject(hdc, font) : NULL;
    int old_bk = SetBkMode(hdc, TRANSPARENT);
    COLORREF old_tc = SetTextColor(hdc, fg);

    draw_utf8_text(hdc, rc, text,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SetBkMode(hdc, old_bk);
    SetTextColor(hdc, old_tc);
    if (oldfont) SelectObject(hdc, oldfont);
}

/* ── Status pulse ───────────────────────────────────────────────────── */

void ns_draw_pulse(HDC hdc, const RECT *rc, COLORREF colour, float phase)
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
