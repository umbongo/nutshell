/* src/ui/chat_listview.c — Owner-drawn chat message list with virtual scroll.
 *
 * Registers a custom window class "NutshellChatList" that paints ChatMsgItem
 * entries using GDI.  Scroll state is maintained internally; the parent only
 * needs to call chat_listview_invalidate() when the underlying list changes.
 */

#ifdef _WIN32

#include "chat_listview.h"
#include <windowsx.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Window class name ──────────────────────────────────────────────── */

static const char *CHATLIST_CLASS = "NutshellChatList";

/* ── Colour helper: theme stores 0x00RRGGBB, GDI wants 0x00BBGGRR ── */

#define RGB_FROM_THEME(c) \
    RGB(((c) >> 16) & 0xFF, ((c) >> 8) & 0xFF, (c) & 0xFF)

/* ── DPI-aware pixel scaling ────────────────────────────────────────── */

#define CLV_SCALE(lv, px) ((int)((float)(px) * (lv)->dpi_scale + 0.5f))

/* ── Base layout constants (96 DPI) ─────────────────────────────────── */

#define BASE_MSG_GAP      12
#define BASE_USER_PAD_H   10
#define BASE_USER_PAD_V    8
#define BASE_AI_INDENT    30
#define BASE_CODE_PAD      6
#define BASE_BORDER_W      3   /* Left-border width for thinking blocks */
#define BASE_ICON_SIZE    20   /* AI avatar circle diameter */
#define BASE_CORNER_R      6   /* User bubble corner radius */
#define BASE_SIDE_PAD      8   /* Left/right margin for the whole panel */

/* ── Forward declarations ───────────────────────────────────────────── */

static LRESULT CALLBACK ChatListWndProc(HWND, UINT, WPARAM, LPARAM);
static void     recalc_layout(ChatListView *lv);
static void     update_scrollbar(ChatListView *lv);
static int      measure_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                             int width);
static void     paint_user_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                                RECT *rc);
static void     paint_ai_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                               RECT *rc);
static void     paint_command_item(ChatListView *lv, HDC hdc,
                                   ChatMsgItem *item, RECT *rc);
static void     paint_status_item(ChatListView *lv, HDC hdc,
                                  ChatMsgItem *item, RECT *rc);

/* ── UTF-8 → UTF-16 helper (caller must free returned buffer) ───── */

static wchar_t *utf8_to_wide(const char *utf8, int *out_len)
{
    if (!utf8 || !*utf8) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (len <= 0) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    wchar_t *buf = (wchar_t *)malloc((size_t)len * sizeof(wchar_t));
    if (!buf) {
        if (out_len) *out_len = 0;
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, buf, len);
    if (out_len) *out_len = len - 1;   /* exclude NUL */
    return buf;
}

/* ── Draw multiline text (UTF-8 source) and return height ────────── */

static int draw_text_utf8(HDC hdc, const char *text, RECT *rc, UINT flags)
{
    int wlen;
    wchar_t *w = utf8_to_wide(text, &wlen);
    if (!w) return 0;
    int h = DrawTextW(hdc, w, wlen, rc, flags);
    free(w);
    return h;
}

/* ── Rounded-rect helper ────────────────────────────────────────────── */

static void fill_rounded_rect(HDC hdc, const RECT *rc, int radius,
                               COLORREF fill)
{
    HBRUSH br = CreateSolidBrush(fill);
    HPEN   pen = CreatePen(PS_SOLID, 1, fill);
    HGDIOBJ old_br  = SelectObject(hdc, br);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius, radius);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_br);
    DeleteObject(pen);
    DeleteObject(br);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Registration
 * ══════════════════════════════════════════════════════════════════════ */

