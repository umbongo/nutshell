/* src/ui/chat_listview.c — Owner-drawn chat message list with virtual scroll.
 *
 * Registers a custom window class "NutshellChatList" that paints ChatMsgItem
 * entries using GDI.  Scroll state is maintained internally; the parent only
 * needs to call chat_listview_invalidate() when the underlying list changes.
 */

#ifdef _WIN32

#include "chat_listview.h"
#include "chat_activity.h"
#include "resource.h"
#include "custom_scrollbar.h"
#include "dpi_util.h"
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
#define BASE_THINK_MAX_H 400   /* Max height of expanded thinking region */
#define BASE_THINK_MIN_H  30   /* Min height of expanded thinking region */
#define BASE_THINK_HDR_H  20   /* Height of thinking toggle header line */
#define BASE_ICON_SIZE    20   /* AI avatar circle diameter */
#define BASE_CORNER_R      6   /* User bubble corner radius */
#define BASE_SIDE_PAD      8   /* Left/right margin for the whole panel */

/* ── Command button layout constants (96 DPI) ───────────────────────── */

#define BASE_BTN_W        76   /* Allow/Deny button width */
#define BASE_BTN_H        28   /* Allow/Deny button height */
#define BASE_BTN_GAP       8   /* Gap between buttons */
#define BASE_TAG_PAD_H     6   /* Safety tag horizontal padding */
#define BASE_TAG_PAD_V     2   /* Safety tag vertical padding */
#define BASE_TAG_H        18   /* Safety tag height */
#define BASE_ALLOW_ALL_H  28   /* "Allow All" button height */
#define BASE_CMD_CONTAINER_MAX_H  260  /* Max command container height */
#define BASE_CMD_CARD_GAP   1   /* Divider gap between cards in container */
#define BASE_SCROLLBAR_W    6   /* Custom scrollbar width */

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
#define CLR_BTN_ALLOW_SEL RGB(144, 213, 148)   /* pastel green — Allow Selected */
#define CLR_BTN_ALLOW_ALL RGB(240, 180, 110)   /* pastel orange — Allow All */
#define CLR_BTN_CANCEL    RGB(210, 60, 60)     /* red — Cancel */
#define CLR_CHK_BORDER    RGB(160, 160, 160)   /* checkbox border */
#define CLR_CHK_FILL      RGB(0, 160, 80)      /* checkbox checked fill */
#define CLR_LINK_TEXT     RGB(100, 140, 180)   /* auto-approve link */
#define CLR_BLOCKED_TEXT  RGB(128, 128, 128)   /* greyed-out command text */
#define CLR_RETRY_TEXT    RGB(100, 140, 180)   /* [Retry] link colour */
#define CLR_EXEC_TAG      RGB(180, 140, 220)   /* pastel purple for [EXEC] commands */

/* Height of the inline activity indicator line (96 DPI base) */
#define BASE_ACTIVITY_H   28
#define BASE_DOT_SIZE       8

/* ── Forward declarations ───────────────────────────────────────────── */

static LRESULT CALLBACK ChatListWndProc(HWND, UINT, WPARAM, LPARAM);
static void     recalc_layout(ChatListView *lv);
static void     recalc_dpi_constants(ChatListView *lv);
static void     update_scrollbar(ChatListView *lv);
static int      measure_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                             int width);
static void     paint_user_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                                RECT *rc);
static void     paint_ai_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                               RECT *rc);
static void     paint_cmd_card(ChatListView *lv, HDC hdc,
                               ChatMsgItem *item, RECT *rc);
static void     paint_cmd_container(ChatListView *lv, HDC hdc, RECT *rc);
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
        if (item->type == CHAT_ITEM_COMMAND && !item->u.cmd.settled) {
            (*total)++;
            if (item->u.cmd.approved == -1 && !item->u.cmd.blocked)
                (*pending)++;
        }
        item = item->next;
    }
}

/* Count how many pending commands have their tickbox selected */
static int count_selected(const ChatMsgList *list)
{
    int n = 0;
    if (!list) return 0;
    ChatMsgItem *item = list->head;
    while (item) {
        if (item->type == CHAT_ITEM_COMMAND && !item->u.cmd.settled &&
            item->u.cmd.approved == -1 && !item->u.cmd.blocked &&
            item->u.cmd.selected)
            n++;
        item = item->next;
    }
    return n;
}

/* command_index_of: reserved for future use (e.g., tooltip lookup) */

/* ── Is this the first command item in the list? ────────────────────── */

static int is_first_command(const ChatMsgList *list, const ChatMsgItem *target)
{
    ChatMsgItem *item = list->head;
    while (item) {
        if (item->type == CHAT_ITEM_COMMAND && !item->u.cmd.settled)
            return (item == target) ? 1 : 0;
        item = item->next;
    }
    return 0;
}

/* command_index_of and is_last_command removed — no longer needed
 * with the container-based command rendering approach. */

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

/* ── Selection: check if an item overlaps the selection range ──────── */

static int item_in_selection(const ChatListView *lv, int item_top, int item_h)
{
    if (!lv->sel_valid) return 0;
    int sy = lv->sel_start_y, ey = lv->sel_end_y;
    if (sy > ey) { int tmp = sy; sy = ey; ey = tmp; }
    int item_bot = item_top + item_h;
    return (item_bot > sy && item_top < ey);
}

/* ── Selection: extract text from all items in the selection range ─── */

static size_t sel_extract_text(const ChatListView *lv, char *buf, size_t buf_sz)
{
    if (!lv->sel_valid || !lv->msg_list || buf_sz == 0) return 0;

    int sy = lv->sel_start_y, ey = lv->sel_end_y;
    if (sy > ey) { int tmp = sy; sy = ey; ey = tmp; }

    size_t pos = 0;
    int y = lv->msg_gap;  /* content Y (not scroll-adjusted) */
    ChatMsgItem *item = lv->msg_list->head;

    while (item && pos < buf_sz - 1) {
        int h = item->measured_height;
        int item_bot = y + h;

        if (item_bot > sy && y < ey) {
            const char *txt = NULL;
            switch (item->type) {
            case CHAT_ITEM_USER:
            case CHAT_ITEM_AI_TEXT:
            case CHAT_ITEM_STATUS:
                txt = item->text;
                break;
            case CHAT_ITEM_COMMAND:
                txt = item->u.cmd.command ? item->u.cmd.command : item->text;
                break;
            }
            if (txt && *txt) {
                if (pos > 0 && pos < buf_sz - 1)
                    buf[pos++] = '\n';
                size_t len = strlen(txt);
                if (len > buf_sz - 1 - pos) len = buf_sz - 1 - pos;
                memcpy(buf + pos, txt, len);
                pos += len;
            }
        }

        y += h + lv->msg_gap;
        item = item->next;
    }

    buf[pos] = '\0';
    return pos;
}

/* ── Selection: copy selected text to clipboard ───────────────────── */

static void sel_copy_to_clipboard(ChatListView *lv)
{
    char buf[32768];
    size_t n = sel_extract_text(lv, buf, sizeof(buf));
    if (n > 0 && OpenClipboard(lv->hwnd)) {
        EmptyClipboard();
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, n + 1);
        if (hg) {
            char *dst = (char *)GlobalLock(hg);
            memcpy(dst, buf, n + 1);
            GlobalUnlock(hg);
            SetClipboardData(CF_TEXT, hg);
        }
        CloseClipboard();
    }
}

/* ── Selection: select all items ──────────────────────────────────── */

static void sel_select_all(ChatListView *lv)
{
    lv->sel_start_y = 0;
    lv->sel_end_y = lv->total_height;
    lv->sel_valid = 1;
    InvalidateRect(lv->hwnd, NULL, FALSE);
}

/* ── Selection: clear selection ───────────────────────────────────── */

static void sel_clear(ChatListView *lv)
{
    if (lv->sel_valid) {
        lv->sel_valid = 0;
        lv->sel_active = 0;
        InvalidateRect(lv->hwnd, NULL, FALSE);
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
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
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
    /* Fonts changed — mark all dirty for full remeasure */
    {
        ChatMsgItem *fi = lv->msg_list ? lv->msg_list->head : NULL;
        while (fi) { fi->dirty = 1; fi = fi->next; }
    }
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

void chat_listview_set_activity(HWND hwnd, ActivityState *activity)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return;
    lv->activity = activity;
}

void chat_listview_set_pulse(HWND hwnd, int toggle)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return;
    lv->pulse_toggle = toggle;
}

void chat_listview_toggle_cmd_expand(HWND hwnd)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return;
    /* No longer collapse/expand — reset container scroll instead */
    lv->cmd_scroll_y = 0;
    recalc_layout(lv);
    InvalidateRect(hwnd, NULL, TRUE);
}

void chat_listview_reset_cmd_expand(HWND hwnd)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return;
    lv->cmd_scroll_y = 0;
}

void chat_listview_set_scrollbar(HWND hwnd, HWND scrollbar)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return;
    lv->ext_scrollbar = scrollbar;
}

void chat_listview_set_model(HWND hwnd, const char *model)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return;
    if (model)
        snprintf(lv->model_name, sizeof(lv->model_name), "%s", model);
    else
        lv->model_name[0] = '\0';
    InvalidateRect(hwnd, NULL, FALSE);
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

int chat_listview_is_near_bottom(HWND hwnd)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return 1;
    int max_scroll = lv->total_height - lv->viewport_height;
    if (max_scroll <= 0) return 1;
    /* "Near bottom" = within a small margin of the bottom.
     * Tight threshold so expanding the thinking box (which increases
     * total_height) doesn't keep triggering scroll_to_bottom. */
    int margin = CLV_SCALE(lv, 60);
    return lv->scroll_y >= max_scroll - margin;
}

