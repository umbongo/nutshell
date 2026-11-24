/* src/ui/ns_slider.c -- see ns_slider.h. */
#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <stdlib.h>
#include "ns_slider.h"
#include "dpi_util.h"
#include "ns_draw.h"
#include "ns_hover.h"
#include "ns_slider_math.h"
#include "ns_scale.h"
#include "ns_tokens.h"
#include "ns_type.h"
#include "themed_button.h"

#define NS_SLIDER_CLASS "Nutshell_Slider"
#define NS_SLIDER_HOT_ID 1

typedef struct {
    int min, max, value;
    int dragging;
    int tracking;        /* a TrackMouseEvent(TME_LEAVE) is pending */
    NsHover hover;
} NsSlider;

typedef struct { int track_x, track_w, cy, thumb_d, track_h; } Geom;

static void geom(HWND hwnd, Geom *g)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int dpi = get_window_dpi(hwnd);
    g->thumb_d = ns_scale(SP_LG, dpi);
    g->track_h = ns_scale(SP_XS, dpi);
    g->track_x = g->thumb_d / 2;
    g->track_w = rc.right - g->thumb_d;
    if (g->track_w < 0) g->track_w = 0;
    g->cy = rc.bottom / 2;
}

static void notify(HWND hwnd)
{
    HWND parent = GetParent(hwnd);
    if (parent)
        SendMessage(parent, WM_COMMAND,
                    MAKEWPARAM(GetDlgCtrlID(hwnd), NSS_CHANGED), (LPARAM)hwnd);
}

static void set_value(HWND hwnd, NsSlider *s, int v, int send)
{
    if (v < s->min) v = s->min;
    if (v > s->max) v = s->max;
    if (v == s->value) return;
    s->value = v;
    InvalidateRect(hwnd, NULL, FALSE);
    if (send) notify(hwnd);
}