void chat_listview_register(HINSTANCE hInstance)
{
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = ChatListWndProc;
    wc.hInstance      = hInstance;
    wc.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground  = NULL;          /* We paint everything ourselves */
    wc.lpszClassName  = CHATLIST_CLASS;
    RegisterClassExA(&wc);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Creation
 * ══════════════════════════════════════════════════════════════════════ */

HWND chat_listview_create(HWND parent, int x, int y, int w, int h,
                          ChatMsgList *msg_list, const ThemeColors *theme)
{
    ChatListView *lv = (ChatListView *)calloc(1, sizeof(ChatListView));
    if (!lv) return NULL;

    lv->msg_list = msg_list;
    lv->theme    = theme;
    lv->dpi_scale = 1.0f;

    /* Compute scaled layout constants */
    lv->msg_gap    = CLV_SCALE(lv, BASE_MSG_GAP);
    lv->user_pad_h = CLV_SCALE(lv, BASE_USER_PAD_H);
    lv->user_pad_v = CLV_SCALE(lv, BASE_USER_PAD_V);
    lv->ai_indent  = CLV_SCALE(lv, BASE_AI_INDENT);
    lv->code_pad   = CLV_SCALE(lv, BASE_CODE_PAD);

    HWND hwnd = CreateWindowExA(
        0, CHATLIST_CLASS, NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
        x, y, w, h,
        parent, NULL, GetModuleHandle(NULL), lv);

    if (!hwnd) { free(lv); return NULL; }
    return hwnd;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Public API helpers
 * ══════════════════════════════════════════════════════════════════════ */

static ChatListView *lv_from_hwnd(HWND hwnd)
{
    return (ChatListView *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}

void chat_listview_set_fonts(HWND hwnd, HFONT font, HFONT mono,
                             HFONT bold, HFONT small_font, HFONT icon)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return;
    lv->hFont     = font;
    lv->hMonoFont = mono;
    lv->hBoldFont = bold;
    lv->hSmallFont = small_font;
    lv->hIconFont = icon;
    /* Fonts changed — need full remeasure */
    recalc_layout(lv);
    InvalidateRect(hwnd, NULL, TRUE);
}

void chat_listview_set_theme(HWND hwnd, const ThemeColors *theme)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return;
    lv->theme = theme;
    InvalidateRect(hwnd, NULL, TRUE);
}

void chat_listview_invalidate(HWND hwnd)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return;
    recalc_layout(lv);
    InvalidateRect(hwnd, NULL, TRUE);
}

void chat_listview_scroll_to_bottom(HWND hwnd)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return;
    int max_scroll = lv->total_height - lv->viewport_height;
    if (max_scroll < 0) max_scroll = 0;
    lv->scroll_y = max_scroll;
    update_scrollbar(lv);
    InvalidateRect(hwnd, NULL, TRUE);
}

void chat_listview_relayout(HWND hwnd)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    lv->viewport_height = rc.bottom - rc.top;
    recalc_layout(lv);
    InvalidateRect(hwnd, NULL, TRUE);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Layout: measure all items, compute total_height
 * ══════════════════════════════════════════════════════════════════════ */

static void recalc_layout(ChatListView *lv)
{
    if (!lv || !lv->hwnd) return;

    RECT rc;
    GetClientRect(lv->hwnd, &rc);
    int width = rc.right - rc.left;
    if (width <= 0) return;

    HDC hdc = GetDC(lv->hwnd);
    if (!hdc) return;

    int y = lv->msg_gap;  /* Start with a top margin */
    ChatMsgItem *item = lv->msg_list ? lv->msg_list->head : NULL;

    while (item) {
        int h = measure_item(lv, hdc, item, width);
        item->measured_height = h;
        item->dirty = 0;
        y += h + lv->msg_gap;
        item = item->next;
    }

    ReleaseDC(lv->hwnd, hdc);

    lv->total_height = y;
    lv->viewport_height = rc.bottom - rc.top;

    /* Clamp scroll */
    int max_scroll = lv->total_height - lv->viewport_height;
    if (max_scroll < 0) max_scroll = 0;
    if (lv->scroll_y > max_scroll) lv->scroll_y = max_scroll;
    if (lv->scroll_y < 0) lv->scroll_y = 0;

    update_scrollbar(lv);
}

/* ── Measure a single item ──────────────────────────────────────────── */

