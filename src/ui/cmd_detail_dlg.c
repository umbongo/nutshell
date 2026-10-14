/* src/ui/cmd_detail_dlg.c -- the command details popup. See
 * cmd_detail_dlg.h and the spec it cites. Window plumbing follows
 * paste_dlg.c (own class, owner disabled, own loop) and the About window
 * in window.c (client size trimmed to the computed layout). */

#ifdef _WIN32

#include "cmd_detail_dlg.h"
#include "chat_listview.h"      /* chatlv_safety_tag_colors/_text */
#include "clipboard.h"
#include "cmd_detail_layout.h"
#include "dpi_util.h"
#include "dwm_util.h"
#include "ns_draw.h"
#include "ns_font.h"
#include "ns_scale.h"
#include "ns_tokens.h"
#include "ns_type.h"
#include "themed_button.h"      /* theme_cr, hover tracking for owner-draw buttons */
#include <stdlib.h>
#include <string.h>

#define CMD_DETAIL_CLASS  L"Nutshell_CmdDetail"

/* Control ids, local to this window. The Close button is IDCANCEL so
 * IsDialogMessage's own Esc handling would agree with ours. */
#define IDC_DETAIL_EDIT   100
#define IDC_DETAIL_COPY   101

#define TIMER_COPIED      1
#define COPIED_MS         1500   /* how long the Copy button reads "Copied" */

#define NOTE_MAX_LINES    8      /* the note is cut off (with "...") past this */

#define WIN_BASE_W        560    /* 96-DPI base sizes, scaled with ns_scale() */
#define WIN_BASE_H        360
#define WIN_MIN_W         400
#define WIN_MIN_H         300

static const wchar_t NO_NOTE_TEXT[] =
    L"The assistant gave no explanation for this command.";

typedef struct {
    HWND hwnd, hEdit, hCopy, hClose;
    /* Own copies of everything shown. */
    char    *command;       /* UTF-8, for the clipboard */
    wchar_t *command_w;     /* UTF-16 with CRLF line ends, for the EDIT */
    wchar_t *note_w;        /* UTF-16, "" when there is no note */
    CmdSafetyLevel safety;

    int dpi;
    int tag_w;
    int copied;             /* 1 while the Copy button reads "Copied" */
    int done;               /* set by WM_DESTROY; ends the modal loop */
    HBRUSH bg_brush;        /* cmd_bg, for the EDIT; owned, deleted on destroy */
    HFONT f_caption, f_body, f_mono;   /* from the ns_font cache -- never deleted */
    CmdDetailLayout lay;
} DetailData;

/* ---- helpers ------------------------------------------------------------ */

/* UTF-8 -> UTF-16, optionally turning every bare LF into CRLF (a multiline
 * EDIT only breaks lines on CRLF). Caller frees. Never NULL for non-NULL
 * input except on allocation failure. */
static wchar_t *to_wide(const char *utf8, int crlf)
{
    if (!utf8) utf8 = "";
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (n <= 0) n = 1;
    wchar_t *tmp = (wchar_t *)calloc((size_t)n + 1, sizeof(wchar_t));
    if (!tmp) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, tmp, n);
    if (!crlf) return tmp;

    size_t lf = 0;
    for (const wchar_t *p = tmp; *p; p++) if (*p == L'\n') lf++;
    wchar_t *out = (wchar_t *)calloc(wcslen(tmp) + lf + 1, sizeof(wchar_t));
    if (!out) { free(tmp); return NULL; }
    wchar_t *o = out;
    for (const wchar_t *p = tmp; *p; p++) {
        if (*p == L'\n' && (p == tmp || p[-1] != L'\r')) *o++ = L'\r';
        *o++ = *p;
    }
    free(tmp);
    return out;
}

static void fetch_fonts(DetailData *d)
{
    d->f_caption = ns_font(FONT_CAPTION, d->dpi);
    d->f_body    = ns_font(FONT_BODY, d->dpi);
    d->f_mono    = ns_font(FONT_MONO, d->dpi);
}

static void apply_fonts(DetailData *d)
{
    if (d->f_mono) SendMessage(d->hEdit, WM_SETFONT, (WPARAM)d->f_mono, TRUE);
    if (d->f_body) {
        SendMessage(d->hCopy, WM_SETFONT, (WPARAM)d->f_body, TRUE);
        SendMessage(d->hClose, WM_SETFONT, (WPARAM)d->f_body, TRUE);
    }
}