void chat_listview_relayout(HWND hwnd)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    lv->viewport_height = rc.bottom - rc.top;
    /* Mark all items dirty — width changed, heights need recalculating */
    ChatMsgItem *ri = lv->msg_list ? lv->msg_list->head : NULL;
    while (ri) { ri->dirty = 1; ri = ri->next; }
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

    /* Pass 1: measure items (only dirty items need remeasuring).
     * Exception: unsettled command items are always remeasured because
     * Pass 2 overwrites their measured_height with the container height,
     * and we need the original card height for cmd_heights[]. */
    ChatMsgItem *item = lv->msg_list ? lv->msg_list->head : NULL;
    while (item) {
        int force = (item->type == CHAT_ITEM_COMMAND && !item->u.cmd.settled);
        if (item->dirty || item->measured_height == 0 || force)
            item->measured_height = measure_item(lv, hdc, item, width);
        item->dirty = 0;
        item = item->next;
    }
    ReleaseDC(lv->hwnd, hdc);

    /* Pass 2: compute command container — group all commands into a
     * scrollable container drawn by the first command item. */
    {
        int n = 0;
        ChatMsgItem *first_cmd = NULL;
        int sum_h = 0;
        int card_gap = CLV_SCALE(lv, BASE_CMD_CARD_GAP);

        item = lv->msg_list ? lv->msg_list->head : NULL;
        while (item) {
            if (item->type == CHAT_ITEM_COMMAND && !item->u.cmd.settled) {
                if (!first_cmd) first_cmd = item;
                if (n < 16)
                    lv->cmd_heights[n] = item->measured_height;
                sum_h += item->measured_height;
                if (n > 0) sum_h += card_gap;
                n++;
            }
            item = item->next;
        }
        lv->cmd_count = n;
        lv->cmd_total_h = sum_h;

        if (n > 0 && first_cmd) {
            int max_h = CLV_SCALE(lv, BASE_CMD_CONTAINER_MAX_H);
            int visible_h = (sum_h > max_h) ? max_h : sum_h;
            lv->cmd_visible_h = visible_h;
            int pad = CLV_SCALE(lv, 6);  /* container top/bottom padding */

            /* Action buttons below the container */
            int dummy_total, pending;
            count_commands(lv->msg_list, &dummy_total, &pending);
            int action_h = 0;
            if (pending > 0)
                action_h = CLV_SCALE(lv, 4) + CLV_SCALE(lv, BASE_ALLOW_ALL_H);

            int container_h = visible_h + 2 * pad + action_h;

            /* First command absorbs the full container height */
            first_cmd->measured_height = container_h;
            /* Hide all subsequent commands (painted by container) */
            item = first_cmd->next;
            while (item) {
                if (item->type == CHAT_ITEM_COMMAND && !item->u.cmd.settled)
                    item->measured_height = 0;
                item = item->next;
            }

            /* Clamp cmd_scroll_y */
            int max_cmd_scroll = sum_h - visible_h;
            if (max_cmd_scroll < 0) max_cmd_scroll = 0;
            if (lv->cmd_scroll_y > max_cmd_scroll)
                lv->cmd_scroll_y = max_cmd_scroll;
            if (lv->cmd_scroll_y < 0) lv->cmd_scroll_y = 0;
        }
    }

    /* Pass 3: compute total height (skip h=0 items) */
    int y = lv->msg_gap;
    item = lv->msg_list ? lv->msg_list->head : NULL;
    while (item) {
        if (item->measured_height > 0)
            y += item->measured_height + lv->msg_gap;
        item = item->next;
    }

    /* Reserve space for the activity indicator when active */
    if (lv->activity && lv->activity->phase != ACTIVITY_IDLE)
        y += CLV_SCALE(lv, BASE_ACTIVITY_H) + lv->msg_gap;

    lv->total_height = y;
    lv->viewport_height = rc.bottom - rc.top;

    /* Clamp scroll */
    int max_scroll = lv->total_height - lv->viewport_height;
    if (max_scroll < 0) max_scroll = 0;
    if (lv->scroll_y > max_scroll) lv->scroll_y = max_scroll;
    if (lv->scroll_y < 0) lv->scroll_y = 0;

    update_scrollbar(lv);
}

/* Build a measurement-ready copy of AI text: strip [EXEC]/[/EXEC] tags
 * and trailing newlines before [EXEC] markers.  Caller must free(). */
static char *ai_text_for_measure(const char *text)
{
    size_t len = strlen(text);
    char *out = malloc(len + 1);
    if (!out) return NULL;
    char *dst = out;
    const char *pos = text;

    while (*pos) {
        const char *exec = strstr(pos, "[EXEC]");
        if (!exec) {
            size_t remain = strlen(pos);
            memcpy(dst, pos, remain);
            dst += remain;
            break;
        }
        /* Copy text before [EXEC], stripping trailing newlines */
        size_t seg_len = (size_t)(exec - pos);
        memcpy(dst, pos, seg_len);
        dst += seg_len;
        while (dst > out && (dst[-1] == '\n' || dst[-1] == '\r'))
            dst--;

        /* Find [/EXEC] and copy command content (without tags) */
        const char *cmd_start = exec + 6;
        const char *exec_end = strstr(cmd_start, "[/EXEC]");
        if (!exec_end) {
            size_t remain = strlen(cmd_start);
            memcpy(dst, cmd_start, remain);
            dst += remain;
            break;
        }
        size_t cmd_len = (size_t)(exec_end - cmd_start);
        memcpy(dst, cmd_start, cmd_len);
        dst += cmd_len;
        pos = exec_end + 7;
    }
    *dst = '\0';
    return out;
}

/* ── Measure a single item ──────────────────────────────────────────── */

