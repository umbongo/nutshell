/* src/ui/chat_listview.c — Owner-drawn chat message list with virtual scroll.
 *
 * Registers a custom window class "NutshellChatList" that paints ChatMsgItem
 * entries using GDI.  Scroll state is maintained internally; the parent only
 * needs to call chat_listview_invalidate() when the underlying list changes.
 */

#ifdef _WIN32

#include "chat_listview.h"
#include "resource.h"
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

/* ── Command button layout constants (96 DPI) ───────────────────────── */

#define BASE_BTN_W        70   /* Allow/Deny button width */
#define BASE_BTN_H        24   /* Allow/Deny button height */
#define BASE_BTN_GAP       8   /* Gap between buttons */
#define BASE_TAG_PAD_H     6   /* Safety tag horizontal padding */
#define BASE_TAG_PAD_V     2   /* Safety tag vertical padding */
#define BASE_TAG_H        18   /* Safety tag height */
#define BASE_ALLOW_ALL_H  28   /* "Allow All" ghost button height */

/* ── Safety tag colours (hardcoded — not theme-dependent) ───────────── */

#define CLR_TAG_SAFE     RGB(140, 140, 140)  /* grey */
#define CLR_TAG_WRITE    RGB(220, 140, 30)   /* orange */
#define CLR_TAG_CRITICAL RGB(210, 50, 50)    /* red */

/* ── Button colours ─────────────────────────────────────────────────── */

#define CLR_BTN_ALLOW     RGB(0, 160, 80)
#define CLR_BTN_ALLOW_P   RGB(0, 120, 60)     /* pressed */
#define CLR_BTN_DENY      RGB(200, 50, 50)
#define CLR_BTN_DENY_P    RGB(160, 30, 30)     /* pressed */
#define CLR_BTN_TEXT      RGB(255, 255, 255)
#define CLR_GHOST_BORDER  RGB(100, 160, 120)   /* Allow All outline */
#define CLR_GHOST_TEXT    RGB(100, 160, 120)
#define CLR_LINK_TEXT     RGB(100, 140, 180)   /* auto-approve link */
#define CLR_BLOCKED_TEXT  RGB(128, 128, 128)   /* greyed-out command text */

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
                                   ChatMsgItem *item, RECT *rc,
                                   int cmd_index, int is_first_cmd,
                                   int is_last_cmd, int total_cmds);
static void     paint_status_item(ChatListView *lv, HDC hdc,
                                  ChatMsgItem *item, RECT *rc);

/* ── Helpers: count pending commands, compute command index ──────────── */

static void count_commands(const ChatMsgList *list, int *total, int *pending)
{
    *total = 0;
    *pending = 0;
    if (!list) return;
    ChatMsgItem *item = list->head;
    while (item) {
        if (item->type == CHAT_ITEM_COMMAND) {
            (*total)++;
            if (item->u.cmd.approved == -1 && !item->u.cmd.blocked)
                (*pending)++;
        }
        item = item->next;
    }
}

/* command_index_of: reserved for future use (e.g., tooltip lookup) */

/* ── Is this the first command item in the list? ────────────────────── */

static int is_first_command(const ChatMsgList *list, const ChatMsgItem *target)
{
    ChatMsgItem *item = list->head;
    while (item) {
        if (item->type == CHAT_ITEM_COMMAND)
            return (item == target) ? 1 : 0;
        item = item->next;
    }
    return 0;
}

static int is_last_command(const ChatMsgList *list, const ChatMsgItem *target)
{
    (void)list;
    /* Walk from target forward: if no more COMMAND items, it's the last */
    const ChatMsgItem *item = target->next;
    while (item) {
        if (item->type == CHAT_ITEM_COMMAND) return 0;
        item = item->next;
    }
    return (target->type == CHAT_ITEM_COMMAND) ? 1 : 0;
}

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

/* ── Draw outlined (ghost) rounded rect ─────────────────────────────── */

static void draw_ghost_rect(HDC hdc, const RECT *rc, int radius,
                             COLORREF border_clr)
{
    HPEN pen = CreatePen(PS_SOLID, 1, border_clr);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    HGDIOBJ old_br  = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius, radius);
    SelectObject(hdc, old_br);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}

