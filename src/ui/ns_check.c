/* src/ui/ns_check.c -- see ns_check.h. */
#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <stdlib.h>
#include "ns_check.h"
#include "ns_check_geom.h"
#include "ns_draw.h"
#include "ns_hover.h"
#include "ns_scale.h"
#include "ns_tokens.h"
#include "ns_type.h"
#include "themed_button.h"

#define NS_CHECK_SUBCLASS_ID 0x4E43   /* 'NC' */
#define NS_CHECK_HOT_ID      1

typedef struct {
    NsHover hover;
    int     tracking;   /* a TrackMouseEvent(TME_LEAVE) is pending */
} NsCheck;

/* Repaint now, so whatever the native control painted for a state change is
 * overdrawn in the same message. */
static void redraw_now(HWND hwnd)
{
    RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
}

static int window_dpi(HWND hwnd)
{
    HDC dc = GetDC(hwnd);
    int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(hwnd, dc);
    return dpi > 0 ? dpi : 96;
}

static void paint(HWND hwnd, HDC target, const NsCheck *nc)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) return;

    const ThemeTokens *tk = ns_tokens();
    int dpi = window_dpi(hwnd);

    /* Double-buffered: no flicker between the background and the box. */
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = dc ? CreateCompatibleBitmap(target, w, h) : NULL;
    if (!dc || !bmp) {
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        return;
    }
    HGDIOBJ old_bmp = SelectObject(dc, bmp);

    COLORREF page = theme_cr(tk->bg_primary.base);
    HBRUSH bg = CreateSolidBrush(page);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    int enabled = IsWindowEnabled(hwnd) != 0;
    int checked = SendMessage(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
    int pressed = enabled && (SendMessage(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED);
    int hot = enabled &&
              ns_hover_state_for(&nc->hover, NS_CHECK_HOT_ID) != 0;
    int focused = enabled && GetFocus() == hwnd &&
                  !(SendMessage(hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);

    NsCheckLayout L;
    ns_check_layout(w, h, dpi, &L);
    RECT box = { L.box_x, L.box_y, L.box_x + L.box_w, L.box_y + L.box_h };

    COLORREF border = theme_cr(enabled ? tk->border : tk->text_disabled);
    if (checked) {
        unsigned int fill_rgb = !enabled ? tk->accent.disabled
                              : pressed  ? tk->accent.pressed
                              : hot      ? tk->accent.hover
                                         : tk->accent.base;
        COLORREF fill = theme_cr(fill_rgb);
        ns_draw_round_fill(dc, &box, L.radius, fill, 255);
        float xy[6];
        ns_check_mark(L.box_x, L.box_y, L.box_w, xy);
        ns_draw_polyline(dc, xy, 3, theme_cr(tk->accent.label),
                         ns_check_mark_width(dpi));
    } else {
        COLORREF inner = page;
        if (pressed)
            inner = rgb_alpha(theme_cr(tk->text_main), page, 0.16f);
        else if (hot)
            inner = rgb_alpha(theme_cr(tk->text_main), page, 0.08f);
        ns_draw_round_fill(dc, &box, L.radius, inner, 255);
        COLORREF stroke = (hot || pressed) && enabled
                              ? theme_cr(tk->accent.base) : border;
        ns_draw_round_stroke(dc, &box, L.radius, stroke,
                             hot || pressed ? STROKE_RULE : STROKE_HAIRLINE);
    }
    if (focused)
        ns_draw_focus_ring(dc, &box, theme_cr(tk->focus), dpi);

    WCHAR text[256];
    int n = GetWindowTextW(hwnd, text, (int)(sizeof text / sizeof text[0]));
    if (n > 0 && L.label_w > 0) {
        HFONT font = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
        HGDIOBJ old_font = font ? SelectObject(dc, font) : NULL;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, theme_cr(enabled ? tk->text_main : tk->text_disabled));
        RECT tr = { L.label_x, L.label_y, L.label_x + L.label_w,
                    L.label_y + L.label_h };
        DrawTextW(dc, text, n, &tr,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX |
                  DT_END_ELLIPSIS);
        if (old_font) SelectObject(dc, old_font);
    }

    BitBlt(target, 0, 0, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

static LRESULT CALLBACK check_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                   UINT_PTR id, DWORD_PTR ref)
{
    NsCheck *nc = (NsCheck *)ref;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;                      /* paint() covers the whole client */
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (hdc) paint(hwnd, hdc, nc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        if (wp) paint(hwnd, (HDC)wp, nc);
        return 0;
    case WM_MOUSEMOVE:
        if (!nc->tracking) {
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, hwnd, 0 };
            nc->tracking = TrackMouseEvent(&t) != 0;
        }
        if (ns_hover_move(&nc->hover, NS_CHECK_HOT_ID).changed)
            InvalidateRect(hwnd, NULL, FALSE);
        break;
    case WM_MOUSELEAVE:
        nc->tracking = 0;
        if (ns_hover_leave(&nc->hover).changed)
            InvalidateRect(hwnd, NULL, FALSE);
        break;
    case BM_SETCHECK: case BM_SETSTATE: case WM_ENABLE: case WM_SETTEXT:
    case WM_SETFONT: case WM_SETFOCUS: case WM_KILLFOCUS:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_KEYDOWN: case WM_KEYUP: case WM_CAPTURECHANGED:
    case WM_UPDATEUISTATE: case WM_THEMECHANGED: case BM_CLICK: {
        LRESULT r = DefSubclassProc(hwnd, msg, wp, lp);
        redraw_now(hwnd);
        return r;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, check_proc, id);
        free(nc);
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void ns_check_attach(HWND chk)
{
    if (!chk) return;
    DWORD_PTR existing = 0;
    if (GetWindowSubclass(chk, check_proc, NS_CHECK_SUBCLASS_ID, &existing))
        return;
    NsCheck *nc = (NsCheck *)calloc(1, sizeof *nc);
    if (!nc) return;
    ns_hover_init(&nc->hover);
    if (!SetWindowSubclass(chk, check_proc, NS_CHECK_SUBCLASS_ID,
                           (DWORD_PTR)nc))
        free(nc);
    else
        InvalidateRect(chk, NULL, FALSE);
}