static int measure_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                        int width)
{
    int side_pad = CLV_SCALE(lv, BASE_SIDE_PAD);
    int text_w;
    RECT rc;
    HGDIOBJ old_font;

    if (!item->text || item->text_len == 0)
        return lv->msg_gap;

    switch (item->type) {
    case CHAT_ITEM_USER: {
        /* User bubble: right-aligned with padding */
        int max_bubble_w = (width * 3) / 4;  /* Max 75% of width */
        text_w = max_bubble_w - 2 * lv->user_pad_h;
        if (text_w < 40) text_w = 40;

        old_font = SelectObject(hdc, lv->hFont ? lv->hFont
                                                : GetStockObject(DEFAULT_GUI_FONT));
        SetRect(&rc, 0, 0, text_w, 0);
        draw_text_utf8(hdc, item->text, &rc,
                       DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(hdc, old_font);
        return (rc.bottom - rc.top) + 2 * lv->user_pad_v;
    }

    case CHAT_ITEM_AI_TEXT: {
        /* AI text: left-indented, full width minus indent */
        text_w = width - lv->ai_indent - side_pad;
        if (text_w < 40) text_w = 40;

        old_font = SelectObject(hdc, lv->hFont ? lv->hFont
                                                : GetStockObject(DEFAULT_GUI_FONT));
        SetRect(&rc, 0, 0, text_w, 0);
        draw_text_utf8(hdc, item->text, &rc,
                       DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(hdc, old_font);

        int h = rc.bottom - rc.top;

        /* If there's visible thinking text, add space for it */
        if (item->u.ai.thinking_text && !item->u.ai.thinking_collapsed) {
            int think_w = text_w - CLV_SCALE(lv, BASE_BORDER_W) -
                          CLV_SCALE(lv, 4);
            if (think_w < 20) think_w = 20;
            old_font = SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                        : GetStockObject(DEFAULT_GUI_FONT));
            SetRect(&rc, 0, 0, think_w, 0);
            draw_text_utf8(hdc, item->u.ai.thinking_text, &rc,
                           DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(hdc, old_font);
            h += (rc.bottom - rc.top) + CLV_SCALE(lv, 8);
        }

        return h + CLV_SCALE(lv, BASE_ICON_SIZE);  /* Icon row above text */
    }

    case CHAT_ITEM_COMMAND: {
        /* Command block: monospace, padded */
        const char *cmd_text = item->u.cmd.command ? item->u.cmd.command
                                                   : item->text;
        text_w = width - 2 * side_pad - 2 * lv->code_pad;
        if (text_w < 40) text_w = 40;

        old_font = SelectObject(hdc, lv->hMonoFont ? lv->hMonoFont
                                                    : GetStockObject(ANSI_FIXED_FONT));
        SetRect(&rc, 0, 0, text_w, 0);
        draw_text_utf8(hdc, cmd_text, &rc,
                       DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(hdc, old_font);
        return (rc.bottom - rc.top) + 2 * lv->code_pad +
               CLV_SCALE(lv, 24);  /* Extra for status row (approve/deny) */
    }

    case CHAT_ITEM_STATUS: {
        text_w = width - 2 * side_pad;
        if (text_w < 40) text_w = 40;

        old_font = SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                                    : GetStockObject(DEFAULT_GUI_FONT));
        SetRect(&rc, 0, 0, text_w, 0);
        draw_text_utf8(hdc, item->text, &rc,
                       DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_CENTER);
        SelectObject(hdc, old_font);
        return (rc.bottom - rc.top) + CLV_SCALE(lv, 8);
    }

    default:
        return lv->msg_gap;
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Scrollbar management
 * ══════════════════════════════════════════════════════════════════════ */

static void update_scrollbar(ChatListView *lv)
{
    SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = lv->total_height > 0 ? lv->total_height - 1 : 0;
    si.nPage  = (UINT)lv->viewport_height;
    si.nPos   = lv->scroll_y;
    SetScrollInfo(lv->hwnd, SB_VERT, &si, TRUE);
}

static void clamp_scroll(ChatListView *lv)
{
    int max_scroll = lv->total_height - lv->viewport_height;
    if (max_scroll < 0) max_scroll = 0;
    if (lv->scroll_y > max_scroll) lv->scroll_y = max_scroll;
    if (lv->scroll_y < 0) lv->scroll_y = 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Paint routines
 * ══════════════════════════════════════════════════════════════════════ */

static void paint_user_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                            RECT *rc)
{
    const ThemeChatColors *tc = &lv->theme->chat;
    int corner = CLV_SCALE(lv, BASE_CORNER_R);

    /* Right-align the bubble */
    int bubble_w = (rc->right - rc->left);
    int max_w = ((rc->right - rc->left + 2 * CLV_SCALE(lv, BASE_SIDE_PAD))
                 * 3) / 4;
    if (bubble_w > max_w) bubble_w = max_w;

    RECT bubble;
    bubble.right  = rc->right;
    bubble.left   = rc->right - bubble_w;
    bubble.top    = rc->top;
    bubble.bottom = rc->bottom;

    /* Draw bubble background */
    fill_rounded_rect(hdc, &bubble, corner, RGB_FROM_THEME(tc->user_bubble));

    /* Draw text */
    RECT text_rc = bubble;
    text_rc.left   += lv->user_pad_h;
    text_rc.right  -= lv->user_pad_h;
    text_rc.top    += lv->user_pad_v;
    text_rc.bottom -= lv->user_pad_v;

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB_FROM_THEME(tc->user_text));
    HGDIOBJ old_font = SelectObject(hdc, lv->hFont ? lv->hFont
                                        : GetStockObject(DEFAULT_GUI_FONT));
    draw_text_utf8(hdc, item->text, &text_rc,
                   DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdc, old_font);
}

static void paint_ai_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                           RECT *rc)
{
    const ThemeChatColors *tc = &lv->theme->chat;
    int icon_sz = CLV_SCALE(lv, BASE_ICON_SIZE);
    int side_pad = CLV_SCALE(lv, BASE_SIDE_PAD);

    /* AI avatar circle */
    HBRUSH avatar_br = CreateSolidBrush(RGB_FROM_THEME(tc->ai_accent));
    HGDIOBJ old_br = SelectObject(hdc, avatar_br);
    HPEN null_pen = CreatePen(PS_SOLID, 1, RGB_FROM_THEME(tc->ai_accent));
    HGDIOBJ old_pen = SelectObject(hdc, null_pen);
    Ellipse(hdc, rc->left, rc->top,
            rc->left + icon_sz, rc->top + icon_sz);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_br);
    DeleteObject(null_pen);
    DeleteObject(avatar_br);

    /* "AI" label next to avatar */
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB_FROM_THEME(tc->ai_accent));
    HGDIOBJ old_font = SelectObject(hdc, lv->hBoldFont ? lv->hBoldFont
                                        : GetStockObject(DEFAULT_GUI_FONT));
    RECT label_rc;
    label_rc.left   = rc->left + icon_sz + CLV_SCALE(lv, 6);
    label_rc.top    = rc->top;
    label_rc.right  = rc->right;
    label_rc.bottom = rc->top + icon_sz;
    DrawTextA(hdc, "AI", 2, &label_rc,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    /* Thinking region (if visible) */
    int content_top = rc->top + icon_sz + CLV_SCALE(lv, 4);

    if (item->u.ai.thinking_text && !item->u.ai.thinking_collapsed) {
        int border_w = CLV_SCALE(lv, BASE_BORDER_W);
        int think_left = rc->left + lv->ai_indent;
        int text_w = rc->right - think_left - side_pad;
        if (text_w < 20) text_w = 20;

        /* Left border bar */
        RECT bar_rc;
        bar_rc.left   = think_left;
        bar_rc.top    = content_top;
        bar_rc.right  = think_left + border_w;
        bar_rc.bottom = content_top + CLV_SCALE(lv, 40);  /* approximate */

        HBRUSH bar_br = CreateSolidBrush(RGB_FROM_THEME(tc->thinking_border));

        /* Measure thinking text height to set bar bottom */
        HGDIOBJ old_think_font = SelectObject(hdc, lv->hSmallFont
                                 ? lv->hSmallFont
                                 : GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(hdc, RGB_FROM_THEME(tc->thinking_text));
        RECT think_rc;
        think_rc.left = think_left + border_w + CLV_SCALE(lv, 4);
        think_rc.top  = content_top;
        think_rc.right = think_rc.left + text_w - border_w - CLV_SCALE(lv, 4);
        think_rc.bottom = rc->bottom;

        /* Measure first, then draw */
        RECT meas_rc = think_rc;
        draw_text_utf8(hdc, item->u.ai.thinking_text, &meas_rc,
                       DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        int think_h = meas_rc.bottom - meas_rc.top;

        bar_rc.bottom = content_top + think_h;
        FillRect(hdc, &bar_rc, bar_br);
        DeleteObject(bar_br);

        /* Draw thinking text */
        draw_text_utf8(hdc, item->u.ai.thinking_text, &think_rc,
                       DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(hdc, old_think_font);

        content_top += think_h + CLV_SCALE(lv, 8);
    }

    /* Main AI text content */
    SetTextColor(hdc, RGB_FROM_THEME(lv->theme->text_main));
    SelectObject(hdc, lv->hFont ? lv->hFont
                                : GetStockObject(DEFAULT_GUI_FONT));
    RECT text_rc;
    text_rc.left   = rc->left + lv->ai_indent;
    text_rc.top    = content_top;
    text_rc.right  = rc->right - side_pad;
    text_rc.bottom = rc->bottom;
    draw_text_utf8(hdc, item->text, &text_rc,
                   DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdc, old_font);
}

static void paint_command_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                               RECT *rc)
{
    const ThemeChatColors *tc = &lv->theme->chat;
    int side_pad = CLV_SCALE(lv, BASE_SIDE_PAD);
    int border_w = CLV_SCALE(lv, 1);

    /* Command block background with border */
    RECT block = *rc;
    block.left  += side_pad;
    block.right -= side_pad;

    HBRUSH bg_br = CreateSolidBrush(RGB_FROM_THEME(tc->cmd_bg));
    FillRect(hdc, &block, bg_br);
    DeleteObject(bg_br);

    /* Border */
    HPEN border_pen = CreatePen(PS_SOLID, border_w,
                                RGB_FROM_THEME(tc->cmd_border));
    HGDIOBJ old_pen = SelectObject(hdc, border_pen);
    HGDIOBJ old_br  = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, block.left, block.top, block.right, block.bottom);
    SelectObject(hdc, old_br);
    SelectObject(hdc, old_pen);
    DeleteObject(border_pen);

    /* Command text (monospace) */
    const char *cmd_text = item->u.cmd.command ? item->u.cmd.command
                                               : item->text;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB_FROM_THEME(tc->cmd_text));
    HGDIOBJ old_font = SelectObject(hdc, lv->hMonoFont ? lv->hMonoFont
                                        : GetStockObject(ANSI_FIXED_FONT));
    RECT text_rc;
    text_rc.left   = block.left + lv->code_pad;
    text_rc.top    = block.top + lv->code_pad;
    text_rc.right  = block.right - lv->code_pad;
    text_rc.bottom = block.bottom - CLV_SCALE(lv, 24);
    draw_text_utf8(hdc, cmd_text, &text_rc,
                   DT_WORDBREAK | DT_NOPREFIX);

    /* Status row: approval state */
    RECT status_rc;
    status_rc.left   = block.left + lv->code_pad;
    status_rc.top    = text_rc.bottom + CLV_SCALE(lv, 2);
    status_rc.right  = block.right - lv->code_pad;
    status_rc.bottom = block.bottom - CLV_SCALE(lv, 2);

    const char *status;
    COLORREF status_color;
    if (item->u.cmd.blocked) {
        status = "BLOCKED";
        status_color = RGB_FROM_THEME(tc->indicator_red);
    } else if (item->u.cmd.approved == 1) {
        status = "APPROVED";
        status_color = RGB_FROM_THEME(tc->indicator_green);
    } else if (item->u.cmd.approved == 0) {
        status = "DENIED";
        status_color = RGB_FROM_THEME(tc->indicator_red);
    } else {
        status = "PENDING";
        status_color = RGB_FROM_THEME(tc->indicator_yellow);
    }

    SetTextColor(hdc, status_color);
    SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                     : GetStockObject(DEFAULT_GUI_FONT));
    DrawTextA(hdc, status, -1, &status_rc,
              DT_SINGLELINE | DT_RIGHT | DT_VCENTER | DT_NOPREFIX);
    SelectObject(hdc, old_font);
}