/* The risk tag is as wide as its text plus SP_SM padding each side. */
static void measure_tag(DetailData *d)
{
    d->tag_w = 0;
    HDC hdc = GetDC(d->hwnd);
    if (!hdc) return;
    HGDIOBJ old = SelectObject(hdc, d->f_caption ? d->f_caption
                                   : GetStockObject(DEFAULT_GUI_FONT));
    const char *text = chatlv_safety_tag_text(d->safety);
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32A(hdc, text, (int)strlen(text), &sz);
    SelectObject(hdc, old);
    ReleaseDC(d->hwnd, hdc);
    d->tag_w = sz.cx + 2 * ns_scale(SP_SM, d->dpi);
}

static const wchar_t *note_text(const DetailData *d)
{
    return (d->note_w && d->note_w[0]) ? d->note_w : NO_NOTE_TEXT;
}

/* Measure the note, lay the window out and place the child controls. */
static void detail_relayout(DetailData *d)
{
    if (!d->hEdit) return;
    RECT rc;
    GetClientRect(d->hwnd, &rc);
    int cw = rc.right, ch = rc.bottom;

    int margin = ns_scale(SP_LG, d->dpi);
    int note_w = cw - 2 * margin;
    if (note_w < 1) note_w = 1;

    int note_h = 0;
    HDC hdc = GetDC(d->hwnd);
    if (hdc) {
        HGDIOBJ old = SelectObject(hdc, d->f_body ? d->f_body
                                       : GetStockObject(DEFAULT_GUI_FONT));
        TEXTMETRICW tm;
        GetTextMetricsW(hdc, &tm);
        RECT mr = { 0, 0, note_w, 0 };
        DrawTextW(hdc, note_text(d), -1, &mr,
                  DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        note_h = mr.bottom - mr.top;
        if (note_h > NOTE_MAX_LINES * tm.tmHeight)
            note_h = NOTE_MAX_LINES * tm.tmHeight;
        SelectObject(hdc, old);
        ReleaseDC(d->hwnd, hdc);
    }

    cmd_detail_layout(cw, ch, d->dpi, note_h, d->tag_w, &d->lay);

    MoveWindow(d->hEdit, d->lay.command.x, d->lay.command.y,
               d->lay.command.w, d->lay.command.h, TRUE);
    MoveWindow(d->hCopy, d->lay.copy_btn.x, d->lay.copy_btn.y,
               d->lay.copy_btn.w, d->lay.copy_btn.h, TRUE);
    MoveWindow(d->hClose, d->lay.close_btn.x, d->lay.close_btn.y,
               d->lay.close_btn.w, d->lay.close_btn.h, TRUE);
    InvalidateRect(d->hwnd, NULL, TRUE);
}

static RECT nrect_to_rect(const NsRect *r)
{
    RECT out = { r->x, r->y, r->x + r->w, r->y + r->h };
    return out;
}

/* ---- painting ----------------------------------------------------------- */

static void detail_paint(DetailData *d, HDC hdc)
{
    const ThemeTokens *tk = ns_tokens();
    RECT client;
    GetClientRect(d->hwnd, &client);

    HBRUSH bg = CreateSolidBrush(theme_cr(tk->bg_primary.base));
    FillRect(hdc, &client, bg);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);

    /* "The assistant says:" -- the note is model prose, so it is labelled
     * as such and kept visually secondary to the risk tag. */
    HGDIOBJ old = SelectObject(hdc, d->f_caption ? d->f_caption
                                   : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(hdc, theme_cr(tk->text_dim));
    RECT label_rc = nrect_to_rect(&d->lay.label);
    DrawTextW(hdc, L"The assistant says:", -1, &label_rc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    /* The note, or the fixed no-explanation line in text_dim. */
    int has_note = d->note_w && d->note_w[0];
    SelectObject(hdc, d->f_body ? d->f_body : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(hdc, theme_cr(has_note ? tk->text_main : tk->text_dim));
    RECT note_rc = nrect_to_rect(&d->lay.note);
    DrawTextW(hdc, note_text(d), -1, &note_rc,
              DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(hdc, old);

    /* The risk tag, drawn exactly as the card draws it. */
    if (d->lay.tag.w > 0 && d->lay.tag.h > 0) {
        RECT tag_rc = nrect_to_rect(&d->lay.tag);
        COLORREF tbg, tfg;
        chatlv_safety_tag_colors(d->safety, &tbg, &tfg);
        ns_draw_chip(hdc, &tag_rc, tbg, tfg, d->f_caption,
                     chatlv_safety_tag_text(d->safety));
    }
}

static void detail_draw_button(DetailData *d, const DRAWITEMSTRUCT *dis)
{
    const ThemeTokens *tk = ns_tokens();
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    /* Clear the corners with the window background so the rounded fill
     * sits on a clean surface. */
    HBRUSH bg = CreateSolidBrush(theme_cr(tk->bg_primary.base));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    themed_button_track_hover(dis->hwndItem);

    int is_copy = (int)dis->CtlID == IDC_DETAIL_COPY;
    const ThemeSurface *surface = is_copy ? &tk->accent : &tk->bg_secondary;

    NsBtnState state;
    if (dis->itemState & ODS_DISABLED)       state = NS_BTN_DISABLED;
    else if (dis->itemState & ODS_SELECTED)  state = NS_BTN_PRESSED;
    else if (themed_button_is_hot(dis->hwndItem)) state = NS_BTN_HOVER;
    else                                     state = NS_BTN_REST;

    const char *label = is_copy ? (d->copied ? "Copied" : "Copy") : "Close";
    ns_draw_button(hdc, &rc, surface, state,
                   (dis->itemState & ODS_FOCUS) ? 1 : 0, label,
                   d->f_body, d->dpi);
}

/* ---- window procedure --------------------------------------------------- */

static LRESULT CALLBACK DetailProc(HWND hwnd, UINT msg, WPARAM wParam,
                                   LPARAM lParam)
{
    DetailData *d = (DetailData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lParam;
        d = (DetailData *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        d->hwnd = hwnd;
        d->dpi = get_window_dpi(hwnd);
        fetch_fonts(d);

        const ThemeTokens *tk = ns_tokens();
        d->bg_brush = CreateSolidBrush(theme_cr(tk->chat.cmd_bg));

        HINSTANCE inst = GetModuleHandle(NULL);
        d->hEdit = CreateWindowExW(0, L"EDIT", d->command_w,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_DETAIL_EDIT, inst, NULL);
        d->hCopy = CreateWindowExW(0, L"BUTTON", L"Copy",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_DETAIL_COPY, inst, NULL);
        d->hClose = CreateWindowExW(0, L"BUTTON", L"Close",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDCANCEL, inst, NULL);
        if (!d->hEdit || !d->hCopy || !d->hClose) return -1;

        apply_fonts(d);
        measure_tag(d);
        ns_dwm_set_dark_mode(hwnd, tk->is_dark ? TRUE : FALSE);
        detail_relayout(d);
        return 0;
    }

    case WM_SIZE:
        if (d) detail_relayout(d);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        int dpi = d ? d->dpi : 96;
        mmi->ptMinTrackSize.x = ns_scale(WIN_MIN_W, dpi);
        mmi->ptMinTrackSize.y = ns_scale(WIN_MIN_H, dpi);
        return 0;
    }

    case WM_DPICHANGED: {
        if (!d) break;
        d->dpi = (int)HIWORD(wParam);
        /* The popup's own handles come straight from the shared cache for
         * the new DPI. ns_font_flush() would delete the panel's cached
         * fonts out from under it, so it is never called from here. */
        fetch_fonts(d);
        apply_fonts(d);
        measure_tag(d);
        const RECT *sug = (const RECT *)lParam;
        SetWindowPos(hwnd, NULL, sug->left, sug->top,
                     sug->right - sug->left, sug->bottom - sug->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        detail_relayout(d);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;   /* WM_PAINT fills the whole client area */

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (d) detail_paint(d, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    /* A read-only EDIT asks for its colours with WM_CTLCOLORSTATIC, not
     * WM_CTLCOLOREDIT; answer both so it never falls back to the system
     * window colours. */
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        if (d && (HWND)lParam == d->hEdit) {
            const ThemeTokens *tk = ns_tokens();
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, theme_cr(tk->chat.cmd_text));
            SetBkColor(hdc, theme_cr(tk->chat.cmd_bg));
            return (LRESULT)d->bg_brush;
        }
        break;
    }

    case WM_DRAWITEM:
        if (d) {
            detail_draw_button(d, (const DRAWITEMSTRUCT *)lParam);
            return TRUE;
        }
        break;

    case WM_COMMAND:
        if (!d) break;
        if (LOWORD(wParam) == IDC_DETAIL_COPY) {
            /* The command, exactly as stored: no trailing newline, so
             * pasting into a terminal cannot run it. */
            if (d->command && d->command[0])
                clip_copy_utf8(hwnd, d->command, strlen(d->command));
            d->copied = 1;
            SetWindowTextW(d->hCopy, L"Copied");
            InvalidateRect(d->hCopy, NULL, FALSE);
            /* A plain one-shot WM_TIMER rather than ns_motion: that
             * module drives eased progress for animations (an NsAnimList
             * stepped by a 16 ms timer); reverting a label after a fixed
             * delay has no progress to ease and would only keep a 16 ms
             * timer running for 1.5 s. */
            SetTimer(hwnd, TIMER_COPIED, COPIED_MS, NULL);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_TIMER:
        if (d && wParam == TIMER_COPIED) {
            KillTimer(hwnd, TIMER_COPIED);
            d->copied = 0;
            SetWindowTextW(d->hCopy, L"Copy");
            InvalidateRect(d->hCopy, NULL, FALSE);
            return 0;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (d) {
            KillTimer(hwnd, TIMER_COPIED);
            if (d->bg_brush) { DeleteObject(d->bg_brush); d->bg_brush = NULL; }
            d->done = 1;
        }
        /* Re-enable the owner before this window goes, so activation
         * returns to it instead of some other application. */
        {
            HWND owner = GetWindow(hwnd, GW_OWNER);
            if (owner && IsWindow(owner)) {
                EnableWindow(owner, TRUE);
                SetForegroundWindow(owner);
            }
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ---- public API --------------------------------------------------------- */

void cmd_detail_show(HWND owner, const char *command, const char *note,
                     CmdSafetyLevel safety, int dpi)
{
    if (!owner || !IsWindow(owner)) return;
    if (dpi <= 0) dpi = 96;

    DetailData d;
    memset(&d, 0, sizeof(d));
    d.safety = safety;
    d.command = _strdup(command ? command : "");
    d.command_w = to_wide(command, 1);
    d.note_w = to_wide(note, 0);
    if (!d.command || !d.command_w || !d.note_w) goto cleanup;

    static int registered = 0;
    if (!registered) {
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = DetailProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = CMD_DETAIL_CLASS;
        if (!RegisterClassExW(&wc)) goto cleanup;
        registered = 1;
    }

    const DWORD style = WS_OVERLAPPEDWINDOW;
    int want_cw = ns_scale(WIN_BASE_W, dpi);
    int want_ch = ns_scale(WIN_BASE_H, dpi);
    RECT wr = { 0, 0, want_cw, want_ch };
    AdjustWindowRectEx(&wr, style, FALSE, 0);
    int win_w = wr.right - wr.left;
    int win_h = wr.bottom - wr.top;

    /* Centre on the owner's top-level window, kept inside its monitor's
     * work area. */
    HWND top = GetAncestor(owner, GA_ROOT);
    RECT orc;
    GetWindowRect(top ? top : owner, &orc);
    int x = orc.left + ((orc.right - orc.left) - win_w) / 2;
    int y = orc.top + ((orc.bottom - orc.top) - win_h) / 2;
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfo(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi)) {
        if (x + win_w > mi.rcWork.right) x = mi.rcWork.right - win_w;
        if (y + win_h > mi.rcWork.bottom) y = mi.rcWork.bottom - win_h;
        if (x < mi.rcWork.left) x = mi.rcWork.left;
        if (y < mi.rcWork.top) y = mi.rcWork.top;
    }

    HWND hwnd = CreateWindowExW(0, CMD_DETAIL_CLASS, L"Command details", style,
                                x, y, win_w, win_h, owner, NULL,
                                GetModuleHandle(NULL), &d);
    if (!hwnd) goto cleanup;

    /* AdjustWindowRectEx sizes the caption at the system DPI; on a
     * per-monitor-DPI window that can be a few pixels off, so trim until
     * the client area is exactly what was asked for. */
    {
        RECT cr, outer;
        GetClientRect(hwnd, &cr);
        GetWindowRect(hwnd, &outer);
        int dw = want_cw - cr.right;
        int dh = want_ch - cr.bottom;
        if (dw != 0 || dh != 0)
            SetWindowPos(hwnd, NULL, x, y,
                         (outer.right - outer.left) + dw,
                         (outer.bottom - outer.top) + dh,
                         SWP_NOZORDER | SWP_NOACTIVATE);
    }

    EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    SetFocus(d.hClose);

    MSG m;
    while (!d.done) {
        BOOL got = GetMessage(&m, NULL, 0, 0);
        if (got == 0) {            /* WM_QUIT: not ours to swallow */
            PostQuitMessage((int)m.wParam);
            break;
        }
        if (got < 0) break;
        if (m.message == WM_KEYDOWN &&
            (m.hwnd == hwnd || IsChild(hwnd, m.hwnd)) &&
            (m.wParam == VK_ESCAPE ||
             (m.wParam == VK_RETURN && m.hwnd != d.hEdit))) {
            DestroyWindow(hwnd);
            continue;
        }
        /* Tab cycles Copy, Close and the text box. */
        if (!IsDialogMessage(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    if (IsWindow(hwnd)) DestroyWindow(hwnd);   /* left the loop on WM_QUIT */

cleanup:
    free(d.command);
    free(d.command_w);
    free(d.note_w);
}

#endif /* _WIN32 */
