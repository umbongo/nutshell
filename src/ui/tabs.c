#include "tabs.h"
#include "app_font.h"
#include "ns_font.h"
#include "ns_scale.h"
#include "ui_theme.h"
#include "xmalloc.h"
#include "logger.h"
#include "tooltip.h"
#include <stdio.h>
#include <commctrl.h>
#include "dpi_util.h"
#include "icons.h"
#include "ns_draw.h"
#include "ns_tokens.h"
#include "ns_hover.h"
#include "ns_reduced_motion.h"

#ifdef _WIN32

static const char *TABS_CLASS_NAME = "Nutshell_Tabs";

/* ---- Layout constants (base values at 96 DPI) ---------------------------- */
#define TAB_V_PAD_BASE    12   /* vertical inset above/below tab strip */
#define BTN_SIZE_BASE     24
#define TAB_GAP_BASE      8
#define BTN_GAP_BASE      2
#define INDICATOR_W_BASE  12
#define INDICATOR_GAP_BASE 4
#define CLOSE_SIZE_BASE   12
#define TAB_MIN_W_BASE    100
#define ACCENT_BAR_H_BASE 3
#define PAD_BASE          8   /* left margin before [+] button */
#define TAB_START_GAP_BASE 12  /* gap between [+] and first tab */

/* TAB_OVERHEAD and TAB_START_X as scaled expressions -- ns_scale(base, dpi)
 * inside functions that have `data` in scope, per the Design-System
 * Foundation's one DPI-scaling helper (task 10; replaces the old S() macro). */
#define TAB_OVERHEAD_S (ns_scale(INDICATOR_GAP_BASE, data->dpi) + ns_scale(INDICATOR_W_BASE, data->dpi) + ns_scale(INDICATOR_GAP_BASE, data->dpi) \
                        + ns_scale(INDICATOR_W_BASE, data->dpi) + ns_scale(INDICATOR_GAP_BASE, data->dpi) + ns_scale(CLOSE_SIZE_BASE, data->dpi) + ns_scale(10, data->dpi))
#define TAB_START_X_S  (ns_scale(PAD_BASE, data->dpi) + ns_scale(BTN_SIZE_BASE, data->dpi) + ns_scale(TAB_START_GAP_BASE, data->dpi))

/* Return the pixel width needed to show title in full. */
static int tab_w_s(HDC hdc, const char *title, int overhead, int min_w)
{
    SIZE sz = {0, 0};
    if (title && title[0])
        GetTextExtentPoint32A(hdc, title, (int)strlen(title), &sz);
    int w = sz.cx + overhead;
    return w < min_w ? min_w : w;
}

typedef struct TabControlData {
    TabManager m;

    TabSelectCallback    on_select;
    TabNewCallback       on_new;
    TabCloseCallback     on_close;
    TabSettingsCallback  on_settings;
    TabLogToggleCallback on_log_toggle;
    TabAiCallback        on_ai;
    TabStatusClickCallback on_status_click;

    HFONT hFont;
    HFONT hSmallFont;  /* cached small font for indicator labels */
    HWND  hTooltip;    /* Win32 tooltip control */
    int   ai_active;   /* 1 = API key configured -> green, 0 = grey */
    int   dpi;         /* per-window DPI for layout scaling */
    char  font_name[64];
    const ThemeColors *theme;
    float pulse_phase; /* 0..2*PI — advanced by WM_TIMER for connecting tabs */
    NsHover hover;     /* hot/pressed element id -- see TABS_HOVER_* below */
    BOOL  tracking_mouse; /* TRUE while TrackMouseEvent is armed */
} TabControlData;

/* ns_hover element ids for the tab strip's painted controls (none of
 * these are child windows, so they go through ns_hover like the
 * approval card in chat_listview.c does -- see
 * docs/superpowers/specs/2026-09-07-design-system-foundation-design.md
 * section 3). The right-side/add buttons keep small fixed ids; each tab
 * gets two ids (its background and its close glyph) so the two can hot
 * independently. */
enum {
    TABS_HOVER_ADD = 0,
    TABS_HOVER_LEFT,
    TABS_HOVER_RIGHT,
    TABS_HOVER_AI
};
#define TABS_HOVER_TAB_BASE   10
#define TABS_HOVER_TAB(i)     (TABS_HOVER_TAB_BASE + (i) * 2)
#define TABS_HOVER_CLOSE(i)   (TABS_HOVER_TAB_BASE + (i) * 2 + 1)

#define TABS_TIMER_PULSE  0x7501
#define TABS_PULSE_MS     50