/* ── Safety tag colour ──────────────────────────────────────────────── */

static COLORREF safety_tag_color(CmdSafetyLevel level)
{
    switch (level) {
    case CMD_WRITE:    return CLR_TAG_WRITE;
    case CMD_CRITICAL: return CLR_TAG_CRITICAL;
    default:           return CLR_TAG_SAFE;
    }
}

static const char *safety_tag_text(CmdSafetyLevel level)
{
    switch (level) {
    case CMD_WRITE:    return "WRITE";
    case CMD_CRITICAL: return "CRITICAL";
    default:           return "SAFE";
    }
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
        /* Command block: monospace, padded, with safety tag + buttons */
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

        int h = (rc.bottom - rc.top) + 2 * lv->code_pad;

        /* Safety tag row at top */
        h += CLV_SCALE(lv, BASE_TAG_H) + CLV_SCALE(lv, 4);

        /* Button/status row at bottom */
        h += CLV_SCALE(lv, BASE_BTN_H) + CLV_SCALE(lv, 4);

        /* If blocked, add help text row */
        if (item->u.cmd.blocked)
            h += CLV_SCALE(lv, 16);

        /* "Allow All" ghost button above first command when multiple exist */
        int total_cmds, pending_cmds;
        count_commands(lv->msg_list, &total_cmds, &pending_cmds);
        if (total_cmds > 1 && is_first_command(lv->msg_list, item))
            h += CLV_SCALE(lv, BASE_ALLOW_ALL_H) + CLV_SCALE(lv, 4);

        /* "Allow all commands this session" link below last command */
        if (total_cmds > 1 && is_last_command(lv->msg_list, item)
            && item->u.cmd.approved == -1)
            h += CLV_SCALE(lv, 18);

        return h;
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

/* ── Paint command item with safety tag, buttons, blocked state ────── */

static void paint_command_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                               RECT *rc, int cmd_index, int is_first_cmd,
                               int is_last_cmd, int total_cmds)
{
    (void)cmd_index;  /* index used only for hit testing, not painting */

    const ThemeChatColors *tc = &lv->theme->chat;
    int side_pad = CLV_SCALE(lv, BASE_SIDE_PAD);
    int border_w = CLV_SCALE(lv, 1);
    int corner   = CLV_SCALE(lv, BASE_CORNER_R);
    int btn_w    = CLV_SCALE(lv, BASE_BTN_W);
    int btn_h    = CLV_SCALE(lv, BASE_BTN_H);
    int btn_gap  = CLV_SCALE(lv, BASE_BTN_GAP);
    int tag_h    = CLV_SCALE(lv, BASE_TAG_H);
    int tag_pad  = CLV_SCALE(lv, BASE_TAG_PAD_H);

    int cur_top = rc->top;

    /* ── "Allow All (N commands)" ghost button above first command ─── */
    if (is_first_cmd && total_cmds > 1) {
        int aa_h = CLV_SCALE(lv, BASE_ALLOW_ALL_H);
        int aa_w = CLV_SCALE(lv, 180);
        RECT aa_rc;
        aa_rc.left   = rc->left + side_pad;
        aa_rc.top    = cur_top;
        aa_rc.right  = aa_rc.left + aa_w;
        aa_rc.bottom = cur_top + aa_h;

        draw_ghost_rect(hdc, &aa_rc, corner, CLR_GHOST_BORDER);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, CLR_GHOST_TEXT);
        HGDIOBJ old_font = SelectObject(hdc, lv->hSmallFont
                                        ? lv->hSmallFont
                                        : GetStockObject(DEFAULT_GUI_FONT));
        char aa_text[64];
        snprintf(aa_text, sizeof(aa_text), "Allow All (%d commands)",
                 total_cmds);
        DrawTextA(hdc, aa_text, -1, &aa_rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, old_font);

        cur_top += aa_h + CLV_SCALE(lv, 4);
    }

    /* ── Command block background with border ─────────────────────── */
    RECT block;
    block.left   = rc->left + side_pad;
    block.top    = cur_top;
    block.right  = rc->right - side_pad;
    block.bottom = rc->bottom;

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

    /* ── Safety tag (top-right corner) ────────────────────────────── */
    COLORREF tag_clr = safety_tag_color(item->u.cmd.safety);
    const char *tag_text = safety_tag_text(item->u.cmd.safety);

    SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ old_font = SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                        : GetStockObject(DEFAULT_GUI_FONT));

    /* Measure tag text width */
    SIZE tag_sz;
    GetTextExtentPoint32A(hdc, tag_text, (int)strlen(tag_text), &tag_sz);
    int tag_w = tag_sz.cx + 2 * tag_pad;

    RECT tag_rc;
    tag_rc.right  = block.right - lv->code_pad;
    tag_rc.left   = tag_rc.right - tag_w;
    tag_rc.top    = block.top + CLV_SCALE(lv, 3);
    tag_rc.bottom = tag_rc.top + tag_h;

    fill_rounded_rect(hdc, &tag_rc, CLV_SCALE(lv, 3), tag_clr);
    SetTextColor(hdc, CLR_BTN_TEXT);
    DrawTextA(hdc, tag_text, -1, &tag_rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    int content_top = tag_rc.bottom + CLV_SCALE(lv, 4);

    /* ── Command text (monospace) ─────────────────────────────────── */
    const char *cmd_text = item->u.cmd.command ? item->u.cmd.command
                                               : item->text;

    if (item->u.cmd.blocked) {
        SetTextColor(hdc, CLR_BLOCKED_TEXT);
    } else {
        SetTextColor(hdc, RGB_FROM_THEME(tc->cmd_text));
    }
    HGDIOBJ mono_font = lv->hMonoFont ? lv->hMonoFont
                                       : GetStockObject(ANSI_FIXED_FONT);
    SelectObject(hdc, mono_font);
    RECT text_rc;
    text_rc.left   = block.left + lv->code_pad;
    text_rc.top    = content_top;
    text_rc.right  = block.right - lv->code_pad;
    text_rc.bottom = block.bottom - CLV_SCALE(lv, BASE_BTN_H) -
                     CLV_SCALE(lv, 8);
    if (item->u.cmd.blocked)
        text_rc.bottom -= CLV_SCALE(lv, 16);
    draw_text_utf8(hdc, cmd_text, &text_rc,
                   DT_WORDBREAK | DT_NOPREFIX);

    int status_top = text_rc.bottom + CLV_SCALE(lv, 4);

    /* ── Blocked state: lock icon + help text ─────────────────────── */
    if (item->u.cmd.blocked) {
        /* Lock icon via Fluent UI icon font (U+E72E = Lock) */
        if (lv->hIconFont) {
            SelectObject(hdc, lv->hIconFont);
            SetTextColor(hdc, CLR_BLOCKED_TEXT);
            RECT lock_rc;
            lock_rc.left   = block.left + lv->code_pad;
            lock_rc.top    = status_top;
            lock_rc.right  = lock_rc.left + CLV_SCALE(lv, 20);
            lock_rc.bottom = status_top + CLV_SCALE(lv, 16);
            DrawTextW(hdc, L"\xE72E", 1, &lock_rc,
                      DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }

        SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                         : GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(hdc, CLR_BLOCKED_TEXT);
        RECT help_rc;
        help_rc.left   = block.left + lv->code_pad + CLV_SCALE(lv, 22);
        help_rc.top    = status_top;
        help_rc.right  = block.right - lv->code_pad;
        help_rc.bottom = status_top + CLV_SCALE(lv, 16);
        DrawTextA(hdc, "Enable \"Permit Write\" to approve", -1, &help_rc,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        status_top += CLV_SCALE(lv, 16);
    }

    /* ── Button row / Status text ─────────────────────────────────── */
    if (!item->u.cmd.blocked && item->u.cmd.approved == -1) {
        /* PENDING: Draw Allow + Deny buttons */
        RECT allow_rc;
        allow_rc.left   = block.left + lv->code_pad;
        allow_rc.top    = status_top;
        allow_rc.right  = allow_rc.left + btn_w;
        allow_rc.bottom = allow_rc.top + btn_h;
        fill_rounded_rect(hdc, &allow_rc, corner, CLR_BTN_ALLOW);
        SetTextColor(hdc, CLR_BTN_TEXT);
        SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                         : GetStockObject(DEFAULT_GUI_FONT));
        DrawTextA(hdc, "Allow", -1, &allow_rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        RECT deny_rc;
        deny_rc.left   = allow_rc.right + btn_gap;
        deny_rc.top    = status_top;
        deny_rc.right  = deny_rc.left + btn_w;
        deny_rc.bottom = deny_rc.top + btn_h;
        fill_rounded_rect(hdc, &deny_rc, corner, CLR_BTN_DENY);
        SetTextColor(hdc, CLR_BTN_TEXT);
        DrawTextA(hdc, "Deny", -1, &deny_rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else {
        /* Show status text */
        const char *status;
        COLORREF status_color;
        if (item->u.cmd.blocked) {
            status = "Blocked";
            status_color = RGB_FROM_THEME(tc->indicator_red);
        } else if (item->u.cmd.approved == 1) {
            status = "Approved";
            status_color = RGB_FROM_THEME(tc->indicator_green);
        } else if (item->u.cmd.approved == 0) {
            status = "Denied";
            status_color = RGB_FROM_THEME(tc->indicator_red);
        } else {
            status = "Pending";
            status_color = RGB_FROM_THEME(tc->indicator_yellow);
        }

        RECT sts_rc;
        sts_rc.left   = block.left + lv->code_pad;
        sts_rc.top    = status_top;
        sts_rc.right  = block.right - lv->code_pad;
        sts_rc.bottom = status_top + btn_h;

        SetTextColor(hdc, status_color);
        SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                         : GetStockObject(DEFAULT_GUI_FONT));
        DrawTextA(hdc, status, -1, &sts_rc,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }

    /* "Allow all commands this session" link after last command */
    if (is_last_cmd && total_cmds > 1 && item->u.cmd.approved == -1) {
        int link_y = rc->bottom - CLV_SCALE(lv, 16);
        RECT link_rc;
        link_rc.left   = rc->left + side_pad;
        link_rc.top    = link_y;
        link_rc.right  = rc->right - side_pad;
        link_rc.bottom = link_y + CLV_SCALE(lv, 14);
        SetTextColor(hdc, RGB_FROM_THEME(lv->theme->accent));
        SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                         : GetStockObject(DEFAULT_GUI_FONT));
        DrawTextA(hdc, "Allow all commands this session", -1, &link_rc,
                  DT_SINGLELINE | DT_CENTER | DT_NOPREFIX);
    }

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

    /* Pre-count commands for "Allow All" rendering */
    int total_cmds, pending_cmds;
    count_commands(lv->msg_list, &total_cmds, &pending_cmds);

    /* Walk items, skip those above viewport, stop after those below */
    int y = lv->msg_gap - lv->scroll_y;
    int side_pad = CLV_SCALE(lv, BASE_SIDE_PAD);
    ChatMsgItem *item = lv->msg_list ? lv->msg_list->head : NULL;
    int cmd_idx = 0;

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
                paint_command_item(lv, mem_dc, item, &item_rc,
                                   cmd_idx,
                                   is_first_command(lv->msg_list, item),
                                   is_last_command(lv->msg_list, item),
                                   total_cmds);
                cmd_idx++;
                break;
            case CHAT_ITEM_STATUS:
                paint_status_item(lv, mem_dc, item, &item_rc);
                break;
            }
        }

        if (item->type == CHAT_ITEM_COMMAND && !(y + h > 0 && y < ch))
            cmd_idx++;

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
 *  Hit testing: convert mouse click to button action
 * ══════════════════════════════════════════════════════════════════════ */

static void on_lbuttondown(ChatListView *lv, int mx, int my)
{
    int side_pad = CLV_SCALE(lv, BASE_SIDE_PAD);
    int btn_w    = CLV_SCALE(lv, BASE_BTN_W);
    int btn_h    = CLV_SCALE(lv, BASE_BTN_H);
    int btn_gap  = CLV_SCALE(lv, BASE_BTN_GAP);
    int code_pad = lv->code_pad;

    int total_cmds, pending_cmds;
    count_commands(lv->msg_list, &total_cmds, &pending_cmds);

    /* Walk items to find which one was clicked */
    int y = lv->msg_gap - lv->scroll_y;
    ChatMsgItem *item = lv->msg_list ? lv->msg_list->head : NULL;
    int cmd_idx = 0;
    HWND parent = GetParent(lv->hwnd);

    while (item) {
        int h = item->measured_height;

        if (item->type == CHAT_ITEM_COMMAND && my >= y && my < y + h) {
            int cur_top = y;
            int first_cmd = is_first_command(lv->msg_list, item);

            /* Check "Allow All" ghost button above first command */
            if (first_cmd && total_cmds > 1) {
                int aa_h = CLV_SCALE(lv, BASE_ALLOW_ALL_H);
                int aa_w = CLV_SCALE(lv, 180);
                RECT aa_rc;
                aa_rc.left   = side_pad + side_pad;
                aa_rc.top    = cur_top;
                aa_rc.right  = aa_rc.left + aa_w;
                aa_rc.bottom = cur_top + aa_h;

                if (mx >= aa_rc.left && mx < aa_rc.right &&
                    my >= aa_rc.top && my < aa_rc.bottom) {
                    if (parent)
                        PostMessage(parent, WM_COMMAND,
                                    MAKEWPARAM(IDC_CMD_APPROVE_ALL, 0), 0);
                    return;
                }
                cur_top += aa_h + CLV_SCALE(lv, 4);
            }

            /* Block area: left + side_pad to right - side_pad */
            int block_left  = side_pad + side_pad;

            /* Skip over command text area to find button row */
            /* We need to estimate where the button row starts.
             * Use the measured_height minus the trailing content. */
            int status_top;
            if (item->u.cmd.blocked) {
                /* blocked: no buttons, but has help text */
                status_top = y + h - btn_h - CLV_SCALE(lv, 4) -
                             CLV_SCALE(lv, 16);
            } else {
                status_top = y + h - btn_h - CLV_SCALE(lv, 4);
            }

            /* Only test Allow/Deny if pending and not blocked */
            if (!item->u.cmd.blocked && item->u.cmd.approved == -1) {
                /* Allow button */
                RECT allow_rc;
                allow_rc.left   = block_left + code_pad;
                allow_rc.top    = status_top;
                allow_rc.right  = allow_rc.left + btn_w;
                allow_rc.bottom = allow_rc.top + btn_h;

                if (mx >= allow_rc.left && mx < allow_rc.right &&
                    my >= allow_rc.top && my < allow_rc.bottom) {
                    if (parent)
                        PostMessage(parent, WM_COMMAND,
                                    MAKEWPARAM(IDC_CMD_APPROVE_BASE + cmd_idx,
                                               0), 0);
                    return;
                }

                /* Deny button */
                RECT deny_rc;
                deny_rc.left   = allow_rc.right + btn_gap;
                deny_rc.top    = status_top;
                deny_rc.right  = deny_rc.left + btn_w;
                deny_rc.bottom = deny_rc.top + btn_h;

                if (mx >= deny_rc.left && mx < deny_rc.right &&
                    my >= deny_rc.top && my < deny_rc.bottom) {
                    if (parent)
                        PostMessage(parent, WM_COMMAND,
                                    MAKEWPARAM(IDC_CMD_DENY_BASE + cmd_idx,
                                               0), 0);
                    return;
                }
            }

            /* Check "Allow all commands this session" link */
            {
                int total_cmds2, pending_cmds2;
                count_commands(lv->msg_list, &total_cmds2, &pending_cmds2);
                if (total_cmds2 > 1 && is_last_command(lv->msg_list, item)
                    && item->u.cmd.approved == -1) {
                    int link_y = y + h - CLV_SCALE(lv, 16);
                    if (my >= link_y && my < y + h) {
                        if (parent)
                            PostMessage(parent, WM_COMMAND,
                                        MAKEWPARAM(IDC_AUTO_APPROVE, 0), 0);
                        return;
                    }
                }
            }

            return;  /* Click was in command block but not on a button */
        }

        if (item->type == CHAT_ITEM_COMMAND)
            cmd_idx++;

        y += h + lv->msg_gap;
        item = item->next;
    }
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

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        on_lbuttondown(lv, mx, my);
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
