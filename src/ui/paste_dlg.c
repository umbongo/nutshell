#include "paste_dlg.h"
#include "paste_preview.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

/* ---- Control IDs -------------------------------------------------------- */

#define IDC_PASTE_SUMMARY  2001
#define IDC_PASTE_EDIT     2002

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
    HFONT     hDlgFont;     /* MS Shell Dlg 8pt for labels/buttons */
    HWND      hSummary;
    HWND      hEdit;
    HWND      hBtnPaste;
    HWND      hBtnCancel;
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

/* ---- Layout constants --------------------------------------------------- */

#define MARGIN      10
#define BTN_W       75
#define BTN_H       23
#define BTN_GAP     10
#define SUMMARY_H   20
#define FOOTER_H    (MARGIN + BTN_H + MARGIN)

/* ---- Reposition controls on resize -------------------------------------- */

static void layout_controls(PasteDlgData *d, int cw, int ch)
{
    if (!d) return;

    /* Summary label: top */
    SetWindowPos(d->hSummary, NULL,
        MARGIN, MARGIN, cw - 2 * MARGIN, SUMMARY_H,
        SWP_NOZORDER);

    /* Text area: fills middle */
    int edit_top = MARGIN + SUMMARY_H + MARGIN;
    int edit_h = ch - edit_top - FOOTER_H;
    if (edit_h < 20) edit_h = 20;
    SetWindowPos(d->hEdit, NULL,
        MARGIN, edit_top, cw - 2 * MARGIN, edit_h,
        SWP_NOZORDER);

    /* Buttons: bottom-right */
    int btn_y = ch - MARGIN - BTN_H;
    SetWindowPos(d->hBtnCancel, NULL,
        cw - MARGIN - BTN_W, btn_y, BTN_W, BTN_H,
        SWP_NOZORDER);
    SetWindowPos(d->hBtnPaste, NULL,
        cw - MARGIN - BTN_W - BTN_GAP - BTN_W, btn_y, BTN_W, BTN_H,
        SWP_NOZORDER);
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

        RECT rc;
        GetClientRect(hwnd, &rc);
        int cw = rc.right;
        int ch = rc.bottom;

        /* Summary label */
        nd->hSummary = CreateWindow("STATIC", nd->summary,
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            MARGIN, MARGIN, cw - 2 * MARGIN, SUMMARY_H,
            hwnd, (HMENU)IDC_PASTE_SUMMARY, NULL, NULL);

        /* Multiline read-only edit for paste content */
        int edit_top = MARGIN + SUMMARY_H + MARGIN;
        int edit_h = ch - edit_top - FOOTER_H;
        if (edit_h < 20) edit_h = 20;

        nd->hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", nd->edit_text,
            WS_VISIBLE | WS_CHILD | WS_BORDER |
            ES_MULTILINE | ES_READONLY | ES_AUTOHSCROLL |
            WS_VSCROLL | WS_HSCROLL,
            MARGIN, edit_top, cw - 2 * MARGIN, edit_h,
            hwnd, (HMENU)IDC_PASTE_EDIT, NULL, NULL);

        /* Buttons */
        int btn_y = ch - MARGIN - BTN_H;
        nd->hBtnPaste = CreateWindow("BUTTON", "Paste",
            WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            cw - MARGIN - BTN_W - BTN_GAP - BTN_W, btn_y, BTN_W, BTN_H,
            hwnd, (HMENU)IDOK, NULL, NULL);

        nd->hBtnCancel = CreateWindow("BUTTON", "Cancel",
            WS_VISIBLE | WS_CHILD,
            cw - MARGIN - BTN_W, btn_y, BTN_W, BTN_H,
            hwnd, (HMENU)IDCANCEL, NULL, NULL);

        /* Create MS Shell Dlg 8pt for labels/buttons */
        {
            HDC hdc = GetDC(hwnd);
            int h = -MulDiv(8, GetDeviceCaps(hdc, LOGPIXELSY), 72);
            ReleaseDC(hwnd, hdc);
            nd->hDlgFont = CreateFont(
                h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "MS Shell Dlg");
            if (nd->hDlgFont)
                EnumChildWindows(hwnd, SetFontCb, (LPARAM)nd->hDlgFont);
        }

        /* Apply terminal font to the text area */
        if (nd->hTermFont)
            SendMessage(nd->hEdit, WM_SETFONT,
                        (WPARAM)nd->hTermFont, (LPARAM)TRUE);

        /* Brush for text area background */
        nd->hBgBrush = CreateSolidBrush(nd->bg);

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
        mmi->ptMinTrackSize.x = 400;
        mmi->ptMinTrackSize.y = 250;
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
                       const char *font_name, int font_size)
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

    paste_build_summary(line_count, strlen(raw_text),
                        d.summary, sizeof(d.summary));

    d.edit_text = build_edit_text(lines, line_count);
    paste_line_free(lines, line_count);

    if (!d.edit_text) return 0;

    /* Create terminal font for text area */
    {
        HDC hdc = GetDC(NULL);
        int h = -MulDiv(font_size, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        ReleaseDC(NULL, hdc);
        d.hTermFont = CreateFont(
            h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
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

    HWND hwnd = CreateWindowEx(
        0, PASTE_CLASS, "Confirm Paste",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 400,
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
