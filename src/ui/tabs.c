#include "tabs.h"
#include "xmalloc.h"
#include "logger.h"
#include "tooltip.h"
#include <stdio.h>
#include <commctrl.h>

#ifdef _WIN32

static const char *TABS_CLASS_NAME = "Nutshell_Tabs";

/* ---- Layout constants ---------------------------------------------------- */
#define TAB_H_PAD   4     /* vertical padding above/below tab */
#define BTN_SIZE    24    /* all non-tab buttons are 24×24 squares */
#define TAB_START_X 36    /* x where first tab begins: 4 + BTN_SIZE + 8 */
#define TAB_GAP     4     /* gap between tabs */
#define BTN_GAP     2     /* gap between right-side buttons */
#define DOT_SIZE    8     /* status dot diameter (hit-test legacy; use INDICATOR_* for rendering) */
#define INDICATOR_W  12   /* indicator width: 50% wider than original 8 */
#define INDICATOR_GAP 3   /* equal gap: before status, between, after log */
#define CLOSE_SIZE  12    /* ✕ button size */
/* Fixed pixel overhead per tab: left-side indicators + right-side close btn */
#define TAB_OVERHEAD (INDICATOR_GAP + INDICATOR_W + INDICATOR_GAP \
                      + INDICATOR_W + INDICATOR_GAP + CLOSE_SIZE + 10)
#define TAB_MIN_W   80    /* minimum tab width */

/* Return the pixel width needed to show title in full. */
static int tab_w(HDC hdc, const char *title)
{
    SIZE sz = {0, 0};
    if (title && title[0])
        GetTextExtentPoint32A(hdc, title, (int)strlen(title), &sz);
    int w = sz.cx + TAB_OVERHEAD;
    return w < TAB_MIN_W ? TAB_MIN_W : w;
}

/* 15% darker than inactive tabs RGB(230,230,230) → RGB(196,196,196) */
#define BTN_COLOR   RGB(196, 196, 196)

typedef struct TabControlData {
    TabManager m;

    TabSelectCallback    on_select;
    TabNewCallback       on_new;
    TabCloseCallback     on_close;
    TabSettingsCallback  on_settings;
    TabLogToggleCallback on_log_toggle;

    HFONT hFont;
    HWND  hTooltip;  /* Win32 tooltip control */
} TabControlData;

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

