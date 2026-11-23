/* src/ui/notice_dlg.c -- the themed modal notice dialog (class
 * Nutshell_Notice). See notice_dlg.h. Modelled on pw_prompt.c (real STATIC /
 * BUTTON children, theming, modal loop) and cheatsheet_dlg.c (owner DPI,
 * WM_DPICHANGED). */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "notice_dlg.h"
#include "notice_layout.h"
#include "clipboard.h"
#include "themed_button.h"
#include "ns_tokens.h"
#include "ns_font.h"
#include "ns_scale.h"
#include "ns_type.h"
#include "ns_draw.h"
#include "dpi_util.h"

#define NOTICE_CLASS "Nutshell_Notice"
#define NOTICE_STYLE (WS_POPUP | WS_CAPTION | WS_SYSMENU)
#define NOTICE_EXSTYLE WS_EX_DLGMODALFRAME   /* deliberately not topmost */

typedef struct {
    Notice  n;                 /* a copy: the caller's may not outlive the call */
    const ThemeColors *theme;
    int     dpi;
    int     closing;           /* set by a button, Escape, WM_CLOSE; ends the loop */
    int     result;            /* 1 = action pressed */
    NoticeLayout lay;
    HWND    h_title, h_opening, h_closing, h_action, h_ok;
    HWND    h_label[NOTICE_ITEMS_MAX], h_value[NOTICE_ITEMS_MAX];
    HFONT   bold;              /* FONT_BODY at weight 700, ours to delete */
    HBRUSH  br_bg;             /* bg_primary.base: window and static text */
} NoticeDlg;

/* Windows 10 1607+; resolved at run time as pw_prompt.c does. */
static void notice_adjust_rect(RECT *rc, DWORD style, DWORD exstyle, int dpi)
{
    typedef BOOL (WINAPI *AdjustForDpi_fn)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static AdjustForDpi_fn pfn = NULL;
    static int resolved = 0;
    if (!resolved) {
        HMODULE h = GetModuleHandleA("user32.dll");
        if (h) pfn = (AdjustForDpi_fn)(void (*)(void))GetProcAddress(h, "AdjustWindowRectExForDpi");
        resolved = 1;
    }
    if (pfn && pfn(rc, style, FALSE, exstyle, (UINT)dpi)) return;
    AdjustWindowRectEx(rc, style, FALSE, exstyle);
}

/* The bold body font for list labels: no ramp role is bold at body size, so
 * it is the ramp's body font with the weight raised (never a fresh face). */
static void notice_make_bold(NoticeDlg *d)
{
    if (d->bold) { DeleteObject((HGDIOBJ)d->bold); d->bold = NULL; }
    HFONT body = ns_font(FONT_BODY, d->dpi);
    LOGFONTA lf;
    if (body && GetObjectA((HGDIOBJ)body, (int)sizeof(lf), &lf) > 0) {
        lf.lfWeight = FW_BOLD;
        d->bold = CreateFontIndirectA(&lf);
    }
}

static HFONT notice_label_font(const NoticeDlg *d)
{
    return d->bold ? d->bold : ns_font(FONT_BODY, d->dpi);
}

