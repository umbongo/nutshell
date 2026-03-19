#include "paste_dlg.h"
#include "paste_preview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#include "ui_theme.h"
#include "custom_scrollbar.h"
#include "edit_scroll.h"
#include "../core/app_font.h"

/* ---- Control IDs -------------------------------------------------------- */

#define IDC_PASTE_SUMMARY  2001
#define IDC_PASTE_EDIT     2002
#define TIMER_PASTE_SCROLL 1     /* Sync custom scrollbar with EDIT */

static const char *PASTE_CLASS = "Nutshell_PastePreview";

/* ---- Dialog state ------------------------------------------------------- */

typedef struct {
    int       result;       /* 1 = confirmed, 0 = cancelled */
    char     *edit_text;    /* CRLF-joined text for the EDIT control */
    char      summary[256];
    COLORREF  fg;
    COLORREF  bg;
    HBRUSH    hBgBrush;
    HFONT     hTermFont;    /* terminal font for text area */
    HFONT     hDlgFont;     /* Inter 9pt for labels/buttons */
    HWND      hSummary;
    HWND      hEdit;
    HWND      hBtnPaste;
    HWND      hBtnCancel;
    HWND      hScrollbar;   /* custom themed scrollbar */
    int       line_h;       /* cached line height for scroll math */
    int       dpi;
    const ThemeColors *theme;
} PasteDlgData;

/* ---- Colour helper ------------------------------------------------------ */

static COLORREF hex_to_cr(const char *hex)
{
    if (!hex || hex[0] != '#' || strlen(hex) < 7) return RGB(0, 0, 0);
    unsigned int r = 0, g = 0, b = 0;
    if (sscanf(hex + 1, "%02x%02x%02x", &r, &g, &b) != 3)
        return RGB(0, 0, 0);
    return RGB((BYTE)r, (BYTE)g, (BYTE)b);
}

/* ---- Build CRLF text from lines ----------------------------------------- */

static char *build_edit_text(char **lines, int count)
{
    if (!lines || count <= 0) return NULL;

    /* Calculate total length: each line + \r\n, minus trailing \r\n */
    size_t total = 0;
    for (int i = 0; i < count; i++)
        total += strlen(lines[i]) + 2; /* \r\n */

    char *buf = (char *)malloc(total + 1);
    if (!buf) return NULL;

    size_t pos = 0;
    for (int i = 0; i < count; i++) {
        size_t len = strlen(lines[i]);
        memcpy(buf + pos, lines[i], len);
        pos += len;
        if (i < count - 1) {
            buf[pos++] = '\r';
            buf[pos++] = '\n';
        }
    }
    buf[pos] = '\0';
    return buf;
}

/* ---- Layout constants (base values at 96 DPI) --------------------------- */

#define MARGIN_BASE      10
#define BTN_W_BASE       75
#define BTN_H_BASE       23
#define BTN_GAP_BASE     10
#define SUMMARY_H_BASE   20

/* ---- Sync custom scrollbar with edit control ---- */

static void paste_sync_scroll(PasteDlgData *d)
{
    if (!d || !d->hEdit || !d->hScrollbar) return;
    int lh = d->line_h > 0 ? d->line_h : 1;
    csb_sync_edit(d->hEdit, d->hScrollbar, lh);
}

/* ---- Reposition controls on resize -------------------------------------- */

