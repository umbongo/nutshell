#ifndef NUTSHELL_CUSTOM_SCROLLBAR_H
#define NUTSHELL_CUSTOM_SCROLLBAR_H

#ifdef _WIN32
#include <windows.h>
#include "ui_theme.h"
#include "edit_scroll.h"

/*
 * Custom themed scrollbar control — replaces WS_VSCROLL with an owner-drawn
 * child window that respects the app's colour scheme.
 *
 * Track:  bg_secondary
 * Thumb:  text_dim (hover: accent)
 * Arrows: none (modern flat style)
 *
 * Usage:
 *   csb_register(hInstance);
 *   HWND hSb = csb_create(hParent, x, y, w, h, theme);
 *   csb_set_range(hSb, nMin, nMax, nPage);
 *   csb_set_pos(hSb, nPos);
 *   int pos = csb_get_trackpos(hSb);
 *
 * The control sends WM_VSCROLL messages to its parent, with the same
 * notification codes (SB_LINEUP, SB_LINEDOWN, SB_PAGEUP, SB_PAGEDOWN,
 * SB_THUMBTRACK, SB_THUMBPOSITION, SB_TOP, SB_BOTTOM) so existing
 * WM_VSCROLL handlers work unchanged.
 */

#define CSB_CLASS_NAME "Nutshell_CustomScrollbar"
#define CSB_WIDTH 14  /* px — slim modern scrollbar */

typedef struct {
    int nMin;
    int nMax;
    int nPage;
    int nPos;
    /* Internal state */
    int thumb_y;       /* top of thumb in client coords */
    int thumb_h;       /* thumb height in px */
    int dragging;      /* 1 if thumb is being dragged */
    int drag_offset;   /* mouse Y offset from thumb top when drag started */
    int hovering;      /* 1 if mouse is over the thumb */
    int tracking;      /* 1 if TrackMouseEvent is active */
    const ThemeColors *theme;
} CsbData;

/* Convert 0xRRGGBB to COLORREF (duplicated here to keep header standalone) */
static inline COLORREF csb_cr(unsigned int rgb)
{
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

/* Recalculate thumb position and height from scroll range */
static inline void csb_recalc(CsbData *d, int client_h)
{
    if (client_h <= 0) { d->thumb_y = 0; d->thumb_h = 0; return; }

    int range = d->nMax - d->nMin + 1;
    if (range <= d->nPage || range <= 0) {
        /* No scrollable content — thumb fills track (or hide) */
        d->thumb_y = 0;
        d->thumb_h = client_h;
        return;
    }

    /* Thumb height proportional to visible page */
    d->thumb_h = (int)((long long)d->nPage * client_h / range);
    if (d->thumb_h < 20) d->thumb_h = 20;  /* minimum grab size */
    if (d->thumb_h > client_h) d->thumb_h = client_h;

    /* Thumb position */
    int track_range = client_h - d->thumb_h;
    int scroll_range = range - d->nPage;
    if (scroll_range > 0 && track_range > 0) {
        d->thumb_y = (int)((long long)(d->nPos - d->nMin) * track_range
                           / scroll_range);
        if (d->thumb_y < 0) d->thumb_y = 0;
        if (d->thumb_y > track_range) d->thumb_y = track_range;
    } else {
        d->thumb_y = 0;
    }
}

/* Compute nPos from a thumb pixel position */
static inline int csb_pos_from_thumb(CsbData *d, int client_h, int thumb_top)
{
    int track_range = client_h - d->thumb_h;
    int range = d->nMax - d->nMin + 1;
    int scroll_range = range - d->nPage;

    if (track_range <= 0 || scroll_range <= 0)
        return d->nMin;

    int pos = d->nMin + (int)((long long)thumb_top * scroll_range / track_range);
    if (pos < d->nMin) pos = d->nMin;
    if (pos > d->nMin + scroll_range) pos = d->nMin + scroll_range;
    return pos;
}

static inline void csb_paint(HWND hwnd, CsbData *d)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int h = rc.bottom - rc.top;

    csb_recalc(d, h);

    /* Track background */
    HBRUSH hTrack = CreateSolidBrush(csb_cr(d->theme->bg_secondary));
    FillRect(hdc, &rc, hTrack);
    DeleteObject(hTrack);

    /* 1px left border */
    HPEN hBorderPen = CreatePen(PS_SOLID, 1, csb_cr(d->theme->border));
    HGDIOBJ oldPen = SelectObject(hdc, hBorderPen);
    MoveToEx(hdc, 0, 0, NULL);
    LineTo(hdc, 0, h);
    SelectObject(hdc, oldPen);
    DeleteObject(hBorderPen);

    /* Thumb — only draw if there's scrollable content */
    int range = d->nMax - d->nMin + 1;
    if (range > d->nPage && d->thumb_h < h) {
        unsigned int thumb_rgb = (d->hovering || d->dragging)
                                     ? d->theme->accent
                                     : d->theme->text_dim;
        HBRUSH hThumb = CreateSolidBrush(csb_cr(thumb_rgb));
        /* Inset thumb by 2px on left/right for a floating look */
        RECT tr = { 2, d->thumb_y, rc.right - 2, d->thumb_y + d->thumb_h };
        /* Rounded rect for modern feel */
        HGDIOBJ oldBr = SelectObject(hdc, hThumb);
        HPEN hNullPen = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ oldPen2 = SelectObject(hdc, hNullPen);
        RoundRect(hdc, tr.left, tr.top, tr.right, tr.bottom, 6, 6);
        SelectObject(hdc, oldPen2);
        SelectObject(hdc, oldBr);
        DeleteObject(hNullPen);
        DeleteObject(hThumb);
    }

    EndPaint(hwnd, &ps);
}