/* Height of `text` wrapped at `width` in `font`, never less than one line. */
static int notice_text_h(HDC hdc, HFONT font, const char *text, int width)
{
    HGDIOBJ old = font ? SelectObject(hdc, (HGDIOBJ)font) : NULL;
    RECT rc = { 0, 0, width > 1 ? width : 1, 0 };
    DrawTextA(hdc, text && text[0] ? text : "Ag", -1, &rc,
              DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    TEXTMETRICA tm;
    int line = 1;
    if (GetTextMetricsA(hdc, &tm)) line = tm.tmHeight;
    if (old) SelectObject(hdc, old);
    return rc.bottom - rc.top > line ? rc.bottom - rc.top : line;
}

static int notice_text_w(HDC hdc, HFONT font, const char *text)
{
    HGDIOBJ old = font ? SelectObject(hdc, (HGDIOBJ)font) : NULL;
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32A(hdc, text, (int)strlen(text), &sz);
    if (old) SelectObject(hdc, old);
    return (int)sz.cx;
}

/* Measure with the real fonts at d->dpi and lay out into d->lay. */
static void notice_measure(HWND hwnd, NoticeDlg *d)
{
    NoticeLayoutIn in;
    memset(&in, 0, sizeof(in));
    in.dpi = d->dpi;
    in.item_count = d->n.item_count < NOTICE_ITEMS_MAX ? d->n.item_count : NOTICE_ITEMS_MAX;
    if (in.item_count < 0) in.item_count = 0;
    in.has_action = d->n.action[0] != '\0';

    NoticeLayout probe;                     /* text_w does not depend on heights */
    notice_layout(&in, &probe);
    int text_w = probe.text_w;
    HFONT body = ns_font(FONT_BODY, d->dpi);

    HDC hdc = GetDC(hwnd);
    in.title_h = notice_text_h(hdc, ns_font(FONT_TITLE, d->dpi), "Ag", text_w);
    in.opening_h = notice_text_h(hdc, body, d->n.opening, text_w);
    in.closing_h = d->n.closing[0] ? notice_text_h(hdc, body, d->n.closing, text_w) : 0;

    int cap = text_w * 4 / 10;
    int lw = 0;
    for (int i = 0; i < in.item_count; i++) {
        int w = notice_text_w(hdc, notice_label_font(d), d->n.items[i].label);
        if (w > lw) lw = w;
    }
    if (lw > cap) lw = cap;
    in.label_w = lw;
    int vw = text_w - lw - ns_scale(SP_SM, d->dpi);
    for (int i = 0; i < in.item_count; i++)
        in.value_h[i] = notice_text_h(hdc, body, d->n.items[i].value, vw);
    ReleaseDC(hwnd, hdc);
    notice_layout(&in, &d->lay);
}

static void move_to(HWND h, const NsRect *r)
{
    if (h) MoveWindow(h, r->x, r->y, r->w, r->h, TRUE);
}

static void notice_place(NoticeDlg *d)
{
    HFONT body = ns_font(FONT_BODY, d->dpi);
    SendMessageA(d->h_title, WM_SETFONT, (WPARAM)ns_font(FONT_TITLE, d->dpi), TRUE);
    SendMessageA(d->h_opening, WM_SETFONT, (WPARAM)body, TRUE);
    if (d->h_closing) SendMessageA(d->h_closing, WM_SETFONT, (WPARAM)body, TRUE);
    SendMessageA(d->h_ok, WM_SETFONT, (WPARAM)body, TRUE);
    if (d->h_action) SendMessageA(d->h_action, WM_SETFONT, (WPARAM)body, TRUE);
    for (int i = 0; i < d->n.item_count && i < NOTICE_ITEMS_MAX; i++) {
        SendMessageA(d->h_label[i], WM_SETFONT, (WPARAM)notice_label_font(d), TRUE);
        SendMessageA(d->h_value[i], WM_SETFONT, (WPARAM)body, TRUE);
        move_to(d->h_label[i], &d->lay.label[i]);
        move_to(d->h_value[i], &d->lay.value[i]);
    }
    move_to(d->h_title, &d->lay.title);
    move_to(d->h_opening, &d->lay.opening);
    move_to(d->h_closing, &d->lay.closing);
    move_to(d->h_action, &d->lay.action);
    move_to(d->h_ok, &d->lay.ok);
}

/* Measure, lay out, size the window to the layout and centre it on `anchor`
 * (the owner's rectangle, the rectangle WM_DPICHANGED suggests, or the work
 * area), kept inside the monitor's work area. */
static void notice_fit(HWND hwnd, NoticeDlg *d, const RECT *anchor)
{
    notice_measure(hwnd, d);
    notice_place(d);
    RECT crc = { 0, 0, d->lay.client_w, d->lay.client_h };
    notice_adjust_rect(&crc, NOTICE_STYLE, NOTICE_EXSTYLE, d->dpi);
    int w = crc.right - crc.left, h = crc.bottom - crc.top;
    RECT work = *anchor;
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoA(MonitorFromRect(anchor, MONITOR_DEFAULTTONEAREST), &mi))
        work = mi.rcWork;
    int x = anchor->left + (anchor->right - anchor->left - w) / 2;
    int y = anchor->top + (anchor->bottom - anchor->top - h) / 2;
    if (x + w > work.right) x = work.right - w;
    if (y + h > work.bottom) y = work.bottom - h;
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;
    SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hwnd, NULL, TRUE);
}