/* Convert 0xRRGGBB to COLORREF (0x00BBGGRR) */
static COLORREF tc(unsigned int rgb)
{
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

/* Return the COLORREF for a connection-status dot, from ns_tokens()'s
 * intents (Design-System Foundation, task 10): connecting -> warning,
 * connected -> success, disconnected -> danger, idle -> text_dim. */
static COLORREF status_color(TabStatus s)
{
    const ThemeTokens *tok = ns_tokens();
    switch (s) {
        case TAB_CONNECTING:    return tc(tok->warning.base);
        case TAB_CONNECTED:     return tc(tok->success.base);
        case TAB_DISCONNECTED:  return tc(tok->danger.base);
        case TAB_IDLE: /* fall-through */
        default:                return tc(tok->text_dim);
    }
}

/* (Re-)fetch hFont and hSmallFont from the ns_font cache for font_name/dpi.
 * The cache owns both handles — nothing here to delete. */
static void tabs_create_fonts(TabControlData *data, int dpi)
{
    data->dpi = dpi;
    data->hFont      = ns_font(FONT_BODY, dpi);
    /* Small caption font for indicator labels ("L", "AI") on the tab strip. */
    data->hSmallFont = ns_font(FONT_CAPTION, dpi);
}

/* Removed manual draw_chip_icon logic in favour of Fluent icons */

/* Rect of one of the small fixed right-side/add buttons (TABS_HOVER_ADD/
 * LEFT/RIGHT/AI), matching the paint loop's geometry exactly. */
static void tabs_rect_for_btn(HWND hwnd, TabControlData *data, int btn_id,
                              RECT *out)
{
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    int btnSz = ns_scale(BTN_SIZE_BASE, data->dpi);
    int pad   = ns_scale(PAD_BASE, data->dpi);
    int btnGap = ns_scale(BTN_GAP_BASE, data->dpi);
    int btnY  = (rcClient.bottom - btnSz) / 2;
    int aiX    = rcClient.right - btnSz - pad;
    int rightX = aiX - btnSz - btnGap;
    int leftX  = rightX - btnSz - btnGap;

    switch (btn_id) {
    case TABS_HOVER_ADD:   SetRect(out, pad, btnY, pad + btnSz, btnY + btnSz); break;
    case TABS_HOVER_LEFT:  SetRect(out, leftX, btnY, leftX + btnSz, btnY + btnSz); break;
    case TABS_HOVER_RIGHT: SetRect(out, rightX, btnY, rightX + btnSz, btnY + btnSz); break;
    case TABS_HOVER_AI:    SetRect(out, aiX, btnY, aiX + btnSz, btnY + btnSz); break;
    default:               SetRectEmpty(out); break;
    }
}

/* Rect of tab `index`'s background (want_close = 0) or its close glyph
 * (want_close = 1). Mirrors the paint loop's geometry exactly -- tab
 * widths depend on each title, so this walks from the first tab like
 * paint and the click hit-test do. Returns 0 (rect zeroed) if `index` is
 * out of range, e.g. the tab was closed since the id was captured. */
static int tabs_rect_for_tab(HWND hwnd, TabControlData *data, int index,
                             int want_close, RECT *out)
{
    SetRectEmpty(out);
    if (!data || index < 0 || index >= data->m.count) return 0;

    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    int overhead = TAB_OVERHEAD_S;
    int minW     = ns_scale(TAB_MIN_W_BASE, data->dpi);
    int tabVPad  = ns_scale(TAB_V_PAD_BASE, data->dpi);
    int tabH     = rcClient.bottom - tabVPad;
    int tabY     = tabVPad / 2;
    int closeSz  = ns_scale(CLOSE_SIZE_BASE, data->dpi);
    int pad      = ns_scale(PAD_BASE, data->dpi);
    int x        = TAB_START_X_S;

    HDC hdc = GetDC(hwnd);
    HFONT hOld = (HFONT)SelectObject(hdc, data->hFont);
    int tw = 0;
    for (int i = 0; i <= index; i++) {
        tw = tab_w_s(hdc, data->m.tabs[i].title, overhead, minW);
        if (i < index) x += tw + ns_scale(TAB_GAP_BASE, data->dpi);
    }
    SelectObject(hdc, hOld);
    ReleaseDC(hwnd, hdc);

    if (want_close) {
        int closeX = x + tw - closeSz - pad;
        int closeY = tabY + (tabH - closeSz) / 2;
        SetRect(out, closeX, closeY, closeX + closeSz, closeY + closeSz);
    } else {
        SetRect(out, x, tabY, x + tw, tabY + tabH);
    }
    return 1;
}

/* Decode a TABS_HOVER_* id back into its rect. Used both for the
 * currently-hot element and, on WM_MOUSEMOVE, for the previously-hot one
 * (which chatlv-style keyed-by-position hit-testing can't report once
 * the cursor has moved off it). */
static int tabs_rect_for_id(HWND hwnd, TabControlData *data, int id, RECT *out)
{
    SetRectEmpty(out);
    if (id < 0) return 0;
    if (id < TABS_HOVER_TAB_BASE) {
        tabs_rect_for_btn(hwnd, data, id, out);
        return 1;
    }
    int offset = id - TABS_HOVER_TAB_BASE;
    return tabs_rect_for_tab(hwnd, data, offset / 2, offset % 2, out);
}

/* Hover hit-test: figure out which painted element is under (mx, my).
 * Returns a TABS_HOVER_* id, or -1 when nothing is hovered, plus its
 * rect via out_rc (for targeted invalidation). Mirrors the geometry of
 * the paint and click paths — keep in sync if those change. */
static int tabs_hit_id(HWND hwnd, TabControlData *data, int mx, int my,
                       RECT *out_rc)
{
    if (!data) return -1;

    RECT rcClient;
    GetClientRect(hwnd, &rcClient);

    int btnSz_h  = ns_scale(BTN_SIZE_BASE, data->dpi);
    int pad_h    = ns_scale(PAD_BASE, data->dpi);
    int btnGap_h = ns_scale(BTN_GAP_BASE, data->dpi);
    int btnY_h   = (rcClient.bottom - btnSz_h) / 2;

    /* [+] add */
    if (mx >= pad_h && mx <= pad_h + btnSz_h &&
        my >= btnY_h && my <= btnY_h + btnSz_h) {
        if (out_rc) tabs_rect_for_btn(hwnd, data, TABS_HOVER_ADD, out_rc);
        return TABS_HOVER_ADD;
    }

    /* Right-side cluster */
    int aiX_h    = rcClient.right - btnSz_h - pad_h;
    int rightX_h = aiX_h - btnSz_h - btnGap_h;
    int leftX_h  = rightX_h - btnSz_h - btnGap_h;
    if (my >= btnY_h && my <= btnY_h + btnSz_h) {
        if (mx >= aiX_h    && mx <= aiX_h    + btnSz_h) {
            if (out_rc) tabs_rect_for_btn(hwnd, data, TABS_HOVER_AI, out_rc);
            return TABS_HOVER_AI;
        }
        if (mx >= rightX_h && mx <= rightX_h + btnSz_h) {
            if (out_rc) tabs_rect_for_btn(hwnd, data, TABS_HOVER_RIGHT, out_rc);
            return TABS_HOVER_RIGHT;
        }
        if (mx >= leftX_h  && mx <= leftX_h  + btnSz_h) {
            if (out_rc) tabs_rect_for_btn(hwnd, data, TABS_HOVER_LEFT, out_rc);
            return TABS_HOVER_LEFT;
        }
    }

    /* Tabs (background, or close glyph within the hovered tab) */
    int overhead_h = TAB_OVERHEAD_S;
    int minW_h     = ns_scale(TAB_MIN_W_BASE, data->dpi);
    int tabVPad_h  = ns_scale(TAB_V_PAD_BASE, data->dpi);
    int tabH_h     = rcClient.bottom - tabVPad_h;
    int tabY_h     = tabVPad_h / 2;
    int closeSz_h  = ns_scale(CLOSE_SIZE_BASE, data->dpi);
    int x = TAB_START_X_S;
    HDC hdc_h = GetDC(hwnd);
    HFONT hOld = (HFONT)SelectObject(hdc_h, data->hFont);
    for (int i = 0; i < data->m.count; i++) {
        int tw = tab_w_s(hdc_h, data->m.tabs[i].title, overhead_h, minW_h);
        if (mx >= x && mx <= x + tw) {
            SelectObject(hdc_h, hOld);
            ReleaseDC(hwnd, hdc_h);
            int closeX = x + tw - closeSz_h - pad_h;
            int closeY = tabY_h + (tabH_h - closeSz_h) / 2;
            if (mx >= closeX && mx <= closeX + closeSz_h &&
                my >= closeY && my <= closeY + closeSz_h) {
                if (out_rc)
                    SetRect(out_rc, closeX, closeY,
                            closeX + closeSz_h, closeY + closeSz_h);
                return TABS_HOVER_CLOSE(i);
            }
            if (out_rc) SetRect(out_rc, x, tabY_h, x + tw, tabY_h + tabH_h);
            return TABS_HOVER_TAB(i);
        }
        x += tw + ns_scale(TAB_GAP_BASE, data->dpi);
    }
    SelectObject(hdc_h, hOld);
    ReleaseDC(hwnd, hdc_h);
    return -1;
}

static LRESULT CALLBACK TabsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            data = xcalloc(1, sizeof(TabControlData));
            tabmgr_init(&data->m);
            ns_hover_init(&data->hover);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)data);

            (void)snprintf(data->font_name, sizeof(data->font_name),
                           "%s", APP_FONT_DEFAULT);
            tabs_create_fonts(data, get_window_dpi(hwnd));

            /* Create tooltip control — one tool covers the entire tab strip */
            data->hTooltip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
                                            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                            0, 0, 0, 0,
                                            hwnd, NULL, GetModuleHandle(NULL), NULL);
            if (data->hTooltip) {
                TOOLINFO ti = {0};
                ti.cbSize   = sizeof(TOOLINFO);
                ti.uFlags   = TTF_SUBCLASS | TTF_IDISHWND;
                ti.hwnd     = hwnd;
                ti.uId      = (UINT_PTR)hwnd;
                ti.lpszText = LPSTR_TEXTCALLBACK;
                GetClientRect(hwnd, &ti.rect);
                SendMessage(data->hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
                /* Enable multiline tooltips (needed for \n to render) */
                SendMessage(data->hTooltip, TTM_SETMAXTIPWIDTH, 0, 500);
            }
            SetTimer(hwnd, TABS_TIMER_PULSE, TABS_PULSE_MS, NULL);
            return 0;
        }

        case WM_TIMER:
            if (data && wParam == TABS_TIMER_PULSE) {
                /* Advance phase; only invalidate if any tab is CONNECTING.
                 * Reduced motion: leave pulse_phase at rest (0) so
                 * ns_draw_pulse always renders its smallest, static ring
                 * instead of animating. */
                int any_connecting = 0;
                for (int i = 0; i < data->m.count; i++) {
                    if (data->m.tabs[i].status == TAB_CONNECTING) { any_connecting = 1; break; }
                }
                if (any_connecting && !ns_reduced_motion()) {
                    data->pulse_phase += 0.18f;
                    if (data->pulse_phase > 6.2831853f) data->pulse_phase -= 6.2831853f;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, TABS_TIMER_PULSE);
            if (data) {
                /* data->hFont / data->hSmallFont come from the ns_font cache. */
                if (data->hTooltip)   DestroyWindow(data->hTooltip);
                free(data);
            }
            return 0;

        case WM_MOUSEMOVE: {
            if (!data) return 0;

            /* Hit-test, feed ns_hover, and invalidate only the (up to)
             * two rects that actually changed state -- never a full
             * repaint just because the cursor moved. */
            int mx = (int)(short)LOWORD(lParam);
            int my = (int)(short)HIWORD(lParam);
            RECT new_rc;
            int hit_id = tabs_hit_id(hwnd, data, mx, my, &new_rc);
            NsHoverChange ch = ns_hover_move(&data->hover, hit_id);
            if (ch.changed) {
                RECT old_rc;
                if (tabs_rect_for_id(hwnd, data, ch.old_id, &old_rc))
                    InvalidateRect(hwnd, &old_rc, FALSE);
                if (ch.new_id >= 0)
                    InvalidateRect(hwnd, &new_rc, FALSE);
            }

            /* Arm WM_MOUSELEAVE delivery once per enter. */
            if (!data->tracking_mouse) {
                TRACKMOUSEEVENT tme = {0};
                tme.cbSize    = sizeof(tme);
                tme.dwFlags   = TME_LEAVE;
                tme.hwndTrack = hwnd;
                if (TrackMouseEvent(&tme)) data->tracking_mouse = TRUE;
            }

            /* Relay mouse moves to the tooltip control so it can trigger */
            if (data->hTooltip) {
                MSG msg2 = {hwnd, WM_MOUSEMOVE, wParam, lParam, 0, {0, 0}};
                SendMessage(data->hTooltip, TTM_RELAYEVENT, 0, (LPARAM)&msg2);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            if (data) {
                data->tracking_mouse = FALSE;
                NsHoverChange ch = ns_hover_leave(&data->hover);
                if (ch.changed) {
                    RECT old_rc;
                    if (tabs_rect_for_id(hwnd, data, ch.old_id, &old_rc))
                        InvalidateRect(hwnd, &old_rc, FALSE);
                }
            }
            return 0;

        case WM_SETCURSOR:
            if (data && LOWORD(lParam) == HTCLIENT) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                int hit_id = tabs_hit_id(hwnd, data, pt.x, pt.y, NULL);
                SetCursor(LoadCursor(NULL, hit_id >= 0 ? IDC_HAND : IDC_ARROW));
                return TRUE;
            }
            break;

        case WM_NOTIFY: {
            const NMHDR *nmhdr = (const NMHDR *)lParam;
            if (data && data->hTooltip && nmhdr->hwndFrom == data->hTooltip &&
                (nmhdr->code == TTN_GETDISPINFOA || nmhdr->code == TTN_NEEDTEXTA)) {
                NMTTDISPINFOA *ttt = (NMTTDISPINFOA *)lParam;
                /* Determine which tab is under the cursor */
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                HDC hdc_m = GetDC(hwnd);
                HFONT hOldF = (HFONT)SelectObject(hdc_m, data->hFont);
                int tx = TAB_START_X_S;
                int tab_idx = -1;
                int overhead_s = TAB_OVERHEAD_S;
                int min_w_s = ns_scale(TAB_MIN_W_BASE, data->dpi);
                for (int i = 0; i < data->m.count; i++) {
                    int tw = tab_w_s(hdc_m, data->m.tabs[i].title, overhead_s, min_w_s);
                    if (pt.x >= tx && pt.x <= tx + tw) { tab_idx = i; break; }
                    tx += tw + ns_scale(TAB_GAP_BASE, data->dpi);
                }
                SelectObject(hdc_m, hOldF);
                ReleaseDC(hwnd, hdc_m);
                /* Check if cursor is over any button */
                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                const char *btn_tip = tabs_btn_tooltip_at(pt.x, rcClient.right, data->dpi);
                if (btn_tip) {
                    ttt->lpszText = (LPSTR)btn_tip;
                } else if (tab_idx >= 0) {
                    TabEntry *e = &data->m.tabs[tab_idx];
                    unsigned long elapsed = 0;
                    if (e->connect_ms > 0) {
                        ULONGLONG now_ms = GetTickCount64();
                        elapsed = (unsigned long)((now_ms - e->connect_ms) / 1000ULL);
                    }
                    static char tip_buf[256];
                    const char *log_path = e->logging ? "active" : NULL;
                    tooltip_build_text(e->status, e->title, e->host,
                                       e->username, elapsed, log_path,
                                       tip_buf, sizeof(tip_buf));
                    ttt->lpszText = tip_buf;
                } else {
                    ttt->lpszText = (LPSTR)"";
                }
                return 0;
            }
            break;
        }

        case WM_ERASEBKGND:
            return 1;   /* suppress erase — we fill in WM_PAINT */

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdcReal = BeginPaint(hwnd, &ps);

            RECT rcClient;
            GetClientRect(hwnd, &rcClient);

            /* Double-buffer: paint to off-screen bitmap, then blit once */
            HDC hdc = CreateCompatibleDC(hdcReal);
            HBITMAP hBmp = CreateCompatibleBitmap(hdcReal,
                                rcClient.right, rcClient.bottom);
            HGDIOBJ hOldBmp = SelectObject(hdc, hBmp);

            /* Theme colours. tabs_set_theme() runs right after tabs_create()
             * in WM_CREATE, before this window is ever painted, so
             * data->theme is always set here -- no neutral fallback needed
             * (Design-System Foundation, task 10). */
            const ThemeColors *t = data->theme;
            COLORREF cBg       = tc(t->bg_secondary);
            COLORREF cTabAct   = tc(t->bg_primary);
            COLORREF cTabInact = tc(t->bg_secondary);
            COLORREF cBorder   = tc(t->border);
            COLORREF cText     = tc(t->text_main);
            COLORREF cDim      = tc(t->text_dim);
            COLORREF cAccent   = tc(t->accent);
            COLORREF cBtn      = tc(t->bg_secondary);

            /* Background */
            HBRUSH bgBrush = CreateSolidBrush(cBg);
            FillRect(hdc, &rcClient, bgBrush);
            DeleteObject(bgBrush);

            HFONT hOldFont = (HFONT)SelectObject(hdc, data->hFont);
            SetBkMode(hdc, TRANSPARENT);

            /* Scaled layout values */
            int btnSz   = ns_scale(BTN_SIZE_BASE, data->dpi);
            int pad      = ns_scale(PAD_BASE, data->dpi);
            int tabGap   = ns_scale(TAB_GAP_BASE, data->dpi);
            int btnGap   = ns_scale(BTN_GAP_BASE, data->dpi);
            int indW     = ns_scale(INDICATOR_W_BASE, data->dpi);
            int indGap   = ns_scale(INDICATOR_GAP_BASE, data->dpi);
            int closeSz  = ns_scale(CLOSE_SIZE_BASE, data->dpi);
            int tabVPad  = ns_scale(TAB_V_PAD_BASE, data->dpi);
            int accentH  = ns_scale(ACCENT_BAR_H_BASE, data->dpi);
            int overhead = TAB_OVERHEAD_S;
            int minW     = ns_scale(TAB_MIN_W_BASE, data->dpi);
            int tabStartX = TAB_START_X_S;
            int rr       = ns_scale(3, data->dpi);  /* corner radius */

            /* ---- [+] Add button ---- */
            int btnY = (rcClient.bottom - btnSz) / 2;
            RECT rcAdd = {pad, btnY, pad + btnSz, btnY + btnSz};
            {
                COLORREF addFill = ns_hover_state_for(&data->hover, TABS_HOVER_ADD)
                                 ? rgb_alpha(cBtn, cTabAct, 0.6f) : cBtn;
                HPEN hPen = CreatePen(PS_SOLID, 1, cBorder);
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                HBRUSH hBr = CreateSolidBrush(addFill);
                HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);
                RoundRect(hdc, rcAdd.left, rcAdd.top, rcAdd.right, rcAdd.bottom, rr, rr);
                SelectObject(hdc, hOldBr);
                SelectObject(hdc, hOldPen);
                DeleteObject(hBr);
                DeleteObject(hPen);
            }
            ns_icon_draw(hdc, NS_ICON_PLUS, &rcAdd, cText, (UINT)data->dpi);

            /* ---- Tabs ---- */
            int tabH = rcClient.bottom - tabVPad;
            int tabY = tabVPad / 2;
            int x    = tabStartX;

            for (int i = 0; i < data->m.count; i++) {
                int tw = tab_w_s(hdc, data->m.tabs[i].title, overhead, minW);
                RECT rcTab = {x, tabY, x + tw, tabY + tabH};

                /* Background & border. Hover fill comes straight from the
                 * resolved tokens (Design-System Foundation task 6),
                 * not a local rgb_alpha blend. */
                int is_active = (i == data->m.active_index);
                int is_hover  = (!is_active &&
                                 ns_hover_state_for(&data->hover, TABS_HOVER_TAB(i)));
                COLORREF tabFill = is_active ? cTabAct
                                 : (is_hover ? tc(ns_tokens()->bg_secondary.hover)
                                             : cTabInact);
                HPEN hPen = CreatePen(PS_SOLID, 1, cBorder);
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                HBRUSH hTabBrush = CreateSolidBrush(tabFill);
                HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hTabBrush);

                RoundRect(hdc, rcTab.left, rcTab.top, rcTab.right, rcTab.bottom, ns_scale(6, data->dpi), ns_scale(6, data->dpi));

                SelectObject(hdc, hOldBrush);
                SelectObject(hdc, hOldPen);
                DeleteObject(hTabBrush);
                DeleteObject(hPen);

                /* Accent bar at bottom of active tab */
                if (is_active) {
                    RECT rcBar = {rcTab.left + rr, rcTab.bottom - accentH,
                                  rcTab.right - rr, rcTab.bottom};
                    HBRUSH hAccent = CreateSolidBrush(cAccent);
                    FillRect(hdc, &rcBar, hAccent);
                    DeleteObject(hAccent);
                }

                /* Indicator dimensions: inner height with scaled margin */
                int indicH = tabH - ns_scale(10, data->dpi);
                if (indicH < ns_scale(4, data->dpi)) indicH = ns_scale(4, data->dpi);

                /* ---- Status indicator ---- */
                int indX = x + indGap;
                int indY = tabY + (tabH - indicH) / 2;
                {
                    COLORREF sCol = status_color(data->m.tabs[i].status);
                    HBRUSH sBrush = CreateSolidBrush(sCol);
                    HPEN sPen     = CreatePen(PS_SOLID, 1, sCol);
                    HPEN hOldSPen  = (HPEN)SelectObject(hdc, sPen);
                    HBRUSH hOldSBr = (HBRUSH)SelectObject(hdc, sBrush);
                    RoundRect(hdc, indX, indY, indX + indW, indY + indicH, rr, rr);
                    SelectObject(hdc, hOldSBr);
                    SelectObject(hdc, hOldSPen);
                    DeleteObject(sBrush);
                    DeleteObject(sPen);

                    if (data->m.tabs[i].status == TAB_CONNECTING) {
                        RECT dot_rc = { indX, indY, indX + indW, indY + indicH };
                        ns_draw_pulse(hdc, &dot_rc, sCol, data->pulse_phase);
                    }
                }

                /* ---- Log button ---- */
                int logX = indX + indW + indGap;
                {
                    int logY = indY;
                    COLORREF logCol = data->m.tabs[i].logging
                                    ? tc(ns_tokens()->success.base)
                                    : tc(ns_tokens()->border);
                    HBRUSH lBrush = CreateSolidBrush(logCol);
                    HPEN lPen     = CreatePen(PS_SOLID, 1, logCol);
                    HPEN hOldLPen  = (HPEN)SelectObject(hdc, lPen);
                    HBRUSH hOldLBr = (HBRUSH)SelectObject(hdc, lBrush);
                    RoundRect(hdc, logX, logY, logX + indW, logY + indicH, rr, rr);
                    SelectObject(hdc, hOldLBr);
                    SelectObject(hdc, hOldLPen);
                    DeleteObject(lBrush);
                    DeleteObject(lPen);

                    /* "L" label — use cached small font */
                    HFONT hPrevFont = (HFONT)SelectObject(hdc, data->hSmallFont);
                    SetTextColor(hdc, cText);
                    RECT rcL = {logX, logY, logX + indW, logY + indicH};
                    DrawText(hdc, "L", 1, &rcL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hdc, hPrevFont);
                }

                /* ---- Title text ---- */
                SetTextColor(hdc, cText);
                RECT rcText = rcTab;
                rcText.left  += indGap + indW + indGap + indW + indGap;
                rcText.right -= closeSz + ns_scale(6, data->dpi);
                DrawText(hdc, data->m.tabs[i].title, -1, &rcText,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                /* ---- ✕ close button -- brightens to text_main while hot */
                int closeX = x + tw - closeSz - pad;
                int closeY = tabY + (tabH - closeSz) / 2;
                RECT rcClose = {closeX, closeY, closeX + closeSz, closeY + closeSz};
                int close_hot = ns_hover_state_for(&data->hover, TABS_HOVER_CLOSE(i));
                ns_icon_draw(hdc, NS_ICON_CLOSE, &rcClose,
                            close_hot ? cText : cDim, (UINT)data->dpi);

                x += tw + tabGap;
            }

            /* ---- Right-side buttons: [◀][▶][AI] ---- */
            {
                int aiX    = rcClient.right - btnSz - pad;
                int rightX = aiX - btnSz - btnGap;
                int leftX  = rightX - btnSz - btnGap;

                COLORREF hoverFill = rgb_alpha(cBtn, cTabAct, 0.6f);
                HPEN rBtnPen = CreatePen(PS_SOLID, 1, cBorder);
                HPEN hOldBtnPen = (HPEN)SelectObject(hdc, rBtnPen);

                /* Helper macro: paint one square button's bg, picking the
                 * hover-tinted fill if this button is currently hovered. */
                #define DRAW_BTN_BG(rcptr, hover_id) do { \
                    COLORREF _f = ns_hover_state_for(&data->hover, (hover_id)) \
                                  ? hoverFill : cBtn; \
                    HBRUSH _b = CreateSolidBrush(_f); \
                    HBRUSH _ob = (HBRUSH)SelectObject(hdc, _b); \
                    RoundRect(hdc, (rcptr)->left, (rcptr)->top, \
                              (rcptr)->right, (rcptr)->bottom, rr, rr); \
                    SelectObject(hdc, _ob); \
                    DeleteObject(_b); \
                } while (0)

                /* ◀ Left arrow */
                if (leftX > x) {
                    RECT rcLeft = {leftX, btnY, leftX + btnSz, btnY + btnSz};
                    DRAW_BTN_BG(&rcLeft, TABS_HOVER_LEFT);
                    ns_icon_draw(hdc, NS_ICON_CHEV_LEFT, &rcLeft, cDim, (UINT)data->dpi);
                }
                /* ▶ Right arrow */
                if (rightX > x) {
                    RECT rcRight = {rightX, btnY, rightX + btnSz, btnY + btnSz};
                    DRAW_BTN_BG(&rcRight, TABS_HOVER_RIGHT);
                    ns_icon_draw(hdc, NS_ICON_CHEV_RIGHT, &rcRight, cDim, (UINT)data->dpi);
                }
                /* AI button — CPU chip glyph. Chip body and pins always
                 * render in cDim; the inner sparkle uses the accent
                 * color, which goes green only when an AI key is
                 * configured. The accent region is small by design so
                 * the green stays well under 35% of the icon. */
                if (aiX > x) {
                    RECT rcAi = {aiX, btnY, aiX + btnSz, btnY + btnSz};
                    DRAW_BTN_BG(&rcAi, TABS_HOVER_AI);
                    COLORREF aiAccent = data->ai_active
                                      ? tc(ns_tokens()->success.base) : cDim;
                    ns_icon_draw_accent(hdc, NS_ICON_AI, &rcAi,
                                        cDim, aiAccent, 0, 255,
                                        (UINT)data->dpi);
                }

                #undef DRAW_BTN_BG
                SelectObject(hdc, hOldBtnPen);
                DeleteObject(rBtnPen);
            }

            SelectObject(hdc, hOldFont);

            /* Blit double-buffer to screen */
            BitBlt(hdcReal, 0, 0, rcClient.right, rcClient.bottom,
                   hdc, 0, 0, SRCCOPY);
            SelectObject(hdc, hOldBmp);
            DeleteObject(hBmp);
            DeleteDC(hdc);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = (int)(short)LOWORD(lParam);
            int my = (int)(short)HIWORD(lParam);

            int btnSz_h  = ns_scale(BTN_SIZE_BASE, data->dpi);
            int pad_h    = ns_scale(PAD_BASE, data->dpi);
            int btnGap_h = ns_scale(BTN_GAP_BASE, data->dpi);
            int indW_h   = ns_scale(INDICATOR_W_BASE, data->dpi);
            int indGap_h = ns_scale(INDICATOR_GAP_BASE, data->dpi);
            int closeSz_h = ns_scale(CLOSE_SIZE_BASE, data->dpi);
            int tabVPad_h = ns_scale(TAB_V_PAD_BASE, data->dpi);
            int overhead_h = TAB_OVERHEAD_S;
            int minW_h    = ns_scale(TAB_MIN_W_BASE, data->dpi);

            /* Hit test [+] button */
            if (mx >= pad_h && mx <= pad_h + btnSz_h) {
                if (data->on_new) data->on_new();
                return 0;
            }

            /* Hit test right-side buttons: [◀][▶][AI] */
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            int aiX    = rcClient.right - btnSz_h - pad_h;
            int rightX = aiX - btnSz_h - btnGap_h;
            int leftX  = rightX - btnSz_h - btnGap_h;

            if (mx >= aiX && mx <= aiX + btnSz_h) {
                if (data->on_ai) data->on_ai();
                return 0;
            }
            if (mx >= leftX && mx <= leftX + btnSz_h) {
                int new_idx = tabmgr_navigate(&data->m, -1);
                if (new_idx >= 0) {
                    InvalidateRect(hwnd, NULL, FALSE);
                    if (data->on_select)
                        data->on_select(new_idx, data->m.tabs[new_idx].user_data);
                }
                return 0;
            }
            if (mx >= rightX && mx <= rightX + btnSz_h) {
                int new_idx = tabmgr_navigate(&data->m, 1);
                if (new_idx >= 0) {
                    InvalidateRect(hwnd, NULL, FALSE);
                    if (data->on_select)
                        data->on_select(new_idx, data->m.tabs[new_idx].user_data);
                }
                return 0;
            }

            /* Hit test tabs */
            int tabH = rcClient.bottom - tabVPad_h;
            int tabY = tabVPad_h / 2;
            int x = TAB_START_X_S;

            HDC hdc_ht = GetDC(hwnd);
            HFONT hOldHt = (HFONT)SelectObject(hdc_ht, data->hFont);

            for (int i = 0; i < data->m.count; i++) {
                int tw = tab_w_s(hdc_ht, data->m.tabs[i].title, overhead_h, minW_h);
                if (mx >= x && mx <= x + tw) {
                    SelectObject(hdc_ht, hOldHt);
                    ReleaseDC(hwnd, hdc_ht);
                    /* Check ✕ close button first */
                    int closeX = x + tw - closeSz_h - pad_h;
                    int closeY = tabY + (tabH - closeSz_h) / 2;
                    if (mx >= closeX && mx <= closeX + closeSz_h &&
                        my >= closeY && my <= closeY + closeSz_h) {
                        if (data->on_close)
                            data->on_close(i, data->m.tabs[i].user_data);
                        return 0;
                    }
                    /* Check status indicator dot */
                    int indX_h = x + indGap_h;
                    int indH_h = tabH - ns_scale(10, data->dpi);
                    if (indH_h < ns_scale(4, data->dpi)) indH_h = ns_scale(4, data->dpi);
                    int indY_h = tabY + (tabH - indH_h) / 2;
                    if (mx >= indX_h && mx <= indX_h + indW_h &&
                        my >= indY_h && my <= indY_h + indH_h) {
                        if (data->on_status_click)
                            data->on_status_click(i, data->m.tabs[i].user_data,
                                                  data->m.tabs[i].status);
                        return 0;
                    }
                    /* Check log button — geometry MUST mirror the paint
                     * path (see status indicator above) so hit zones don't
                     * drift off the visible rect at high DPI. */
                    int logBtnX = x + indGap_h + indW_h + indGap_h;
                    int logBtnY = indY_h;
                    if (mx >= logBtnX && mx <= logBtnX + indW_h &&
                        my >= logBtnY && my <= logBtnY + indH_h) {
                        if (data->on_log_toggle)
                            data->on_log_toggle(i, data->m.tabs[i].user_data);
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                    /* Otherwise select the tab */
                    if (i != data->m.active_index) {
                        data->m.active_index = i;
                        InvalidateRect(hwnd, NULL, FALSE);
                        if (data->on_select)
                            data->on_select(i, data->m.tabs[i].user_data);
                    }
                    return 0;
                }
                x += tw + ns_scale(TAB_GAP_BASE, data->dpi);
            }
            SelectObject(hdc_ht, hOldHt);
            ReleaseDC(hwnd, hdc_ht);
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ---- Public API ---------------------------------------------------------- */

void tabs_init(HINSTANCE hInstance)
{
    WNDCLASSEX wc = {0};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = TabsWndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = TABS_CLASS_NAME;
    RegisterClassEx(&wc);
}

HWND tabs_create(HWND parent, int x, int y, int width, int height)
{
    return CreateWindowEx(0, TABS_CLASS_NAME, "Tabs",
                          WS_CHILD | WS_VISIBLE,
                          x, y, width, height,
                          parent, NULL, GetModuleHandle(NULL), NULL);
}

int tabs_add(HWND hwnd, const char *title, void *user_data)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return -1;
    int prev_active = data->m.active_index; /* -1 when no tabs existed yet */
    int idx = tabmgr_add(&data->m, title, user_data);
    InvalidateRect(hwnd, NULL, FALSE);
    /* First tab: tabmgr_add sets active_index=0 automatically, but
     * tabs_set_active() will see active_index==idx and return early without
     * firing on_select.  Fire it here so g_active_session gets set. */
    if (prev_active < 0 && data->m.active_index == idx && data->on_select)
        data->on_select(idx, data->m.tabs[idx].user_data);
    return idx;
}