static void layout_controls(PasteDlgData *d, int cw, int ch)
{
    if (!d) return;
    int dpi = d->dpi > 0 ? d->dpi : 96;
    int margin = MulDiv(MARGIN_BASE, dpi, 96);
    int btn_w = MulDiv(BTN_W_BASE, dpi, 96);
    int btn_h = MulDiv(BTN_H_BASE, dpi, 96);
    int btn_gap = MulDiv(BTN_GAP_BASE, dpi, 96);
    int summ_h = MulDiv(SUMMARY_H_BASE, dpi, 96);
    int footer_h = margin + btn_h + margin;
    int sb_w = MulDiv(CSB_WIDTH, dpi, 96);

    /* Summary label: top */
    SetWindowPos(d->hSummary, NULL,
        margin, margin, cw - 2 * margin, summ_h,
        SWP_NOZORDER);

    /* Text area: fills middle, narrowed for scrollbar */
    int edit_top = margin + summ_h + margin;
    int edit_h = ch - edit_top - footer_h;
    if (edit_h < 20) edit_h = 20;
    int edit_w = cw - 2 * margin - sb_w;
    if (edit_w < 20) edit_w = 20;
    SetWindowPos(d->hEdit, NULL,
        margin, edit_top, edit_w, edit_h,
        SWP_NOZORDER);

    /* Custom scrollbar: right of edit */
    if (d->hScrollbar)
        MoveWindow(d->hScrollbar,
            margin + edit_w, edit_top, sb_w, edit_h, TRUE);

    /* Buttons: bottom-right */
    int btn_y = ch - margin - btn_h;
    SetWindowPos(d->hBtnCancel, NULL,
        cw - margin - btn_w, btn_y, btn_w, btn_h,
        SWP_NOZORDER);
    SetWindowPos(d->hBtnPaste, NULL,
        cw - margin - btn_w - btn_gap - btn_w, btn_y, btn_w, btn_h,
        SWP_NOZORDER);

    paste_sync_scroll(d);
}

/* ---- EnumChildWindows callback: apply font ------------------------------ */

static BOOL CALLBACK SetFontCb(HWND hChild, LPARAM lParam)
{
    SendMessage(hChild, WM_SETFONT, (WPARAM)(HFONT)lParam, (LPARAM)TRUE);
    return TRUE;
}

/* ---- Window procedure --------------------------------------------------- */