static void paint_status_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                              RECT *rc)
{
    const ThemeChatColors *tc = &lv->theme->chat;

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB_FROM_THEME(tc->status_text));
    HGDIOBJ old_font = SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                        : GetStockObject(DEFAULT_GUI_FONT));
    RECT text_rc = *rc;
    text_rc.left  += CLV_SCALE(lv, BASE_SIDE_PAD);
    text_rc.right -= CLV_SCALE(lv, BASE_SIDE_PAD);
    draw_text_utf8(hdc, item->text, &text_rc,
                   DT_WORDBREAK | DT_NOPREFIX | DT_CENTER);
    SelectObject(hdc, old_font);
}

/* ══════════════════════════════════════════════════════════════════════
 *  WM_PAINT: double-buffered, virtual-scroll painting
 * ══════════════════════════════════════════════════════════════════════ */

static void on_paint(ChatListView *lv)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(lv->hwnd, &ps);

    RECT client;
    GetClientRect(lv->hwnd, &client);
    int cw = client.right - client.left;
    int ch = client.bottom - client.top;

    /* Double-buffer: paint to offscreen bitmap */
    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, cw, ch);
    HGDIOBJ old_bmp = SelectObject(mem_dc, bmp);

    /* Clear background */
    COLORREF bg = RGB_FROM_THEME(lv->theme->bg_primary);
    HBRUSH bg_br = CreateSolidBrush(bg);
    FillRect(mem_dc, &client, bg_br);
    DeleteObject(bg_br);

    /* Walk items, skip those above viewport, stop after those below */
    int y = lv->msg_gap - lv->scroll_y;
    int side_pad = CLV_SCALE(lv, BASE_SIDE_PAD);
    ChatMsgItem *item = lv->msg_list ? lv->msg_list->head : NULL;

    while (item) {
        int h = item->measured_height;

        /* Skip items entirely above viewport */
        if (y + h > 0 && y < ch) {
            RECT item_rc;
            item_rc.left   = side_pad;
            item_rc.top    = y;
            item_rc.right  = cw - side_pad;
            item_rc.bottom = y + h;

            switch (item->type) {
            case CHAT_ITEM_USER:
                paint_user_item(lv, mem_dc, item, &item_rc);
                break;
            case CHAT_ITEM_AI_TEXT:
                paint_ai_item(lv, mem_dc, item, &item_rc);
                break;
            case CHAT_ITEM_COMMAND:
                paint_command_item(lv, mem_dc, item, &item_rc);
                break;
            case CHAT_ITEM_STATUS:
                paint_status_item(lv, mem_dc, item, &item_rc);
                break;
            }
        }

        /* Stop if we've gone past the viewport */
        if (y > ch) break;

        y += h + lv->msg_gap;
        item = item->next;
    }

    /* Blit to screen */
    BitBlt(hdc, 0, 0, cw, ch, mem_dc, 0, 0, SRCCOPY);

    SelectObject(mem_dc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem_dc);

    EndPaint(lv->hwnd, &ps);
}