void tabs_remove(HWND hwnd, int index)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return;
    tabmgr_remove(&data->m, index);
    InvalidateRect(hwnd, NULL, FALSE);
}

void tabs_set_callbacks(HWND hwnd,
                        TabSelectCallback   on_select,
                        TabNewCallback      on_new,
                        TabCloseCallback    on_close,
                        TabSettingsCallback  on_settings,
                        TabLogToggleCallback on_log_toggle)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return;
    data->on_select     = on_select;
    data->on_new        = on_new;
    data->on_close      = on_close;
    data->on_settings   = on_settings;
    data->on_log_toggle = on_log_toggle;
}

void tabs_clear(HWND hwnd)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return;
    tabmgr_init(&data->m);
    InvalidateRect(hwnd, NULL, FALSE);
}

void tabs_set_active(HWND hwnd, int index)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return;
    if (data->m.active_index == index) return;
    tabmgr_set_active(&data->m, index);
    InvalidateRect(hwnd, NULL, FALSE);
    if (data->on_select)
        data->on_select(index, data->m.tabs[index].user_data);
}

int tabs_get_active(HWND hwnd)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return -1;
    return tabmgr_get_active(&data->m);
}

void *tabs_get_user_data(HWND hwnd, int index)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return NULL;
    return tabmgr_get_user_data(&data->m, index);
}

