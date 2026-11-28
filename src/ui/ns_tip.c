/* src/ui/ns_tip.c -- the themed tooltip helper. See ns_tip.h. */

#ifdef _WIN32

#include "ns_tip.h"
#include "ns_draw.h"
#include "ns_scale.h"
#include "ns_type.h"
#include "themed_button.h"
#include <commctrl.h>
#include <string.h>

HWND ns_tip_create(HWND owner, int dpi, HFONT font, const ThemeTokens *tokens)
{
    HWND tip = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL,
                               WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               owner, NULL, GetModuleHandle(NULL), NULL);
    if (!tip) return NULL;

    /* Visual styles ignore TTM_SETTIPBKCOLOR / TTM_SETTIPTEXTCOLOR. */
    ns_control_classic(tip);
    if (tokens) {
        SendMessageA(tip, TTM_SETTIPBKCOLOR,
                     (WPARAM)theme_cr(tokens->raised.base), 0);
        SendMessageA(tip, TTM_SETTIPTEXTCOLOR,
                     (WPARAM)theme_cr(tokens->text_main), 0);
    }
    if (font) SendMessageA(tip, WM_SETFONT, (WPARAM)font, FALSE);

    SendMessageA(tip, TTM_SETMAXTIPWIDTH, 0, (LPARAM)ns_scale(360, dpi));
    int m = ns_scale(SP_XS, dpi);
    RECT margin = { m, m, m, m };
    SendMessageA(tip, TTM_SETMARGIN, 0, (LPARAM)&margin);
    SendMessageA(tip, TTM_SETDELAYTIME, TTDT_INITIAL, MAKELPARAM(500, 0));
    SendMessageA(tip, TTM_SETDELAYTIME, TTDT_RESHOW, MAKELPARAM(100, 0));
    SendMessageA(tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, MAKELPARAM(32767, 0));
    return tip;
}

int ns_tip_add(HWND tip, HWND tool, const char *text)
{
    if (!tip || !tool) return 0;
    TOOLINFOA ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(TOOLINFOA);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = GetParent(tool);
    ti.uId = (UINT_PTR)tool;
    ti.lpszText = (LPSTR)text;
    return (int)SendMessageA(tip, TTM_ADDTOOLA, 0, (LPARAM)&ti);
}

int ns_tip_add_combo(HWND tip, HWND combo, const char *text)
{
    COMBOBOXINFO cbi;
    memset(&cbi, 0, sizeof(cbi));
    cbi.cbSize = sizeof(cbi);
    if (!combo || !GetComboBoxInfo(combo, &cbi) || !cbi.hwndItem) return 0;
    return ns_tip_add(tip, cbi.hwndItem, text);
}

int ns_tip_add_rect(HWND tip, HWND host, UINT_PTR id, const RECT *rc,
                    const char *text)
{
    if (!tip || !host) return 0;
    TOOLINFOA ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(TOOLINFOA);
    ti.uFlags = TTF_SUBCLASS;
    ti.hwnd = host;
    ti.uId = id;
    if (rc) ti.rect = *rc;
    ti.lpszText = (LPSTR)text;
    return (int)SendMessageA(tip, TTM_ADDTOOLA, 0, (LPARAM)&ti);
}

void ns_tip_move_rect(HWND tip, HWND host, UINT_PTR id, const RECT *rc)
{
    if (!tip || !host) return;
    TOOLINFOA ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(TOOLINFOA);
    ti.hwnd = host;
    ti.uId = id;
    if (rc) ti.rect = *rc;
    SendMessageA(tip, TTM_NEWTOOLRECTA, 0, (LPARAM)&ti);
}

/* The id of the one tracking tool ns_tip_pop uses. */
#define NS_TIP_POP_ID ((UINT_PTR)0x4E5F01u)

static void pop_fill(TOOLINFOA *ti, HWND host)
{
    memset(ti, 0, sizeof(*ti));
    ti->cbSize = sizeof(TOOLINFOA);
    ti->hwnd = host;
    ti->uId = NS_TIP_POP_ID;
}

int ns_tip_pop(HWND tip, HWND host, const char *text, const RECT *anchor)
{
    if (!tip || !host || !text || !anchor) return 0;
    TOOLINFOA ti;
    pop_fill(&ti, host);
    if (!SendMessageA(tip, TTM_GETTOOLINFOA, 0, (LPARAM)&ti)) {
        pop_fill(&ti, host);
        ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
        ti.lpszText = (LPSTR)text;
        if (!SendMessageA(tip, TTM_ADDTOOLA, 0, (LPARAM)&ti)) return 0;
    }
    pop_fill(&ti, host);
    ti.lpszText = (LPSTR)text;
    SendMessageA(tip, TTM_UPDATETIPTEXTA, 0, (LPARAM)&ti);

    /* Place it below the control, or above when it would run off the monitor. */
    int x = anchor->left;
    int y = anchor->bottom + 2;
    LRESULT sz = SendMessageA(tip, TTM_GETBUBBLESIZE, 0, (LPARAM)&ti);
    int w = LOWORD(sz), h = HIWORD(sz);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromRect(anchor, MONITOR_DEFAULTTONEAREST);
    if (mon && GetMonitorInfo(mon, &mi) && h > 0) {
        if (y + h > mi.rcWork.bottom && anchor->top - 2 - h >= mi.rcWork.top)
            y = anchor->top - 2 - h;
        if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
        if (x < mi.rcWork.left) x = mi.rcWork.left;
    }
    SendMessageA(tip, TTM_TRACKPOSITION, 0, MAKELPARAM(x, y));
    SendMessageA(tip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
    return 1;
}

void ns_tip_unpop(HWND tip, HWND host)
{
    if (!tip || !host) return;
    TOOLINFOA ti;
    pop_fill(&ti, host);
    SendMessageA(tip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
}

#endif /* _WIN32 */
