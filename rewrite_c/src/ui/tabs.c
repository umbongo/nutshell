#include "tabs.h"
#include "xmalloc.h"
#include "logger.h"
#include "tooltip.h"
#include <stdio.h>
#include <commctrl.h>

#ifdef _WIN32

static const char *TABS_CLASS_NAME = "CongaSSH_Tabs";

/* ---- Layout constants ---------------------------------------------------- */
#define TAB_W       120   /* width of each tab */
#define TAB_H_PAD   4     /* vertical padding above/below tab */
#define ADD_BTN_W   24    /* [+] button width */
#define ADD_BTN_H   24
#define SET_BTN_W   30    /* [Set] button width */
#define TAB_START_X 36    /* x where first tab begins: 4 + ADD_BTN_W + 8 */
#define TAB_GAP     4     /* gap between tabs */
#define DOT_SIZE    8     /* status dot diameter */
#define CLOSE_SIZE  12    /* ✕ button size */

typedef struct TabControlData {
    TabManager m;

    TabSelectCallback  on_select;
    TabNewCallback     on_new;
    TabCloseCallback   on_close;
    TabSettingsCallback on_settings;

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
            NMHDR *nmhdr = (NMHDR *)lParam;
            if (data && data->hTooltip && nmhdr->hwndFrom == data->hTooltip &&
                (nmhdr->code == TTN_GETDISPINFOA || nmhdr->code == TTN_NEEDTEXTA)) {
                NMTTDISPINFOA *ttt = (NMTTDISPINFOA *)lParam;
                /* Determine which tab is under the cursor */
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                int tx = TAB_START_X;
                int tab_idx = -1;
                for (int i = 0; i < data->m.count; i++) {
                    if (pt.x >= tx && pt.x <= tx + TAB_W) { tab_idx = i; break; }
                    tx += TAB_W + TAB_GAP;
                }
                if (tab_idx >= 0) {
                    TabEntry *e = &data->m.tabs[tab_idx];
                    unsigned long elapsed = 0;
                    if (e->connect_ms > 0) {
                        ULONGLONG now_ms = GetTickCount64();
                        elapsed = (unsigned long)((now_ms - e->connect_ms) / 1000ULL);
                    }
                    static char tip_buf[256];
                    tooltip_build_text(e->status, e->host, e->username,
                                       elapsed, NULL, tip_buf, sizeof(tip_buf));
                    ttt->lpszText = tip_buf;
                } else {
                    ttt->lpszText = "";
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
            int btnH = ADD_BTN_H;
            int btnY = (rcClient.bottom - btnH) / 2;
            RECT rcAdd = {4, btnY, 4 + ADD_BTN_W, btnY + btnH};
            HBRUSH btnBrush = CreateSolidBrush(RGB(220, 220, 220));
            FillRect(hMemDC, &rcAdd, btnBrush);
            DeleteObject(btnBrush);
            DrawText(hMemDC, "+", -1, &rcAdd, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            /* ---- Tabs ---- */
            int tabH = rcClient.bottom - TAB_H_PAD;
            int tabY = TAB_H_PAD / 2;
            int x    = TAB_START_X;

            for (int i = 0; i < data->m.count; i++) {
                RECT rcTab = {x, tabY, x + TAB_W, tabY + tabH};

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

                /* ---- Status dot ---- */
                int dotX = x + 6;
                int dotY = tabY + (tabH - DOT_SIZE) / 2;
                HBRUSH dotBrush = CreateSolidBrush(status_color(data->m.tabs[i].status));
                HPEN dotPen     = CreatePen(PS_SOLID, 1, status_color(data->m.tabs[i].status));
                HPEN hOldDotPen  = (HPEN)SelectObject(hMemDC, dotPen);
                HBRUSH hOldDotBr = (HBRUSH)SelectObject(hMemDC, dotBrush);
                Ellipse(hMemDC, dotX, dotY, dotX + DOT_SIZE, dotY + DOT_SIZE);
                SelectObject(hMemDC, hOldDotBr);
                SelectObject(hMemDC, hOldDotPen);
                DeleteObject(dotBrush);
                DeleteObject(dotPen);

                /* ---- Title text ---- */
                RECT rcText = rcTab;
                rcText.left  += DOT_SIZE + 10;  /* after dot */
                rcText.right -= CLOSE_SIZE + 6; /* before close button */
                DrawText(hMemDC, data->m.tabs[i].title, -1, &rcText,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                /* ---- ✕ close button ---- */
                int closeX = x + TAB_W - CLOSE_SIZE - 4;
                int closeY = tabY + (tabH - CLOSE_SIZE) / 2;
                RECT rcClose = {closeX, closeY, closeX + CLOSE_SIZE, closeY + CLOSE_SIZE};
                SetTextColor(hMemDC, RGB(120, 120, 120));
                DrawText(hMemDC, "\xD7", -1, &rcClose,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SetTextColor(hMemDC, RGB(0, 0, 0));

                x += TAB_W + TAB_GAP;
            }

            /* ---- [Set] Settings button ---- */
            int setX = rcClient.right - SET_BTN_W - 4;
            if (setX > x) {
                RECT rcSet = {setX, btnY, setX + SET_BTN_W, btnY + btnH};
                HBRUSH setBrush = CreateSolidBrush(RGB(220, 220, 220));
                FillRect(hMemDC, &rcSet, setBrush);
                DeleteObject(setBrush);
                DrawText(hMemDC, "Set", -1, &rcSet, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
            if (mx >= 4 && mx <= 4 + ADD_BTN_W) {
                if (data->on_new) data->on_new();
                return 0;
            }

            /* Hit test tabs */
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            int tabH = rcClient.bottom - TAB_H_PAD;
            int tabY = TAB_H_PAD / 2;
            int x = TAB_START_X;

            for (int i = 0; i < data->m.count; i++) {
                if (mx >= x && mx <= x + TAB_W) {
                    /* Check ✕ close button first */
                    int closeX = x + TAB_W - CLOSE_SIZE - 4;
                    int closeY = tabY + (tabH - CLOSE_SIZE) / 2;
                    if (mx >= closeX && mx <= closeX + CLOSE_SIZE &&
                        my >= closeY && my <= closeY + CLOSE_SIZE) {
                        if (data->on_close)
                            data->on_close(i, data->m.tabs[i].user_data);
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
                x += TAB_W + TAB_GAP;
            }

            /* Hit test [Set] button */
            int setX = rcClient.right - SET_BTN_W - 4;
            if (mx >= setX && mx <= setX + SET_BTN_W) {
                if (data->on_settings) data->on_settings();
            }
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
    int idx = tabmgr_add(&data->m, title, user_data);
    InvalidateRect(hwnd, NULL, FALSE);
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
                        TabSelectCallback  on_select,
                        TabNewCallback     on_new,
                        TabCloseCallback   on_close,
                        TabSettingsCallback on_settings)
{
    TabControlData *data = (TabControlData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!data) return;
    data->on_select   = on_select;
    data->on_new      = on_new;
    data->on_close    = on_close;
    data->on_settings = on_settings;
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

#endif
