/* src/ui/pw_reveal.c -- hold-to-reveal eye button for ES_PASSWORD edits.
 *
 * The eye is a child window of the edit, drawn inside its right edge. All
 * press/leave/capture decisions come from the pure state machine in
 * src/core/pw_reveal_state.c; this file only maps window messages to its
 * events and applies the result with EM_SETPASSWORDCHAR. */

#include <windows.h>
#include <commctrl.h>
#include <stdlib.h>
#include <string.h>
#include "pw_reveal.h"
#include "pw_reveal_state.h"
#include "ns_hover.h"
#include "ns_scale.h"
#include "ns_type.h"
#include "icons.h"
#include "themed_button.h"

#ifndef WM_DPICHANGED_AFTERPARENT
#define WM_DPICHANGED_AFTERPARENT 0x02E3
#endif

#define PW_EYE_CLASS "NutshellPwEye"
#define PW_REVEAL_SUBCLASS_ID 0x5057u
#define PW_EYE_HIT 0

typedef struct {
    PwRevealState st;
    NsHover       hover;
    WCHAR         mask;
    HWND          edit;
    HWND          eye;
    HWND          tip;
    int           applying; /* inside apply_reveal: ignore our own messages */
    int           checking; /* inside the emptied check: no recursion */
} PwReveal;

static PwReveal *state_of_eye(HWND eye)
{
    return (PwReveal *)GetWindowLongPtrA(eye, GWLP_USERDATA);
}

static void apply_reveal(PwReveal *pr)
{
    pr->applying = 1;
    SendMessageW(pr->edit, EM_SETPASSWORDCHAR,
                 pr->st.revealed ? 0 : (WPARAM)pr->mask, 0);
    InvalidateRect(pr->edit, NULL, TRUE);
    pr->applying = 0;
    if (pr->eye) InvalidateRect(pr->eye, NULL, FALSE);
}

/* Feed one event; apply the new mask state when `revealed` changed.
 * Returns 1 when it changed. */
static int feed(PwReveal *pr, PwRevealEvent ev)
{
    int has_text = GetWindowTextLengthW(pr->edit) > 0;
    int changed = pw_reveal_feed(&pr->st, ev, has_text);
    if (changed && ev != PWR_DESTROY) apply_reveal(pr);
    return changed;
}

static int eye_width(HWND edit)
{
    return ns_scale(SZ_ICON + SP_SM, get_window_dpi(edit));
}

static void layout(PwReveal *pr)
{
    RECT cr;
    int w = eye_width(pr->edit);
    GetClientRect(pr->edit, &cr);
    SetWindowPos(pr->eye, HWND_TOP, cr.right - w, 0, w, cr.bottom,
                 SWP_NOACTIVATE);
    SendMessageW(pr->edit, EM_SETMARGINS, EC_RIGHTMARGIN,
                 MAKELPARAM(0, (WORD)w));
}

static LRESULT CALLBACK edit_proc(HWND hwnd, UINT m, WPARAM wp, LPARAM lp,
                                  UINT_PTR id, DWORD_PTR ref)
{
    PwReveal *pr = (PwReveal *)ref;

    if ((m == WM_COPY || m == WM_CUT) && pr->st.revealed)
        return 0;

    LRESULT r = DefSubclassProc(hwnd, m, wp, lp);

    if (m == WM_NCDESTROY) {
        pw_reveal_feed(&pr->st, PWR_DESTROY, 0);
        RemoveWindowSubclass(hwnd, edit_proc, id);
        if (pr->tip && IsWindow(pr->tip)) DestroyWindow(pr->tip);
        free(pr);
        return r;
    }
    if (m == WM_SIZE || m == WM_DPICHANGED_AFTERPARENT)
        layout(pr);
    /* Any message may have emptied the field (Alt+Backspace undo, IME,
     * drops, ...): check after all of them. `checking` stops the length
     * query re-entering this check through WM_GETTEXTLENGTH. */
    if (pr->st.revealed && !pr->applying && !pr->checking) {
        pr->checking = 1;
        if (GetWindowTextLengthW(hwnd) == 0) feed(pr, PWR_EMPTIED);
        pr->checking = 0;
    }
    return r;
}

static int point_inside(HWND eye, LPARAM lp)
{
    RECT rc;
    POINT pt;
    pt.x = (short)LOWORD(lp);
    pt.y = (short)HIWORD(lp);
    GetClientRect(eye, &rc);
    return PtInRect(&rc, pt);
}

static void paint_eye(HWND eye, PwReveal *pr)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(eye, &ps);
    RECT rc;
    GetClientRect(eye, &rc);
    int dpi = get_window_dpi(eye);
    const ThemeTokens *t = ns_tokens();
    int visual = pr ? ns_hover_state_for(&pr->hover, PW_EYE_HIT) : 0;
    unsigned int bg = visual == 2 ? t->bg_secondary.pressed
                    : visual == 1 ? t->bg_secondary.hover
                                  : t->bg_secondary.base;
    HBRUSH br = CreateSolidBrush(theme_cr(bg));
    FillRect(hdc, &rc, br);
    DeleteObject(br);

    int isz = ns_scale(SZ_ICON, dpi);
    RECT ir;
    ir.left   = rc.left + ((rc.right - rc.left) - isz) / 2;
    ir.top    = rc.top + ((rc.bottom - rc.top) - isz) / 2;
    ir.right  = ir.left + isz;
    ir.bottom = ir.top + isz;
    ns_icon_draw(hdc, NS_ICON_EYE, &ir,
                 theme_cr(visual ? t->text_main : t->text_dim), (UINT)dpi);
    EndPaint(eye, &ps);
}