static LRESULT CALLBACK TabsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            data = xcalloc(1, sizeof(TabControlData));
            tabmgr_init(&data->m);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)data);

            HDC hdc = GetDC(hwnd);
            int logPixelsY = GetDeviceCaps(hdc, LOGPIXELSY);
            ReleaseDC(hwnd, hdc);
            int height = -MulDiv(9, logPixelsY, 72);
            data->hFont = CreateFont(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     VARIABLE_PITCH | FF_SWISS, "Segoe UI");

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
                if (data->hFont)    DeleteObject(data->hFont);
                if (data->hTooltip) DestroyWindow(data->hTooltip);
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
                int tx = TAB_START_X;
                int tab_idx = -1;
                for (int i = 0; i < data->m.count; i++) {
                    int tw = tab_w(hdc_m, data->m.tabs[i].title);
                    if (pt.x >= tx && pt.x <= tx + tw) { tab_idx = i; break; }
                    tx += tw + TAB_GAP;
                }
                SelectObject(hdc_m, hOldF);
                ReleaseDC(hwnd, hdc_m);
                /* Check if cursor is over right-side buttons */
                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                int cogX   = rcClient.right - BTN_SIZE - 4;
                int rightX = cogX - BTN_SIZE - BTN_GAP;
                int leftX  = rightX - BTN_SIZE - BTN_GAP;
                if (pt.x >= cogX && pt.x <= cogX + BTN_SIZE) {
                    ttt->lpszText = (LPSTR)"Settings";
                } else if (pt.x >= leftX && pt.x <= leftX + BTN_SIZE) {
                    ttt->lpszText = (LPSTR)"Previous tab";
                } else if (pt.x >= rightX && pt.x <= rightX + BTN_SIZE) {
                    ttt->lpszText = (LPSTR)"Next tab";
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

            /* Double buffering */
            HDC hMemDC = CreateCompatibleDC(hdc);
            HBITMAP hBitmap = CreateCompatibleBitmap(hdc, rcClient.right, rcClient.bottom);
            HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);

            /* Background */
            HBRUSH bgBrush = CreateSolidBrush(RGB(242, 242, 242));
            FillRect(hMemDC, &rcClient, bgBrush);
            DeleteObject(bgBrush);

            HFONT hOldFont = (HFONT)SelectObject(hMemDC, data->hFont);
            SetBkMode(hMemDC, TRANSPARENT);

            /* ---- [+] Add button ---- */
            int btnY = (rcClient.bottom - BTN_SIZE) / 2;
            RECT rcAdd = {4, btnY, 4 + BTN_SIZE, btnY + BTN_SIZE};
            HBRUSH btnBrush = CreateSolidBrush(BTN_COLOR);
            FillRect(hMemDC, &rcAdd, btnBrush);
            DeleteObject(btnBrush);
            DrawText(hMemDC, "+", -1, &rcAdd, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            /* ---- Tabs ---- */
            int tabH = rcClient.bottom - TAB_H_PAD;
            int tabY = TAB_H_PAD / 2;
            int x    = TAB_START_X;

            for (int i = 0; i < data->m.count; i++) {
                int tw = tab_w(hMemDC, data->m.tabs[i].title);
                RECT rcTab = {x, tabY, x + tw, tabY + tabH};

                /* Background & border */
                HPEN hPen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
                HPEN hOldPen = (HPEN)SelectObject(hMemDC, hPen);
                HBRUSH hTabBrush = (i == data->m.active_index)
                                   ? CreateSolidBrush(RGB(255, 255, 255))
                                   : CreateSolidBrush(RGB(230, 230, 230));
                HBRUSH hOldBrush = (HBRUSH)SelectObject(hMemDC, hTabBrush);

                RoundRect(hMemDC, rcTab.left, rcTab.top, rcTab.right, rcTab.bottom, 6, 6);

                SelectObject(hMemDC, hOldBrush);
                SelectObject(hMemDC, hOldPen);
                DeleteObject(hTabBrush);
                DeleteObject(hPen);

                /* Indicator dimensions: full inner height minus 3 px top+bottom */
                int indicH = tabH - 6;
                if (indicH < 4) indicH = 4;

                /* ---- Status indicator (INDICATOR_W × indicH rounded rect) ---- */
                int indX = x + INDICATOR_GAP;
                int indY = tabY + (tabH - indicH) / 2;
                {
                    HBRUSH sBrush = CreateSolidBrush(status_color(data->m.tabs[i].status));
                    HPEN sPen     = CreatePen(PS_SOLID, 1, status_color(data->m.tabs[i].status));
                    HPEN hOldSPen  = (HPEN)SelectObject(hMemDC, sPen);
                    HBRUSH hOldSBr = (HBRUSH)SelectObject(hMemDC, sBrush);
                    RoundRect(hMemDC, indX, indY, indX + INDICATOR_W, indY + indicH, 3, 3);
                    SelectObject(hMemDC, hOldSBr);
                    SelectObject(hMemDC, hOldSPen);
                    DeleteObject(sBrush);
                    DeleteObject(sPen);
                }

                /* ---- Log button (INDICATOR_W × indicH rounded rect with "L") ---- */
                int logX = indX + INDICATOR_W + INDICATOR_GAP;
                {
                    int logY = indY;
                    COLORREF logCol = data->m.tabs[i].logging
                                    ? RGB(0, 160, 80)    /* green when on */
                                    : RGB(180, 180, 180); /* gray when off */
                    HBRUSH lBrush = CreateSolidBrush(logCol);
                    HPEN lPen     = CreatePen(PS_SOLID, 1, logCol);
                    HPEN hOldLPen  = (HPEN)SelectObject(hMemDC, lPen);
                    HBRUSH hOldLBr = (HBRUSH)SelectObject(hMemDC, lBrush);
                    RoundRect(hMemDC, logX, logY, logX + INDICATOR_W, logY + indicH, 3, 3);
                    SelectObject(hMemDC, hOldLBr);
                    SelectObject(hMemDC, hOldLPen);
                    DeleteObject(lBrush);
                    DeleteObject(lPen);

                    /* "L" label — scaled to indicator height */
                    int fh = -(indicH * 7 / 10);  /* ~70% of indicator height */
                    HFONT hSmall = CreateFont(fh, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                              VARIABLE_PITCH | FF_SWISS, "Segoe UI");
                    HFONT hPrevFont = (HFONT)SelectObject(hMemDC, hSmall);
                    SetTextColor(hMemDC, RGB(0, 0, 0));
                    RECT rcL = {logX, logY, logX + INDICATOR_W, logY + indicH};
                    DrawText(hMemDC, "L", 1, &rcL, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hMemDC, hPrevFont);
                    DeleteObject(hSmall);
                }

                /* ---- Title text ---- */
                RECT rcText = rcTab;
                rcText.left  += INDICATOR_GAP + INDICATOR_W + INDICATOR_GAP
                                + INDICATOR_W + INDICATOR_GAP; /* after status + log */
                rcText.right -= CLOSE_SIZE + 6; /* before close button */
                DrawText(hMemDC, data->m.tabs[i].title, -1, &rcText,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                /* ---- ✕ close button ---- */
                int closeX = x + tw - CLOSE_SIZE - 4;
                int closeY = tabY + (tabH - CLOSE_SIZE) / 2;
                RECT rcClose = {closeX, closeY, closeX + CLOSE_SIZE, closeY + CLOSE_SIZE};
                SetTextColor(hMemDC, RGB(120, 120, 120));
                DrawTextW(hMemDC, L"\x00D7", -1, &rcClose,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SetTextColor(hMemDC, RGB(0, 0, 0));

                x += tw + TAB_GAP;
            }

            /* ---- Right-side buttons: [◀][▶][⚙] ---- */
            {
                int cogX   = rcClient.right - BTN_SIZE - 4;
                int rightX = cogX - BTN_SIZE - BTN_GAP;
                int leftX  = rightX - BTN_SIZE - BTN_GAP;

                HBRUSH rBtnBrush = CreateSolidBrush(BTN_COLOR);

                /* ◀ Left arrow */
                if (leftX > x) {
                    RECT rcLeft = {leftX, btnY, leftX + BTN_SIZE, btnY + BTN_SIZE};
                    FillRect(hMemDC, &rcLeft, rBtnBrush);
                    DrawTextW(hMemDC, L"\x25C0", -1, &rcLeft,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                /* ▶ Right arrow */
                if (rightX > x) {
                    RECT rcRight = {rightX, btnY, rightX + BTN_SIZE, btnY + BTN_SIZE};
                    FillRect(hMemDC, &rcRight, rBtnBrush);
                    DrawTextW(hMemDC, L"\x25B6", -1, &rcRight,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                /* ⚙ Cog (settings) */
                if (cogX > x) {
                    RECT rcCog = {cogX, btnY, cogX + BTN_SIZE, btnY + BTN_SIZE};
                    FillRect(hMemDC, &rcCog, rBtnBrush);
                    DrawTextW(hMemDC, L"\x2699", -1, &rcCog,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }

                DeleteObject(rBtnBrush);
            }

            SelectObject(hMemDC, hOldFont);
            BitBlt(hdc, 0, 0, rcClient.right, rcClient.bottom, hMemDC, 0, 0, SRCCOPY);
            SelectObject(hMemDC, hOldBitmap);
            DeleteObject(hBitmap);
            DeleteDC(hMemDC);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = (int)(short)LOWORD(lParam);
            int my = (int)(short)HIWORD(lParam);

            /* Hit test [+] button */
            if (mx >= 4 && mx <= 4 + BTN_SIZE) {
                if (data->on_new) data->on_new();
                return 0;
            }

            /* Hit test right-side buttons: [◀][▶][⚙] */
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            int cogX   = rcClient.right - BTN_SIZE - 4;
            int rightX = cogX - BTN_SIZE - BTN_GAP;
            int leftX  = rightX - BTN_SIZE - BTN_GAP;

            if (mx >= cogX && mx <= cogX + BTN_SIZE) {
                if (data->on_settings) data->on_settings();
                return 0;
            }
            if (mx >= leftX && mx <= leftX + BTN_SIZE) {
                int new_idx = tabmgr_navigate(&data->m, -1);
                if (new_idx >= 0) {
                    InvalidateRect(hwnd, NULL, FALSE);
                    if (data->on_select)
                        data->on_select(new_idx, data->m.tabs[new_idx].user_data);
                }
                return 0;
            }
            if (mx >= rightX && mx <= rightX + BTN_SIZE) {
                int new_idx = tabmgr_navigate(&data->m, 1);
                if (new_idx >= 0) {
                    InvalidateRect(hwnd, NULL, FALSE);
                    if (data->on_select)
                        data->on_select(new_idx, data->m.tabs[new_idx].user_data);
                }
                return 0;
            }

            /* Hit test tabs */
            int tabH = rcClient.bottom - TAB_H_PAD;
            int tabY = TAB_H_PAD / 2;
            int x = TAB_START_X;

            HDC hdc_ht = GetDC(hwnd);
            HFONT hOldHt = (HFONT)SelectObject(hdc_ht, data->hFont);

            for (int i = 0; i < data->m.count; i++) {
                int tw = tab_w(hdc_ht, data->m.tabs[i].title);
                if (mx >= x && mx <= x + tw) {
                    SelectObject(hdc_ht, hOldHt);
                    ReleaseDC(hwnd, hdc_ht);
                    /* Check ✕ close button first */
                    int closeX = x + tw - CLOSE_SIZE - 4;
                    int closeY = tabY + (tabH - CLOSE_SIZE) / 2;
                    if (mx >= closeX && mx <= closeX + CLOSE_SIZE &&
                        my >= closeY && my <= closeY + CLOSE_SIZE) {
                        if (data->on_close)
                            data->on_close(i, data->m.tabs[i].user_data);
                        return 0;
                    }
                    /* Check log button (INDICATOR_W wide, full inner height) */
                    int logBtnX = x + INDICATOR_GAP + INDICATOR_W + INDICATOR_GAP;
                    int logBtnH = tabH - 6;
                    if (logBtnH < 4) logBtnH = 4;
                    int logBtnY = tabY + (tabH - logBtnH) / 2;
                    if (mx >= logBtnX && mx <= logBtnX + INDICATOR_W &&
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
                x += tw + TAB_GAP;
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

#endif