static inline LRESULT CALLBACK csb_wndproc(HWND hwnd, UINT msg,
                                            WPARAM wParam, LPARAM lParam)
{
    CsbData *d = (CsbData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_PAINT:
        if (d) csb_paint(hwnd, d);
        else   DefWindowProc(hwnd, msg, wParam, lParam);
        return 0;

    case WM_ERASEBKGND:
        return 1;  /* we fill the whole area in WM_PAINT */

    case WM_LBUTTONDOWN: {
        if (!d) break;
        SetCapture(hwnd);
        int my = (int)(short)HIWORD(lParam);
        HWND hParent = GetParent(hwnd);

        if (my < d->thumb_y) {
            /* Click above thumb — page up */
            SendMessage(hParent, WM_VSCROLL, SB_PAGEUP, (LPARAM)hwnd);
        } else if (my >= d->thumb_y + d->thumb_h) {
            /* Click below thumb — page down */
            SendMessage(hParent, WM_VSCROLL, SB_PAGEDOWN, (LPARAM)hwnd);
        } else {
            /* On thumb — start drag */
            d->dragging = 1;
            d->drag_offset = my - d->thumb_y;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!d) break;
        int my = (int)(short)HIWORD(lParam);

        /* Enable hover tracking */
        if (!d->tracking) {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
            d->tracking = 1;
        }

        if (d->dragging) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int h = rc.bottom - rc.top;
            int new_top = my - d->drag_offset;
            if (new_top < 0) new_top = 0;
            if (new_top > h - d->thumb_h) new_top = h - d->thumb_h;

            d->nPos = csb_pos_from_thumb(d, h, new_top);
            csb_recalc(d, h);
            InvalidateRect(hwnd, NULL, FALSE);

            /* Send SB_THUMBTRACK to parent */
            HWND hParent = GetParent(hwnd);
            SendMessage(hParent, WM_VSCROLL,
                        MAKEWPARAM(SB_THUMBTRACK, 0), (LPARAM)hwnd);
        } else {
            /* Check hover state */
            int was_hovering = d->hovering;
            d->hovering = (my >= d->thumb_y && my < d->thumb_y + d->thumb_h);
            if (d->hovering != was_hovering)
                InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        if (d) {
            d->tracking = 0;
            if (d->hovering) {
                d->hovering = 0;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (d && d->dragging) {
            d->dragging = 0;
            ReleaseCapture();
            /* Send SB_THUMBPOSITION (final) to parent */
            HWND hParent = GetParent(hwnd);
            SendMessage(hParent, WM_VSCROLL,
                        MAKEWPARAM(SB_THUMBPOSITION, 0), (LPARAM)hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        } else {
            ReleaseCapture();
        }
        return 0;

    case WM_DESTROY:
        if (d) {
            free(d);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* Register the custom scrollbar window class. Call once at startup. */
static inline void csb_register(HINSTANCE hInst)
{
    WNDCLASSEX wc = {0};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.lpfnWndProc   = csb_wndproc;
    wc.hInstance      = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = CSB_CLASS_NAME;
    RegisterClassEx(&wc);
}

/* Create the custom scrollbar as a child of hParent. */
static inline HWND csb_create(HWND hParent, int x, int y, int w, int h,
                               const ThemeColors *theme, HINSTANCE hInst)
{
    CsbData *d = (CsbData *)calloc(1, sizeof(CsbData));
    if (!d) return NULL;
    d->theme = theme;
    d->nPage = 1;

    HWND hwnd = CreateWindowEx(
        0, CSB_CLASS_NAME, NULL,
        WS_CHILD | WS_VISIBLE,
        x, y, w, h,
        hParent, NULL, hInst, NULL);

    if (hwnd)
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)d);
    else
        free(d);

    return hwnd;
}

/* Update the scroll range (nMin, nMax, nPage). Repaints automatically. */
static inline void csb_set_range(HWND hwnd, int nMin, int nMax, int nPage)
{
    CsbData *d = (CsbData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    d->nMin  = nMin;
    d->nMax  = nMax;
    d->nPage = nPage;
    InvalidateRect(hwnd, NULL, FALSE);
}

/* Set the current scroll position. Repaints automatically. */
static inline void csb_set_pos(HWND hwnd, int nPos)
{
    CsbData *d = (CsbData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    d->nPos = nPos;
    InvalidateRect(hwnd, NULL, FALSE);
}

/* Get the current track position (use in WM_VSCROLL SB_THUMBTRACK handler). */
static inline int csb_get_trackpos(HWND hwnd)
{
    CsbData *d = (CsbData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return 0;
    return d->nPos;
}

/* Update the theme pointer (e.g. after settings change). */
static inline void csb_set_theme(HWND hwnd, const ThemeColors *theme)
{
    CsbData *d = (CsbData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    d->theme = theme;
    InvalidateRect(hwnd, NULL, FALSE);
}

/* Sync a custom scrollbar with a multiline EDIT/RichEdit control.
 * Reads the edit's line count and first visible line, then updates
 * the scrollbar range and position. */
static inline void csb_sync_edit(HWND hEdit, HWND hScrollbar, int line_h)
{
    if (!hEdit || !hScrollbar) return;
    int total = (int)SendMessage(hEdit, EM_GETLINECOUNT, 0, 0);
    int first = (int)SendMessage(hEdit, EM_GETFIRSTVISIBLELINE, 0, 0);
    RECT erc;
    GetClientRect(hEdit, &erc);
    int eh = erc.bottom - erc.top;
    int nMin, nMax, nPage, nPos;
    edit_scroll_params(total, first, eh, line_h,
                       &nMin, &nMax, &nPage, &nPos);
    csb_set_range(hScrollbar, nMin, nMax, nPage);
    csb_set_pos(hScrollbar, nPos);
}

#endif /* _WIN32 */
#endif /* NUTSHELL_CUSTOM_SCROLLBAR_H */