static int measure_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                        int width)
{
    int side_pad = CLV_SCALE(lv, BASE_SIDE_PAD);
    int text_w;
    RECT rc;
    HGDIOBJ old_font;

    if ((!item->text || item->text_len == 0) &&
        !(item->type == CHAT_ITEM_COMMAND && item->u.cmd.command))
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
        {
            int total = (rc.bottom - rc.top) + 2 * lv->user_pad_v;
            if (item->queued)
                total += CLV_SCALE(lv, 16);
            return total;
        }
    }

    case CHAT_ITEM_AI_TEXT: {
        /* AI text: left-indented.  on_paint applies side_pad to item_rc,
         * and paint_ai_item subtracts another side_pad on the right, so
         * the effective text width is width - 2*side_pad - indent - side_pad. */
        text_w = width - lv->ai_indent - 3 * side_pad;
        if (text_w < 40) text_w = 40;

        old_font = SelectObject(hdc, lv->hFont ? lv->hFont
                                                : GetStockObject(DEFAULT_GUI_FONT));
        SetRect(&rc, 0, 0, text_w, 0);

        const char *measure_text = item->text;
        char *stripped = NULL;
        if (strstr(item->text, "[EXEC]")) {
            stripped = ai_text_for_measure(item->text);
            if (stripped) measure_text = stripped;
        }
        draw_text_utf8(hdc, measure_text, &rc,
                       DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        free(stripped);

        SelectObject(hdc, old_font);

        int h = rc.bottom - rc.top;

        /* Icon row + gap before content (must match paint_ai_item layout) */
        int total = h + CLV_SCALE(lv, BASE_ICON_SIZE) + CLV_SCALE(lv, 4);

        /* Thinking block height — shown during streaming and after completion */
        if (item->u.ai.thinking_text && item->u.ai.thinking_text[0]) {
            int hdr_h = CLV_SCALE(lv, BASE_THINK_HDR_H);
            int pad = lv->code_pad;
            int gap = CLV_SCALE(lv, 6);

            if (item->u.ai.thinking_collapsed) {
                /* Collapsed: header + border + gap */
                total += hdr_h + 2 * pad + gap;
            } else {
                /* Expanded: measure thinking text height */
                int think_w = text_w - 2 * pad;
                if (think_w < 20) think_w = 20;
                HGDIOBJ tf = SelectObject(hdc, lv->hFont ? lv->hFont
                                          : GetStockObject(DEFAULT_GUI_FONT));
                RECT trc;
                SetRect(&trc, 0, 0, think_w, 0);
                draw_text_utf8(hdc, item->u.ai.thinking_text, &trc,
                               DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
                int think_h = trc.bottom - trc.top;
                SelectObject(hdc, tf);

                int max_h = CLV_SCALE(lv, BASE_THINK_MAX_H);
                if (think_h > max_h) think_h = max_h;

                /* header + separator + body + border + gap */
                total += hdr_h + think_h + 3 * pad + gap;
            }
        }

        return total;
    }

    case CHAT_ITEM_COMMAND: {
        /* Settled commands are hidden — their info is already in the
         * AI text as [EXEC] blocks rendered in purple. */
        if (item->u.cmd.settled)
            return 0;
        /* Compact command card: safety tag inline with command text,
         * plus a slim status row.  Container logic in recalc_layout
         * handles grouping and scroll capping. */
        const char *cmd_text = item->u.cmd.command ? item->u.cmd.command
                                                   : item->text;
        text_w = width - 4 * side_pad - 2 * lv->code_pad;

        /* Subtract inline safety tag width so measurement matches the
         * narrower text area that paint_cmd_card actually uses. */
        {
            const char *tag = safety_tag_text(item->u.cmd.safety);
            HGDIOBJ tf = SelectObject(hdc, lv->hSmallFont
                             ? lv->hSmallFont
                             : GetStockObject(DEFAULT_GUI_FONT));
            SIZE tsz;
            GetTextExtentPoint32A(hdc, tag, (int)strlen(tag), &tsz);
            SelectObject(hdc, tf);
            int tag_w = tsz.cx + 2 * CLV_SCALE(lv, BASE_TAG_PAD_H);
            text_w -= tag_w + CLV_SCALE(lv, 4);
        }
        if (text_w < 40) text_w = 40;

        old_font = SelectObject(hdc, lv->hMonoFont ? lv->hMonoFont
                                                    : GetStockObject(ANSI_FIXED_FONT));
        SetRect(&rc, 0, 0, text_w, 0);
        draw_text_utf8(hdc, cmd_text, &rc,
                       DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(hdc, old_font);

        int h = (rc.bottom - rc.top) + 2 * lv->code_pad;
        /* Ensure minimum height for inline safety tag */
        int min_h = CLV_SCALE(lv, BASE_TAG_H) + 2 * lv->code_pad;
        if (h < min_h) h = min_h;
        /* Compact status row + bottom padding */
        h += CLV_SCALE(lv, 2) + CLV_SCALE(lv, 20) + CLV_SCALE(lv, 4);
        return h;
    }

    case CHAT_ITEM_STATUS: {
        /* on_paint applies side_pad to item_rc, paint_status_item adds
         * another side_pad on each side, so effective width is
         * width - 4*side_pad. */
        text_w = width - 4 * side_pad;
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
    if (lv->ext_scrollbar) {
        int max_val = lv->total_height > 0 ? lv->total_height - 1 : 0;
        csb_set_range(lv->ext_scrollbar, 0, max_val, lv->viewport_height);
        csb_set_pos(lv->ext_scrollbar, lv->scroll_y);
    }
}

static void clamp_scroll(ChatListView *lv)
{
    int max_scroll = lv->total_height - lv->viewport_height;
    if (max_scroll < 0) max_scroll = 0;
    if (lv->scroll_y > max_scroll) lv->scroll_y = max_scroll;
    if (lv->scroll_y < 0) lv->scroll_y = 0;
}

/* ── Recalculate scaled layout constants from current dpi_scale ─────── */

static void recalc_dpi_constants(ChatListView *lv)
{
    lv->msg_gap    = CLV_SCALE(lv, BASE_MSG_GAP);
    lv->user_pad_h = CLV_SCALE(lv, BASE_USER_PAD_H);
    lv->user_pad_v = CLV_SCALE(lv, BASE_USER_PAD_V);
    lv->ai_indent  = CLV_SCALE(lv, BASE_AI_INDENT);
    lv->code_pad   = CLV_SCALE(lv, BASE_CODE_PAD);
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

    if (item->queued) {
        int label_h = CLV_SCALE(lv, 14);
        RECT label_rc;
        label_rc.left   = text_rc.left;
        label_rc.top    = text_rc.bottom + CLV_SCALE(lv, 2);
        label_rc.right  = text_rc.right;
        label_rc.bottom = label_rc.top + label_h;

        HGDIOBJ qf = SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                       : GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(hdc, RGB(160, 160, 160));
        DrawTextA(hdc, "Queued \xe2\x80\x94 ", -1, &label_rc,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SIZE qs;
        GetTextExtentPoint32A(hdc, "Queued -- ", 10, &qs);
        RECT cancel_rc = label_rc;
        cancel_rc.left += qs.cx;
        SetTextColor(hdc, CLR_RETRY_TEXT);
        DrawTextA(hdc, "Cancel", -1, &cancel_rc,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(hdc, qf);
    }
}

/* Draw AI text with [EXEC]...[/EXEC] segments highlighted in purple.
 * Uses the same rect and flags as draw_text_utf8 but splits at markers. */
static void draw_ai_text_with_exec(ChatListView *lv, HDC hdc,
                                    const char *text, RECT *rc)
{
    COLORREF normal_clr = RGB_FROM_THEME(lv->theme->text_main);
    COLORREF exec_clr = CLR_EXEC_TAG;
    HFONT mono_font = lv->hMonoFont ? lv->hMonoFont
                                     : (HFONT)GetStockObject(ANSI_FIXED_FONT);
    HFONT text_font = lv->hFont ? lv->hFont
                                 : (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    /* If no [EXEC] markers, fast path */
    if (!strstr(text, "[EXEC]")) {
        SetTextColor(hdc, normal_clr);
        draw_text_utf8(hdc, text, rc, DT_WORDBREAK | DT_NOPREFIX);
        return;
    }

    /* Multi-segment rendering: split at [EXEC]/[/EXEC] boundaries */
    const char *pos = text;
    int y = rc->top;

    while (*pos) {
        const char *exec_start = strstr(pos, "[EXEC]");

        if (!exec_start) {
            /* Remaining text is normal */
            if (*pos) {
                RECT seg_rc = { rc->left, y, rc->right, rc->bottom };
                SetTextColor(hdc, normal_clr);
                SelectObject(hdc, text_font);
                int h = draw_text_utf8(hdc, pos, &seg_rc,
                                       DT_WORDBREAK | DT_NOPREFIX);
                y += h;
            }
            break;
        }

        /* Draw text before [EXEC] */
        if (exec_start > pos) {
            /* Copy segment to temp buffer */
            size_t seg_len = (size_t)(exec_start - pos);
            char *seg = malloc(seg_len + 1);
            if (seg) {
                memcpy(seg, pos, seg_len);
                seg[seg_len] = '\0';
                /* Strip trailing newlines to remove blank line before [EXEC] */
                size_t trim = seg_len;
                while (trim > 0 && (seg[trim - 1] == '\n' || seg[trim - 1] == '\r'))
                    trim--;
                seg[trim] = '\0';
                if (trim > 0) {
                    RECT seg_rc = { rc->left, y, rc->right, rc->bottom };
                    SetTextColor(hdc, normal_clr);
                    SelectObject(hdc, text_font);
                    int h = draw_text_utf8(hdc, seg, &seg_rc,
                                           DT_WORDBREAK | DT_NOPREFIX);
                    y += h;
                }
                free(seg);
            }
        }

        /* Find [/EXEC] */
        const char *cmd_start = exec_start + 6;
        const char *exec_end = strstr(cmd_start, "[/EXEC]");

        if (!exec_end) {
            /* No closing tag — render rest as exec */
            RECT seg_rc = { rc->left, y, rc->right, rc->bottom };
            SetTextColor(hdc, exec_clr);
            SelectObject(hdc, mono_font);
            int h = draw_text_utf8(hdc, exec_start, &seg_rc,
                                   DT_WORDBREAK | DT_NOPREFIX);
            y += h;
            break;
        }

        /* Draw the command (between [EXEC] and [/EXEC]) in purple with mono font */
        {
            size_t cmd_len = (size_t)(exec_end - cmd_start);
            char *cmd_text = malloc(cmd_len + 1);
            if (cmd_text) {
                memcpy(cmd_text, cmd_start, cmd_len);
                cmd_text[cmd_len] = '\0';
                RECT seg_rc = { rc->left, y, rc->right, rc->bottom };
                SetTextColor(hdc, exec_clr);
                SelectObject(hdc, mono_font);
                int h = draw_text_utf8(hdc, cmd_text, &seg_rc,
                                       DT_WORDBREAK | DT_NOPREFIX);
                y += h;
                free(cmd_text);
            }
        }

        pos = exec_end + 7; /* skip [/EXEC] */
    }
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

    /* Model name after "AI" label in smaller font */
    if (lv->model_name[0]) {
        SIZE ai_sz;
        GetTextExtentPoint32A(hdc, "AI", 2, &ai_sz);
        SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                         : GetStockObject(DEFAULT_GUI_FONT));
        char model_label[80];
        snprintf(model_label, sizeof(model_label), " \xC2\xB7 %s", lv->model_name);
        RECT model_rc = label_rc;
        model_rc.left += ai_sz.cx;
        draw_text_utf8(hdc, model_label, &model_rc,
                       DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        /* Restore font for content rendering below */
        SelectObject(hdc, lv->hFont ? lv->hFont
                          : GetStockObject(DEFAULT_GUI_FONT));
    }

    int content_top = rc->top + icon_sz + CLV_SCALE(lv, 4);

    /* ── Thinking block (contained box) — shown during streaming and
     * after completion.  During streaming the header shows a pulsing
     * green dot; after completion it shows "Thought for X.Xs". ───── */
    if (item->u.ai.thinking_text && item->u.ai.thinking_text[0]) {
        int hdr_h   = CLV_SCALE(lv, BASE_THINK_HDR_H);
        int pad     = lv->code_pad;
        int corner  = CLV_SCALE(lv, 6);
        int gap     = CLV_SCALE(lv, 6);
        int box_left  = rc->left + lv->ai_indent;
        int box_right = rc->right - side_pad;

        if (item->u.ai.thinking_collapsed) {
            /* ── Collapsed: single header row ─────────────────── */
            RECT box_rc;
            SetRect(&box_rc, box_left, content_top,
                    box_right, content_top + hdr_h + 2 * pad);
            fill_rounded_rect(hdc, &box_rc, corner,
                              RGB_FROM_THEME(tc->cmd_bg));
            /* Border */
            HPEN border_pen = CreatePen(PS_SOLID, 1,
                                        RGB_FROM_THEME(tc->cmd_border));
            HGDIOBJ old_pen2 = SelectObject(hdc, border_pen);
            HGDIOBJ old_br2 = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, box_rc.left, box_rc.top,
                      box_rc.right, box_rc.bottom, corner, corner);
            SelectObject(hdc, old_pen2);
            SelectObject(hdc, old_br2);
            DeleteObject(border_pen);

            /* Header text: chevron + optional pulsing dot + label */
            SetTextColor(hdc, RGB_FROM_THEME(tc->thinking_text));
            SelectObject(hdc, lv->hBoldFont ? lv->hBoldFont
                              : GetStockObject(DEFAULT_GUI_FONT));
            RECT hdr_rc;
            SetRect(&hdr_rc, box_rc.left + pad, box_rc.top + pad,
                    box_rc.right - pad, box_rc.bottom - pad);

            if (item->u.ai.thinking_complete) {
                char hdr_buf[64];
                snprintf(hdr_buf, sizeof(hdr_buf),
                         "\xe2\x96\xb6  Thought for %.1fs",
                         (double)item->u.ai.thinking_elapsed);
                draw_text_utf8(hdc, hdr_buf, &hdr_rc,
                               DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX
                               | DT_END_ELLIPSIS);
            } else {
                /* Draw chevron */
                draw_text_utf8(hdc, "\xe2\x96\xb6", &hdr_rc,
                               DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                /* Measure chevron width to position dot after it */
                RECT chev_rc;
                SetRect(&chev_rc, 0, 0, 0, 0);
                draw_text_utf8(hdc, "\xe2\x96\xb6", &chev_rc,
                               DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
                int dot_x = hdr_rc.left + (chev_rc.right - chev_rc.left)
                            + CLV_SCALE(lv, 6);
                int dot_sz = CLV_SCALE(lv, BASE_DOT_SIZE);
                int dot_y = hdr_rc.top
                            + (hdr_rc.bottom - hdr_rc.top - dot_sz) / 2;
                /* Pulsing dot — blend with bg on alternate ticks */
                COLORREF dot_clr = RGB_FROM_THEME(tc->thinking_text);
                if (lv->pulse_toggle) {
                    COLORREF bg = RGB_FROM_THEME(tc->cmd_bg);
                    dot_clr = RGB(
                        (GetRValue(dot_clr) + GetRValue(bg)) / 2,
                        (GetGValue(dot_clr) + GetGValue(bg)) / 2,
                        (GetBValue(dot_clr) + GetBValue(bg)) / 2);
                }
                HBRUSH dbr = CreateSolidBrush(dot_clr);
                HPEN   dpen = CreatePen(PS_SOLID, 1, dot_clr);
                HGDIOBJ obr = SelectObject(hdc, dbr);
                HGDIOBJ old_dpen = SelectObject(hdc, dpen);
                Ellipse(hdc, dot_x, dot_y,
                        dot_x + dot_sz, dot_y + dot_sz);
                SelectObject(hdc, old_dpen);
                SelectObject(hdc, obr);
                DeleteObject(dpen);
                DeleteObject(dbr);

                /* "Thinking..." label after the dot */
                RECT lbl_rc;
                SetRect(&lbl_rc, dot_x + dot_sz + CLV_SCALE(lv, 4),
                        hdr_rc.top, hdr_rc.right, hdr_rc.bottom);
                draw_text_utf8(hdc, "Thinking...", &lbl_rc,
                               DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

                /* Right-aligned elapsed timer */
                char time_buf[16];
                snprintf(time_buf, sizeof(time_buf), "%.1fs",
                         (double)item->u.ai.thinking_elapsed);
                RECT time_rc;
                SetRect(&time_rc, hdr_rc.left, hdr_rc.top,
                        hdr_rc.right, hdr_rc.bottom);
                draw_text_utf8(hdc, time_buf, &time_rc,
                               DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX
                               | DT_RIGHT);
            }

            content_top = box_rc.bottom + gap;
        } else {
            /* ── Expanded: header + separator + scrollable body ── */

            /* Measure full thinking text height */
            int think_w = box_right - box_left - 2 * pad;
            if (think_w < 20) think_w = 20;
            HGDIOBJ tf = SelectObject(hdc, lv->hFont ? lv->hFont
                                      : GetStockObject(DEFAULT_GUI_FONT));
            RECT mr;
            SetRect(&mr, 0, 0, think_w, 0);
            draw_text_utf8(hdc, item->u.ai.thinking_text, &mr,
                           DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            int full_h = mr.bottom - mr.top;
            int max_h = CLV_SCALE(lv, BASE_THINK_MAX_H);
            int vis_h = full_h;
            if (vis_h > max_h) vis_h = max_h;
            if (vis_h < CLV_SCALE(lv, BASE_THINK_MIN_H))
                vis_h = CLV_SCALE(lv, BASE_THINK_MIN_H);

            int box_h = hdr_h + pad + vis_h + 2 * pad;
            RECT box_rc;
            SetRect(&box_rc, box_left, content_top,
                    box_right, content_top + box_h);
            fill_rounded_rect(hdc, &box_rc, corner,
                              RGB_FROM_THEME(tc->cmd_bg));
            /* Border */
            HPEN border_pen = CreatePen(PS_SOLID, 1,
                                        RGB_FROM_THEME(tc->cmd_border));
            HGDIOBJ old_pen2 = SelectObject(hdc, border_pen);
            HGDIOBJ old_br2 = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            RoundRect(hdc, box_rc.left, box_rc.top,
                      box_rc.right, box_rc.bottom, corner, corner);
            SelectObject(hdc, old_pen2);
            SelectObject(hdc, old_br2);
            DeleteObject(border_pen);

            /* Header row */
            SetTextColor(hdc, RGB_FROM_THEME(tc->thinking_text));
            SelectObject(hdc, lv->hBoldFont ? lv->hBoldFont
                              : GetStockObject(DEFAULT_GUI_FONT));
            RECT hdr_rc;
            SetRect(&hdr_rc, box_rc.left + pad, box_rc.top + pad,
                    box_rc.right - pad, box_rc.top + pad + hdr_h);

            if (item->u.ai.thinking_complete) {
                char hdr_buf[64];
                snprintf(hdr_buf, sizeof(hdr_buf),
                         "\xe2\x96\xbc  Thought for %.1fs",
                         (double)item->u.ai.thinking_elapsed);
                draw_text_utf8(hdc, hdr_buf, &hdr_rc,
                               DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX
                               | DT_END_ELLIPSIS);
            } else {
                /* Draw chevron */
                draw_text_utf8(hdc, "\xe2\x96\xbc", &hdr_rc,
                               DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                /* Pulsing dot after chevron */
                RECT chev_rc;
                SetRect(&chev_rc, 0, 0, 0, 0);
                draw_text_utf8(hdc, "\xe2\x96\xbc", &chev_rc,
                               DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
                int dot_x = hdr_rc.left + (chev_rc.right - chev_rc.left)
                            + CLV_SCALE(lv, 6);
                int dot_sz = CLV_SCALE(lv, BASE_DOT_SIZE);
                int dot_y = hdr_rc.top
                            + (hdr_rc.bottom - hdr_rc.top - dot_sz) / 2;
                COLORREF dot_clr = RGB_FROM_THEME(tc->thinking_text);
                if (lv->pulse_toggle) {
                    COLORREF bg = RGB_FROM_THEME(tc->cmd_bg);
                    dot_clr = RGB(
                        (GetRValue(dot_clr) + GetRValue(bg)) / 2,
                        (GetGValue(dot_clr) + GetGValue(bg)) / 2,
                        (GetBValue(dot_clr) + GetBValue(bg)) / 2);
                }
                HBRUSH dbr = CreateSolidBrush(dot_clr);
                HPEN   dpen = CreatePen(PS_SOLID, 1, dot_clr);
                HGDIOBJ obr = SelectObject(hdc, dbr);
                HGDIOBJ old_dpen = SelectObject(hdc, dpen);
                Ellipse(hdc, dot_x, dot_y,
                        dot_x + dot_sz, dot_y + dot_sz);
                SelectObject(hdc, old_dpen);
                SelectObject(hdc, obr);
                DeleteObject(dpen);
                DeleteObject(dbr);

                /* "Thinking..." label */
                RECT lbl_rc;
                SetRect(&lbl_rc, dot_x + dot_sz + CLV_SCALE(lv, 4),
                        hdr_rc.top, hdr_rc.right, hdr_rc.bottom);
                draw_text_utf8(hdc, "Thinking...", &lbl_rc,
                               DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

                /* Right-aligned elapsed timer */
                char time_buf[16];
                snprintf(time_buf, sizeof(time_buf), "%.1fs",
                         (double)item->u.ai.thinking_elapsed);
                RECT time_rc;
                SetRect(&time_rc, hdr_rc.left, hdr_rc.top,
                        hdr_rc.right, hdr_rc.bottom);
                draw_text_utf8(hdc, time_buf, &time_rc,
                               DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX
                               | DT_RIGHT);
            }

            /* Separator line */
            int sep_y = box_rc.top + pad + hdr_h;
            HPEN sep_pen = CreatePen(PS_SOLID, 1,
                                     RGB_FROM_THEME(tc->cmd_border));
            HGDIOBJ old_sep = SelectObject(hdc, sep_pen);
            MoveToEx(hdc, box_rc.left + pad, sep_y, NULL);
            LineTo(hdc, box_rc.right - pad, sep_y);
            SelectObject(hdc, old_sep);
            DeleteObject(sep_pen);

            /* Body: thinking text with clip region and scroll offset */
            int body_top = sep_y + pad;
            int sb_w = (full_h > vis_h) ? CLV_SCALE(lv, 6) : 0;
            RECT clip_rc;
            SetRect(&clip_rc, box_rc.left + pad, body_top,
                    box_rc.right - pad - sb_w, body_top + vis_h);
            HRGN clip_rgn = CreateRectRgnIndirect(&clip_rc);
            SelectClipRgn(hdc, clip_rgn);

            SetTextColor(hdc, RGB_FROM_THEME(tc->thinking_text));
            SelectObject(hdc, lv->hFont ? lv->hFont
                              : GetStockObject(DEFAULT_GUI_FONT));
            RECT body_rc;
            SetRect(&body_rc, clip_rc.left,
                    body_top - item->u.ai.thinking_scroll_y,
                    clip_rc.right,
                    body_top - item->u.ai.thinking_scroll_y + full_h);
            draw_text_utf8(hdc, item->u.ai.thinking_text, &body_rc,
                           DT_WORDBREAK | DT_NOPREFIX);

            SelectClipRgn(hdc, NULL);
            DeleteObject(clip_rgn);

            /* Scrollbar thumb when content overflows */
            if (full_h > vis_h) {
                int sb_x = box_rc.right - pad - sb_w;
                int sb_inset = CLV_SCALE(lv, 1);
                int track_h = vis_h;
                int thumb_h = (vis_h * vis_h) / full_h;
                int min_thumb = CLV_SCALE(lv, 20);
                if (thumb_h < min_thumb) thumb_h = min_thumb;
                int max_scroll = full_h - vis_h;
                int thumb_y = body_top;
                if (max_scroll > 0)
                    thumb_y += (item->u.ai.thinking_scroll_y
                                * (track_h - thumb_h)) / max_scroll;

                /* Draw rounded thumb in thinking_text color */
                COLORREF thumb_clr = RGB_FROM_THEME(tc->thinking_text);
                HBRUSH tb = CreateSolidBrush(thumb_clr);
                HPEN np = CreatePen(PS_NULL, 0, 0);
                HGDIOBJ ob = SelectObject(hdc, tb);
                HGDIOBJ op = SelectObject(hdc, np);
                RoundRect(hdc, sb_x + sb_inset, thumb_y,
                          sb_x + sb_w - sb_inset, thumb_y + thumb_h,
                          sb_w, sb_w);
                SelectObject(hdc, op);
                SelectObject(hdc, ob);
                DeleteObject(np);
                DeleteObject(tb);
            }
            SelectObject(hdc, tf);

            content_top = box_rc.bottom + gap;
        }
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
    draw_ai_text_with_exec(lv, hdc, item->text, &text_rc);
    SelectObject(hdc, old_font);
}

/* ── Paint a single compact command card (no outer border — container
 *    handles that).  Draws: command text + inline safety tag + status row. */

static void paint_cmd_card(ChatListView *lv, HDC hdc,
                           ChatMsgItem *item, RECT *rc)
{
    const ThemeChatColors *tc = &lv->theme->chat;
    int code_pad = lv->code_pad;
    int tag_h    = CLV_SCALE(lv, BASE_TAG_H);
    int tag_pad  = CLV_SCALE(lv, BASE_TAG_PAD_H);

    SetBkMode(hdc, TRANSPARENT);

    /* ── Safety tag (top-right, inline with command text) ──────────── */
    COLORREF tag_clr = safety_tag_color(item->u.cmd.safety);
    const char *tag_text = safety_tag_text(item->u.cmd.safety);

    HGDIOBJ old_font = SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                        : GetStockObject(DEFAULT_GUI_FONT));
    SIZE tag_sz;
    GetTextExtentPoint32A(hdc, tag_text, (int)strlen(tag_text), &tag_sz);
    int tag_w = tag_sz.cx + 2 * tag_pad;

    RECT tag_rc;
    tag_rc.right  = rc->right - code_pad;
    tag_rc.left   = tag_rc.right - tag_w;
    tag_rc.top    = rc->top + code_pad;
    tag_rc.bottom = tag_rc.top + tag_h;

    fill_rounded_rect(hdc, &tag_rc, CLV_SCALE(lv, 3), tag_clr);
    SetTextColor(hdc, CLR_BTN_TEXT);
    DrawTextA(hdc, tag_text, -1, &tag_rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    /* ── Command text (monospace, left of tag) ────────────────────── */
    const char *cmd_text = item->u.cmd.command ? item->u.cmd.command
                                               : item->text;
    if (item->u.cmd.blocked)
        SetTextColor(hdc, CLR_BLOCKED_TEXT);
    else
        SetTextColor(hdc, RGB_FROM_THEME(tc->cmd_text));

    SelectObject(hdc, lv->hMonoFont ? lv->hMonoFont
                                     : GetStockObject(ANSI_FIXED_FONT));
    RECT text_rc;
    text_rc.left   = rc->left + code_pad;
    text_rc.top    = rc->top + code_pad;
    text_rc.right  = tag_rc.left - CLV_SCALE(lv, 4);
    text_rc.bottom = rc->bottom - CLV_SCALE(lv, 22) - CLV_SCALE(lv, 2);
    draw_text_utf8(hdc, cmd_text, &text_rc, DT_WORDBREAK | DT_NOPREFIX);

    /* ── Status row ───────────────────────────────────────────────── */
    int status_top = rc->bottom - CLV_SCALE(lv, 20) - CLV_SCALE(lv, 4);
    int status_h   = CLV_SCALE(lv, 20);

    SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                     : GetStockObject(DEFAULT_GUI_FONT));

    if (item->u.cmd.blocked) {
        /* Lock icon + "Blocked" */
        if (lv->hIconFont) {
            SelectObject(hdc, lv->hIconFont);
            SetTextColor(hdc, CLR_BLOCKED_TEXT);
            RECT lock_rc = { rc->left + code_pad, status_top,
                             rc->left + code_pad + CLV_SCALE(lv, 18),
                             status_top + status_h };
            DrawTextW(hdc, L"\xE72E", 1, &lock_rc,
                      DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        }
        SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                         : GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(hdc, RGB_FROM_THEME(tc->indicator_red));
        RECT blk_rc = { rc->left + code_pad + CLV_SCALE(lv, 20), status_top,
                        rc->right - code_pad, status_top + status_h };
        DrawTextA(hdc, "Blocked", -1, &blk_rc,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    } else if (item->u.cmd.approved == -1) {
        /* Checkbox */
        int chk_sz = CLV_SCALE(lv, 14);
        int chk_x  = rc->left + code_pad;
        int chk_y  = status_top + (status_h - chk_sz) / 2;
        RECT chk_rc = { chk_x, chk_y, chk_x + chk_sz, chk_y + chk_sz };

        if (item->u.cmd.selected) {
            fill_rounded_rect(hdc, &chk_rc, CLV_SCALE(lv, 3), CLR_CHK_FILL);
            SetTextColor(hdc, CLR_BTN_TEXT);
            DrawTextW(hdc, L"\x2713", 1, &chk_rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            draw_ghost_rect(hdc, &chk_rc, CLV_SCALE(lv, 3), CLR_CHK_BORDER);
        }

        SetTextColor(hdc, CLR_CHK_BORDER);
        RECT lbl_rc = { chk_x + chk_sz + CLV_SCALE(lv, 4), status_top,
                        rc->right - code_pad, status_top + status_h };
        DrawTextA(hdc, item->u.cmd.selected ? "Selected" : "Select",
                  -1, &lbl_rc,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    } else {
        const char *status;
        COLORREF status_color;
        if (item->u.cmd.approved == 1) {
            status = "Approved";
            status_color = RGB_FROM_THEME(tc->indicator_green);
        } else {
            status = "Denied";
            status_color = RGB_FROM_THEME(tc->indicator_red);
        }
        SetTextColor(hdc, status_color);
        RECT sts_rc = { rc->left + code_pad, status_top,
                        rc->right - code_pad, status_top + status_h };
        DrawTextA(hdc, status, -1, &sts_rc,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }

    SelectObject(hdc, old_font);
}

/* ── Paint the grouped command container: outer box, scrollable cards,
 *    themed scrollbar, and action buttons below. ──────────────────── */

static void paint_cmd_container(ChatListView *lv, HDC hdc, RECT *rc)
{
    const ThemeChatColors *tc = &lv->theme->chat;
    int side_pad = CLV_SCALE(lv, BASE_SIDE_PAD);
    int border_w = CLV_SCALE(lv, 1);
    int corner   = CLV_SCALE(lv, BASE_CORNER_R);
    int pad      = CLV_SCALE(lv, 6);
    int card_gap = CLV_SCALE(lv, BASE_CMD_CARD_GAP);
    int sb_w     = CLV_SCALE(lv, BASE_SCROLLBAR_W);
    int code_pad = lv->code_pad;

    int dummy_total, pending_cmds;
    count_commands(lv->msg_list, &dummy_total, &pending_cmds);
    int action_h = (pending_cmds > 0)
                 ? CLV_SCALE(lv, 4) + CLV_SCALE(lv, BASE_ALLOW_ALL_H) : 0;

    int needs_scroll = (lv->cmd_total_h > lv->cmd_visible_h);

    /* ── Container box (excludes action buttons) ──────────────────── */
    RECT box;
    box.left   = rc->left + side_pad;
    box.top    = rc->top;
    box.right  = rc->right - side_pad;
    box.bottom = rc->bottom - action_h;

    /* Background */
    HBRUSH bg_br = CreateSolidBrush(RGB_FROM_THEME(tc->cmd_bg));
    FillRect(hdc, &box, bg_br);
    DeleteObject(bg_br);

    /* Border */
    HPEN border_pen = CreatePen(PS_SOLID, border_w,
                                RGB_FROM_THEME(tc->cmd_border));
    HGDIOBJ old_pen = SelectObject(hdc, border_pen);
    HGDIOBJ old_br  = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, box.left, box.top, box.right, box.bottom, corner, corner);
    SelectObject(hdc, old_br);
    SelectObject(hdc, old_pen);
    DeleteObject(border_pen);

    /* ── Clipped scroll region for cards ──────────────────────────── */
    RECT clip;
    clip.left   = box.left + border_w;
    clip.top    = box.top + border_w;
    clip.right  = box.right - border_w;
    clip.bottom = box.bottom - border_w;

    int saved_dc = SaveDC(hdc);
    IntersectClipRect(hdc, clip.left, clip.top, clip.right, clip.bottom);

    /* Card content area (narrower if scrollbar shown) */
    int card_right = needs_scroll ? (clip.right - sb_w - CLV_SCALE(lv, 3))
                                  : clip.right;

    /* Walk all command items and paint them within the clipped region */
    int card_y = box.top + pad - lv->cmd_scroll_y;
    int ci = 0;
    ChatMsgItem *cmd = lv->msg_list ? lv->msg_list->head : NULL;
    while (cmd) {
        if (cmd->type == CHAT_ITEM_COMMAND && !cmd->u.cmd.settled && ci < lv->cmd_count) {
            /* Divider line between cards */
            if (ci > 0) {
                RECT div_rc = { clip.left + code_pad, card_y - card_gap,
                                card_right - code_pad, card_y };
                HBRUSH div_br = CreateSolidBrush(RGB_FROM_THEME(tc->cmd_border));
                FillRect(hdc, &div_rc, div_br);
                DeleteObject(div_br);
            }

            int card_h = lv->cmd_heights[ci];
            RECT card_rc = { clip.left, card_y, card_right, card_y + card_h };
            paint_cmd_card(lv, hdc, cmd, &card_rc);

            card_y += card_h + card_gap;
            ci++;
        }
        cmd = cmd->next;
    }

    RestoreDC(hdc, saved_dc);

    /* ── Themed scrollbar (inside box, right edge) ────────────────── */
    if (needs_scroll) {
        int track_left  = box.right - border_w - sb_w - CLV_SCALE(lv, 2);
        int track_top   = box.top + pad;
        int track_bot   = box.bottom - pad;
        int track_h     = track_bot - track_top;

        /* Track */
        RECT track_rc = { track_left, track_top,
                          track_left + sb_w, track_bot };
        fill_rounded_rect(hdc, &track_rc, sb_w / 2,
                          RGB_FROM_THEME(tc->cmd_border));

        /* Thumb */
        int max_cmd_scroll = lv->cmd_total_h - lv->cmd_visible_h;
        if (max_cmd_scroll < 1) max_cmd_scroll = 1;
        int thumb_h = track_h * lv->cmd_visible_h / lv->cmd_total_h;
        if (thumb_h < CLV_SCALE(lv, 20)) thumb_h = CLV_SCALE(lv, 20);
        int thumb_y = track_top +
            (lv->cmd_scroll_y * (track_h - thumb_h)) / max_cmd_scroll;
        RECT thumb_rc = { track_left, thumb_y,
                          track_left + sb_w, thumb_y + thumb_h };
        fill_rounded_rect(hdc, &thumb_rc, sb_w / 2,
                          RGB_FROM_THEME(lv->theme->accent));
    }

    /* ── Action buttons: Allow Selected | Cancel | Allow All ──────── */
    if (pending_cmds > 0) {
        int ab_h   = CLV_SCALE(lv, BASE_ALLOW_ALL_H);
        int ab_y   = box.bottom + CLV_SCALE(lv, 4);
        int ab_w   = CLV_SCALE(lv, 120);
        int ab_gap = CLV_SCALE(lv, 6);
        int ab_x   = box.left;

        SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ old_font = SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                            : GetStockObject(DEFAULT_GUI_FONT));

        /* Allow Selected (pastel green) */
        RECT sel_rc = { ab_x, ab_y, ab_x + ab_w, ab_y + ab_h };
        fill_rounded_rect(hdc, &sel_rc, corner, CLR_BTN_ALLOW_SEL);
        SetTextColor(hdc, RGB(30, 30, 30));
        {
            int nsel = count_selected(lv->msg_list);
            char sel_text[48];
            if (nsel > 0)
                snprintf(sel_text, sizeof(sel_text),
                         "Allow Selected (%d)", nsel);
            else
                snprintf(sel_text, sizeof(sel_text), "Allow Selected");
            DrawTextA(hdc, sel_text, -1, &sel_rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        /* Cancel (red) */
        int cx = ab_x + ab_w + ab_gap;
        RECT can_rc = { cx, ab_y, cx + CLV_SCALE(lv, 80), ab_y + ab_h };
        fill_rounded_rect(hdc, &can_rc, corner, CLR_BTN_CANCEL);
        SetTextColor(hdc, CLR_BTN_TEXT);
        DrawTextA(hdc, "Cancel", -1, &can_rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        /* Allow All (pastel orange) */
        int ax = can_rc.right + ab_gap;
        RECT all_rc = { ax, ab_y, ax + CLV_SCALE(lv, 100), ab_y + ab_h };
        fill_rounded_rect(hdc, &all_rc, corner, CLR_BTN_ALLOW_ALL);
        SetTextColor(hdc, RGB(30, 30, 30));
        DrawTextA(hdc, "Allow All", -1, &all_rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        SelectObject(hdc, old_font);
    }
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

/* ── Activity indicator colour from health status ───────────────────── */

static COLORREF activity_health_color(const ChatListView *lv, HealthStatus h)
{
    const ThemeChatColors *tc = &lv->theme->chat;
    switch (h) {
    case HEALTH_YELLOW: return RGB_FROM_THEME(tc->indicator_yellow);
    case HEALTH_RED:    return RGB_FROM_THEME(tc->indicator_red);
    default:            return RGB_FROM_THEME(tc->indicator_green);
    }
}

/* Blend colour towards background for half-pulse effect */
static COLORREF blend_with_bg(COLORREF fg, COLORREF bg_clr)
{
    int r = (GetRValue(fg) + GetRValue(bg_clr)) / 2;
    int g = (GetGValue(fg) + GetGValue(bg_clr)) / 2;
    int b = (GetBValue(fg) + GetBValue(bg_clr)) / 2;
    return RGB(r, g, b);
}

/* Paint the inline activity indicator below the last message.
 * Returns the height consumed (0 if idle). */
static int paint_activity_indicator(ChatListView *lv, HDC hdc,
                                     int y, int cw)
{
    if (!lv->activity || lv->activity->phase == ACTIVITY_IDLE
        || lv->activity->phase == ACTIVITY_THINKING)
        return 0;

    int act_h  = CLV_SCALE(lv, BASE_ACTIVITY_H);
    int dot_sz = CLV_SCALE(lv, BASE_DOT_SIZE);
    int pad    = CLV_SCALE(lv, BASE_SIDE_PAD);
    int indent = CLV_SCALE(lv, BASE_AI_INDENT);

    COLORREF bg_clr = RGB_FROM_THEME(lv->theme->bg_primary);
    COLORREF dot_clr = activity_health_color(lv, lv->activity->health);
    if (lv->pulse_toggle)
        dot_clr = blend_with_bg(dot_clr, bg_clr);

    /* Draw pulsing dot */
    int dot_x = pad + indent;
    int dot_y = y + (act_h - dot_sz) / 2;
    HBRUSH dot_br = CreateSolidBrush(dot_clr);
    HPEN   dot_pen = CreatePen(PS_SOLID, 1, dot_clr);
    HGDIOBJ old_br  = SelectObject(hdc, dot_br);
    HGDIOBJ old_pen = SelectObject(hdc, dot_pen);
    Ellipse(hdc, dot_x, dot_y, dot_x + dot_sz, dot_y + dot_sz);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_br);
    DeleteObject(dot_pen);
    DeleteObject(dot_br);

    /* Draw status text */
    char status_buf[128];
    float now = (float)GetTickCount() / 1000.0f;
    chat_activity_format(lv->activity, now, status_buf, sizeof(status_buf));

    /* Use a muted version of the dot colour for text readability */
    COLORREF text_clr = RGB(
        (GetRValue(dot_clr) * 3 + GetRValue(bg_clr)) / 4,
        (GetGValue(dot_clr) * 3 + GetGValue(bg_clr)) / 4,
        (GetBValue(dot_clr) * 3 + GetBValue(bg_clr)) / 4);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text_clr);
    HGDIOBJ old_font = SelectObject(hdc, lv->hSmallFont
                                         ? lv->hSmallFont
                                         : GetStockObject(DEFAULT_GUI_FONT));
    RECT text_rc;
    text_rc.left   = dot_x + dot_sz + CLV_SCALE(lv, 6);
    text_rc.top    = y;
    text_rc.right  = cw - pad;
    text_rc.bottom = y + act_h;
    DrawTextA(hdc, status_buf, -1, &text_rc,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    /* Draw [Retry] link when stalled */
    if (lv->activity->health == HEALTH_RED) {
        SIZE sz;
        GetTextExtentPoint32A(hdc, status_buf, (int)strlen(status_buf), &sz);
        int retry_x = text_rc.left + sz.cx + CLV_SCALE(lv, 10);
        SetTextColor(hdc, CLR_RETRY_TEXT);
        RECT retry_rc;
        retry_rc.left   = retry_x;
        retry_rc.top    = y;
        retry_rc.right  = cw - pad;
        retry_rc.bottom = y + act_h;
        DrawTextA(hdc, "[Retry]", -1, &retry_rc,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }

    SelectObject(hdc, old_font);
    return act_h;
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

        /* Skip hidden items (h=0 command items absorbed by container) */
        if (h == 0) {
            item = item->next;
            continue;
        }

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
                if (item->u.cmd.settled)
                    paint_cmd_card(lv, mem_dc, item, &item_rc);
                else
                    paint_cmd_container(lv, mem_dc, &item_rc);
                break;
            case CHAT_ITEM_STATUS:
                paint_status_item(lv, mem_dc, item, &item_rc);
                break;
            }

            /* Selection highlight: invert colours for items in selection */
            if (lv->sel_valid) {
                int content_y = y + lv->scroll_y;
                if (item_in_selection(lv, content_y, h)) {
                    RECT sel_rc;
                    sel_rc.left   = 0;
                    sel_rc.right  = cw;
                    sel_rc.top    = y;
                    sel_rc.bottom = y + h;
                    /* Clamp to viewport */
                    if (sel_rc.top < 0) sel_rc.top = 0;
                    if (sel_rc.bottom > ch) sel_rc.bottom = ch;
                    if (sel_rc.top < sel_rc.bottom)
                        InvertRect(mem_dc, &sel_rc);
                }
            }
        }

        /* Stop if we've gone past the viewport */
        if (y > ch) break;

        y += h + lv->msg_gap;
        item = item->next;
    }

    /* Paint inline activity indicator below the last message */
    if (y <= ch)
        paint_activity_indicator(lv, mem_dc, y, cw);

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

static int on_lbuttondown(ChatListView *lv, int mx, int my)
{
    SetFocus(lv->hwnd);   /* Acquire keyboard focus so WM_KEYDOWN fires */
    int side_pad = CLV_SCALE(lv, BASE_SIDE_PAD);
    int code_pad = lv->code_pad;

    int total_cmds, pending_cmds;
    count_commands(lv->msg_list, &total_cmds, &pending_cmds);

    /* Walk items to find which one was clicked */
    int y = lv->msg_gap - lv->scroll_y;
    ChatMsgItem *item = lv->msg_list ? lv->msg_list->head : NULL;
    HWND parent = GetParent(lv->hwnd);

    while (item) {
        int h = item->measured_height;

        /* Skip hidden items */
        if (h == 0) {
            item = item->next;
            continue;
        }

        /* Check click on [Cancel] for queued user messages */
        if (item->type == CHAT_ITEM_USER && item->queued
            && my >= y && my < y + h) {
            int label_top = y + h - CLV_SCALE(lv, 16);
            if (my >= label_top) {
                PostMessage(parent, WM_COMMAND,
                            MAKEWPARAM(IDC_QUEUE_CANCEL, 0), 0);
                return 1;
            }
        }

        /* Check click on thinking toggle header for AI items */
        if (item->type == CHAT_ITEM_AI_TEXT && my >= y && my < y + h
            && item->u.ai.thinking_text) {
            int icon_sz = CLV_SCALE(lv, BASE_ICON_SIZE);
            int hdr_h = CLV_SCALE(lv, BASE_THINK_HDR_H);
            int click_h = hdr_h + 2 * lv->code_pad;
            int hdr_top = y + icon_sz + CLV_SCALE(lv, 4);
            int think_left = side_pad + lv->ai_indent;

            if (my >= hdr_top && my < hdr_top + click_h &&
                mx >= think_left) {
                item->u.ai.thinking_collapsed =
                    !item->u.ai.thinking_collapsed;
                if (item->u.ai.thinking_collapsed) {
                    item->u.ai.thinking_scroll_y = 0;
                } else {
                    item->u.ai.thinking_autoscroll = 1;
                }
                item->dirty = 1;
                recalc_layout(lv);
                InvalidateRect(lv->hwnd, NULL, FALSE);
                return 1;
            }
        }

        /* ── Command container hit testing ─────────────────────────── */
        if (item->type == CHAT_ITEM_COMMAND && !item->u.cmd.settled && my >= y && my < y + h) {
            int pad      = CLV_SCALE(lv, 6);
            int card_gap = CLV_SCALE(lv, BASE_CMD_CARD_GAP);
            int sb_w     = CLV_SCALE(lv, BASE_SCROLLBAR_W);
            int action_h = (pending_cmds > 0)
                         ? CLV_SCALE(lv, 4) + CLV_SCALE(lv, BASE_ALLOW_ALL_H)
                         : 0;

            RECT client_rc;
            GetClientRect(lv->hwnd, &client_rc);
            int cw2 = client_rc.right - client_rc.left;

            int box_left  = side_pad + side_pad;
            int box_right = cw2 - side_pad - side_pad;
            int box_top   = y;
            int box_bot   = y + h - action_h;

            /* ── Scrollbar click ──────────────────────────────────── */
            int needs_scroll = (lv->cmd_total_h > lv->cmd_visible_h);
            if (needs_scroll) {
                int sb_right = box_right - CLV_SCALE(lv, 2);
                int sb_left  = sb_right - sb_w;
                if (mx >= sb_left && mx <= sb_right &&
                    my >= box_top + pad && my < box_bot - pad) {
                    int track_h = lv->cmd_visible_h;
                    int max_cs = lv->cmd_total_h - lv->cmd_visible_h;
                    int rel = my - box_top - pad;
                    lv->cmd_scroll_y = (rel * max_cs) / track_h;
                    if (lv->cmd_scroll_y < 0) lv->cmd_scroll_y = 0;
                    if (lv->cmd_scroll_y > max_cs) lv->cmd_scroll_y = max_cs;
                    InvalidateRect(lv->hwnd, NULL, FALSE);
                    return 1;
                }
            }

            /* ── Action buttons below container ───────────────────── */
            if (pending_cmds > 0 && my >= box_bot) {
                int ab_h   = CLV_SCALE(lv, BASE_ALLOW_ALL_H);
                int ab_y2  = box_bot + CLV_SCALE(lv, 4);
                int ab_w2  = CLV_SCALE(lv, 120);
                int ab_gap = CLV_SCALE(lv, 6);

                /* Allow Selected */
                if (mx >= box_left && mx < box_left + ab_w2 &&
                    my >= ab_y2 && my < ab_y2 + ab_h) {
                    if (parent)
                        PostMessage(parent, WM_COMMAND,
                                    MAKEWPARAM(IDC_CMD_APPROVE_SEL, 0), 0);
                    return 1;
                }
                /* Cancel */
                int cx2 = box_left + ab_w2 + ab_gap;
                int cw3 = CLV_SCALE(lv, 80);
                if (mx >= cx2 && mx < cx2 + cw3 &&
                    my >= ab_y2 && my < ab_y2 + ab_h) {
                    if (parent)
                        PostMessage(parent, WM_COMMAND,
                                    MAKEWPARAM(IDC_CMD_CANCEL_ALL, 0), 0);
                    return 1;
                }
                /* Allow All */
                int ax2 = cx2 + cw3 + ab_gap;
                int aw2 = CLV_SCALE(lv, 100);
                if (mx >= ax2 && mx < ax2 + aw2 &&
                    my >= ab_y2 && my < ab_y2 + ab_h) {
                    if (parent)
                        PostMessage(parent, WM_COMMAND,
                                    MAKEWPARAM(IDC_CMD_APPROVE_ALL, 0), 0);
                    return 1;
                }
                return 0;
            }

            /* ── Card click (checkbox) — walk cards with scroll offset */
            {
                int card_y2 = box_top + pad - lv->cmd_scroll_y;
                int ci = 0;
                ChatMsgItem *cmd2 = lv->msg_list ? lv->msg_list->head : NULL;
                while (cmd2) {
                    if (cmd2->type == CHAT_ITEM_COMMAND && !cmd2->u.cmd.settled && ci < lv->cmd_count) {
                        int card_h = lv->cmd_heights[ci];
                        /* Only if click is within visible container area */
                        if (my >= card_y2 && my < card_y2 + card_h &&
                            my >= box_top + pad && my < box_bot - pad) {
                            /* Check checkbox area */
                            if (!cmd2->u.cmd.blocked &&
                                cmd2->u.cmd.approved == -1) {
                                int st = card_y2 + card_h -
                                         CLV_SCALE(lv, 20) - CLV_SCALE(lv, 4);
                                int chk_sz = CLV_SCALE(lv, 14);
                                int chk_x = box_left + code_pad;
                                if (mx >= chk_x &&
                                    mx < chk_x + chk_sz + CLV_SCALE(lv, 60) &&
                                    my >= st &&
                                    my < st + CLV_SCALE(lv, 20)) {
                                    cmd2->u.cmd.selected =
                                        !cmd2->u.cmd.selected;
                                    InvalidateRect(lv->hwnd, NULL, FALSE);
                                    return 1;
                                }
                            }
                            return 0;  /* Click in card but not interactive */
                        }
                        card_y2 += card_h + card_gap;
                        ci++;
                    }
                    cmd2 = cmd2->next;
                }
            }
            return 0;
        }

        y += h + lv->msg_gap;
        item = item->next;
    }

    /* Check click on [Retry] link in the activity indicator */
    if (lv->activity && lv->activity->phase != ACTIVITY_IDLE &&
        lv->activity->health == HEALTH_RED) {
        int act_h = CLV_SCALE(lv, BASE_ACTIVITY_H);
        if (my >= y && my < y + act_h) {
            HWND par = GetParent(lv->hwnd);
            if (par)
                PostMessage(par, WM_COMMAND,
                            MAKEWPARAM(IDC_ACTIVITY_RETRY, 0), 0);
            return 1;
        }
    }
    return 0;  /* No interactive element was clicked */
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

        /* Detect DPI using shared helper (tries GetDpiForWindow, falls back to GetDeviceCaps) */
        {
            UINT dpi = (UINT)get_window_dpi(hwnd);
            lv->dpi_scale = (float)dpi / 96.0f;
        }

        recalc_dpi_constants(lv);

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
        if (on_lbuttondown(lv, mx, my)) {
            sel_clear(lv);
            return 0;  /* A button/link was clicked */
        }
        /* Start text selection drag */
        sel_clear(lv);
        lv->sel_start_y = my + lv->scroll_y;
        lv->sel_end_y   = lv->sel_start_y;
        lv->sel_active  = 1;
        SetCapture(hwnd);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!lv->sel_active) break;
        int my = GET_Y_LPARAM(lParam);
        lv->sel_end_y = my + lv->scroll_y;
        /* Auto-scroll when dragging near edges */
        if (my < 0) {
            lv->scroll_y += my;  /* my is negative, scrolls up */
            clamp_scroll(lv);
            update_scrollbar(lv);
        } else if (my > lv->viewport_height) {
            lv->scroll_y += my - lv->viewport_height;
            clamp_scroll(lv);
            update_scrollbar(lv);
        }
        int sy = lv->sel_start_y, ey = lv->sel_end_y;
        if (sy > ey) { int tmp = sy; sy = ey; ey = tmp; }
        lv->sel_valid = (ey - sy > 4);  /* small threshold to avoid accidental select */
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_LBUTTONUP: {
        if (!lv->sel_active) break;
        ReleaseCapture();
        lv->sel_active = 0;
        int my = GET_Y_LPARAM(lParam);
        lv->sel_end_y = my + lv->scroll_y;
        int sy = lv->sel_start_y, ey = lv->sel_end_y;
        if (sy > ey) { int tmp = sy; sy = ey; ey = tmp; }
        lv->sel_valid = (ey - sy > 4);
        if (lv->sel_valid) {
            /* Auto-copy to clipboard on mouse release (like the terminal) */
            sel_copy_to_clipboard(lv);
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_RBUTTONDOWN: {
        /* Right-click: paste clipboard into parent's input field */
        HWND par = GetParent(hwnd);
        if (par)
            PostMessage(par, WM_COMMAND,
                        MAKEWPARAM(IDC_CHATLIST_PASTE, 0), 0);
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
            if (lv->ext_scrollbar) {
                lv->scroll_y = csb_get_trackpos(lv->ext_scrollbar);
            } else {
                SCROLLINFO si;
                memset(&si, 0, sizeof(si));
                si.cbSize = sizeof(si);
                si.fMask  = SIF_TRACKPOS;
                GetScrollInfo(hwnd, SB_VERT, &si);
                lv->scroll_y = si.nTrackPos;
            }
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

        /* Check if cursor is over an expanded thinking region */
        {
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ScreenToClient(hwnd, &pt);

            int wy = lv->msg_gap - lv->scroll_y;
            ChatMsgItem *wi = lv->msg_list ? lv->msg_list->head : NULL;
            while (wi) {
                int wh = wi->measured_height;
                if (wi->type == CHAT_ITEM_AI_TEXT && wi->u.ai.thinking_text
                    && !wi->u.ai.thinking_collapsed
                    && pt.y >= wy && pt.y < wy + wh) {
                    /* Compute thinking content region bounds */
                    int icon_sz = CLV_SCALE(lv, BASE_ICON_SIZE);
                    int hdr_h = CLV_SCALE(lv, BASE_THINK_HDR_H);
                    int think_top = wy + icon_sz + CLV_SCALE(lv, 4)
                                    + hdr_h + CLV_SCALE(lv, 4);
                    int side_pad2 = CLV_SCALE(lv, BASE_SIDE_PAD);
                    int think_left = side_pad2 + lv->ai_indent;

                    /* Measure full content to check overflow */
                    HDC tdc = GetDC(hwnd);
                    HGDIOBJ tf = SelectObject(tdc, lv->hSmallFont
                                 ? lv->hSmallFont
                                 : GetStockObject(DEFAULT_GUI_FONT));
                    int border_w = CLV_SCALE(lv, BASE_BORDER_W);
                    RECT crc;
                    GetClientRect(hwnd, &crc);
                    int tw = crc.right - think_left - side_pad2
                             - border_w - CLV_SCALE(lv, 8);
                    if (tw < 20) tw = 20;
                    RECT mr;
                    SetRect(&mr, 0, 0, tw, 0);
                    draw_text_utf8(tdc, wi->u.ai.thinking_text, &mr,
                                   DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
                    int full_h = mr.bottom - mr.top;
                    SelectObject(tdc, tf);
                    ReleaseDC(hwnd, tdc);

                    int max_h = CLV_SCALE(lv, BASE_THINK_MAX_H);
                    int min_h = CLV_SCALE(lv, BASE_THINK_MIN_H);
                    int vis_h = full_h;
                    if (vis_h < min_h) vis_h = min_h;
                    if (vis_h > max_h) vis_h = max_h;

                    /* Is cursor in the thinking content area and is there overflow? */
                    if (full_h > vis_h && pt.x >= think_left
                        && pt.y >= think_top
                        && pt.y < think_top + vis_h) {
                        int max_scroll = full_h - vis_h;
                        int old_sy = wi->u.ai.thinking_scroll_y;
                        wi->u.ai.thinking_scroll_y +=
                            (-delta * scroll_amount) / WHEEL_DELTA;
                        if (wi->u.ai.thinking_scroll_y < 0)
                            wi->u.ai.thinking_scroll_y = 0;
                        if (wi->u.ai.thinking_scroll_y > max_scroll)
                            wi->u.ai.thinking_scroll_y = max_scroll;
                        /* Auto-scroll: disengage on scroll-up,
                         * re-engage when user reaches bottom */
                        if (wi->u.ai.thinking_scroll_y >= max_scroll)
                            wi->u.ai.thinking_autoscroll = 1;
                        else
                            wi->u.ai.thinking_autoscroll = 0;
                        if (wi->u.ai.thinking_scroll_y != old_sy) {
                            InvalidateRect(hwnd, NULL, FALSE);
                            return 0;  /* consumed by thinking scroll */
                        }
                        /* At boundary — bubble to parent list scroll */
                        break;
                    }
                    break;
                }
                wy += wh + lv->msg_gap;
                wi = wi->next;
            }
        }

        /* Check if cursor is over the command container */
        if (lv->cmd_count > 0 && lv->cmd_total_h > lv->cmd_visible_h) {
            POINT cpt;
            cpt.x = GET_X_LPARAM(lParam);
            cpt.y = GET_Y_LPARAM(lParam);
            ScreenToClient(hwnd, &cpt);

            int wy2 = lv->msg_gap - lv->scroll_y;
            ChatMsgItem *wi2 = lv->msg_list ? lv->msg_list->head : NULL;
            while (wi2) {
                int wh2 = wi2->measured_height;
                if (wh2 == 0) { wi2 = wi2->next; continue; }
                if (wi2->type == CHAT_ITEM_COMMAND &&
                    is_first_command(lv->msg_list, wi2) &&
                    cpt.y >= wy2 && cpt.y < wy2 + wh2) {
                    int max_cs = lv->cmd_total_h - lv->cmd_visible_h;
                    int old_cs = lv->cmd_scroll_y;
                    lv->cmd_scroll_y -= (delta * scroll_amount) / WHEEL_DELTA;
                    if (lv->cmd_scroll_y < 0) lv->cmd_scroll_y = 0;
                    if (lv->cmd_scroll_y > max_cs) lv->cmd_scroll_y = max_cs;
                    if (lv->cmd_scroll_y != old_cs) {
                        InvalidateRect(hwnd, NULL, FALSE);
                        return 0;
                    }
                    break;
                }
                wy2 += wh2 + lv->msg_gap;
                wi2 = wi2->next;
            }
        }

        int old_pos = lv->scroll_y;
        lv->scroll_y -= (delta * scroll_amount) / WHEEL_DELTA;
        clamp_scroll(lv);

        if (lv->scroll_y != old_pos) {
            update_scrollbar(lv);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_DPICHANGED: {
        UINT new_dpi = HIWORD(wParam);   /* new DPI value */
        lv->dpi_scale = (float)new_dpi / 96.0f;
        recalc_dpi_constants(lv);
        /* Use the suggested window rect provided by the system */
        RECT *suggested = (RECT *)lParam;
        SetWindowPos(hwnd, NULL,
                     suggested->left, suggested->top,
                     suggested->right  - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        recalc_layout(lv);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_KEYDOWN: {
        /* Ctrl+C: copy selection to clipboard */
        if (wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (lv->sel_valid)
                sel_copy_to_clipboard(lv);
            return 0;
        }
        /* Ctrl+A: select all */
        if (wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            sel_select_all(lv);
            return 0;
        }
        /* Escape: clear selection */
        if (wParam == VK_ESCAPE && lv->sel_valid) {
            sel_clear(lv);
            return 0;
        }

        int old_pos = lv->scroll_y;
        int line_h  = lv->msg_gap > 0 ? lv->msg_gap * 3 : 36;

        switch (wParam) {
        case VK_PRIOR:   /* Page Up */
            lv->scroll_y -= lv->viewport_height;
            break;
        case VK_NEXT:    /* Page Down */
            lv->scroll_y += lv->viewport_height;
            break;
        case VK_HOME:
            lv->scroll_y = 0;
            break;
        case VK_END: {
            int max_s = lv->total_height - lv->viewport_height;
            lv->scroll_y = max_s > 0 ? max_s : 0;
            break;
        }
        case VK_UP:
            lv->scroll_y -= line_h;
            break;
        case VK_DOWN:
            lv->scroll_y += line_h;
            break;
        default:
            break;
        }

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
