#ifndef NUTSHELL_THEMED_BUTTON_H
#define NUTSHELL_THEMED_BUTTON_H

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#include <commctrl.h>
#include "ui_theme.h"
#include "theme.h"  /* theme_is_dark() */

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
 * Draw a themed owner-draw button.
 *   dis       — DRAWITEMSTRUCT from WM_DRAWITEM
 *   theme     — active ThemeColors
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
    int pressed = (dis->itemState & ODS_SELECTED) != 0;

    /* Colours */
    unsigned int bg_rgb = is_primary ? theme->accent : theme->bg_secondary;
    unsigned int fg_rgb = is_primary ? 0xFFFFFF : theme->text_main;
    if (pressed) {
        /* Darken by ~20% */
        unsigned int r = (bg_rgb >> 16) & 0xFF;
        unsigned int g = (bg_rgb >> 8)  & 0xFF;
        unsigned int b =  bg_rgb        & 0xFF;
        r = r * 4 / 5; g = g * 4 / 5; b = b * 4 / 5;
        bg_rgb = (r << 16) | (g << 8) | b;
    }

    COLORREF bg_cr = theme_cr(bg_rgb);
    COLORREF fg_cr = theme_cr(fg_rgb);
    COLORREF border_cr = theme_cr(theme->border);

    /* Clear corners with parent background so RoundRect corners are clean */
    HBRUSH hBgBr = CreateSolidBrush(theme_cr(theme->bg_primary));
    FillRect(hdc, &rc, hBgBr);
    DeleteObject(hBgBr);

    /* Round-rect background with border */
    HBRUSH hBr = CreateSolidBrush(bg_cr);
    HPEN hPen = CreatePen(PS_SOLID, 1, border_cr);
    HGDIOBJ oldBr = SelectObject(hdc, hBr);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 4, 4);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    DeleteObject(hPen);
    DeleteObject(hBr);

    /* Text */
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, fg_cr);

    wchar_t text[64];
    GetWindowTextW(dis->hwndItem, text, (int)(sizeof(text)/sizeof(text[0])));
    DrawTextW(hdc, text, -1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Focus rect */
    if (dis->itemState & ODS_FOCUS) {
        RECT fr = rc;
        InflateRect(&fr, -3, -3);
        DrawFocusRect(hdc, &fr);
    }
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
