#include "tabs.h"
#include "app_font.h"
#include "ui_theme.h"
#include "xmalloc.h"
#include "logger.h"
#include "tooltip.h"
#include <stdio.h>
#include <commctrl.h>

#ifdef _WIN32

static const char *TABS_CLASS_NAME = "Nutshell_Tabs";

/* ---- Layout constants (base values at 96 DPI) ---------------------------- */
#define TAB_H_PAD_BASE    4
#define BTN_SIZE_BASE     24
#define TAB_GAP_BASE      4
#define BTN_GAP_BASE      2
#define INDICATOR_W_BASE  12
#define INDICATOR_GAP_BASE 3
#define CLOSE_SIZE_BASE   12
#define TAB_MIN_W_BASE    80
#define ACCENT_BAR_H_BASE 3
#define PAD_BASE          4   /* left margin before [+] button */
#define TAB_START_GAP_BASE 8  /* gap between [+] and first tab */

/* DPI-scaled layout helper — use S(base) inside functions that have `data` */
#define S(px) MulDiv((px), data->dpi, 96)

/* TAB_OVERHEAD and TAB_START_X as scaled expressions */
#define TAB_OVERHEAD_S (S(INDICATOR_GAP_BASE) + S(INDICATOR_W_BASE) + S(INDICATOR_GAP_BASE) \
                        + S(INDICATOR_W_BASE) + S(INDICATOR_GAP_BASE) + S(CLOSE_SIZE_BASE) + S(10))
#define TAB_START_X_S  (S(PAD_BASE) + S(BTN_SIZE_BASE) + S(TAB_START_GAP_BASE))

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
} TabControlData;

