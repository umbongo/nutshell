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

#endif /* _WIN32 */
