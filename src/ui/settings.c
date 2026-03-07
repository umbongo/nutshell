#include "settings_dlg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <commctrl.h>

/* ---- Control IDs -------------------------------------------------------- */

#define IDC_FONT_COMBO      1001
#define IDC_FONTSIZE_COMBO  1002
#define IDC_SCROLLBACK_EDIT 1003
#define IDC_PASTEDELAY_EDIT 1004
#define IDC_FG_SWATCH       1005
#define IDC_BG_SWATCH       1007
#define IDC_LOG_CHECK       1009
#define IDC_LOG_DIR_EDIT    1010
#define IDC_LOG_FMT_EDIT    1011
#define IDC_SCHEME_COMBO    1012

static const char *SETTINGS_CLASS = "Nutshell_Settings";

/* ---- Font list ---------------------------------------------------------- */

/* 10 SSH-compatible monospace fonts.
 * CBS_DROPDOWNLIST prevents free-text entry — only these may be chosen. */
static const char * const k_fonts[] = {
    "Consolas",
    "Cascadia Code",
    "Cascadia Mono",
    "Courier New",
    "Lucida Console",
    "Lucida Sans Typewriter",
    "Fira Code",
    "JetBrains Mono",
    "Source Code Pro",
    "Hack",
};
#define NUM_FONTS ((int)(sizeof(k_fonts) / sizeof(k_fonts[0])))

/* ---- Discrete font sizes ------------------------------------------------ */

/* Must match k_allowed_sizes in loader.c and window.c. */
static const int k_font_sizes[] = { 6, 8, 10, 12, 14, 16, 18, 20 };
#define NUM_FONT_SIZES ((int)(sizeof(k_font_sizes) / sizeof(k_font_sizes[0])))

/* ---- Colour scheme list ------------------------------------------------- */

typedef struct { const char *name; const char *fg; const char *bg; } ColorScheme;

/* 10 curated terminal colour presets.  Scheme 0 is the application default. */
static const ColorScheme k_schemes[] = {
    { "Default",         "#000000", "#FFFFFF" },
    { "Solarized Dark",  "#839496", "#002B36" },
    { "Solarized Light", "#657B83", "#FDF6E3" },
    { "Dracula",         "#F8F8F2", "#282A36" },
    { "Nord",            "#D8DEE9", "#2E3440" },
    { "Monokai",         "#F8F8F2", "#272822" },
    { "One Dark",        "#ABB2BF", "#282C34" },
    { "Gruvbox Dark",    "#EBDBB2", "#282828" },
    { "Cobalt Blue",     "#FFFFFF", "#002240" },
    { "Classic Dark",    "#FFFFFF", "#000000" },
};
#define NUM_SCHEMES ((int)(sizeof(k_schemes) / sizeof(k_schemes[0])))

/* ---- Dialog state ------------------------------------------------------- */

typedef struct {
    Config  *cfg;
    COLORREF fg;
    COLORREF bg;
    HBRUSH   hFgBrush;
    HBRUSH   hBgBrush;
    HWND     hFgSwatch;
    HWND     hBgSwatch;
    HWND     hTooltip;
    HFONT    hDlgFont;   /* MS Shell Dlg 8pt — applied to all child controls */
} SettingsDlgData;

/* ---- Colour helpers ------------------------------------------------------ */

/* Parse "#RRGGBB" -> COLORREF (0x00BBGGRR). Returns 0 on failure. */
static COLORREF hex_to_colorref(const char *hex)
{
    if (!hex || hex[0] != '#' || strlen(hex) < 7) return (COLORREF)0;
    unsigned int r = 0, g = 0, b = 0;
    if (sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b) != 3) return (COLORREF)0;
    return RGB((BYTE)r, (BYTE)g, (BYTE)b);
}

/* Format COLORREF -> "#RRGGBB" string. */
static void colorref_to_hex(COLORREF c, char *buf, size_t sz)
{
    (void)snprintf(buf, sz, "#%02X%02X%02X",
                   (unsigned)GetRValue(c),
                   (unsigned)GetGValue(c),
                   (unsigned)GetBValue(c));
}

/* ---- Layout helpers ----------------------------------------------------- */

static HWND make_label(HWND parent, const char *text, int x, int y, int w)
{
    return CreateWindow("STATIC", text,
        WS_VISIBLE | WS_CHILD | SS_RIGHT,
        x, y + 3, w, 18, parent, NULL, NULL, NULL);
}

static HWND make_edit(HWND parent, const char *text,
                      int x, int y, int w, HMENU id)
{
    return CreateWindow("EDIT", text,
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        x, y, w, 23, parent, id, NULL, NULL);
}

