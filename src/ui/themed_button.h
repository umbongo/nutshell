#ifndef NUTSHELL_THEMED_BUTTON_H
#define NUTSHELL_THEMED_BUTTON_H

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#include <commctrl.h>
#include "ui_theme.h"
#include "theme.h"  /* theme_is_dark() */
#include "ns_draw.h"
#include "ns_tokens.h"
#include "dpi_util.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

/* Convert 0xRRGGBB to COLORREF */
static inline COLORREF theme_cr(unsigned int rgb)
{
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

/*
 * Apply dark/light title bar to a window based on the theme's bg_primary.
 * Call from WM_CREATE after setting up the theme.
 */
static inline void themed_apply_title_bar(HWND hwnd, const ThemeColors *theme)
{
    if (!hwnd || !theme) return;
    BOOL dark = theme_is_dark(theme->bg_primary) ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                          &dark, sizeof(dark));
}

/*
 * Hover tracking for owner-draw themed buttons. ns_draw_button() needs a
 * hover state, and the plain BS_OWNERDRAW "Button" class does not expose
 * one, so a tiny subclass tracks WM_MOUSEMOVE/WM_MOUSELEAVE per button and
 * stamps a window property with the result. The core hover tracker
 * (ns_hover) arrives in Task 6; this is deliberately minimal.
 */

#define THEMED_BUTTON_HOVER_SUBCLASS_ID 43
#define THEMED_BUTTON_HOVER_PROP "NutshellThemedButtonHover"

