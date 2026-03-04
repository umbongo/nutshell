#include "settings_dlg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <commctrl.h>

/* ---- Control IDs -------------------------------------------------------- */

#define IDC_FONT_EDIT       1001
#define IDC_FONTSIZE_EDIT   1002
#define IDC_SCROLLBACK_EDIT 1003
#define IDC_PASTEDELAY_EDIT 1004
#define IDC_FG_SWATCH       1005
#define IDC_FG_CHOOSE       1006
#define IDC_BG_SWATCH       1007
#define IDC_BG_CHOOSE       1008
#define IDC_LOG_CHECK       1009
#define IDC_LOG_DIR_EDIT    1010
#define IDC_LOG_FMT_EDIT    1011

static const char *SETTINGS_CLASS = "CongaSSH_Settings";

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

static HWND make_button(HWND parent, const char *text,
                        int x, int y, int w, HMENU id)
{
    return CreateWindow("BUTTON", text,
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        x, y, w, 23, parent, id, NULL, NULL);
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

        /* Resolve initial colours from config strings */
        nd->fg = hex_to_colorref(nd->cfg->settings.foreground_colour);
        nd->bg = hex_to_colorref(nd->cfg->settings.background_colour);
        nd->hFgBrush = CreateSolidBrush(nd->fg);
        nd->hBgBrush = CreateSolidBrush(nd->bg);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int cw = rc.right;
        int ch = rc.bottom;

        /* Column geometry */
        int lx = 10;   /* label x       */
        int lw = 120;  /* label width    */
        int ex = 135;  /* control x      */
        int ew = 200;  /* default edit w */

        /* Row 1: Font name */
        make_label(hwnd, "Font:", lx, 20, lw);
        make_edit(hwnd, nd->cfg->settings.font,
                  ex, 20, ew, (HMENU)IDC_FONT_EDIT);

        /* Row 2: Font size */
        {
            char buf[16];
            (void)snprintf(buf, sizeof(buf), "%d",
                           nd->cfg->settings.font_size);
            make_label(hwnd, "Font Size:", lx, 55, lw);
            make_edit(hwnd, buf, ex, 55, 60, (HMENU)IDC_FONTSIZE_EDIT);
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

        /* Row 5: Foreground colour */
        make_label(hwnd, "Foreground:", lx, 160, lw);
        nd->hFgSwatch = CreateWindow("STATIC", "",
            WS_VISIBLE | WS_CHILD | WS_BORDER | SS_NOTIFY,
            ex, 160, 40, 23, hwnd, (HMENU)IDC_FG_SWATCH, NULL, NULL);
        make_button(hwnd, "Choose...", ex + 48, 160, 75, (HMENU)IDC_FG_CHOOSE);

        /* Row 6: Background colour */
        make_label(hwnd, "Background:", lx, 195, lw);
        nd->hBgSwatch = CreateWindow("STATIC", "",
            WS_VISIBLE | WS_CHILD | WS_BORDER | SS_NOTIFY,
            ex, 195, 40, 23, hwnd, (HMENU)IDC_BG_SWATCH, NULL, NULL);
        make_button(hwnd, "Choose...", ex + 48, 195, 75, (HMENU)IDC_BG_CHOOSE);

        /* Row 7: Enable logging */
        make_label(hwnd, "Logging:", lx, 235, lw);
        {
            HWND hChk = CreateWindow("BUTTON", "Enable session logging",
                WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                ex, 235, 200, 20, hwnd, (HMENU)IDC_LOG_CHECK, NULL, NULL);
            SendMessage(hChk, BM_SETCHECK,
                        nd->cfg->settings.logging_enabled
                            ? BST_CHECKED : BST_UNCHECKED, 0);
        }

        /* Row 8: Log directory */
        make_label(hwnd, "Log Directory:", lx, 270, lw);
        make_edit(hwnd, nd->cfg->settings.log_dir,
                  ex, 270, ew, (HMENU)IDC_LOG_DIR_EDIT);

        /* Row 9: Log name format */
        make_label(hwnd, "Log Name Format:", lx, 305, lw);
        make_edit(hwnd, nd->cfg->settings.log_format,
                  ex, 305, ew, (HMENU)IDC_LOG_FMT_EDIT);

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
        CreateWindow("STATIC", "Conga.SSH v1.1",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            0, ch - 45, cw, 20, hwnd, NULL, NULL, NULL);
        CreateWindow("STATIC", "Copyright (C) 2026 Thomas Sulkiewicz",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            0, ch - 25, cw, 20, hwnd, NULL, NULL, NULL);

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

        case IDC_FG_CHOOSE: {
            if (!d) break;
            static COLORREF s_custom_fg[16];
            CHOOSECOLOR cc;
            memset(&cc, 0, sizeof(cc));
            cc.lStructSize  = sizeof(cc);
            cc.hwndOwner    = hwnd;
            cc.rgbResult    = d->fg;
            cc.lpCustColors = s_custom_fg;
            cc.Flags        = CC_RGBINIT | CC_FULLOPEN;
            if (ChooseColor(&cc)) {
                d->fg = cc.rgbResult;
                DeleteObject(d->hFgBrush);
                d->hFgBrush = CreateSolidBrush(d->fg);
                InvalidateRect(d->hFgSwatch, NULL, TRUE);
            }
            break;
        }

        case IDC_BG_CHOOSE: {
            if (!d) break;
            static COLORREF s_custom_bg[16];
            CHOOSECOLOR cc;
            memset(&cc, 0, sizeof(cc));
            cc.lStructSize  = sizeof(cc);
            cc.hwndOwner    = hwnd;
            cc.rgbResult    = d->bg;
            cc.lpCustColors = s_custom_bg;
            cc.Flags        = CC_RGBINIT | CC_FULLOPEN;
            if (ChooseColor(&cc)) {
                d->bg = cc.rgbResult;
                DeleteObject(d->hBgBrush);
                d->hBgBrush = CreateSolidBrush(d->bg);
                InvalidateRect(d->hBgSwatch, NULL, TRUE);
            }
            break;
        }

        case IDOK: {
            if (!d) { DestroyWindow(hwnd); break; }
            Settings *s = &d->cfg->settings;

            /* Read text fields */
            GetDlgItemText(hwnd, IDC_FONT_EDIT,
                           s->font, (int)sizeof(s->font));

            /* Read numeric fields; keep existing value on parse failure */
            BOOL ok;
            UINT v;

            v = GetDlgItemInt(hwnd, IDC_FONTSIZE_EDIT, &ok, FALSE);
            if (ok) s->font_size = (int)v;

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

            /* Colours */
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