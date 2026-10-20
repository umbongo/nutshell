#include "cheatsheet_dlg.h"

#ifdef _WIN32
#include "cheatsheet_layout.h"
#include "shortcuts.h"
#include "ns_font.h"
#include "ns_scale.h"
#include "ns_type.h"
#include "ns_draw.h"
#include "ns_tokens.h"
#include "themed_button.h"
#include "custom_scrollbar.h"
#include "dpi_util.h"
#include <stdlib.h>
#include <string.h>

#define CHEAT_CLASS       "Nutshell_Shortcuts"
#define CHEAT_TITLE       "Keyboard Shortcuts"
#define CHEAT_STYLE       (WS_POPUP | WS_CAPTION | WS_SYSMENU)
#define CHEAT_EXSTYLE     WS_EX_DLGMODALFRAME

typedef struct {
    const ThemeColors *theme;
    HWND     hClose;       /* owner-draw Close button, id IDOK */
    HWND     hScroll;      /* themed scrollbar; NULL until the page first scrolls */
    int      dpi;
    int      scroll;       /* px into the page */
    int      closing;      /* set by Close/Esc/Enter/WM_CLOSE; ends the loop */
    CsInput  in;
    CsLayout lay;
} CheatData;

static int s_open = 0;     /* a second Help > Keyboard Shortcuts is ignored */

/* Measures one string for cheatsheet_input_from_table: the shared ns_font
 * cache's font for `role` at the window's DPI, selected into the HDC. */
typedef struct { HDC hdc; int dpi; } CheatMeasure;

static int cheat_measure_text(void *ctx, NsFontRole role, const char *text)
{
    CheatMeasure *m = (CheatMeasure *)ctx;
    if (!text || !text[0]) return 0;
    HFONT font = ns_font(role, m->dpi);
    HGDIOBJ old = font ? SelectObject(m->hdc, (HGDIOBJ)font) : NULL;
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32A(m->hdc, text, (int)strlen(text), &sz);
    if (old) SelectObject(m->hdc, old);
    return (int)sz.cx;
}

/* Fill d->in from the table, measured with the real fonts at d->dpi, for a
 * work area of work_w x work_h. The frame size comes from the live window. */
static void cheat_measure(HWND hwnd, CheatData *d, int work_w, int work_h)
{
    CsInput *in = &d->in;
    memset(in, 0, sizeof(*in));
    in->dpi = d->dpi;
    in->work_w = work_w;
    in->work_h = work_h;

    RECT wr, cr;
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    in->nc_w = (wr.right - wr.left) - cr.right;
    in->nc_h = (wr.bottom - wr.top) - cr.bottom;

    in->body_h      = ns_type_line_px(FONT_BODY, d->dpi);
    in->caption_h   = ns_type_line_px(FONT_CAPTION, d->dpi);
    in->heading_h   = ns_type_line_px(FONT_HEADING, d->dpi);
    in->scrollbar_w = ns_scale(CSB_WIDTH, d->dpi);

    CheatMeasure m;
    m.hdc = GetDC(hwnd);
    m.dpi = d->dpi;
    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    cheatsheet_input_from_table(in, t, n, cheat_measure_text, &m);
    ReleaseDC(hwnd, m.hdc);
}

/* Close button and scrollbar where the layout puts them. */
static void cheat_place_children(HWND hwnd, CheatData *d)
{
    const CsLayout *l = &d->lay;
    if (d->hClose)
        MoveWindow(d->hClose, l->button.x, l->button.y, l->button.w, l->button.h, TRUE);
    if (l->scrolls) {
        if (!d->hScroll) {
            csb_register(GetModuleHandle(NULL));
            d->hScroll = csb_create(hwnd, l->scrollbar.x, l->scrollbar.y,
                                    l->scrollbar.w, l->scrollbar.h,
                                    d->theme, GetModuleHandle(NULL));
        } else {
            MoveWindow(d->hScroll, l->scrollbar.x, l->scrollbar.y,
                       l->scrollbar.w, l->scrollbar.h, TRUE);
            ShowWindow(d->hScroll, SW_SHOWNOACTIVATE);
        }
        if (d->hScroll) {
            csb_set_range(d->hScroll, 0, l->page_h > 0 ? l->page_h - 1 : 0, l->view.h);
            csb_set_pos(d->hScroll, d->scroll);
        }
    } else if (d->hScroll) {
        ShowWindow(d->hScroll, SW_HIDE);
    }
    InvalidateRect(hwnd, NULL, FALSE);
}

