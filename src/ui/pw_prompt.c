/* src/ui/pw_prompt.c -- modal themed secret prompt (SSH key passphrase).
 *
 * Moved out of window.c; colours come from ns_tokens(), geometry from the
 * ns_type.h grid, and the field carries the hold-to-reveal eye. */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pw_prompt.h"
#include "pw_prompt_layout.h"
#include "pw_reveal.h"
#include "secure_zero.h"
#include "ns_tokens.h"
#include "ns_font.h"
#include "ns_scale.h"
#include "ns_type.h"
#include "ns_draw.h"
#include "themed_button.h"
#include "dpi_util.h"

#define PW_ID_EDIT 101
#define PW_ID_SAVE 102
#define PW_ID_TEXT 103

#define PW_SAVE_TEXT "Save password to this profile"

typedef struct {
    char        buf[256];
    int         ok;
    int         dpi;
    /* label, detail and text are read in WM_CREATE only (inside
     * CreateWindowExA, before the modal loop) and cleared afterwards: the
     * caller's strings may not outlive that. Nothing later reads them. */
    const char *label;
    const char *detail;    /* NULL = no detail row */
    const char *text;      /* NULL = no text area */
    int         echo;      /* 1 = no ES_PASSWORD, no reveal eye */
    int         offer_save;
    int         save;      /* checkbox state captured on OK */
    int         edit_max;  /* EM_LIMITTEXT: out_size - 1, capped to buf */
    HWND        detail_wnd;
    HWND        text_wnd;
    const ThemeColors *theme;
    HBRUSH      br_bg;     /* bg_primary.base, window and static text */
    HBRUSH      br_edit;   /* bg_secondary.base, the field */
} PwCtx;

/* An owner-drawn OK / Cancel button at column x of the layout's button row. */
static HWND pw_make_button(HWND parent, const char *text, int id, int x,
                           const PwLayout *L, HFONT font)
{
    HWND b = CreateWindowExA(0, "BUTTON", text,
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    x, L->y_btn, L->bw, L->ctlh,
                    parent, (HMENU)(UINT_PTR)id, NULL, NULL);
    SendMessageA(b, WM_SETFONT, (WPARAM)font, TRUE);
    return b;
}

/* AdjustWindowRectExForDpi is Windows 10 1607+; resolve it at run time (as
 * dpi_util.h does GetDpiForWindow) and fall back to the system-DPI call. */
static void pw_adjust_rect(RECT *rc, DWORD style, DWORD exstyle, int dpi)
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