static LRESULT CALLBACK PasteDlgProc(HWND hwnd, UINT msg,
                                      WPARAM wParam, LPARAM lParam)
{
    PasteDlgData *d = (PasteDlgData *)(LONG_PTR)
                       GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        LPCREATESTRUCT cs = (LPCREATESTRUCT)lParam;
        PasteDlgData *nd = (PasteDlgData *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)nd);

        /* Get per-monitor DPI for layout scaling */
        {
            HDC hdc_dpi = GetDC(hwnd);
            nd->dpi = GetDeviceCaps(hdc_dpi, LOGPIXELSY);
            ReleaseDC(hwnd, hdc_dpi);
        }
        int margin = MulDiv(MARGIN_BASE, nd->dpi, 96);
        int btn_w = MulDiv(BTN_W_BASE, nd->dpi, 96);
        int btn_h = MulDiv(BTN_H_BASE, nd->dpi, 96);
        int btn_gap = MulDiv(BTN_GAP_BASE, nd->dpi, 96);
        int summ_h = MulDiv(SUMMARY_H_BASE, nd->dpi, 96);
        int footer_h = margin + btn_h + margin;
        int sb_w = MulDiv(CSB_WIDTH, nd->dpi, 96);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int cw = rc.right;
        int ch = rc.bottom;

        /* Summary label */
        nd->hSummary = CreateWindow("STATIC", nd->summary,
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            margin, margin, cw - 2 * margin, summ_h,
            hwnd, (HMENU)IDC_PASTE_SUMMARY, NULL, NULL);

        /* Multiline read-only edit — no WS_VSCROLL, custom scrollbar instead */
        int edit_top = margin + summ_h + margin;
        int edit_h = ch - edit_top - footer_h;
        if (edit_h < 20) edit_h = 20;
        int edit_w = cw - 2 * margin - sb_w;
        if (edit_w < 20) edit_w = 20;

        nd->hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", nd->edit_text,
            WS_VISIBLE | WS_CHILD | WS_BORDER |
            ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL |
            WS_HSCROLL,
            margin, edit_top, edit_w, edit_h,
            hwnd, (HMENU)IDC_PASTE_EDIT, NULL, NULL);

        /* Custom scrollbar right of the edit control */
        if (nd->theme) {
            nd->hScrollbar = csb_create(hwnd,
                margin + edit_w, edit_top, sb_w, edit_h,
                nd->theme, GetModuleHandle(NULL));
        }

        /* Buttons */
        int btn_y = ch - margin - btn_h;
        nd->hBtnPaste = CreateWindow("BUTTON", "Paste",
            WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            cw - margin - btn_w - btn_gap - btn_w, btn_y, btn_w, btn_h,
            hwnd, (HMENU)IDOK, NULL, NULL);

        nd->hBtnCancel = CreateWindow("BUTTON", "Cancel",
            WS_VISIBLE | WS_CHILD,
            cw - margin - btn_w, btn_y, btn_w, btn_h,
            hwnd, (HMENU)IDCANCEL, NULL, NULL);

        /* Create Cascadia Code 9pt for labels/buttons */
        {
            HDC hdc = GetDC(hwnd);
            int h = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
            ReleaseDC(hwnd, hdc);
            nd->hDlgFont = CreateFont(
                h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, APP_FONT_UI_FACE);
            if (nd->hDlgFont)
                EnumChildWindows(hwnd, SetFontCb, (LPARAM)nd->hDlgFont);
        }

        /* Apply terminal font to the text area and cache line height */
        if (nd->hTermFont)
            SendMessage(nd->hEdit, WM_SETFONT,
                        (WPARAM)nd->hTermFont, (LPARAM)TRUE);
        {
            HDC hdc_lh = GetDC(nd->hEdit);
            TEXTMETRIC tm_lh;
            GetTextMetrics(hdc_lh, &tm_lh);
            nd->line_h = tm_lh.tmHeight + tm_lh.tmExternalLeading;
            ReleaseDC(nd->hEdit, hdc_lh);
        }

        /* Brush for text area background */
        nd->hBgBrush = CreateSolidBrush(nd->bg);

        /* Start scroll sync timer */
        SetTimer(hwnd, TIMER_PASTE_SCROLL, 50, NULL);

        return 0;
    }

    case WM_SIZE: {
        if (!d) break;
        int cw = LOWORD(lParam);
        int ch = HIWORD(lParam);
        layout_controls(d, cw, ch);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        int ddpi = d ? d->dpi : 96;
        mmi->ptMinTrackSize.x = MulDiv(400, ddpi, 96);
        mmi->ptMinTrackSize.y = MulDiv(250, ddpi, 96);
        return 0;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        if (!d) break;
        HWND hCtl = (HWND)lParam;
        if (hCtl == d->hEdit) {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, d->fg);
            SetBkColor(hdc, d->bg);
            return (LRESULT)d->hBgBrush;
        }
        break;
    }

    case WM_TIMER:
        if (wParam == TIMER_PASTE_SCROLL && d)
            paste_sync_scroll(d);
        return 0;

    case WM_VSCROLL:
        /* Handle custom scrollbar messages */
        if (d && d->hScrollbar &&
            (HWND)lParam == d->hScrollbar) {
            WORD code = LOWORD(wParam);
            int first = (int)SendMessage(d->hEdit,
                            EM_GETFIRSTVISIBLELINE, 0, 0);
            int delta = 0;
            RECT erc_v;
            GetClientRect(d->hEdit, &erc_v);
            int lh = d->line_h > 0 ? d->line_h : 1;
            int vis = edit_scroll_visible_lines(
                          erc_v.bottom - erc_v.top, lh);
            switch (code) {
            case SB_LINEUP:    delta = -1;   break;
            case SB_LINEDOWN:  delta =  1;   break;
            case SB_PAGEUP:    delta = -vis; break;
            case SB_PAGEDOWN:  delta =  vis; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                delta = edit_scroll_line_delta(
                    csb_get_trackpos(d->hScrollbar), first);
                break;
            case SB_TOP:    delta = -first;  break;
            case SB_BOTTOM: delta =  99999;  break;
            }
            if (delta != 0)
                SendMessage(d->hEdit, EM_LINESCROLL, 0, (LPARAM)delta);
            paste_sync_scroll(d);
            return 0;
        }
        break;

    case WM_MOUSEWHEEL:
        /* Forward mouse wheel to scroll the edit control */
        if (d && d->hEdit) {
            int zdelta = GET_WHEEL_DELTA_WPARAM(wParam);
            int scroll = edit_scroll_wheel_delta(zdelta, WHEEL_DELTA, 3);
            SendMessage(d->hEdit, EM_LINESCROLL, 0, (LPARAM)scroll);
            paste_sync_scroll(d);
            return 0;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK:
            if (d) d->result = 1;
            DestroyWindow(hwnd);
            break;
        case IDCANCEL:
            if (d) d->result = 0;
            DestroyWindow(hwnd);
            break;
        }
        return 0;

    case WM_DESTROY:
        if (d) {
            KillTimer(hwnd, TIMER_PASTE_SCROLL);
            if (d->hBgBrush) DeleteObject(d->hBgBrush);
            if (d->hDlgFont) DeleteObject(d->hDlgFont);
            if (d->hTermFont) DeleteObject(d->hTermFont);
        }
        return 0;

    case WM_CLOSE:
        if (d) d->result = 0;
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ---- Public API --------------------------------------------------------- */