/* Drop-down list combo.  drop_h = total window height including dropdown. */
static HWND make_combo(HWND parent, int x, int y, int w, int drop_h, HMENU id)
{
    return CreateWindow("COMBOBOX", "",
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | CBS_HASSTRINGS | WS_VSCROLL,
        x, y, w, drop_h, parent, id, NULL, NULL);
}

/* EnumChildWindows callback: send WM_SETFONT to every child control. */
static BOOL CALLBACK SetFontProc(HWND hChild, LPARAM lParam)
{
    SendMessage(hChild, WM_SETFONT, (WPARAM)(HFONT)lParam, (LPARAM)TRUE);
    return TRUE;
}

/* ---- Window procedure --------------------------------------------------- */

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam)
{
    SettingsDlgData *d = (SettingsDlgData *)(LONG_PTR)
                         GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        LPCREATESTRUCT cs = (LPCREATESTRUCT)lParam;
        SettingsDlgData *nd = (SettingsDlgData *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)nd);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int cw = rc.right;
        int ch = rc.bottom;

        /* Column geometry */
        int lx = 10;   /* label x       */
        int lw = 120;  /* label width    */
        int ex = 135;  /* control x      */
        int ew = 200;  /* default edit w */

        /* Row 1: Font (combobox — only the listed fonts may be chosen) */
        make_label(hwnd, "Font:", lx, 20, lw);
        {
            HWND hCombo = make_combo(hwnd, ex, 20, ew, 200, (HMENU)IDC_FONT_COMBO);
            for (int i = 0; i < NUM_FONTS; i++)
                SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)k_fonts[i]);
            /* Pre-select font from config; fall back to index 0 if not listed */
            int sel = 0;
            for (int i = 0; i < NUM_FONTS; i++) {
                if (_stricmp(nd->cfg->settings.font, k_fonts[i]) == 0) {
                    sel = i;
                    break;
                }
            }
            SendMessage(hCombo, CB_SETCURSEL, (WPARAM)sel, 0);
        }

        /* Row 2: Font size (discrete combo — only allowed sizes may be chosen) */
        make_label(hwnd, "Font Size:", lx, 55, lw);
        {
            HWND hSz = make_combo(hwnd, ex, 55, 80, 180, (HMENU)IDC_FONTSIZE_COMBO);
            int sel = 0;
            for (int i = 0; i < NUM_FONT_SIZES; i++) {
                char buf[8];
                (void)snprintf(buf, sizeof(buf), "%d", k_font_sizes[i]);
                SendMessage(hSz, CB_ADDSTRING, 0, (LPARAM)buf);
                if (k_font_sizes[i] == nd->cfg->settings.font_size)
                    sel = i;
            }
            SendMessage(hSz, CB_SETCURSEL, (WPARAM)sel, 0);
        }

        /* Row 3: Scrollback lines */
        {
            char buf[16];
            (void)snprintf(buf, sizeof(buf), "%d",
                           nd->cfg->settings.scrollback_lines);
            make_label(hwnd, "Scrollback Lines:", lx, 90, lw);
            make_edit(hwnd, buf, ex, 90, 80, (HMENU)IDC_SCROLLBACK_EDIT);
        }

        /* Row 4: Paste delay */
        {
            char buf[16];
            (void)snprintf(buf, sizeof(buf), "%d",
                           nd->cfg->settings.paste_delay_ms);
            make_label(hwnd, "Paste Delay (ms):", lx, 125, lw);
            make_edit(hwnd, buf, ex, 125, 80, (HMENU)IDC_PASTEDELAY_EDIT);
        }

        /* Rows 5-6: Colour swatches (read-only display of selected scheme) */
        make_label(hwnd, "Foreground:", lx, 160, lw);
        nd->hFgSwatch = CreateWindow("STATIC", "",
            WS_VISIBLE | WS_CHILD | WS_BORDER | SS_NOTIFY,
            ex, 160, 40, 23, hwnd, (HMENU)IDC_FG_SWATCH, NULL, NULL);

        make_label(hwnd, "Background:", lx, 195, lw);
        nd->hBgSwatch = CreateWindow("STATIC", "",
            WS_VISIBLE | WS_CHILD | WS_BORDER | SS_NOTIFY,
            ex, 195, 40, 23, hwnd, (HMENU)IDC_BG_SWATCH, NULL, NULL);

        /* Row 6.5: Colour scheme (combobox — only listed schemes may be chosen) */
        make_label(hwnd, "Colour Scheme:", lx, 230, lw);
        {
            HWND hScheme = make_combo(hwnd, ex, 230, ew, 180, (HMENU)IDC_SCHEME_COMBO);
            for (int i = 0; i < NUM_SCHEMES; i++)
                SendMessage(hScheme, CB_ADDSTRING, 0, (LPARAM)k_schemes[i].name);

            /* Find matching scheme; default to scheme 0 */
            int sel = 0;
            for (int i = 0; i < NUM_SCHEMES; i++) {
                if (strcmp(nd->cfg->settings.foreground_colour,
                           k_schemes[i].fg) == 0 &&
                    strcmp(nd->cfg->settings.background_colour,
                           k_schemes[i].bg) == 0) {
                    sel = i;
                    break;
                }
            }
            SendMessage(hScheme, CB_SETCURSEL, (WPARAM)sel, 0);

            /* Initialise swatch colours from the selected scheme */
            nd->fg = hex_to_colorref(k_schemes[sel].fg);
            nd->bg = hex_to_colorref(k_schemes[sel].bg);
            nd->hFgBrush = CreateSolidBrush(nd->fg);
            nd->hBgBrush = CreateSolidBrush(nd->bg);
        }

        /* Row 7: Enable logging */
        make_label(hwnd, "Logging:", lx, 270, lw);
        {
            HWND hChk = CreateWindow("BUTTON", "Enable session logging",
                WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                ex, 270, 200, 20, hwnd, (HMENU)IDC_LOG_CHECK, NULL, NULL);
            SendMessage(hChk, BM_SETCHECK,
                        nd->cfg->settings.logging_enabled
                            ? BST_CHECKED : BST_UNCHECKED, 0);
        }

        /* Row 8: Log directory */
        make_label(hwnd, "Log Directory:", lx, 305, lw);
        make_edit(hwnd, nd->cfg->settings.log_dir,
                  ex, 305, ew, (HMENU)IDC_LOG_DIR_EDIT);

        /* Row 9: Log name format */
        make_label(hwnd, "Log Name Format:", lx, 340, lw);
        make_edit(hwnd, nd->cfg->settings.log_format,
                  ex, 340, ew, (HMENU)IDC_LOG_FMT_EDIT);

        /* Tooltip on the log format edit: list strftime specifiers */
        nd->hTooltip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP | TTS_BALLOON,
            0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        if (nd->hTooltip) {
            TOOLINFO ti;
            memset(&ti, 0, sizeof(ti));
            ti.cbSize   = sizeof(TOOLINFO);
            ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd     = hwnd;
            ti.uId      = (UINT_PTR)GetDlgItem(hwnd, IDC_LOG_FMT_EDIT);
            ti.lpszText = "%Y  4-digit year (e.g. 2026)\r\n"
                          "%m  month (01-12)\r\n"
                          "%d  day   (01-31)\r\n"
                          "%H  hour  (00-23)\r\n"
                          "%M  minute (00-59)\r\n"
                          "%S  second (00-59)\r\n"
                          "Example: session-%Y%m%d_%H%M%S";
            SendMessage(nd->hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
            SendMessage(nd->hTooltip, TTM_SETMAXTIPWIDTH, 0, (LPARAM)300);
        }

        /* Footer separator */
        CreateWindow("STATIC", "",
            WS_VISIBLE | WS_CHILD | SS_ETCHEDHORZ,
            0, ch - 60, cw, 2, hwnd, NULL, NULL, NULL);

        /* Action buttons */
        CreateWindow("BUTTON", "Save",
            WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            cw - 170, ch - 90, 75, 23, hwnd, (HMENU)IDOK, NULL, NULL);
        CreateWindow("BUTTON", "Cancel",
            WS_VISIBLE | WS_CHILD,
            cw - 85, ch - 90, 75, 23, hwnd, (HMENU)IDCANCEL, NULL, NULL);

        /* Version / copyright footer */
        CreateWindow("STATIC", "Nutshell v1.3",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            0, ch - 45, cw, 20, hwnd, NULL, NULL, NULL);
        CreateWindow("STATIC", "Copyright (C) 2026 Thomas Sulkiewicz",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            0, ch - 25, cw, 20, hwnd, NULL, NULL, NULL);

        /* Apply MS Shell Dlg 8pt to all child controls to match session
         * manager look (resource dialogs get this automatically). */
        {
            HDC hdc = GetDC(hwnd);
            int h = -MulDiv(8, GetDeviceCaps(hdc, LOGPIXELSY), 72);
            ReleaseDC(hwnd, hdc);
            nd->hDlgFont = CreateFont(
                h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "MS Shell Dlg");
            if (nd->hDlgFont)
                EnumChildWindows(hwnd, SetFontProc, (LPARAM)nd->hDlgFont);
        }

        return 0;
    }

    case WM_CTLCOLORSTATIC:
        /* Paint colour swatches */
        if (d) {
            HWND hCtl = (HWND)lParam;
            if (hCtl == d->hFgSwatch) return (LRESULT)(HBRUSH)d->hFgBrush;
            if (hCtl == d->hBgSwatch) return (LRESULT)(HBRUSH)d->hBgBrush;
        }
        return (LRESULT)NULL;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {

        case IDC_SCHEME_COMBO:
            /* Live preview: update swatches when selection changes */
            if (HIWORD(wParam) == CBN_SELCHANGE && d) {
                int sel = (int)SendDlgItemMessage(hwnd, IDC_SCHEME_COMBO,
                                                  CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < NUM_SCHEMES) {
                    d->fg = hex_to_colorref(k_schemes[sel].fg);
                    d->bg = hex_to_colorref(k_schemes[sel].bg);
                    DeleteObject(d->hFgBrush);
                    DeleteObject(d->hBgBrush);
                    d->hFgBrush = CreateSolidBrush(d->fg);
                    d->hBgBrush = CreateSolidBrush(d->bg);
                    InvalidateRect(d->hFgSwatch, NULL, TRUE);
                    InvalidateRect(d->hBgSwatch, NULL, TRUE);
                }
            }
            break;

        case IDOK: {
            if (!d) { DestroyWindow(hwnd); break; }
            Settings *s = &d->cfg->settings;

            /* Font from combo */
            {
                int sel = (int)SendDlgItemMessage(hwnd, IDC_FONT_COMBO,
                                                  CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < NUM_FONTS) {
                    strncpy(s->font, k_fonts[sel], sizeof(s->font) - 1);
                    s->font[sizeof(s->font) - 1] = '\0';
                }
            }

            /* Font size from discrete combo */
            {
                int sel = (int)SendDlgItemMessage(hwnd, IDC_FONTSIZE_COMBO,
                                                  CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < NUM_FONT_SIZES)
                    s->font_size = k_font_sizes[sel];
            }

            /* Numeric fields; keep existing value on parse failure */
            BOOL ok;
            UINT v;

            v = GetDlgItemInt(hwnd, IDC_SCROLLBACK_EDIT, &ok, FALSE);
            if (ok) s->scrollback_lines = (int)v;

            v = GetDlgItemInt(hwnd, IDC_PASTEDELAY_EDIT, &ok, FALSE);
            if (ok) s->paste_delay_ms = (int)v;

            /* Logging checkbox */
            s->logging_enabled =
                (SendDlgItemMessage(hwnd, IDC_LOG_CHECK,
                                    BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;

            /* Log directory & format */
            GetDlgItemText(hwnd, IDC_LOG_DIR_EDIT,
                           s->log_dir, (int)sizeof(s->log_dir));
            GetDlgItemText(hwnd, IDC_LOG_FMT_EDIT,
                           s->log_format, (int)sizeof(s->log_format));

            /* Colours from selected scheme (tracked in d->fg / d->bg) */
            colorref_to_hex(d->fg, s->foreground_colour,
                            sizeof(s->foreground_colour));
            colorref_to_hex(d->bg, s->background_colour,
                            sizeof(s->background_colour));

            /* Clamp out-of-range values before persisting */
            settings_validate(s);
            config_save(d->cfg, "config.json");
            DestroyWindow(hwnd);
            break;
        }

        case IDCANCEL:
            DestroyWindow(hwnd);
            break;
        }
        return 0;

    case WM_DESTROY:
        if (d) {
            if (d->hFgBrush) DeleteObject(d->hFgBrush);
            if (d->hBgBrush) DeleteObject(d->hBgBrush);
            if (d->hTooltip) DestroyWindow(d->hTooltip);
            if (d->hDlgFont) DeleteObject(d->hDlgFont);
            free(d);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)NULL);
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ---- Public API --------------------------------------------------------- */

void settings_dlg_show(HWND parent, Config *cfg)
{
    if (!cfg) return;

    /* Allocate dialog state — freed in WM_DESTROY */
    SettingsDlgData *d = (SettingsDlgData *)calloc(1u, sizeof(SettingsDlgData));
    if (!d) return;
    d->cfg = cfg;

    WNDCLASSEX wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = SettingsWndProc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = SETTINGS_CLASS;
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(
        0, SETTINGS_CLASS, "Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 480, 530,
        parent, NULL, GetModuleHandle(NULL), d);

    if (hwnd) {
        EnableWindow(parent, FALSE);
        MSG msg;
        while (IsWindow(hwnd) && GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        EnableWindow(parent, TRUE);
        SetFocus(parent);
    } else {
        free(d);
    }
}

#endif