static void paint(HWND hwnd, HDC target, const NsSlider *s)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) return;
    const ThemeTokens *tk = ns_tokens();
    int dpi = get_window_dpi(hwnd);

    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = dc ? CreateCompatibleBitmap(target, w, h) : NULL;
    if (!dc || !bmp) {
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
        return;
    }
    HGDIOBJ old_bmp = SelectObject(dc, bmp);
    HBRUSH bg = CreateSolidBrush(theme_cr(tk->bg_primary.base));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    int enabled = IsWindowEnabled(hwnd) != 0;
    int hot = enabled && ns_hover_state_for(&s->hover, NS_SLIDER_HOT_ID) != 0;
    int focused = enabled && GetFocus() == hwnd;

    Geom g;
    geom(hwnd, &g);
    int tx = ns_slider_x_of(s->value, g.track_x, g.track_w, s->min, s->max);
    int radius = g.track_h / 2;

    RECT track = { g.track_x, g.cy - radius, g.track_x + g.track_w,
                   g.cy - radius + g.track_h };
    ns_draw_round_fill(dc, &track, radius, theme_cr(tk->raised.base), 255);
    if (tx > g.track_x) {
        RECT fill = track;
        fill.right = tx;
        ns_draw_round_fill(dc, &fill, radius,
                           theme_cr(enabled ? tk->accent.base
                                            : tk->accent.disabled), 255);
    }

    int r = g.thumb_d / 2;
    RECT th = { tx - r, g.cy - r, tx - r + g.thumb_d, g.cy - r + g.thumb_d };
    unsigned thumb_fill = !enabled ? tk->raised.disabled
                        : s->dragging ? tk->raised.pressed
                        : hot ? tk->raised.hover : tk->bg_primary.base;
    unsigned thumb_edge = !enabled ? tk->text_disabled
                        : (hot || s->dragging) ? tk->accent.hover
                                               : tk->accent.base;
    ns_draw_round_fill(dc, &th, r, theme_cr(thumb_fill), 255);
    RECT sr = { th.left + STROKE_RULE / 2, th.top + STROKE_RULE / 2,
                th.right - STROKE_RULE / 2, th.bottom - STROKE_RULE / 2 };
    ns_draw_round_stroke(dc, &sr, r, theme_cr(thumb_edge), STROKE_RULE);

    if (focused)
        ns_draw_focus_ring(dc, &rc, theme_cr(tk->focus), dpi);

    BitBlt(target, 0, 0, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

static void drag_to(HWND hwnd, NsSlider *s, int x)
{
    Geom g;
    geom(hwnd, &g);
    set_value(hwnd, s, ns_slider_value_at(x, g.track_x, g.track_w, s->min,
                                          s->max), 1);
}

static LRESULT CALLBACK slider_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    NsSlider *s = (NsSlider *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!s) return DefWindowProc(hwnd, msg, wp, lp);
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (hdc) paint(hwnd, hdc, s);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        if (wp) paint(hwnd, (HDC)wp, s);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_LBUTTONDOWN:
        if (!IsWindowEnabled(hwnd)) return 0;
        SetFocus(hwnd);
        SetCapture(hwnd);
        s->dragging = 1;
        ns_hover_press(&s->hover, NS_SLIDER_HOT_ID);
        drag_to(hwnd, s, GET_X_LPARAM(lp));
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_MOUSEMOVE:
        if (s->dragging) {
            drag_to(hwnd, s, GET_X_LPARAM(lp));
            return 0;
        }
        if (!s->tracking) {
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, hwnd, 0 };
            s->tracking = TrackMouseEvent(&t) != 0;
        }
        if (ns_hover_move(&s->hover, NS_SLIDER_HOT_ID).changed)
            InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_MOUSELEAVE:
        s->tracking = 0;
        if (ns_hover_leave(&s->hover).changed)
            InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP:
        if (s->dragging) ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        s->dragging = 0;
        ns_hover_release(&s->hover);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_KEYDOWN:
        if (!IsWindowEnabled(hwnd)) return 0;
        set_value(hwnd, s, ns_slider_key(s->value, (unsigned)wp, s->min,
                                         s->max), 1);
        return 0;
    case WM_SETFOCUS: case WM_KILLFOCUS: case WM_ENABLE:
    case WM_THEMECHANGED: case WM_DPICHANGED:
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    case WM_NCDESTROY:
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        free(s);
        break;
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static int register_class(void)
{
    WNDCLASSA probe;
    HINSTANCE inst = GetModuleHandle(NULL);
    if (GetClassInfoA(inst, NS_SLIDER_CLASS, &probe)) return 1;
    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc = slider_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = NS_SLIDER_CLASS;
    return RegisterClassA(&wc) != 0 ||
           GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND ns_slider_create(HWND parent, int id, int min, int max, int value)
{
    if (!register_class()) return NULL;
    if (max < min) max = min;
    NsSlider *s = (NsSlider *)calloc(1, sizeof *s);
    if (!s) return NULL;
    s->min = min;
    s->max = max;
    s->value = value < min ? min : (value > max ? max : value);
    ns_hover_init(&s->hover);
    HWND h = CreateWindowExA(0, NS_SLIDER_CLASS, "",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                             0, 0, 100, 20, parent, (HMENU)(UINT_PTR)id,
                             GetModuleHandle(NULL), NULL);
    if (!h) {
        free(s);
        return NULL;
    }
    SetWindowLongPtr(h, GWLP_USERDATA, (LONG_PTR)s);
    return h;
}

void ns_slider_set(HWND slider, int value)
{
    NsSlider *s = slider ? (NsSlider *)GetWindowLongPtr(slider, GWLP_USERDATA)
                         : NULL;
    if (s) set_value(slider, s, value, 0);
}

int ns_slider_get(HWND slider)
{
    NsSlider *s = slider ? (NsSlider *)GetWindowLongPtr(slider, GWLP_USERDATA)
                         : NULL;
    return s ? s->value : 0;
}