static inline LRESULT CALLBACK themed_button_hover_subclass(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    (void)dwRefData;
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!GetPropA(hwnd, THEMED_BUTTON_HOVER_PROP)) {
            SetPropA(hwnd, THEMED_BUTTON_HOVER_PROP, (HANDLE)1);
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        if (GetPropA(hwnd, THEMED_BUTTON_HOVER_PROP)) {
            RemovePropA(hwnd, THEMED_BUTTON_HOVER_PROP);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case WM_NCDESTROY:
        RemovePropA(hwnd, THEMED_BUTTON_HOVER_PROP);
        RemoveWindowSubclass(hwnd, themed_button_hover_subclass, uIdSubclass);
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

/* Idempotent: SetWindowSubclass no-ops if this proc/id is already
 * installed on hwnd, so callers may call it on every paint. */
static inline void themed_button_track_hover(HWND hwnd)
{
    if (!hwnd) return;
    SetWindowSubclass(hwnd, themed_button_hover_subclass,
                      THEMED_BUTTON_HOVER_SUBCLASS_ID, 0);
}

static inline int themed_button_is_hot(HWND hwnd)
{
    return hwnd && GetPropA(hwnd, THEMED_BUTTON_HOVER_PROP) != NULL;
}

/*
 * Draw a themed owner-draw button through ns_draw_button().
 *   dis       — DRAWITEMSTRUCT from WM_DRAWITEM
 *   theme     — active ThemeColors (only bg_primary, for corner clearing)
 *   is_primary — 1 for accent-coloured (Save, Connect, Send),
 *                0 for secondary (Cancel, New, Edit, Delete)
 */
static inline void draw_themed_button(LPDRAWITEMSTRUCT dis,
                                       const ThemeColors *theme,
                                       int is_primary)
{
    if (!dis || !theme) return;

    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    /* Clear corners with parent background so the rounded fill sits on a
     * clean background. */
    HBRUSH hBgBr = CreateSolidBrush(theme_cr(theme->bg_primary));
    FillRect(hdc, &rc, hBgBr);
    DeleteObject(hBgBr);

    themed_button_track_hover(dis->hwndItem);

    const ThemeSurface *surface = is_primary ? &ns_tokens()->accent
                                              : &ns_tokens()->bg_secondary;

    NsBtnState state;
    if (dis->itemState & ODS_DISABLED)
        state = NS_BTN_DISABLED;
    else if (dis->itemState & ODS_SELECTED)
        state = NS_BTN_PRESSED;
    else if (themed_button_is_hot(dis->hwndItem))
        state = NS_BTN_HOVER;
    else
        state = NS_BTN_REST;

    int focused = (dis->itemState & ODS_FOCUS) ? 1 : 0;

    wchar_t wtext[64];
    GetWindowTextW(dis->hwndItem, wtext, (int)(sizeof(wtext)/sizeof(wtext[0])));
    char text[256];
    WideCharToMultiByte(CP_UTF8, 0, wtext, -1, text, (int)sizeof(text),
                        NULL, NULL);

    HFONT font = (HFONT)SendMessage(dis->hwndItem, WM_GETFONT, 0, 0);
    int dpi = get_window_dpi(dis->hwndItem);

    ns_draw_button(hdc, &rc, surface, state, focused, text, font, dpi);
}

/*
 * Themed border + combobox dropdown subclass.
 * dwRefData = pointer to a static ThemeColors (always valid).
 */

#define THEMED_BORDER_SUBCLASS_ID 42

static inline void themed_paint_nc(HWND hwnd, const ThemeColors *theme)
{
    HDC hdc = GetWindowDC(hwnd);
    if (!hdc) return;

    RECT rcWin;
    GetWindowRect(hwnd, &rcWin);
    POINT pt = {0, 0};
    ClientToScreen(hwnd, &pt);
    int bx = pt.x - rcWin.left;
    int by = pt.y - rcWin.top;
    int w = rcWin.right - rcWin.left;
    int h = rcWin.bottom - rcWin.top;
    COLORREF border_cr = theme_cr(theme->border);
    HBRUSH hBr = CreateSolidBrush(border_cr);
    RECT r;
    r = (RECT){0, 0, w, by};        FillRect(hdc, &r, hBr);
    r = (RECT){0, h - by, w, h};    FillRect(hdc, &r, hBr);
    r = (RECT){0, by, bx, h - by};  FillRect(hdc, &r, hBr);
    r = (RECT){w - bx, by, w, h - by}; FillRect(hdc, &r, hBr);
    DeleteObject(hBr);
    ReleaseDC(hwnd, hdc);
}

static inline void themed_paint_combo_button(HWND hwnd, const ThemeColors *theme)
{
    /* Repaint entire combobox: background, text, border, and dropdown arrow */
    HDC hdc = GetDC(hwnd);
    if (!hdc) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int arrow_w = GetSystemMetrics(SM_CXVSCROLL);

    /* Fill entire background with bg_secondary */
    HBRUSH hBgBr = CreateSolidBrush(theme_cr(theme->bg_secondary));
    FillRect(hdc, &rc, hBgBr);
    DeleteObject(hBgBr);

    /* Draw 1px border around the entire combobox */
    HBRUSH hBorderBr = CreateSolidBrush(theme_cr(theme->border));
    FrameRect(hdc, &rc, hBorderBr);
    DeleteObject(hBorderBr);

    /* Draw separator line between text and arrow */
    RECT arrow_rc = {rc.right - arrow_w, rc.top, rc.right, rc.bottom};
    HPEN hPen = CreatePen(PS_SOLID, 1, theme_cr(theme->border));
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    MoveToEx(hdc, arrow_rc.left, arrow_rc.top + 1, NULL);
    LineTo(hdc, arrow_rc.left, arrow_rc.bottom - 1);
    SelectObject(hdc, oldPen);
    DeleteObject(hPen);

    /* Draw the ▼ arrow glyph */
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, theme_cr(theme->text_dim));
    DrawTextW(hdc, L"\x25BC", 1, &arrow_rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Redraw the selected text in the text area */
    char text[128];
    int len = GetWindowTextA(hwnd, text, (int)sizeof(text));
    if (len > 0) {
        RECT text_rc = {rc.left + 4, rc.top + 1,
                        rc.right - arrow_w - 1, rc.bottom - 1};
        SetTextColor(hdc, theme_cr(theme->text_main));
        HFONT hFont = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
        HGDIOBJ oldFont = NULL;
        if (hFont) oldFont = SelectObject(hdc, hFont);
        DrawTextA(hdc, text, len, &text_rc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (oldFont) SelectObject(hdc, oldFont);
    }

    ReleaseDC(hwnd, hdc);
}

static inline LRESULT CALLBACK themed_border_subclass(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    const ThemeColors *theme = (const ThemeColors *)dwRefData;

    if (msg == WM_NCPAINT) {
        themed_paint_nc(hwnd, theme);
        return 0;
    }

    /* For ComboBox: repaint the dropdown button after default paint */
    if (msg == WM_PAINT) {
        char cls[16];
        GetClassNameA(hwnd, cls, (int)sizeof(cls));
        if (_stricmp(cls, "ComboBox") == 0) {
            LRESULT lr = DefSubclassProc(hwnd, msg, wParam, lParam);
            themed_paint_combo_button(hwnd, theme);
            return lr;
        }
    }

    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, themed_border_subclass, uIdSubclass);
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

/*
 * EnumChildWindows callback: subclass EDIT, COMBOBOX, and LISTBOX
 * children to draw themed borders.  lParam = ThemeColors pointer.
 */
static inline BOOL CALLBACK themed_border_enum(HWND hChild, LPARAM lParam)
{
    char cls[32];
    GetClassNameA(hChild, cls, (int)sizeof(cls));
    if (_stricmp(cls, "Edit") == 0 ||
        _stricmp(cls, "ListBox") == 0 ||
        _stricmp(cls, "RichEdit20W") == 0) {
        LONG style = GetWindowLong(hChild, GWL_STYLE);
        LONG exStyle = GetWindowLong(hChild, GWL_EXSTYLE);
        if ((style & WS_BORDER) || (exStyle & WS_EX_CLIENTEDGE)) {
            SetWindowSubclass(hChild, themed_border_subclass,
                              THEMED_BORDER_SUBCLASS_ID,
                              (DWORD_PTR)lParam);
        }
    }
    /* Always subclass ComboBox — needs dropdown arrow theming */
    if (_stricmp(cls, "ComboBox") == 0) {
        SetWindowSubclass(hChild, themed_border_subclass,
                          THEMED_BORDER_SUBCLASS_ID,
                          (DWORD_PTR)lParam);
    }
    return TRUE;
}

/*
 * Apply themed borders to all EDIT/COMBOBOX/LISTBOX children of hwnd.
 * Call once after all child controls are created (end of WM_CREATE).
 * theme must point to a ThemeColors with static lifetime.
 */
static inline void themed_apply_borders(HWND hwnd, const ThemeColors *theme)
{
    if (!hwnd || !theme) return;
    EnumChildWindows(hwnd, themed_border_enum, (LPARAM)theme);
}

#endif /* _WIN32 */
#endif /* NUTSHELL_THEMED_BUTTON_H */