static void invalidate_if(HWND eye, NsHoverChange c)
{
    if (c.changed) InvalidateRect(eye, NULL, FALSE);
}

static LRESULT CALLBACK eye_proc(HWND hwnd, UINT m, WPARAM wp, LPARAM lp)
{
    PwReveal *pr = state_of_eye(hwnd);

    switch (m) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint_eye(hwnd, pr);
        return 0;
    case WM_LBUTTONDOWN:
        if (!pr) return 0;
        SetCapture(hwnd);
        invalidate_if(hwnd, ns_hover_press(&pr->hover, PW_EYE_HIT));
        feed(pr, PWR_PRESS);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        if (!pr) return 0;
        TRACKMOUSEEVENT tme;
        memset(&tme, 0, sizeof tme);
        tme.cbSize = sizeof tme;
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        int inside = point_inside(hwnd, lp);
        invalidate_if(hwnd, ns_hover_move(&pr->hover, inside ? PW_EYE_HIT : -1));
        if (pr->st.pressed) {
            if (!inside && pr->st.inside) feed(pr, PWR_LEAVE);
            else if (inside && !pr->st.inside) feed(pr, PWR_ENTER);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (pr) invalidate_if(hwnd, ns_hover_leave(&pr->hover));
        return 0;
    case WM_LBUTTONUP:
        if (!pr) return 0;
        if (!point_inside(hwnd, lp) && pr->st.inside) feed(pr, PWR_LEAVE);
        feed(pr, PWR_RELEASE);
        invalidate_if(hwnd, ns_hover_release(&pr->hover));
        if (GetCapture() == hwnd) ReleaseCapture();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_CAPTURECHANGED:
        if (pr) {
            feed(pr, PWR_CAPTURE_LOST);
            invalidate_if(hwnd, ns_hover_release(&pr->hover));
        }
        return 0;
    case WM_CANCELMODE:
        if (pr) feed(pr, PWR_DEACTIVATE);
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
        break;
    default:
        break;
    }
    return DefWindowProcA(hwnd, m, wp, lp);
}

static int register_class(void)
{
    static int done = 0;
    if (done) return 1;
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = eye_proc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = PW_EYE_CLASS;
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 0;
    done = 1;
    return 1;
}

static HWND make_tip(HWND edit, HWND eye)
{
    HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
                               WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               edit, NULL, GetModuleHandle(NULL), NULL);
    if (!tip) return NULL;
    static WCHAR text[] = L"Hold to show password";
    TTTOOLINFOW ti;
    memset(&ti, 0, sizeof ti);
    ti.cbSize   = sizeof ti;
    ti.uFlags   = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd     = edit;
    ti.uId      = (UINT_PTR)eye;
    ti.lpszText = text;
    SendMessageW(tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    return tip;
}

HWND pw_reveal_attach(HWND edit)
{
    if (!edit || !IsWindow(edit)) return NULL;

    DWORD_PTR existing = 0;
    if (GetWindowSubclass(edit, edit_proc, PW_REVEAL_SUBCLASS_ID, &existing))
        return ((PwReveal *)existing)->eye;

    if (!register_class()) return NULL;
    PwReveal *pr = (PwReveal *)calloc(1, sizeof *pr);
    if (!pr) return NULL;
    pw_reveal_init(&pr->st);
    ns_hover_init(&pr->hover);
    pr->edit = edit;
    pr->mask = (WCHAR)SendMessageW(edit, EM_GETPASSWORDCHAR, 0, 0);
    if (pr->mask == 0) pr->mask = L'*';

    LONG_PTR style = GetWindowLongPtrA(edit, GWL_STYLE);
    SetWindowLongPtrA(edit, GWL_STYLE, style | WS_CLIPCHILDREN);

    pr->eye = CreateWindowExA(0, PW_EYE_CLASS, "", WS_CHILD | WS_VISIBLE,
                              0, 0, 1, 1, edit, NULL,
                              GetModuleHandle(NULL), NULL);
    if (!pr->eye) { free(pr); return NULL; }
    SetWindowLongPtrA(pr->eye, GWLP_USERDATA, (LONG_PTR)pr);

    if (!SetWindowSubclass(edit, edit_proc, PW_REVEAL_SUBCLASS_ID,
                           (DWORD_PTR)pr)) {
        DestroyWindow(pr->eye);
        free(pr);
        return NULL;
    }
    layout(pr);
    pr->tip = make_tip(edit, pr->eye);
    return pr->eye;
}

static BOOL CALLBACK mask_enum(HWND w, LPARAM lp)
{
    char cls[32];
    (void)lp;
    if (GetClassNameA(w, cls, (int)sizeof cls) && strcmp(cls, PW_EYE_CLASS) == 0) {
        PwReveal *pr = state_of_eye(w);
        if (pr) {
            feed(pr, PWR_DEACTIVATE);
            if (GetCapture() == w) ReleaseCapture();
            ns_hover_release(&pr->hover);
            InvalidateRect(w, NULL, FALSE);
        }
    }
    return TRUE;
}

void pw_reveal_mask_all(HWND root)
{
    if (!root) return;
    EnumChildWindows(root, mask_enum, 0);
}

int pw_reveal_is_revealed(HWND edit)
{
    DWORD_PTR ref = 0;
    if (!edit) return 0;
    if (!GetWindowSubclass(edit, edit_proc, PW_REVEAL_SUBCLASS_ID, &ref))
        return 0;
    return ((PwReveal *)ref)->st.revealed ? 1 : 0;
}