static LRESULT CALLBACK pw_prompt_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PwCtx *ctx = (PwCtx *)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)lp;
        ctx = (PwCtx *)cs->lpCreateParams;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)ctx);
        const ThemeTokens *tk = ns_tokens();
        int dpi = ctx->dpi;
        ctx->br_bg   = CreateSolidBrush(theme_cr(tk->bg_primary.base));
        ctx->br_edit = CreateSolidBrush(theme_cr(tk->bg_secondary.base));
        themed_apply_title_bar(hwnd, ctx->theme);

        PwLayout L;
        pw_layout(dpi, ctx->detail != NULL, ctx->text != NULL, ctx->offer_save, &L);
        HFONT font = ns_font(FONT_BODY, dpi);

        if (ctx->text) {
            /* Read-only, scrolling, wraps (no ES_AUTOHSCROLL). Not a tab
             * stop (maintainer's ruling, 2026-11-05): focus starts in the
             * field and Tab goes field, checkbox, buttons; the mouse wheel
             * and scroll bar scroll the text. The multi-line EDIT shows a
             * bare LF as nothing, hence CRLF. */
            ctx->text_wnd = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                        ES_MULTILINE | ES_READONLY,
                        L.pad, L.y_text, L.fw, L.texth, hwnd,
                        (HMENU)(UINT_PTR)PW_ID_TEXT, NULL, NULL);
            SendMessageA(ctx->text_wnd, WM_SETFONT, (WPARAM)font, TRUE);
            size_t cap = 2 * strlen(ctx->text) + 1;
            char *crlf = (char *)malloc(cap);
            if (crlf) {
                pw_text_crlf(ctx->text, crlf, cap);
                SetWindowTextA(ctx->text_wnd, crlf);
                free(crlf);
            }
        }
        HWND lbl = CreateWindowExA(0, "STATIC", ctx->label ? ctx->label : "",
                        WS_CHILD | WS_VISIBLE | SS_NOPREFIX | SS_ENDELLIPSIS,
                        L.pad, L.y_label, L.fw, L.ctlh, hwnd, NULL, NULL, NULL);
        SendMessageA(lbl, WM_SETFONT, (WPARAM)font, TRUE);
        if (ctx->detail) {
            ctx->detail_wnd = CreateWindowExA(0, "STATIC", ctx->detail,
                        WS_CHILD | WS_VISIBLE | SS_NOPREFIX | SS_ENDELLIPSIS,
                        L.pad, L.y_detail, L.fw, L.ctlh, hwnd, NULL, NULL, NULL);
            SendMessageA(ctx->detail_wnd, WM_SETFONT, (WPARAM)font, TRUE);
        }
        HWND edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL |
                        (ctx->echo ? 0 : ES_PASSWORD),
                        L.pad, L.y_edit, L.fw, L.ctlh, hwnd,
                        (HMENU)(UINT_PTR)PW_ID_EDIT, NULL, NULL);
        SendMessageA(edit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageA(edit, EM_LIMITTEXT, (WPARAM)ctx->edit_max, 0);
        if (ctx->offer_save) {
            HWND chk = CreateWindowExA(0, "BUTTON", PW_SAVE_TEXT,
                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                        L.pad, L.y_save, L.fw, L.ctlh, hwnd,
                        (HMENU)(UINT_PTR)PW_ID_SAVE, NULL, NULL);
            ns_control_classic(chk);   /* keep the WM_CTLCOLORSTATIC colour */
            SendMessageA(chk, WM_SETFONT, (WPARAM)font, TRUE);
        }
        pw_make_button(hwnd, "OK", IDOK, L.pad + L.fw - 2 * L.bw - L.gap, &L, font);
        pw_make_button(hwnd, "Cancel", IDCANCEL, L.pad + L.fw - L.bw, &L, font);
        if (!ctx->echo)
            pw_reveal_attach(edit);   /* NULL on failure: the field still works */
        themed_apply_borders(hwnd, ctx->theme);
        SetFocus(edit);
        ctx->label = NULL;
        ctx->detail = NULL;
        ctx->text = NULL;
        return 0;
    }
    case WM_ERASEBKGND:
        if (ctx && ctx->br_bg) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wp, &rc, ctx->br_bg);
            return 1;
        }
        break;
    case WM_CTLCOLORSTATIC:
        if (ctx) {
            const ThemeTokens *tk = ns_tokens();
            HDC hdc = (HDC)wp;
            int dim = (ctx->detail_wnd && (HWND)lp == ctx->detail_wnd) ||
                      (ctx->text_wnd && (HWND)lp == ctx->text_wnd);
            SetTextColor(hdc, theme_cr(dim ? tk->text_dim : tk->text_main));
            SetBkColor(hdc, theme_cr(tk->bg_primary.base));
            return (LRESULT)ctx->br_bg;
        }
        break;
    case WM_CTLCOLOREDIT:
        if (ctx) {
            const ThemeTokens *tk = ns_tokens();
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, theme_cr(tk->text_main));
            SetBkColor(hdc, theme_cr(tk->bg_secondary.base));
            return (LRESULT)ctx->br_edit;
        }
        break;
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *dis = (const DRAWITEMSTRUCT *)lp;
        if (!ctx || dis->CtlType != ODT_BUTTON) break;
        const ThemeTokens *tk = ns_tokens();
        themed_button_track_hover(dis->hwndItem);
        const ThemeSurface *surface = (dis->CtlID == IDOK) ? &tk->accent : &tk->raised;
        NsBtnState state;
        if (dis->itemState & ODS_DISABLED)      state = NS_BTN_DISABLED;
        else if (dis->itemState & ODS_SELECTED) state = NS_BTN_PRESSED;
        else if (themed_button_is_hot(dis->hwndItem)) state = NS_BTN_HOVER;
        else                                    state = NS_BTN_REST;
        const char *label = (dis->CtlID == IDOK) ? "OK" : "Cancel";
        RECT rc = dis->rcItem;
        FillRect(dis->hDC, &rc, ctx->br_bg);   /* clear the rounded corners */
        ns_draw_button(dis->hDC, &rc, surface, state,
                       (dis->itemState & ODS_FOCUS) ? 1 : 0, label,
                       ns_font(FONT_BODY, ctx->dpi), ctx->dpi);
        return TRUE;
    }
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) {
            pw_reveal_mask_all(hwnd);
            break;
        }
        if (!HIWORD(wp)) {   /* not minimised */
            /* The only field. Return here: DefWindowProc would move focus
             * back to the window itself and typing would be dropped. */
            SetFocus(GetDlgItem(hwnd, PW_ID_EDIT));
            return 0;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK && ctx) {
            GetWindowTextA(GetDlgItem(hwnd, PW_ID_EDIT), ctx->buf, (int)sizeof(ctx->buf));
            ctx->save = ctx->offer_save &&
                        SendDlgItemMessageA(hwnd, PW_ID_SAVE, BM_GETCHECK, 0, 0) == BST_CHECKED;
            ctx->ok = 1;
            DestroyWindow(hwnd);
        } else if (LOWORD(wp) == IDCANCEL && ctx) {
            ctx->buf[0] = '\0';
            ctx->ok = 0;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        /* Clear the EDIT control's own text (control id PW_ID_EDIT, created
         * in WM_CREATE above) before it -- and this window -- go away. The
         * IDOK handler already copied it into ctx->buf; the control's own
         * internal text buffer is a second copy of the secret this
         * process would otherwise leave sitting in memory. WM_DESTROY is
         * the one place that runs on every path this dialog can end on --
         * OK, Cancel, the WM_QUIT path in pw_prompt_show(), and (Win32
         * destroys owned windows when their owner is destroyed) the main
         * window going away while this one is still up -- so it covers
         * all of them without needing the same call at every DestroyWindow
         * site. The child windows are still valid here; they aren't torn
         * down until after this handler returns. */
        SetDlgItemTextA(hwnd, PW_ID_EDIT, "");
        if (ctx) {
            if (ctx->br_bg)   { DeleteObject(ctx->br_bg);   ctx->br_bg = NULL; }
            if (ctx->br_edit) { DeleteObject(ctx->br_edit); ctx->br_edit = NULL; }
        }
        /* No PostQuitMessage() here: pw_prompt_show()'s loop watches
         * this window's own handle (IsWindow(hwnd)) to know when to stop,
         * not WM_QUIT. Posting WM_QUIT on every ordinary close (which is
         * what happens whenever the user answers this dialog -- the
         * common case, not just shutdown) used to be swallowed by that
         * same loop and worked only by coincidence when nothing else was
         * pumping messages concurrently; nested inside another modal loop
         * (a host-key MessageBox, another tab's own passphrase prompt) it
         * could instead terminate the *wrong* loop, or eat the real
         * app-exit WM_QUIT so ui_run() never sees it and the process
         * hangs after its window is gone. See pw_prompt_show(). */
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int pw_prompt_show(HWND parent, const PwPromptOpts *opts, char *out, int out_size,
                   int *save_out)
{
    if (save_out) *save_out = 0;
    static int class_done = 0;
    HINSTANCE inst = GetModuleHandleA(NULL);
    if (!class_done) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc   = pw_prompt_proc;
        wc.hInstance     = inst;
        wc.lpszClassName = "NutshellPassDlg";
        wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
        RegisterClassA(&wc);   /* background painted via WM_ERASEBKGND */
        class_done = 1;
    }

    PwCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.dpi   = get_window_dpi(parent);
    ctx.label = opts ? opts->label : NULL;
    ctx.theme = opts ? opts->theme : NULL;
    ctx.detail = opts ? opts->detail : NULL;
    ctx.text = opts ? opts->text : NULL;
    ctx.echo = opts ? opts->echo : 0;
    ctx.offer_save = opts ? opts->offer_save : 0;
    ctx.edit_max = (out_size > 0 && out_size - 1 < (int)sizeof(ctx.buf) - 1)
                       ? out_size - 1 : (int)sizeof(ctx.buf) - 1;

    /* Client size from the grid (see pw_layout). */
    PwLayout L;
    pw_layout(ctx.dpi, ctx.detail != NULL, ctx.text != NULL, ctx.offer_save, &L);
    RECT crc = { 0, 0, 2 * L.pad + L.fw, L.client_h };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD exstyle = WS_EX_DLGMODALFRAME | WS_EX_TOPMOST;
    pw_adjust_rect(&crc, style, exstyle, ctx.dpi);

    HWND hwnd = CreateWindowExA(
        exstyle,
        "NutshellPassDlg", (opts && opts->title) ? opts->title : "",
        style,
        0, 0, crc.right - crc.left, crc.bottom - crc.top,
        parent, NULL, inst, &ctx);
    if (!hwnd) {
        secure_zero(ctx.buf, sizeof(ctx.buf));
        return 0;
    }

    if (parent) {
        RECT pr, wr;
        GetWindowRect(parent, &pr);
        GetWindowRect(hwnd, &wr);
        int w = wr.right  - wr.left;
        int h = wr.bottom - wr.top;
        int x = pr.left + (pr.right  - pr.left - w) / 2;
        int y = pr.top  + (pr.bottom - pr.top  - h) / 2;
        SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    /* was_disabled: re-enable `parent` only if we're the one who disabled
     * it. Skipping that check used to always re-enable it, which is wrong
     * whenever this prompt is showing nested inside some other modal that
     * had already disabled `parent` itself (another tab's own prompt, a
     * host-key MessageBox, the Settings window) -- this prompt closing
     * would then prematurely re-enable the main window out from under
     * whichever outer modal is still on screen. */
    BOOL was_disabled = FALSE;
    if (parent) was_disabled = EnableWindow(parent, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    /* IsWindow(hwnd), not WM_QUIT, ends this loop -- see pw_prompt_proc's
     * WM_DESTROY comment. GetMessageA still pumps the whole thread's
     * queue (so other windows -- Settings, another tab's own prompt --
     * keep receiving and, via the fallback TranslateMessage/DispatchMessage
     * below, keep handling their own messages exactly as ui_run()'s main
     * loop would), but if it ever does return 0 -- a genuine WM_QUIT,
     * meaning the process is actually exiting -- that message must not be
     * swallowed here: repost it for ui_run()'s own loop to see, and treat
     * the prompt itself as cancelled. */
    MSG m;
    int gm = 0;
    while (IsWindow(hwnd) && (gm = GetMessageA(&m, NULL, 0, 0)) > 0) {
        if (!IsDialogMessageA(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    if (IsWindow(hwnd)) {
        /* The loop exited without the dialog destroying itself (OK/Cancel
         * already would have via pw_prompt_proc's WM_COMMAND, which also
         * clears the edit control's own text -- see its WM_DESTROY). Two
         * ways that happens: gm == 0 is a real WM_QUIT (the process is
         * actually exiting) and must be forwarded so ui_run()'s own loop
         * still sees it; gm == -1 is a GetMessageA error (e.g. some
         * unrelated invalid window handle on this thread) with no real
         * quit involved and `m` not meaningfully populated -- reposting
         * m.wParam in that case would forward garbage as an exit code.
         * Either way the prompt itself is cancelled (ctx.ok stays at its
         * default, 0) and the window must go -- DestroyWindow() also
         * clears its own edit text via WM_DESTROY, so no zombie dialog is
         * left with a GWLP_USERDATA pointing at this returning frame. */
        DestroyWindow(hwnd);
        if (gm == 0)
            PostQuitMessage((int)m.wParam);
    }

    if (parent && !was_disabled) EnableWindow(parent, TRUE);

    if (ctx.ok && out && out_size > 0) {
        strncpy(out, ctx.buf, (size_t)(out_size - 1));
        out[out_size - 1] = '\0';
        if (save_out) *save_out = ctx.save;
        secure_zero(ctx.buf, sizeof(ctx.buf));
        return 1;
    }
    /* M-5: zero the secret buffer on cancellation too */
    secure_zero(ctx.buf, sizeof(ctx.buf));
    return 0;
}