/* Measure, lay out, size and place the window: centred on `anchor` (the
 * owner's rectangle, or the rectangle WM_DPICHANGED suggests), inside the
 * work area of the monitor `anchor` is on. */
static void cheat_fit(HWND hwnd, CheatData *d, const RECT *anchor)
{
    RECT work = *anchor;
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfo(MonitorFromRect(anchor, MONITOR_DEFAULTTONEAREST), &mi))
        work = mi.rcWork;

    cheat_measure(hwnd, d, work.right - work.left, work.bottom - work.top);
    cheatsheet_layout_compute(&d->lay, &d->in);

    int win_w = d->lay.client_w + d->in.nc_w;
    int win_h = d->lay.client_h + d->in.nc_h;
    int x = 0, y = 0;
    cheatsheet_place(anchor->left, anchor->top,
                     anchor->right - anchor->left, anchor->bottom - anchor->top,
                     win_w, win_h, work.left, work.top,
                     work.right - work.left, work.bottom - work.top, &x, &y);
    SetWindowPos(hwnd, NULL, x, y, win_w, win_h, SWP_NOZORDER | SWP_NOACTIVATE);

    /* The frame can differ by a pixel or two from the one measured before
     * the move (per-monitor DPI); trim so the client is exactly the layout. */
    RECT cr;
    GetClientRect(hwnd, &cr);
    int dw = d->lay.client_w - cr.right;
    int dh = d->lay.client_h - cr.bottom;
    if (dw != 0 || dh != 0)
        SetWindowPos(hwnd, NULL, 0, 0, win_w + dw, win_h + dh,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    d->scroll = cheatsheet_scroll_clamp(d->scroll, d->lay.page_h, d->lay.view.h);
    cheat_place_children(hwnd, d);
}

static void cheat_scroll_to(HWND hwnd, CheatData *d, int offset)
{
    int s = cheatsheet_scroll_clamp(offset, d->lay.page_h, d->lay.view.h);
    if (s == d->scroll) return;
    d->scroll = s;
    if (d->hScroll) csb_set_pos(d->hScroll, s);
    RECT v = { d->lay.view.x, d->lay.view.y,
               d->lay.view.x + d->lay.view.w, d->lay.view.y + d->lay.view.h };
    InvalidateRect(hwnd, &v, FALSE);
}

static int cheat_line_step(const CheatData *d)
{
    return d->in.body_h + ns_scale(SP_XS, d->dpi);
}

static int cheat_page_step(const CheatData *d)
{
    int step = d->lay.view.h - cheat_line_step(d);
    return step > 0 ? step : cheat_line_step(d);
}

/* Keys the sheet handles itself. 1 when the key was consumed. */
static int cheat_key(HWND hwnd, CheatData *d, WPARAM vk)
{
    switch (vk) {
    case VK_ESCAPE:
    case VK_RETURN: d->closing = 1; return 1;
    case VK_PRIOR:  cheat_scroll_to(hwnd, d, d->scroll - cheat_page_step(d)); return 1;
    case VK_NEXT:   cheat_scroll_to(hwnd, d, d->scroll + cheat_page_step(d)); return 1;
    case VK_UP:     cheat_scroll_to(hwnd, d, d->scroll - cheat_line_step(d)); return 1;
    case VK_DOWN:   cheat_scroll_to(hwnd, d, d->scroll + cheat_line_step(d)); return 1;
    case VK_HOME:   cheat_scroll_to(hwnd, d, 0); return 1;
    case VK_END:    cheat_scroll_to(hwnd, d, d->lay.page_h); return 1;
    default:        return 0;
    }
}