/* Convert 0xRRGGBB to COLORREF (0x00BBGGRR) */
static COLORREF tc(unsigned int rgb)
{
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

/* Return the COLORREF for a connection-status dot */
static COLORREF status_color(TabStatus s)
{
    switch (s) {
        case TAB_CONNECTING:    return RGB(220, 160,   0);
        case TAB_CONNECTED:     return RGB(  0, 160,  80);
        case TAB_DISCONNECTED:  return RGB(200,  50,  50);
        case TAB_IDLE: /* fall-through */
        default:                return RGB(160, 160, 160);
    }
}

/* (Re-)create hFont and hSmallFont from font_name.  Deletes any previous. */
static void tabs_create_fonts(TabControlData *data, HWND hwnd)
{
    if (data->hFont)      { DeleteObject(data->hFont);      data->hFont = NULL; }
    if (data->hSmallFont) { DeleteObject(data->hSmallFont); data->hSmallFont = NULL; }

    HDC hdc = GetDC(hwnd);
    int logPixelsY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(hwnd, hdc);
    data->dpi = logPixelsY;

    int h = -MulDiv(APP_FONT_UI_SIZE, logPixelsY, 72);
    data->hFont = CreateFont(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_TT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              FIXED_PITCH | FF_MODERN, data->font_name);
    /* Small bold font for indicator labels ("L", "AI") — DPI-scaled */
    int sh = -MulDiv(7, logPixelsY, 72);
    data->hSmallFont = CreateFont(sh, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_TT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   FIXED_PITCH | FF_MODERN, data->font_name);
}

static LRESULT CALLBACK TabsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            data = xcalloc(1, sizeof(TabControlData));
            tabmgr_init(&data->m);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)data);

            (void)snprintf(data->font_name, sizeof(data->font_name),
                           "%s", APP_FONT_DEFAULT);
            tabs_create_fonts(data, hwnd);

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
            return 0;
        }

        case WM_DESTROY:
            if (data) {
                if (data->hFont)      DeleteObject(data->hFont);
                if (data->hSmallFont) DeleteObject(data->hSmallFont);
                if (data->hTooltip)   DestroyWindow(data->hTooltip);
                free(data);
            }
            return 0;

        case WM_MOUSEMOVE:
            /* Relay mouse moves to the tooltip control so it can trigger */
            if (data && data->hTooltip) {
                MSG msg2 = {hwnd, WM_MOUSEMOVE, wParam, lParam, 0, {0, 0}};
                SendMessage(data->hTooltip, TTM_RELAYEVENT, 0, (LPARAM)&msg2);
            }
            return 0;

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
                int min_w_s = S(TAB_MIN_W_BASE);
                for (int i = 0; i < data->m.count; i++) {
                    int tw = tab_w_s(hdc_m, data->m.tabs[i].title, overhead_s, min_w_s);
                    if (pt.x >= tx && pt.x <= tx + tw) { tab_idx = i; break; }
                    tx += tw + S(TAB_GAP_BASE);
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

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rcClient;
            GetClientRect(hwnd, &rcClient);

            /* Theme colours (fallback to light neutral if no theme set) */
            const ThemeColors *t = data->theme;
            COLORREF cBg       = t ? tc(t->bg_secondary) : RGB(242, 242, 242);
            COLORREF cTabAct   = t ? tc(t->bg_primary)   : RGB(255, 255, 255);
            COLORREF cTabInact = t ? tc(t->bg_secondary)  : RGB(230, 230, 230);
            COLORREF cBorder   = t ? tc(t->border)        : RGB(180, 180, 180);
            COLORREF cText     = t ? tc(t->text_main)     : RGB(0, 0, 0);
            COLORREF cDim      = t ? tc(t->text_dim)      : RGB(120, 120, 120);
            COLORREF cAccent   = t ? tc(t->accent)        : RGB(0, 122, 255);
            COLORREF cBtn      = t ? tc(t->bg_secondary)  : RGB(196, 196, 196);

            /* Background */
            HBRUSH bgBrush = CreateSolidBrush(cBg);
            FillRect(hdc, &rcClient, bgBrush);
            DeleteObject(bgBrush);

            HFONT hOldFont = (HFONT)SelectObject(hdc, data->hFont);
            SetBkMode(hdc, TRANSPARENT);

            /* Scaled layout values */
            int btnSz   = S(BTN_SIZE_BASE);
            int pad      = S(PAD_BASE);
            int tabGap   = S(TAB_GAP_BASE);
            int btnGap   = S(BTN_GAP_BASE);
            int indW     = S(INDICATOR_W_BASE);
            int indGap   = S(INDICATOR_GAP_BASE);
            int closeSz  = S(CLOSE_SIZE_BASE);
            int tabHPad  = S(TAB_H_PAD_BASE);
            int accentH  = S(ACCENT_BAR_H_BASE);
            int overhead = TAB_OVERHEAD_S;
            int minW     = S(TAB_MIN_W_BASE);
            int tabStartX = TAB_START_X_S;
            int rr       = S(3);  /* corner radius */

            /* ---- [+] Add button ---- */
            int btnY = (rcClient.bottom - btnSz) / 2;
            RECT rcAdd = {pad, btnY, pad + btnSz, btnY + btnSz};
            {
                HPEN hPen = CreatePen(PS_SOLID, 1, cBorder);
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                HBRUSH hBr = CreateSolidBrush(cBtn);
                HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);
                RoundRect(hdc, rcAdd.left, rcAdd.top, rcAdd.right, rcAdd.bottom, rr, rr);
                SelectObject(hdc, hOldBr);
                SelectObject(hdc, hOldPen);
                DeleteObject(hBr);
                DeleteObject(hPen);
            }
            SetTextColor(hdc, cText);
            DrawText(hdc, "+", -1, &rcAdd, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            /* ---- Tabs ---- */
            int tabH = rcClient.bottom - tabHPad;
            int tabY = tabHPad / 2;
            int x    = tabStartX;

            for (int i = 0; i < data->m.count; i++) {
                int tw = tab_w_s(hdc, data->m.tabs[i].title, overhead, minW);
                RECT rcTab = {x, tabY, x + tw, tabY + tabH};

                /* Background & border */
                int is_active = (i == data->m.active_index);
                HPEN hPen = CreatePen(PS_SOLID, 1, cBorder);
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                HBRUSH hTabBrush = CreateSolidBrush(is_active ? cTabAct : cTabInact);
                HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hTabBrush);

                RoundRect(hdc, rcTab.left, rcTab.top, rcTab.right, rcTab.bottom, S(6), S(6));

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
                int indicH = tabH - S(10);
                if (indicH < S(4)) indicH = S(4);

                /* ---- Status indicator ---- */
                int indX = x + indGap;
                int indY = tabY + (tabH - indicH) / 2;
                {
                    HBRUSH sBrush = CreateSolidBrush(status_color(data->m.tabs[i].status));
                    HPEN sPen     = CreatePen(PS_SOLID, 1, status_color(data->m.tabs[i].status));
                    HPEN hOldSPen  = (HPEN)SelectObject(hdc, sPen);
                    HBRUSH hOldSBr = (HBRUSH)SelectObject(hdc, sBrush);
                    RoundRect(hdc, indX, indY, indX + indW, indY + indicH, rr, rr);
                    SelectObject(hdc, hOldSBr);
                    SelectObject(hdc, hOldSPen);
                    DeleteObject(sBrush);
                    DeleteObject(sPen);
                }

                /* ---- Log button ---- */
                int logX = indX + indW + indGap;
                {
                    int logY = indY;
                    COLORREF logCol = data->m.tabs[i].logging
                                    ? RGB(0, 160, 80)    /* green when on */
                                    : RGB(180, 180, 180); /* gray when off */
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
                rcText.right -= closeSz + S(6);
                DrawText(hdc, data->m.tabs[i].title, -1, &rcText,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                /* ---- ✕ close button ---- */
                int closeX = x + tw - closeSz - pad;
                int closeY = tabY + (tabH - closeSz) / 2;
                RECT rcClose = {closeX, closeY, closeX + closeSz, closeY + closeSz};
                SetTextColor(hdc, cDim);
                DrawTextW(hdc, L"\x00D7", -1, &rcClose,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                x += tw + tabGap;
            }

            /* ---- Right-side buttons: [◀][▶][AI][⚙] ---- */
            {
                int cogX   = rcClient.right - btnSz - pad;
                int aiX    = cogX - btnSz - btnGap;
                int rightX = aiX - btnSz - btnGap;
                int leftX  = rightX - btnSz - btnGap;

                HBRUSH rBtnBrush = CreateSolidBrush(cBtn);
                HPEN rBtnPen = CreatePen(PS_SOLID, 1, cBorder);
                HPEN hOldBtnPen = (HPEN)SelectObject(hdc, rBtnPen);
                HBRUSH hOldBtnBr = (HBRUSH)SelectObject(hdc, rBtnBrush);

                /* ◀ Left arrow */
                if (leftX > x) {
                    RECT rcLeft = {leftX, btnY, leftX + btnSz, btnY + btnSz};
                    RoundRect(hdc, rcLeft.left, rcLeft.top, rcLeft.right, rcLeft.bottom, rr, rr);
                    SetTextColor(hdc, cDim);
                    DrawTextW(hdc, L"\x25C0", -1, &rcLeft,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                /* ▶ Right arrow */
                if (rightX > x) {
                    RECT rcRight = {rightX, btnY, rightX + btnSz, btnY + btnSz};
                    RoundRect(hdc, rcRight.left, rcRight.top, rcRight.right, rcRight.bottom, rr, rr);
                    SetTextColor(hdc, cDim);
                    DrawTextW(hdc, L"\x25B6", -1, &rcRight,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                /* AI button */
                if (aiX > x) {
                    RECT rcAi = {aiX, btnY, aiX + btnSz, btnY + btnSz};
                    RoundRect(hdc, rcAi.left, rcAi.top, rcAi.right, rcAi.bottom, rr, rr);
                    COLORREF aiCol = data->ai_active ? RGB(0, 180, 0) : cDim;
                    SetTextColor(hdc, aiCol);
                    HFONT hPrevAi = (HFONT)SelectObject(hdc, data->hSmallFont);
                    DrawText(hdc, "AI", 2, &rcAi,
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hdc, hPrevAi);
                }
                /* ⚙ Cog (settings) */
                if (cogX > x) {
                    RECT rcCog = {cogX, btnY, cogX + btnSz, btnY + btnSz};
                    RoundRect(hdc, rcCog.left, rcCog.top, rcCog.right, rcCog.bottom, rr, rr);
                    SetTextColor(hdc, cDim);
                    DrawTextW(hdc, L"\x2699", -1, &rcCog,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }

                SelectObject(hdc, hOldBtnBr);
                SelectObject(hdc, hOldBtnPen);
                DeleteObject(rBtnBrush);
                DeleteObject(rBtnPen);
            }

            SelectObject(hdc, hOldFont);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = (int)(short)LOWORD(lParam);
            int my = (int)(short)HIWORD(lParam);

            int btnSz_h  = S(BTN_SIZE_BASE);
            int pad_h    = S(PAD_BASE);
            int btnGap_h = S(BTN_GAP_BASE);
            int indW_h   = S(INDICATOR_W_BASE);
            int indGap_h = S(INDICATOR_GAP_BASE);
            int closeSz_h = S(CLOSE_SIZE_BASE);
            int tabHPad_h = S(TAB_H_PAD_BASE);
            int overhead_h = TAB_OVERHEAD_S;
            int minW_h    = S(TAB_MIN_W_BASE);

            /* Hit test [+] button */
            if (mx >= pad_h && mx <= pad_h + btnSz_h) {
                if (data->on_new) data->on_new();
                return 0;
            }

            /* Hit test right-side buttons: [◀][▶][AI][⚙] */
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            int cogX   = rcClient.right - btnSz_h - pad_h;
            int aiX    = cogX - btnSz_h - btnGap_h;
            int rightX = aiX - btnSz_h - btnGap_h;
            int leftX  = rightX - btnSz_h - btnGap_h;

            if (mx >= cogX && mx <= cogX + btnSz_h) {
                if (data->on_settings) data->on_settings();
                return 0;
            }
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
            int tabH = rcClient.bottom - tabHPad_h;
            int tabY = tabHPad_h / 2;
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
                    int indH_h = tabH - S(10);
                    if (indH_h < S(4)) indH_h = S(4);
                    int indY_h = tabY + (tabH - indH_h) / 2;
                    if (mx >= indX_h && mx <= indX_h + indW_h &&
                        my >= indY_h && my <= indY_h + indH_h) {
                        if (data->on_status_click)
                            data->on_status_click(i, data->m.tabs[i].user_data,
                                                  data->m.tabs[i].status);
                        return 0;
                    }
                    /* Check log button */
                    int logBtnX = x + indGap_h + indW_h + indGap_h;
                    int logBtnH = tabH - S(6);
                    if (logBtnH < S(4)) logBtnH = S(4);
                    int logBtnY = tabY + (tabH - logBtnH) / 2;
                    if (mx >= logBtnX && mx <= logBtnX + indW_h &&
                        my >= logBtnY && my <= logBtnY + logBtnH) {
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
                x += tw + S(TAB_GAP_BASE);
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

void tabs_set_font(HWND hwnd, const char *font_name)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data || !font_name) return;
    (void)snprintf(data->font_name, sizeof(data->font_name), "%s", font_name);
    tabs_create_fonts(data, hwnd);
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