static HWND mk_static(HWND parent, const char *text, int id, DWORD extra)
{
    return CreateWindowExA(0, "STATIC", text,
                           WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | extra,
                           0, 0, 10, 10, parent, (HMENU)(UINT_PTR)id, NULL, NULL);
}

static HWND mk_button(HWND parent, const char *text, int id)
{
    return CreateWindowExA(0, "BUTTON", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                           0, 0, 10, 10, parent, (HMENU)(UINT_PTR)id, NULL, NULL);
}

static const ThemeSurface *tone_surface(const NoticeDlg *d)
{
    const ThemeTokens *tk = ns_tokens();
    return d->n.tone == NOTICE_WARNING ? &tk->warning : &tk->info;
}

static void notice_paint(HWND hwnd, NoticeDlg *d)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    const NoticeLayout *L = &d->lay;
    const ThemeSurface *tone = tone_surface(d);
    COLORREF accent = theme_cr(tone->base);

    RECT bar = { L->bar.x, L->bar.y, L->bar.x + L->bar.w, L->bar.y + L->bar.h };
    HBRUSH br = CreateSolidBrush(accent);
    FillRect(hdc, &bar, br);
    DeleteObject(br);

    RECT ic = { L->icon.x, L->icon.y, L->icon.x + L->icon.w, L->icon.y + L->icon.h };
    ns_draw_round_fill(hdc, &ic, L->icon.w / 2, accent, 255);
    HGDIOBJ old = SelectObject(hdc, (HGDIOBJ)notice_label_font(d));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, theme_cr(tone->label));
    DrawTextA(hdc, d->n.tone == NOTICE_WARNING ? "!" : "i", 1, &ic,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, old);
    EndPaint(hwnd, &ps);
}

/* Closes with `result` (1 = action). Posting WM_NULL wakes a GetMessage loop
 * that is waiting. */
static void notice_close(HWND hwnd, NoticeDlg *d, int result)
{
    d->result = result;
    d->closing = 1;
    PostMessageA(hwnd, WM_NULL, 0, 0);
}

static void notice_copy(HWND hwnd, const NoticeDlg *d)
{
    char text[NOTICE_ITEMS_MAX * 240 + 1400];
    size_t len = notice_plain_text(&d->n, text, sizeof(text));
    /* Windows line ends for the clipboard. */
    char *crlf = (char *)malloc(2 * len + 1);
    if (!crlf) return;
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') crlf[o++] = '\r';
        crlf[o++] = text[i];
    }
    crlf[o] = '\0';
    (void)clip_copy_utf8(hwnd, crlf, o);
    free(crlf);
}

/* Moves focus between the buttons; with one button it stays put. */
static void notice_tab(NoticeDlg *d)
{
    if (!d->h_action) { SetFocus(d->h_ok); return; }
    SetFocus(GetFocus() == d->h_ok ? d->h_action : d->h_ok);
}

/* A key for the dialog, called from the modal loop. Returns 1 when handled. */
static int notice_key(HWND hwnd, NoticeDlg *d, WPARAM vk)
{
    switch (vk) {
    case VK_RETURN:
        notice_close(hwnd, d, (d->h_action && GetFocus() == d->h_action) ? 1 : 0);
        return 1;
    case VK_ESCAPE:
        notice_close(hwnd, d, 0);
        return 1;
    case VK_TAB:
        notice_tab(d);
        return 1;
    case 'C':
        if (GetKeyState(VK_CONTROL) & 0x8000) { notice_copy(hwnd, d); return 1; }
        return 0;
    default:
        return 0;
    }
}