void tabs_set_status(HWND hwnd, int index, TabStatus status)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return;
    tabmgr_set_status(&data->m, index, status);
    InvalidateRect(hwnd, NULL, FALSE);
}

TabStatus tabs_get_status(HWND hwnd, int index)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return TAB_IDLE;
    return tabmgr_get_status(&data->m, index);
}

int tabs_find(HWND hwnd, void *user_data)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return -1;
    return tabmgr_find(&data->m, user_data);
}

void tabs_set_connect_info(HWND hwnd, int index,
                           const char *username, const char *host,
                           unsigned long long connect_ms)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return;
    tabmgr_set_connect_info(&data->m, index, username, host, connect_ms);
}

void tabs_set_logging(HWND hwnd, int index, int logging)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return;
    tabmgr_set_logging(&data->m, index, logging);
    InvalidateRect(hwnd, NULL, FALSE);
}

int tabs_get_logging(HWND hwnd, int index)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return 0;
    return tabmgr_get_logging(&data->m, index);
}

void tabs_set_ai_callback(HWND hwnd, TabAiCallback on_ai)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return;
    data->on_ai = on_ai;
}

void tabs_set_ai_active(HWND hwnd, int active)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return;
    data->ai_active = active;
    InvalidateRect(hwnd, NULL, FALSE);
}

void tabs_set_font(HWND hwnd, const char *font_name, int dpi)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data || !font_name) return;
    (void)snprintf(data->font_name, sizeof(data->font_name), "%s", font_name);
    if (dpi <= 0) dpi = get_window_dpi(hwnd);
    tabs_create_fonts(data, dpi);
    InvalidateRect(hwnd, NULL, FALSE);
}

void tabs_set_theme(HWND hwnd, const ThemeColors *theme)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return;
    data->theme = theme;
    InvalidateRect(hwnd, NULL, FALSE);
}

void tabs_set_status_click_callback(HWND hwnd, TabStatusClickCallback on_status_click)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return;
    data->on_status_click = on_status_click;
}

#endif