/* ══════════════════════════════════════════════════════════════════════
 *  WndProc
 * ══════════════════════════════════════════════════════════════════════ */

static LRESULT CALLBACK ChatListWndProc(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam)
{
    ChatListView *lv;

    if (msg == WM_CREATE) {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)lParam;
        lv = (ChatListView *)cs->lpCreateParams;
        lv->hwnd = hwnd;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)lv);

        RECT rc;
        GetClientRect(hwnd, &rc);
        lv->viewport_height = rc.bottom - rc.top;
        return 0;
    }

    lv = lv_from_hwnd(hwnd);
    if (!lv) return DefWindowProcA(hwnd, msg, wParam, lParam);

    switch (msg) {

    case WM_PAINT:
        on_paint(lv);
        return 0;

    case WM_ERASEBKGND:
        return 1;  /* We handle background in WM_PAINT (double-buffered) */

    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        lv->viewport_height = rc.bottom - rc.top;
        recalc_layout(lv);
        return 0;
    }

    case WM_VSCROLL: {
        int old_pos = lv->scroll_y;
        int line_h  = lv->msg_gap > 0 ? lv->msg_gap * 3 : 36;

        switch (LOWORD(wParam)) {
        case SB_LINEUP:        lv->scroll_y -= line_h;               break;
        case SB_LINEDOWN:      lv->scroll_y += line_h;               break;
        case SB_PAGEUP:        lv->scroll_y -= lv->viewport_height;  break;
        case SB_PAGEDOWN:      lv->scroll_y += lv->viewport_height;  break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si;
            memset(&si, 0, sizeof(si));
            si.cbSize = sizeof(si);
            si.fMask  = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_VERT, &si);
            lv->scroll_y = si.nTrackPos;
            break;
        }
        case SB_TOP:           lv->scroll_y = 0;                     break;
        case SB_BOTTOM: {
            int max_s = lv->total_height - lv->viewport_height;
            lv->scroll_y = max_s > 0 ? max_s : 0;
            break;
        }
        }

        clamp_scroll(lv);
        if (lv->scroll_y != old_pos) {
            update_scrollbar(lv);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int scroll_amount = CLV_SCALE(lv, 40);  /* ~3 lines per notch */
        int old_pos = lv->scroll_y;

        lv->scroll_y -= (delta * scroll_amount) / WHEEL_DELTA;
        clamp_scroll(lv);

        if (lv->scroll_y != old_pos) {
            update_scrollbar(lv);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_DESTROY:
        free(lv);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        return 0;

    default:
        break;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

#endif /* _WIN32 */