static LRESULT CALLBACK notice_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    NoticeDlg *d = (NoticeDlg *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)lp;
        d = (NoticeDlg *)cs->lpCreateParams;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        d->br_bg = CreateSolidBrush(theme_cr(ns_tokens()->bg_primary.base));
        notice_make_bold(d);
        themed_apply_title_bar(hwnd, d->theme);
        d->h_title = mk_static(hwnd, d->n.title, NOTICE_ID_TITLE, SS_ENDELLIPSIS);
        d->h_opening = mk_static(hwnd, d->n.opening, NOTICE_ID_OPENING, 0);
        int cnt = d->n.item_count < NOTICE_ITEMS_MAX ? d->n.item_count : NOTICE_ITEMS_MAX;
        if (cnt < 0) cnt = 0;
        d->n.item_count = cnt;
        for (int i = 0; i < cnt; i++) {
            d->h_label[i] = mk_static(hwnd, d->n.items[i].label,
                                      NOTICE_ID_LABEL0 + i, SS_ENDELLIPSIS);
            d->h_value[i] = mk_static(hwnd, d->n.items[i].value, NOTICE_ID_VALUE0 + i, 0);
        }
        if (d->n.closing[0])
            d->h_closing = mk_static(hwnd, d->n.closing, NOTICE_ID_CLOSING, 0);
        /* Tab order: the action, then OK (OK is the default focus). */
        if (d->n.action[0]) d->h_action = mk_button(hwnd, d->n.action, NOTICE_ID_ACTION);
        d->h_ok = mk_button(hwnd, "OK", NOTICE_ID_OK);
        return 0;
    }
    case WM_ERASEBKGND:
        if (d && d->br_bg) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wp, &rc, d->br_bg);
            return 1;
        }
        break;
    case WM_PAINT:
        if (d) { notice_paint(hwnd, d); return 0; }
        break;
    case WM_CTLCOLORSTATIC:
        if (d) {
            const ThemeTokens *tk = ns_tokens();
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, theme_cr(tk->text_main));
            SetBkColor(hdc, theme_cr(tk->bg_primary.base));
            return (LRESULT)d->br_bg;
        }
        break;
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *dis = (const DRAWITEMSTRUCT *)lp;
        if (!d || dis->CtlType != ODT_BUTTON) break;
        const ThemeTokens *tk = ns_tokens();
        themed_button_track_hover(dis->hwndItem);
        const ThemeSurface *surface = (dis->CtlID == (UINT)NOTICE_ID_OK) ? &tk->accent
                                                                         : &tk->raised;
        NsBtnState state;
        if (dis->itemState & ODS_DISABLED)      state = NS_BTN_DISABLED;
        else if (dis->itemState & ODS_SELECTED) state = NS_BTN_PRESSED;
        else if (themed_button_is_hot(dis->hwndItem)) state = NS_BTN_HOVER;
        else                                    state = NS_BTN_REST;
        char label[64];
        GetWindowTextA(dis->hwndItem, label, (int)sizeof(label));
        RECT rc = dis->rcItem;
        FillRect(dis->hDC, &rc, d->br_bg);   /* clear the rounded corners */
        ns_draw_button(dis->hDC, &rc, surface, state,
                       (dis->itemState & ODS_FOCUS) ? 1 : 0, label,
                       ns_font(FONT_BODY, d->dpi), d->dpi);
        return TRUE;
    }
    case WM_ACTIVATE:
        if (d && LOWORD(wp) != WA_INACTIVE && !HIWORD(wp)) {
            HWND f = GetFocus();
            if (!f || !IsChild(hwnd, f)) SetFocus(d->h_ok);
            return 0;
        }
        break;
    case WM_COMMAND:
        if (!d) break;
        switch (LOWORD(wp)) {
        case NOTICE_ID_ACTION: notice_close(hwnd, d, 1); break;
        case NOTICE_ID_OK:
        case IDCANCEL:         notice_close(hwnd, d, 0); break;
        default: break;
        }
        return 0;
    case WM_CLOSE:                      /* the caption's X means OK */
        if (d) notice_close(hwnd, d, 0);
        return 0;
    case WM_DPICHANGED:
        if (d) {
            /* Fonts come from the shared cache at the new DPI; ns_font_flush()
             * would pull the main window's fonts from under it (as
             * cheatsheet_dlg.c). */
            d->dpi = (int)HIWORD(wp);
            notice_make_bold(d);
            notice_fit(hwnd, d, (const RECT *)lp);
            return 0;
        }
        break;
    case WM_DESTROY:
        if (d) {
            if (d->br_bg) { DeleteObject((HGDIOBJ)d->br_bg); d->br_bg = NULL; }
            if (d->bold)  { DeleteObject((HGDIOBJ)d->bold);  d->bold = NULL; }
        }
        /* No PostQuitMessage: the modal loop ends on IsWindow / the closing
         * flag (see pw_prompt.c's WM_DESTROY). */
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int notice_dlg_show(HWND owner, const Notice *n, const ThemeColors *theme)
{
    if (!n) return 0;
    static int registered = 0;
    HINSTANCE inst = GetModuleHandleA(NULL);
    if (!registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc   = notice_proc;
        wc.hInstance     = inst;
        wc.lpszClassName = NOTICE_CLASS;
        wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
        RegisterClassA(&wc);   /* background painted via WM_ERASEBKGND */
        registered = 1;
    }

    NoticeDlg *d = (NoticeDlg *)calloc(1, sizeof(*d));
    if (!d) return 0;
    d->n = *n;
    d->theme = theme ? theme : ui_theme_get(0);
    d->dpi = get_window_dpi(owner);

    RECT anchor = { 0, 0, 0, 0 };
    if (!owner || !IsWindow(owner) || !GetWindowRect(owner, &anchor))
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &anchor, 0);

    HWND prev_focus = GetFocus();
    HWND dlg = CreateWindowExA(NOTICE_EXSTYLE, NOTICE_CLASS, d->n.title, NOTICE_STYLE,
                               anchor.left, anchor.top, 100, 100,
                               owner, NULL, inst, d);
    if (!dlg) { free(d); return 0; }
    d->dpi = get_window_dpi(dlg);   /* the monitor it was created on */
    notice_fit(dlg, d, &anchor);

    /* Re-enable the owner only if this call disabled it: a notice raised
     * inside another modal must not re-enable the window under it. */
    BOOL was_disabled = owner ? EnableWindow(owner, FALSE) : TRUE;
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    SetFocus(d->h_ok);

    MSG m;
    memset(&m, 0, sizeof(m));
    int gm = 1;
    while (IsWindow(dlg) && !d->closing && (gm = GetMessageA(&m, NULL, 0, 0)) > 0) {
        /* The buttons are owner-drawn, so IsDialogMessage would not treat
         * OK as the default: the keys are taken here. */
        if (m.message == WM_KEYDOWN && (m.hwnd == dlg || IsChild(dlg, m.hwnd)) &&
            notice_key(dlg, d, m.wParam))
            continue;
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }

    int result = d->closing ? d->result : 0;
    if (owner && !was_disabled) EnableWindow(owner, TRUE);
    if (owner && IsWindow(owner)) SetForegroundWindow(owner);   /* before the destroy */
    if (IsWindow(dlg)) DestroyWindow(dlg);
    if (prev_focus && IsWindow(prev_focus)) SetFocus(prev_focus);
    free(d);

    /* A real WM_QUIT belongs to ui_run()'s loop; -1 is an error with no quit
     * in it and must not be forwarded. */
    if (gm == 0) PostQuitMessage((int)m.wParam);
    return result;
}