static void cheat_text(HDC hdc, CsRect r, int dy, NsFontRole role, int dpi,
                       unsigned int colour, const char *s)
{
    if (!s || !s[0] || r.w <= 0 || r.h <= 0) return;
    RECT rc = { r.x, r.y + dy, r.x + r.w, r.y + dy + r.h };
    HFONT f = ns_font(role, dpi);
    HGDIOBJ old = f ? SelectObject(hdc, (HGDIOBJ)f) : NULL;
    SetTextColor(hdc, theme_cr(colour));
    DrawTextA(hdc, s, -1, &rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    if (old) SelectObject(hdc, old);
}

static void cheat_paint(HDC hdc, const RECT *client, const CheatData *d)
{
    const ThemeTokens *tk = ns_tokens();
    const CsLayout *l = &d->lay;

    HBRUSH bg = CreateSolidBrush(theme_cr(tk->bg_primary.base));
    FillRect(hdc, client, bg);
    DeleteObject(bg);

    HRGN clip = CreateRectRgn(l->view.x, l->view.y,
                              l->view.x + l->view.w, l->view.y + l->view.h);
    SelectClipRgn(hdc, clip);
    SetBkMode(hdc, TRANSPARENT);

    const int dy = l->view.y - d->scroll;
    const int top = d->scroll;
    const int bottom = d->scroll + l->view.h;

    const char *names[CS_MAX_SECTIONS];
    size_t ns = shortcuts_sections(names, CS_MAX_SECTIONS);
    for (int s = 0; s < l->n_sections && (size_t)s < ns; s++) {
        cheat_text(hdc, l->heading[s], dy, FONT_HEADING, d->dpi, tk->text_main, names[s]);
        ns_draw_separator(hdc, l->rule[s].x, l->rule[s].x + l->rule[s].w,
                          l->rule[s].y + dy, theme_cr(tk->border));
    }

    size_t n = 0;
    const Shortcut *t = shortcuts_table(&n);
    for (int i = 0; i < l->n_rows && (size_t)i < n; i++) {
        const CsRect *row = &l->row[i];
        if (row->y + row->h < top || row->y > bottom) continue;
        cheat_text(hdc, l->chord[i],  dy, FONT_BODY,    d->dpi, tk->text_main, t[i].chord);
        cheat_text(hdc, l->action[i], dy, FONT_BODY,    d->dpi, tk->text_main, t[i].action);
        cheat_text(hdc, l->cond[i],   dy, FONT_CAPTION, d->dpi, tk->text_dim,  t[i].condition);
    }

    SelectClipRgn(hdc, NULL);
    DeleteObject(clip);
}

static LRESULT CALLBACK CheatWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CheatData *d = (CheatData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT *cs = (CREATESTRUCT *)lParam;
        d = (CheatData *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)d);
        d->dpi = get_window_dpi(hwnd);
        d->hClose = CreateWindowA("BUTTON", "Close",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                  0, 0, 10, 10, hwnd, (HMENU)(UINT_PTR)IDOK,
                                  GetModuleHandle(NULL), NULL);
        if (d->hClose) {
            HFONT font = ns_font(FONT_BODY, d->dpi);
            if (font) SendMessage(d->hClose, WM_SETFONT, (WPARAM)font, TRUE);
        }
        themed_apply_title_bar(hwnd, d->theme);
        themed_apply_borders(hwnd, d->theme);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (d && rc.right > 0 && rc.bottom > 0) {
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, (HGDIOBJ)bmp);
            cheat_paint(mem, &rc, d);
            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (d && dis && dis->CtlID == IDOK) {
            draw_themed_button(dis, d->theme, 1);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
        if (d && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)) {
            d->closing = 1;
            PostMessage(hwnd, WM_NULL, 0, 0);   /* wake the modal GetMessage loop */
            return 0;
        }
        break;

    case WM_CLOSE:                       /* the caption's X: the loop tears down */
        if (d) {
            d->closing = 1;
            PostMessage(hwnd, WM_NULL, 0, 0);   /* wake the modal GetMessage loop */
        }
        return 0;

    case WM_KEYDOWN:
        if (d && cheat_key(hwnd, d, wParam)) return 0;
        break;

    case WM_MOUSEWHEEL:
        if (d && d->lay.scrolls) {
            int lines = 3 * GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            cheat_scroll_to(hwnd, d, d->scroll - lines * cheat_line_step(d));
            return 0;
        }
        break;

    case WM_VSCROLL:
        if (d && d->hScroll && (HWND)lParam == d->hScroll) {
            switch (LOWORD(wParam)) {
            case SB_LINEUP:        cheat_scroll_to(hwnd, d, d->scroll - cheat_line_step(d)); break;
            case SB_LINEDOWN:      cheat_scroll_to(hwnd, d, d->scroll + cheat_line_step(d)); break;
            case SB_PAGEUP:        cheat_scroll_to(hwnd, d, d->scroll - cheat_page_step(d)); break;
            case SB_PAGEDOWN:      cheat_scroll_to(hwnd, d, d->scroll + cheat_page_step(d)); break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: cheat_scroll_to(hwnd, d, csb_get_trackpos(d->hScroll)); break;
            case SB_TOP:           cheat_scroll_to(hwnd, d, 0); break;
            case SB_BOTTOM:        cheat_scroll_to(hwnd, d, d->lay.page_h); break;
            default: break;
            }
            return 0;
        }
        break;

    case WM_DPICHANGED:
        if (d) {
            /* Fonts come straight from the shared cache at the new DPI;
             * ns_font_flush() would pull the main window's fonts out from
             * under it, so it is never called here (as cmd_detail_dlg.c). */
            d->dpi = (int)HIWORD(wParam);
            HFONT font = ns_font(FONT_BODY, d->dpi);
            if (d->hClose && font) SendMessage(d->hClose, WM_SETFONT, (WPARAM)font, TRUE);
            cheat_fit(hwnd, d, (const RECT *)lParam);
            return 0;
        }
        break;

    case WM_NCDESTROY:
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        break;

    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void cheatsheet_show(HWND parent, HWND floating_panel, const ThemeColors *theme)
{
    if (s_open) return;
    if (!theme) theme = ui_theme_get(0);

    static int registered = 0;
    if (!registered) {
        WNDCLASSEXA wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = CheatWndProc;
        wc.hInstance     = GetModuleHandle(NULL);
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = CHEAT_CLASS;
        RegisterClassExA(&wc);
        registered = 1;
    }

    RECT anchor = { 0, 0, 0, 0 };
    if (!parent || !GetWindowRect(parent, &anchor))
        SystemParametersInfoA(SPI_GETWORKAREA, 0, &anchor, 0);

    CheatData d;
    memset(&d, 0, sizeof(d));
    d.theme = theme;

    HWND prev_focus = GetFocus();
    /* Created on the owner's monitor so its DPI is the owner's; cheat_fit()
     * then gives it its real size and position. */
    HWND dlg = CreateWindowExA(CHEAT_EXSTYLE, CHEAT_CLASS, CHEAT_TITLE, CHEAT_STYLE,
                               anchor.left, anchor.top, 400, 300,
                               parent, NULL, GetModuleHandle(NULL), &d);
    if (!dlg) return;
    s_open = 1;
    cheat_fit(dlg, &d, &anchor);

    /* was_disabled (window.c's passphrase prompt): re-enable each window
     * only if this call is the one that disabled it, so a sheet opened
     * inside another modal never re-enables the main window under it. */
    BOOL parent_was_disabled = parent ? EnableWindow(parent, FALSE) : TRUE;
    BOOL panel_was_disabled = TRUE;
    if (floating_panel && IsWindow(floating_panel))
        panel_was_disabled = EnableWindow(floating_panel, FALSE);

    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);
    if (d.hClose) SetFocus(d.hClose);

    MSG m;
    memset(&m, 0, sizeof(m));
    int gm = 1;
    while (IsWindow(dlg) && !d.closing && (gm = GetMessage(&m, NULL, 0, 0)) > 0) {
        /* Keys reach the focused Close button, not the sheet, so they are
         * taken here: IsDialogMessage never sees an owner-draw default. */
        if (m.message == WM_KEYDOWN && (m.hwnd == dlg || IsChild(dlg, m.hwnd)) &&
            cheat_key(dlg, &d, m.wParam))
            continue;
        if (!IsDialogMessage(dlg, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }

    if (parent && !parent_was_disabled) EnableWindow(parent, TRUE);
    if (floating_panel && !panel_was_disabled && IsWindow(floating_panel))
        EnableWindow(floating_panel, TRUE);
    if (parent) SetForegroundWindow(parent);   /* before the destroy, or Windows activates another app */
    if (IsWindow(dlg)) DestroyWindow(dlg);
    if (prev_focus && IsWindow(prev_focus)) SetFocus(prev_focus);
    s_open = 0;

    /* A real WM_QUIT (gm == 0) belongs to ui_run()'s loop; -1 is an error
     * with no quit in it and must not be forwarded. */
    if (gm == 0) PostQuitMessage((int)m.wParam);
}

#endif /* _WIN32 */