int paste_preview_show(HWND parent, const char *raw_text,
                       const char *fg_hex, const char *bg_hex,
                       const char *font_name, int font_size,
                       const char *colour_scheme)
{
    if (!raw_text) return 0;

    /* Format lines */
    int line_count = 0;
    char **lines = paste_format_lines(raw_text, &line_count);
    if (!lines) return 0;

    /* Prepare dialog data */
    PasteDlgData d;
    memset(&d, 0, sizeof(d));
    d.result = 0;
    d.fg = hex_to_cr(fg_hex);
    d.bg = hex_to_cr(bg_hex);

    /* Resolve theme for custom scrollbar */
    {
        int idx = colour_scheme ? ui_theme_find(colour_scheme) : 0;
        d.theme = ui_theme_get(idx);
    }

    paste_build_summary(line_count, strlen(raw_text),
                        d.summary, sizeof(d.summary));

    d.edit_text = build_edit_text(lines, line_count);
    paste_line_free(lines, line_count);

    if (!d.edit_text) return 0;

    /* Create terminal font for text area */
    {
        HDC hdc = GetDC(parent);
        int h = -MulDiv(font_size, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        ReleaseDC(parent, hdc);
        d.hTermFont = CreateFont(
            h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
            font_name ? font_name : "Consolas");
    }

    /* Register window class */
    WNDCLASSEX wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = PasteDlgProc;
    wc.hInstance      = GetModuleHandle(NULL);
    wc.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName  = PASTE_CLASS;
    RegisterClassEx(&wc);

    /* Scale window size for DPI and clamp to screen */
    int pdpi;
    {
        HDC hdc_p = GetDC(parent);
        pdpi = GetDeviceCaps(hdc_p, LOGPIXELSY);
        ReleaseDC(parent, hdc_p);
    }

    int desired_w = MulDiv(520, pdpi, 96);
    int desired_h = MulDiv(400, pdpi, 96);

    /* Get usable screen area (excludes taskbar) */
    RECT work;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
    int screen_w = work.right - work.left;
    int screen_h = work.bottom - work.top;

    int win_w, win_h;
    paste_clamp_size(desired_w, desired_h, screen_w, screen_h,
                     &win_w, &win_h);

    HWND hwnd = CreateWindowEx(
        0, PASTE_CLASS, "Confirm Paste",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        win_w, win_h,
        parent, NULL, GetModuleHandle(NULL), &d);

    if (hwnd) {
        EnableWindow(parent, FALSE);
        MSG msg;
        while (IsWindow(hwnd) && GetMessage(&msg, NULL, 0, 0)) {
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
                d.result = 0;
                DestroyWindow(hwnd);
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        EnableWindow(parent, TRUE);
        SetFocus(parent);
    }

    free(d.edit_text);
    return d.result;
}

#endif
