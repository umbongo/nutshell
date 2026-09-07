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
#include "md_render.h"
#include "icons.h"
#include "ns_draw.h"
#include "ns_scale.h"
#include "ns_type.h"
#include "ns_layout.h"
#include "ns_tokens.h"
#include "ns_hover.h"
#include "ns_reduced_motion.h"
#include "chat_approval.h"
#include "ai_panel_layout.h"
#include "ai_panel_states.h"
#include "ai_prompt.h"
#include "ns_font.h"
#include "stick_scroll.h"
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

/* Integer DPI for ns_scale()/ns_draw_*() calls; ChatListView only keeps a
 * float dpi_scale (1.0 = 96 DPI) today. The old CLV_SCALE(lv, px) macro has
 * been replaced throughout by ns_scale(px, CLV_DPI(lv)) -- the one
 * DPI-scaling helper for the whole UI (Design-System Foundation, task 10). */
#define CLV_DPI(lv) ((int)((lv)->dpi_scale * 96.0f + 0.5f))

/* ── Base layout constants (96 DPI) ─────────────────────────────────── */

#define BASE_MSG_GAP      12
#define BASE_USER_PAD_H   10
#define BASE_USER_PAD_V    8
#define BASE_AI_INDENT    30
#define BASE_CODE_PAD      6
#define BASE_ICON_SIZE    20   /* AI avatar circle diameter */
#define BASE_CORNER_R      6   /* User bubble corner radius */
#define BASE_SIDE_PAD      8   /* Left/right margin for the whole panel */

/* The Thinking disclosure's row/chevron/label/summary/body geometry comes
 * from ai_panel_layout's thinking_layout() (Design-System Foundation +
 * AI Assist Panel, task 1/2) — see build_thinking_layout() below. */

/* ── Command container chrome constants (96 DPI) ─────────────────────
 * Row/tag/button/gap sizing for the approval card itself now comes from
 * ns_layout's approval_card_layout(); only the outer container chrome
 * (border box, scrollbar) is still local to this file. */

#define BASE_SCROLLBAR_W    6   /* Custom scrollbar width */

/* Colours for the approval card and chat text now come from ns_tokens()
 * (Design-System Foundation, task 5) — see paint_cmd_row/paint_cmd_container
 * and draw_ai_text_with_exec/paint_activity_indicator below. */

/* Height of the inline activity indicator line (96 DPI base) */
#define BASE_ACTIVITY_H   28
#define BASE_DOT_SIZE       8

/* ── ns_hover element ids ─────────────────────────────────────────────
 * One NsHover per list view (lv->hover) tracks every painted, clickable
 * element: approval-card rows, the card-wide Deny all / Run N selected
 * actions, the [Retry] link in the activity indicator, and every AI
 * item's Thinking disclosure row. Ids are shared between paint (which
 * knows what it just drew) and the hover hit-test (which re-derives the
 * same geometry via approval_card_layout/approval_card_hit or
 * build_thinking_layout() -- see chatlv_hover_hit() / chatlv_hover_rect_for_id()
 * below), so the two can never drift apart.
 *
 *   row element     -> row * 16 + HIT_* (HIT_TAG/HIT_TEXT/HIT_CHECKBOX)
 *   card action     -> APPROVAL_MAX_CMDS * 16 + HIT_* (HIT_DENY_ALL/
 *                      HIT_RUN_SELECTED, which have no row -- approval_card_hit
 *                      reports -1)
 *   [Retry] link    -> CLV_HOVER_RETRY, clear of the row/card id range
 *   Thinking row    -> CLV_THINK_HOVER_ID(item->id), clear of every id above
 *                      (ChatMsgItem ids are unique and only ever grow, so a
 *                      fixed base above CLV_HOVER_RETRY never collides)
 */
#define CLV_ROW_HIT_ID(row, hit)  ((row) * 16 + (hit))
#define CLV_CARD_HIT_ID(hit)      (APPROVAL_MAX_CMDS * 16 + (hit))
#define CLV_HOVER_RETRY           (APPROVAL_MAX_CMDS * 16 + 16)
#define CLV_THINK_HOVER_BASE      (CLV_HOVER_RETRY + 1)
#define CLV_THINK_HOVER_ID(iid)   (CLV_THINK_HOVER_BASE + (iid))

/* Empty/no-key/no-session state (ai_panel_states.h) -- three suggestion
 * chips or a single action button, painted by paint_empty_state() only
 * when the message list has zero items. No other painted element can
 * exist while this one shows (the list is empty), so a dedicated range
 * far above every id above never collides with it. */
#define CLV_STATE_CHIP_BASE      0x10000
#define CLV_STATE_CHIP_HIT(i)    (CLV_STATE_CHIP_BASE + (i))
#define CLV_STATE_BUTTON_HIT     (CLV_STATE_CHIP_BASE + 100)

/* Geometry for the empty/no-key/no-session state, shared by
 * paint_empty_state() and chatlv_empty_state_hit()/
 * chatlv_hover_rect_for_id() so painting and hit-testing never drift
 * apart. Built fresh from ai_panel_state()/ai_panel_state_body() on every
 * call -- cheap, and always correct after a resize or state change. */
typedef struct {
    RECT icon;
    RECT title;
    RECT body;
    int  is_button;   /* 1 = single action button (NO_KEY/NO_SESSION) */
    RECT button;
    int  n_chips;     /* 0..3, only when !is_button (AI_STATE_EMPTY) */
    RECT chip[3];
} EmptyStateLayout;

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
static void     paint_cmd_container(ChatListView *lv, HDC hdc, RECT *rc);
static void     build_thinking_layout(ChatListView *lv, HDC hdc,
                              ChatMsgItem *item, int box_left, int box_right,
                              int content_top, ThinkingLayout *out,
                              int *out_full_body_h);
static void     paint_cmd_row(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                              const ApprovalRowLayout *row, int row_idx);
static void     paint_cmd_settled_row(ChatListView *lv, HDC hdc,
                              ChatMsgItem *item, RECT *rc);
static void     paint_status_item(ChatListView *lv, HDC hdc,
                                  ChatMsgItem *item, RECT *rc);
static void     build_empty_state_layout(ChatListView *lv, HDC hdc,
                                         const RECT *client_rc,
                                         EmptyStateLayout *out);
static void     paint_empty_state(ChatListView *lv, HDC hdc,
                                  const RECT *client_rc);
static int      chatlv_empty_state_hit(ChatListView *lv, int mx, int my,
                                       RECT *out_rc);
static int      chatlv_list_empty(const ChatListView *lv);

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

/* ── Safety tag colour ──────────────────────────────────────────────── */

/* Safety tag chip colours, from ns_tokens() per the design spec's table:
 * SAFE -> text_dim, WRITE -> warning, CRITICAL -> danger. */
static void safety_tag_colors(CmdSafetyLevel level, COLORREF *bg, COLORREF *fg)
{
    const ThemeTokens *tok = ns_tokens();
    switch (level) {
    case CMD_WRITE:
        *bg = RGB_FROM_THEME(tok->warning.base);
        *fg = RGB_FROM_THEME(tok->warning.label);
        break;
    case CMD_CRITICAL:
        *bg = RGB_FROM_THEME(tok->danger.base);
        *fg = RGB_FROM_THEME(tok->danger.label);
        break;
    default:
        *bg = RGB_FROM_THEME(tok->text_dim);
        *fg = RGB_FROM_THEME(tok->text_dim_label);
        break;
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

/* ── Thinking disclosure geometry ──────────────────────────────────────
 * Wraps ai_panel_layout's thinking_layout() with the one thing it can't
 * know on its own: the wrapped reasoning text's measured height at the
 * body box's width. thinking_layout() needs that height as an input (to
 * clamp it to max_body_h and compute total_h), but the body box's width
 * is itself an output of thinking_layout() -- so this calls it twice when
 * expanded: once to learn body.w (any body_text_h works for that, since
 * row/chevron/label/summary/body.x/body.w never depend on it), then again
 * with the real measured height. Shared by measure_item(), paint_ai_item(),
 * the click/hover hit-tests and the wheel-scroll handler so they can never
 * disagree about where the row and body actually are. */
static void build_thinking_layout(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                                  int box_left, int box_right, int content_top,
                                  ThinkingLayout *out, int *out_full_body_h)
{
    int dpi = CLV_DPI(lv);
    NsRect avail = { box_left, content_top, box_right - box_left, 0 };
    int expanded = !item->u.ai.thinking_collapsed;
    int max_body_h = lv->viewport_height / 2;
    if (max_body_h < 1) max_body_h = 1;

    thinking_layout(avail, expanded, 0, max_body_h, dpi, out);

    int full_h = 0;
    if (expanded && hdc && out->body.w > 0) {
        HGDIOBJ tf = SelectObject(hdc, lv->hFont ? lv->hFont
                                       : GetStockObject(DEFAULT_GUI_FONT));
        RECT mr;
        SetRect(&mr, 0, 0, out->body.w, 0);
        draw_text_utf8(hdc, item->u.ai.thinking_text ? item->u.ai.thinking_text : "",
                       &mr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        full_h = mr.bottom - mr.top;
        SelectObject(hdc, tf);

        thinking_layout(avail, expanded, full_h, max_body_h, dpi, out);
    }
    if (out_full_body_h) *out_full_body_h = full_h;
}

/* A small down-pointing "v" stroke -- the expanded state of the Thinking
 * disclosure's chevron. NS_ICON_CHEV_RIGHT (collapsed) has no rotated/
 * down-pointing counterpart in the icon set, so this draws one directly
 * with two line segments per the design spec. */
static void draw_chevron_down(HDC hdc, const RECT *rc, COLORREF colour)
{
    int cx = (rc->left + rc->right) / 2;
    int top = rc->top + (rc->bottom - rc->top) / 3;
    int bot = rc->bottom - (rc->bottom - rc->top) / 3;
    int half_w = (rc->right - rc->left) / 3;
    if (half_w < 2) half_w = 2;

    HPEN pen = CreatePen(PS_SOLID, STROKE_RULE, colour);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    MoveToEx(hdc, cx - half_w, top, NULL);
    LineTo(hdc, cx, bot);
    LineTo(hdc, cx + half_w, top);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
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
            case CHAT_ITEM_TOOL_CALL:
            case CHAT_ITEM_TOOL_RESULT:
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
    lv->render_markdown = 1;
    lv->state_id = -1;
    lv->stick_to_bottom = 1;
    ns_hover_init(&lv->hover);

    /* Compute scaled layout constants */
    lv->msg_gap    = ns_scale(BASE_MSG_GAP, CLV_DPI(lv));
    lv->user_pad_h = ns_scale(BASE_USER_PAD_H, CLV_DPI(lv));
    lv->user_pad_v = ns_scale(BASE_USER_PAD_V, CLV_DPI(lv));
    lv->ai_indent  = ns_scale(BASE_AI_INDENT, CLV_DPI(lv));
    lv->code_pad   = ns_scale(BASE_CODE_PAD, CLV_DPI(lv));

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
                             HFONT bold, HFONT small_font)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return;
    lv->hFont     = font;
    lv->hMonoFont = mono;
    lv->hBoldFont = bold;
    lv->hSmallFont = small_font;
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

void chat_listview_set_render_markdown(HWND hwnd, int enabled)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return;
    int v = enabled ? 1 : 0;
    if (lv->render_markdown == v) return;   /* no-op */
    lv->render_markdown = v;
    /* Layout heights change with the new mode — recalc and redraw. */
    chat_listview_invalidate(hwnd);
}

void chat_listview_set_state(HWND hwnd, int state_id, int context_lines)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return;
    lv->state_id = state_id;
    lv->state_context_lines = context_lines;
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
    /* A programmatic jump to bottom (sending a prompt, opening the panel,
     * Retry) is a deliberate "go to bottom" action -- re-engage stick. */
    lv->stick_to_bottom = 1;
    lv->scroll_y = max_scroll;
    update_scrollbar(lv);
    InvalidateRect(hwnd, NULL, TRUE);
}

void chat_listview_scroll_to_top(HWND hwnd)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return;
    lv->stick_to_bottom = 0;
    lv->scroll_y = 0;
    update_scrollbar(lv);
    InvalidateRect(hwnd, NULL, TRUE);
}

int chat_listview_is_near_bottom(HWND hwnd)
{
    ChatListView *lv = lv_from_hwnd(hwnd);
    if (!lv) return 1;
    /* Kept for API compatibility -- now just reports the stick-to-bottom
     * bit (set by after_user_scroll() on every user-driven scroll). */
    return lv->stick_to_bottom;
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

/* Approval-card row height at the current DPI. v2 rows are always
 * single-line regardless of card width (approval_row_height no longer
 * takes one) -- see ns_layout.h. */
static int clv_cmd_row_h(ChatListView *lv, int text_h)
{
    return approval_row_height(text_h, CLV_DPI(lv));
}

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
     * scrollable container drawn by the first command item. Each row is a
     * single fixed-height line (checkbox + ellipsised command text + risk
     * tag), laid out by ns_layout's approval_card_layout(); see
     * paint_cmd_container() and build_cmd_card_geometry() below. */
    {
        int n = 0;
        ChatMsgItem *first_cmd = NULL;

        HDC hdc2 = GetDC(lv->hwnd);
        int text_h = 0;
        if (hdc2) {
            HGDIOBJ old = SelectObject(hdc2, lv->hMonoFont ? lv->hMonoFont
                                            : GetStockObject(ANSI_FIXED_FONT));
            TEXTMETRICA tm;
            GetTextMetricsA(hdc2, &tm);
            text_h = tm.tmHeight;
            SelectObject(hdc2, old);
        }

        item = lv->msg_list ? lv->msg_list->head : NULL;
        while (item) {
            if (item->type == CHAT_ITEM_COMMAND && !item->u.cmd.settled) {
                if (!first_cmd) first_cmd = item;
                if (n < APPROVAL_MAX_CMDS) {
                    const char *cmd_text = item->u.cmd.command ? item->u.cmd.command
                                                               : item->text;
                    SIZE sz = { 0, 0 };
                    if (hdc2 && cmd_text && *cmd_text) {
                        HGDIOBJ old = SelectObject(hdc2, lv->hMonoFont ? lv->hMonoFont
                                                    : GetStockObject(ANSI_FIXED_FONT));
                        GetTextExtentPoint32A(hdc2, cmd_text, (int)strlen(cmd_text), &sz);
                        SelectObject(hdc2, old);
                    }
                    lv->cmd_text_w[n] = sz.cx;
                }
                n++;
            }
            item = item->next;
        }
        if (hdc2) ReleaseDC(lv->hwnd, hdc2);

        lv->cmd_count = n;

        if (n > 0 && first_cmd) {
            int row_h = clv_cmd_row_h(lv, text_h);

            int visible_rows = (n < APPROVAL_VISIBLE_MAX) ? n : APPROVAL_VISIBLE_MAX;
            lv->cmd_total_h   = n * row_h;
            lv->cmd_visible_h = visible_rows * row_h;

            int max_cmd_scroll = lv->cmd_total_h - lv->cmd_visible_h;
            if (max_cmd_scroll < 0) max_cmd_scroll = 0;
            if (lv->cmd_scroll_y > max_cmd_scroll) lv->cmd_scroll_y = max_cmd_scroll;
            if (lv->cmd_scroll_y < 0) lv->cmd_scroll_y = 0;

            /* Interior = header row + gap + visible rows + gap + actions
             * row, exactly matching approval_card_layout's own geometry
             * (see build_cmd_card_geometry) so scroll maths never drifts
             * from what is painted. */
            int pad          = ns_scale(SP_MD, CLV_DPI(lv));
            int gap_sm       = ns_scale(SP_SM, CLV_DPI(lv));
            int ctrl_h       = ns_scale(SZ_CTRL_H, CLV_DPI(lv));
            int interior_h   = 2 * pad + 2 * ctrl_h + 2 * gap_sm + visible_rows * row_h;

            int border_w = ns_scale(1, CLV_DPI(lv));
            int container_h = 2 * border_w + interior_h;

            /* First command absorbs the full container height */
            first_cmd->measured_height = container_h;
            /* Hide all subsequent commands (painted by container) */
            item = first_cmd->next;
            while (item) {
                if (item->type == CHAT_ITEM_COMMAND && !item->u.cmd.settled)
                    item->measured_height = 0;
                item = item->next;
            }
        } else {
            lv->cmd_total_h = 0;
            lv->cmd_visible_h = 0;
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
        y += ns_scale(BASE_ACTIVITY_H, CLV_DPI(lv)) + lv->msg_gap;

    lv->total_height = y;
    lv->viewport_height = rc.bottom - rc.top;

    /* Content/viewport change: follow the bottom while stuck, otherwise
     * keep the current position (re-clamped to the new valid range). */
    int max_scroll = lv->total_height - lv->viewport_height;
    lv->scroll_y = stick_scroll_on_layout(lv->stick_to_bottom, lv->scroll_y,
                                           max_scroll);

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
    int side_pad = ns_scale(BASE_SIDE_PAD, CLV_DPI(lv));
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
            return total;
        }
    }

    case CHAT_ITEM_AI_TEXT: {
        /* AI text: left-indented.  on_paint applies side_pad to item_rc,
         * and paint_ai_item subtracts another side_pad on the right, so
         * the effective text width is width - 2*side_pad - indent - side_pad. */
        text_w = width - lv->ai_indent - 3 * side_pad;
        if (text_w < 40) text_w = 40;

        const char *measure_text = item->text;
        char *stripped = NULL;
        if (strstr(item->text, "[EXEC]")) {
            stripped = ai_text_for_measure(item->text);
            if (stripped) measure_text = stripped;
        }
        int h;
        if (lv->render_markdown) {
            h = md_measure_text(hdc, measure_text, text_w,
                                lv->hFont, lv->hMonoFont, lv->hBoldFont,
                                lv->theme);
        } else {
            HGDIOBJ tf = SelectObject(hdc, lv->hFont ? lv->hFont
                                       : GetStockObject(DEFAULT_GUI_FONT));
            RECT mrc;
            SetRect(&mrc, 0, 0, text_w, 0);
            draw_text_utf8(hdc, measure_text, &mrc,
                           DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            h = mrc.bottom - mrc.top;
            SelectObject(hdc, tf);
        }
        free(stripped);

        /* Icon row + gap before content (must match paint_ai_item layout) */
        int total = h + ns_scale(BASE_ICON_SIZE, CLV_DPI(lv)) + ns_scale(4, CLV_DPI(lv));

        /* Thinking disclosure height — a clickable row plus, when
         * expanded, a body box capped at half the thread height. Box
         * position doesn't matter for a height-only measurement, so
         * box_left/content_top are just 0. */
        if (item->u.ai.thinking_text && item->u.ai.thinking_text[0]) {
            ThinkingLayout tl;
            build_thinking_layout(lv, hdc, item, 0, text_w, 0, &tl, NULL);
            total += tl.total_h + ns_scale(SP_SM, CLV_DPI(lv));
        }

        return total;
    }

    case CHAT_ITEM_COMMAND: {
        /* Settled commands paint as one compact inline row (command text +
         * outcome chip, no card/checkbox) via paint_cmd_settled_row() --
         * see build_cmd_card_geometry's caller in on_paint. Row height
         * matches approval_row_height() exactly like an unsettled row so
         * scroll maths and the container never need to special-case it. */
        if (item->u.cmd.settled) {
            int text_line_h;
            old_font = SelectObject(hdc, lv->hMonoFont ? lv->hMonoFont
                                                        : GetStockObject(ANSI_FIXED_FONT));
            {
                TEXTMETRICA tm;
                GetTextMetricsA(hdc, &tm);
                text_line_h = tm.tmHeight;
            }
            SelectObject(hdc, old_font);
            return clv_cmd_row_h(lv, text_line_h);
        }
        /* Unsettled commands are grouped by recalc_layout's Pass 2 into a
         * single scrollable container (first item absorbs the container
         * height, the rest go to 0), so the exact value returned here is
         * always overwritten. One fixed-height row, matching
         * approval_card_layout's row height, keeps Pass 1 from treating
         * this item as needing another remeasure. */
        {
            int text_line_h;
            old_font = SelectObject(hdc, lv->hMonoFont ? lv->hMonoFont
                                                        : GetStockObject(ANSI_FIXED_FONT));
            {
                TEXTMETRICA tm;
                GetTextMetricsA(hdc, &tm);
                text_line_h = tm.tmHeight;
            }
            SelectObject(hdc, old_font);
            return clv_cmd_row_h(lv, text_line_h);
        }
    }

    case CHAT_ITEM_TOOL_CALL:
    case CHAT_ITEM_TOOL_RESULT:
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
                       DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_LEFT);
        SelectObject(hdc, old_font);
        return (rc.bottom - rc.top) + ns_scale(8, CLV_DPI(lv));
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

/* Call after any user-driven scroll change (wheel, scrollbar, keyboard,
 * selection-drag auto-scroll) once lv->scroll_y has been set/clamped to
 * its final value for the event. Recomputes stick_to_bottom from the
 * resulting position -- reaching the bottom re-engages stick, anything
 * else releases it -- and refreshes the scrollbar + repaint. */
static void after_user_scroll(ChatListView *lv)
{
    int max_scroll = lv->total_height - lv->viewport_height;
    if (max_scroll < 0) max_scroll = 0;
    lv->stick_to_bottom = stick_scroll_after_user(lv->scroll_y, max_scroll);
    update_scrollbar(lv);
    InvalidateRect(lv->hwnd, NULL, FALSE);
}

/* ── Recalculate scaled layout constants from current dpi_scale ─────── */

static void recalc_dpi_constants(ChatListView *lv)
{
    lv->msg_gap    = ns_scale(BASE_MSG_GAP, CLV_DPI(lv));
    lv->user_pad_h = ns_scale(BASE_USER_PAD_H, CLV_DPI(lv));
    lv->user_pad_v = ns_scale(BASE_USER_PAD_V, CLV_DPI(lv));
    lv->ai_indent  = ns_scale(BASE_AI_INDENT, CLV_DPI(lv));
    lv->code_pad   = ns_scale(BASE_CODE_PAD, CLV_DPI(lv));
}

/* ══════════════════════════════════════════════════════════════════════
 *  Paint routines
 * ══════════════════════════════════════════════════════════════════════ */

static void paint_user_item(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                            RECT *rc)
{
    const ThemeChatColors *tc = &lv->theme->chat;

    /* Right-align the bubble */
    int bubble_w = (rc->right - rc->left);
    int max_w = ((rc->right - rc->left + 2 * ns_scale(BASE_SIDE_PAD, CLV_DPI(lv)))
                 * 3) / 4;
    if (bubble_w > max_w) bubble_w = max_w;

    RECT bubble;
    bubble.right  = rc->right;
    bubble.left   = rc->right - bubble_w;
    bubble.top    = rc->top;
    bubble.bottom = rc->bottom;

    /* Draw bubble background */
    ns_draw_round_fill(hdc, &bubble, ns_scale(R_CARD, CLV_DPI(lv)),
                       RGB_FROM_THEME(tc->user_bubble), 255);

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

/* Render one AI text segment. Markdown when lv->render_markdown is on,
 * plain word-wrapped UTF-8 in lv->hFont otherwise. The [EXEC] purple
 * monospace path is handled by draw_ai_text_with_exec — segments passed
 * here never contain [EXEC] markers. Returns height consumed in pixels. */
static int draw_ai_segment(ChatListView *lv, HDC hdc, const char *text,
                           int x, int y, int width)
{
    if (lv->render_markdown) {
        return md_render_text(hdc, text, x, y, width,
                              lv->hFont, lv->hMonoFont, lv->hBoldFont,
                              lv->theme);
    }
    HGDIOBJ tf = SelectObject(hdc, lv->hFont ? lv->hFont
                              : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB_FROM_THEME(lv->theme->text_main));
    RECT mrc;
    SetRect(&mrc, x, y, x + width, 0);
    draw_text_utf8(hdc, text, &mrc,
                   DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    int seg_h = mrc.bottom - mrc.top;
    SetRect(&mrc, x, y, x + width, y + seg_h);
    draw_text_utf8(hdc, text, &mrc,
                   DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdc, tf);
    return seg_h;
}

/* Draw AI text with [EXEC]...[/EXEC] segments highlighted in purple.
 * Uses the same rect and flags as draw_text_utf8 but splits at markers. */
static void draw_ai_text_with_exec(ChatListView *lv, HDC hdc,
                                    const char *text, RECT *rc)
{
    COLORREF exec_clr = RGB_FROM_THEME(ns_tokens()->info.base);
    HFONT mono_font = lv->hMonoFont ? lv->hMonoFont
                                     : (HFONT)GetStockObject(ANSI_FIXED_FONT);

    /* If no [EXEC] markers, fast path */
    if (!strstr(text, "[EXEC]")) {
        draw_ai_segment(lv, hdc, text, rc->left, rc->top,
                        rc->right - rc->left);
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
                int h = draw_ai_segment(lv, hdc, pos, rc->left, y,
                                        rc->right - rc->left);
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
                    int h = draw_ai_segment(lv, hdc, seg, rc->left, y,
                                            rc->right - rc->left);
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
    const ThemeTokens *tok = ns_tokens();
    int icon_sz = ns_scale(BASE_ICON_SIZE, CLV_DPI(lv));
    int side_pad = ns_scale(BASE_SIDE_PAD, CLV_DPI(lv));

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

    /* Sparkle glyph inset into the avatar circle */
    {
        int pad = ns_scale(4, CLV_DPI(lv));
        RECT spark_rc = { rc->left + pad, rc->top + pad,
                          rc->left + icon_sz - pad,
                          rc->top + icon_sz - pad };
        ns_icon_draw(hdc, NS_ICON_SPARKLE, &spark_rc,
                     RGB_FROM_THEME(ns_tokens()->accent.label),
                     (UINT)(96.0f * lv->dpi_scale));
    }

    /* "AI" label next to avatar */
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB_FROM_THEME(tc->ai_accent));
    HGDIOBJ old_font = SelectObject(hdc, lv->hBoldFont ? lv->hBoldFont
                                        : GetStockObject(DEFAULT_GUI_FONT));
    RECT label_rc;
    label_rc.left   = rc->left + icon_sz + ns_scale(6, CLV_DPI(lv));
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

    int content_top = rc->top + icon_sz + ns_scale(4, CLV_DPI(lv));

    /* ── Thinking disclosure: chevron + "Thinking" + a dimmed summary, and
     * (when expanded) the full reasoning on bg_secondary with an accent
     * bar down the left, scrollable inside a box capped at half the
     * thread height. Geometry from ai_panel_layout's thinking_layout() via
     * build_thinking_layout() -- shared with measure_item() and the
     * click/hover/wheel handlers so painting never drifts from hit-testing.
     * See docs/superpowers/specs/2026-09-07-ai-assist-panel-design.md
     * "Thought process". ─────────────────────────────────────────────── */
    if (item->u.ai.thinking_text && item->u.ai.thinking_text[0]) {
        int box_left  = rc->left + lv->ai_indent;
        int box_right = rc->right - side_pad;
        int expanded  = !item->u.ai.thinking_collapsed;

        ThinkingLayout tl;
        int full_h = 0;
        build_thinking_layout(lv, hdc, item, box_left, box_right,
                              content_top, &tl, &full_h);

        COLORREF dim_clr = RGB_FROM_THEME(tok->text_dim);

        /* Chevron: CHEV_RIGHT collapsed, a hand-drawn down "v" expanded --
         * no down-pointing icon exists in the icon set. */
        RECT chev_rc = { tl.chevron.x, tl.chevron.y,
                         tl.chevron.x + tl.chevron.w,
                         tl.chevron.y + tl.chevron.h };
        if (expanded)
            draw_chevron_down(hdc, &chev_rc, dim_clr);
        else
            ns_icon_draw(hdc, NS_ICON_CHEV_RIGHT, &chev_rc, dim_clr,
                        (UINT)(96.0f * lv->dpi_scale));

        /* "Thinking" label (FONT_BODY, text_main) */
        RECT think_label_rc = { tl.label.x, tl.label.y,
                                tl.label.x + tl.label.w, tl.label.y + tl.label.h };
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB_FROM_THEME(lv->theme->text_main));
        SelectObject(hdc, lv->hFont ? lv->hFont : GetStockObject(DEFAULT_GUI_FONT));
        DrawTextA(hdc, "Thinking", -1, &think_label_rc,
                  DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);

        /* Summary: "\xC2\xB7 N words" or "\xC2\xB7 streaming\xE2\x80\xA6", text_dim */
        {
            char summary_buf[32];
            ai_thinking_summary(ai_word_count(item->u.ai.thinking_text),
                                !item->u.ai.thinking_complete,
                                summary_buf, sizeof(summary_buf));
            RECT summary_rc = { tl.summary.x, tl.summary.y,
                                tl.summary.x + tl.summary.w,
                                tl.summary.y + tl.summary.h };
            SetTextColor(hdc, dim_clr);
            draw_text_utf8(hdc, summary_buf, &summary_rc,
                           DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX
                           | DT_END_ELLIPSIS);
        }

        if (expanded && tl.body.h > 0) {
            RECT body_rc = { tl.body.x, tl.body.y,
                             tl.body.x + tl.body.w, tl.body.y + tl.body.h };
            int pad_sm = ns_scale(SP_SM, CLV_DPI(lv));

            /* Accent bar down the left, STROKE_BAR wide (fixed px, not
             * DPI-scaled -- see ns_type.h). */
            RECT bar_rc = { box_left + pad_sm, body_rc.top,
                            box_left + pad_sm + STROKE_BAR, body_rc.bottom };
            HBRUSH accent_br = CreateSolidBrush(RGB_FROM_THEME(tok->accent.base));
            FillRect(hdc, &bar_rc, accent_br);
            DeleteObject(accent_br);

            /* Body background */
            HBRUSH bg_br2 = CreateSolidBrush(RGB_FROM_THEME(tok->bg_secondary.base));
            FillRect(hdc, &body_rc, bg_br2);
            DeleteObject(bg_br2);

            int vis_h = tl.body.h;
            int sb_w = (full_h > vis_h) ? ns_scale(6, CLV_DPI(lv)) : 0;
            RECT clip_rc = body_rc;
            clip_rc.left += ns_scale(SP_XS, CLV_DPI(lv));
            clip_rc.right -= sb_w;

            int max_scroll = (full_h > vis_h) ? full_h - vis_h : 0;
            int eff_scroll = item->u.ai.thinking_scroll_y;
            if (eff_scroll > max_scroll) eff_scroll = max_scroll;
            if (eff_scroll < 0) eff_scroll = 0;

            HRGN clip_rgn = CreateRectRgnIndirect(&clip_rc);
            SelectClipRgn(hdc, clip_rgn);

            SetTextColor(hdc, dim_clr);
            HGDIOBJ tf = SelectObject(hdc, lv->hFont ? lv->hFont
                                      : GetStockObject(DEFAULT_GUI_FONT));
            RECT text_body_rc = { clip_rc.left, clip_rc.top - eff_scroll,
                                  clip_rc.right,
                                  clip_rc.top - eff_scroll + full_h };
            draw_text_utf8(hdc, item->u.ai.thinking_text, &text_body_rc,
                           DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(hdc, tf);

            SelectClipRgn(hdc, NULL);
            DeleteObject(clip_rgn);

            /* Scrollbar thumb when content overflows */
            if (full_h > vis_h) {
                int sb_x = body_rc.right - sb_w;
                int track_h = vis_h;
                int thumb_h = (vis_h * vis_h) / full_h;
                int min_thumb = ns_scale(20, CLV_DPI(lv));
                if (thumb_h < min_thumb) thumb_h = min_thumb;
                int thumb_y = body_rc.top;
                if (max_scroll > 0)
                    thumb_y += (eff_scroll * (track_h - thumb_h)) / max_scroll;

                RECT thumb_rc = { sb_x, thumb_y, sb_x + sb_w, thumb_y + thumb_h };
                ns_draw_round_fill(hdc, &thumb_rc, sb_w / 2, dim_clr, 255);
            }
        }

        content_top += tl.total_h + ns_scale(SP_SM, CLV_DPI(lv));
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

/* ── Paint one approval-card v2 row: checkbox, command text (FONT_MONO,
 *    ellipsised per row->ellipsis), risk chip. No per-row Allow/Deny --
 *    decisions are card-wide now (Deny all / Run N selected). A held
 *    (blocked) row's checkbox paints disabled and its text dims. ────── */

static void paint_cmd_row(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                          const ApprovalRowLayout *row, int row_idx)
{
    const ThemeChatColors *tc = &lv->theme->chat;
    const ThemeTokens *tok = ns_tokens();
    int dpi = CLV_DPI(lv);

    /* Risk chip */
    if (row->tag.w > 0 && row->tag.h > 0) {
        RECT tag_rc = { row->tag.x, row->tag.y,
                        row->tag.x + row->tag.w, row->tag.y + row->tag.h };
        COLORREF tbg, tfg;
        safety_tag_colors(item->u.cmd.safety, &tbg, &tfg);
        ns_draw_chip(hdc, &tag_rc, tbg, tfg, lv->hSmallFont,
                    safety_tag_text(item->u.cmd.safety));
    }

    /* Command text (single line, ellipsised when it doesn't fit) */
    if (row->text.w > 0 && row->text.h > 0) {
        RECT text_rc = { row->text.x, row->text.y,
                         row->text.x + row->text.w, row->text.y + row->text.h };
        const char *cmd_text = item->u.cmd.command ? item->u.cmd.command
                                                   : item->text;
        SetTextColor(hdc, row->held ? RGB_FROM_THEME(tok->text_dim)
                                     : RGB_FROM_THEME(tc->cmd_text));
        HGDIOBJ old_f = SelectObject(hdc, lv->hMonoFont ? lv->hMonoFont
                                          : GetStockObject(ANSI_FIXED_FONT));
        UINT flags = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_LEFT;
        if (row->ellipsis) flags |= DT_END_ELLIPSIS;
        draw_text_utf8(hdc, cmd_text, &text_rc, flags);
        SelectObject(hdc, old_f);
    }

    /* Selection checkbox */
    if (row->checkbox.w > 0 && row->checkbox.h > 0) {
        RECT chk_rc = { row->checkbox.x, row->checkbox.y,
                        row->checkbox.x + row->checkbox.w,
                        row->checkbox.y + row->checkbox.h };
        if (row->held) {
            /* Disabled look -- held rows can't be checked. */
            ns_draw_round_stroke(hdc, &chk_rc, ns_scale(R_CTRL, dpi),
                                 RGB_FROM_THEME(tok->text_dim), STROKE_HAIRLINE);
        } else {
            int chk_hover = ns_hover_state_for(&lv->hover,
                                CLV_ROW_HIT_ID(row_idx, HIT_CHECKBOX));
            if (item->u.cmd.selected) {
                COLORREF fill = chk_hover ? RGB_FROM_THEME(tok->success.hover)
                                          : RGB_FROM_THEME(tok->success.base);
                ns_draw_round_fill(hdc, &chk_rc, ns_scale(R_CTRL, dpi), fill, 255);
                SetTextColor(hdc, RGB_FROM_THEME(tok->success.label));
                DrawTextW(hdc, L"\x2713", 1, &chk_rc,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                COLORREF stroke = chk_hover ? RGB_FROM_THEME(tok->accent.base)
                                            : RGB_FROM_THEME(tok->border);
                ns_draw_round_stroke(hdc, &chk_rc, ns_scale(R_CTRL, dpi),
                                     stroke, STROKE_HAIRLINE);
            }
        }
    }
}

/* ── Paint a settled command as one compact inline row: command text on
 *    the left, an outcome chip ("ran"/"held"/"denied"/"skipped") right-
 *    aligned. No card, no header, no checkbox, no buttons -- the command
 *    has already been decided and (for approved ones) already ran; this
 *    is just a record of what happened, matching the AI text's own
 *    [EXEC]-derived history. Geometry from ns_layout's settled_row_layout()
 *    so painting and measure_item() can never disagree about the row's
 *    height. ───────────────────────────────────────────────────────────── */

static void paint_cmd_settled_row(ChatListView *lv, HDC hdc, ChatMsgItem *item,
                                  RECT *rc)
{
    const ThemeTokens *tok = ns_tokens();
    int dpi = CLV_DPI(lv);
    int side_pad = ns_scale(BASE_SIDE_PAD, dpi);

    /* Left/right edges match paint_ai_item's box -- rc is already inset by
     * side_pad on both sides (on_paint), so only the AI indent and one more
     * side_pad (matching paint_ai_item's box_right) are needed here. */
    int box_left  = rc->left + lv->ai_indent;
    int box_right = rc->right - side_pad;
    if (box_right < box_left) box_right = box_left;

    /* Outcome -> label + colours, per priority: an approved (ran) command
     * always shows "ran" even if it was momentarily held before being
     * unblocked; otherwise a still-blocked command shows "held"; then an
     * explicit denial; anything else (approved == -1, not blocked) means
     * the command was superseded before ever being decided -- "skipped". */
    const char *label;
    COLORREF chip_bg, chip_fg, text_clr;
    COLORREF dim = RGB_FROM_THEME(tok->text_dim);
    COLORREF panel_bg = RGB_FROM_THEME(tok->bg_primary.base);

    if (item->u.cmd.approved == 1) {
        label = "ran";
        chip_bg = RGB_FROM_THEME(tok->success.base);
        chip_fg = RGB_FROM_THEME(tok->success.label);
        text_clr = RGB_FROM_THEME(tok->text_main);
    } else if (item->u.cmd.blocked) {
        label = "held";
        chip_bg = RGB_FROM_THEME(tok->warning.base);
        chip_fg = RGB_FROM_THEME(tok->warning.label);
        text_clr = dim;
    } else if (item->u.cmd.approved == 0) {
        label = "denied";
        chip_bg = rgb_alpha(dim, panel_bg, 0.18f);
        chip_fg = dim;
        text_clr = dim;
    } else {
        label = "skipped";
        chip_bg = rgb_alpha(dim, panel_bg, 0.18f);
        chip_fg = dim;
        text_clr = dim;
    }

    /* Chip label width, measured with the small font: label width plus
     * SP_SM padding on both sides. */
    HGDIOBJ old_sf = SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                       : GetStockObject(DEFAULT_GUI_FONT));
    SIZE lsz = { 0, 0 };
    GetTextExtentPoint32A(hdc, label, (int)strlen(label), &lsz);
    SelectObject(hdc, old_sf);
    int chip_w = lsz.cx + 2 * ns_scale(SP_SM, dpi);

    /* Command text width and line height, measured with the mono font
     * (same font used to paint it) -- matches build_cmd_card_geometry's
     * own measurement so ellipsis/height agree with the live card's rows. */
    const char *cmd_text = item->u.cmd.command ? item->u.cmd.command
                                               : item->text;
    HGDIOBJ old_mf = SelectObject(hdc, lv->hMonoFont ? lv->hMonoFont
                                       : GetStockObject(ANSI_FIXED_FONT));
    SIZE csz = { 0, 0 };
    if (cmd_text && *cmd_text)
        GetTextExtentPoint32A(hdc, cmd_text, (int)strlen(cmd_text), &csz);
    TEXTMETRICA tm;
    GetTextMetricsA(hdc, &tm);
    SelectObject(hdc, old_mf);

    NsRect row_r = { box_left, rc->top, box_right - box_left,
                     approval_row_height(tm.tmHeight, dpi) };
    ApprovalRowLayout layout;
    settled_row_layout(row_r, csz.cx, chip_w, tm.tmHeight, dpi, &layout);

    /* Command text (single line, ellipsised when it doesn't fit) */
    if (layout.text.w > 0 && layout.text.h > 0) {
        RECT text_rc = { layout.text.x, layout.text.y,
                         layout.text.x + layout.text.w,
                         layout.text.y + layout.text.h };
        SetTextColor(hdc, text_clr);
        HGDIOBJ old_f = SelectObject(hdc, lv->hMonoFont ? lv->hMonoFont
                                          : GetStockObject(ANSI_FIXED_FONT));
        UINT flags = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_LEFT;
        if (layout.ellipsis) flags |= DT_END_ELLIPSIS;
        draw_text_utf8(hdc, cmd_text, &text_rc, flags);
        SelectObject(hdc, old_f);
    }

    /* Outcome chip */
    if (layout.tag.w > 0 && layout.tag.h > 0) {
        RECT tag_rc = { layout.tag.x, layout.tag.y,
                        layout.tag.x + layout.tag.w,
                        layout.tag.y + layout.tag.h };
        ns_draw_chip(hdc, &tag_rc, chip_bg, chip_fg, lv->hSmallFont, label);
    }
}

/* ── Shared geometry for the live command queue container, computed once
 *    and used by both paint_cmd_container() and on_lbuttondown() so the
 *    two can never drift apart. ───────────────────────────────────────── */

typedef struct {
    ApprovalCardLayout layout;
    ChatMsgItem *cmd_items[APPROVAL_MAX_CMDS];
    int n;
    int held_count;
    int checked_count;   /* checked && !held, over ALL n commands */
    int run_enabled;      /* checked_count > 0 */
    int first_row;
    int box_left, box_top, box_right, box_bot;
    int clip_left, clip_top, clip_right, clip_bot;
    int card_right;
    int needs_scroll;
    int border_w;
} CmdCardGeometry;

static void build_cmd_card_geometry(ChatListView *lv, HDC hdc_for_measure,
                                    int y, int h, int cw, CmdCardGeometry *g)
{
    memset(g, 0, sizeof(*g));

    int side_pad = ns_scale(BASE_SIDE_PAD, CLV_DPI(lv));
    int border_w = ns_scale(1, CLV_DPI(lv));
    int sb_w     = ns_scale(BASE_SCROLLBAR_W, CLV_DPI(lv));

    g->border_w  = border_w;
    g->box_left  = side_pad + side_pad;
    g->box_right = cw - side_pad - side_pad;
    g->box_top   = y;
    g->box_bot   = y + h;

    g->clip_left  = g->box_left + border_w;
    g->clip_right = g->box_right - border_w;
    g->clip_top   = g->box_top + border_w;
    g->clip_bot   = g->box_bot - border_w;

    int checked_arr[APPROVAL_MAX_CMDS], held_arr[APPROVAL_MAX_CMDS];
    int ci = 0;
    ChatMsgItem *c = lv->msg_list ? lv->msg_list->head : NULL;
    while (c && ci < APPROVAL_MAX_CMDS) {
        if (c->type == CHAT_ITEM_COMMAND && !c->u.cmd.settled) {
            g->cmd_items[ci] = c;
            checked_arr[ci] = c->u.cmd.selected ? 1 : 0;
            held_arr[ci] = c->u.cmd.blocked ? 1 : 0;
            if (held_arr[ci]) g->held_count++;
            if (checked_arr[ci] && !held_arr[ci]) g->checked_count++;
            ci++;
        }
        c = c->next;
    }
    /* n comes from the walk above (ci), not lv->cmd_count: WM_PAINT does
     * not recalc_layout, so cmd_count can be stale relative to what's
     * actually unsettled right now (e.g. right after a settle with no
     * re-layout). Deriving n from ci keeps it in sync with cmd_items[]
     * so no slot below n is ever left NULL. Already clamped to
     * APPROVAL_MAX_CMDS by the while loop condition above. */
    int n = ci;
    g->n = n;
    g->run_enabled = g->checked_count > 0;

    g->needs_scroll = (lv->cmd_total_h > lv->cmd_visible_h);
    g->card_right = g->needs_scroll
        ? (g->clip_right - sb_w - ns_scale(3, CLV_DPI(lv))) : g->clip_right;

    int text_h = 0;
    if (hdc_for_measure) {
        HGDIOBJ old = SelectObject(hdc_for_measure, lv->hMonoFont ? lv->hMonoFont
                                    : GetStockObject(ANSI_FIXED_FONT));
        TEXTMETRICA tm;
        GetTextMetricsA(hdc_for_measure, &tm);
        text_h = tm.tmHeight;
        SelectObject(hdc_for_measure, old);
    }
    int row_h = clv_cmd_row_h(lv, text_h);

    int first_row = (row_h > 0) ? (lv->cmd_scroll_y / row_h) : 0;
    int max_first = n - APPROVAL_VISIBLE_MAX;
    if (max_first < 0) max_first = 0;
    if (first_row > max_first) first_row = max_first;
    if (first_row < 0) first_row = 0;
    g->first_row = first_row;

    /* run_enabled/checked_count above are over ALL n commands (not just
     * the visible slice below) -- see ns_layout.h's note on scrolled-out
     * rows still being able to enable Run selected. */
    int rem_n = n - first_row;
    NsRect body = { g->clip_left, g->clip_top,
                   g->card_right - g->clip_left, g->clip_bot - g->clip_top };
    approval_card_layout(body, rem_n, &lv->cmd_text_w[first_row],
                        &checked_arr[first_row], &held_arr[first_row],
                        text_h, CLV_DPI(lv), &g->layout);
}

/* ── Paint the grouped command container: outer box, header ("N commands
 *    · M held"), scrollable rows, themed scrollbar, and the card-wide
 *    Deny all / Run N selected actions. ──────────────────────────────── */

static void paint_cmd_container(ChatListView *lv, HDC hdc, RECT *rc)
{
    const ThemeChatColors *tc = &lv->theme->chat;
    const ThemeTokens *tok = ns_tokens();
    int dpi = CLV_DPI(lv);
    int corner = ns_scale(BASE_CORNER_R, dpi);

    RECT client_rc;
    GetClientRect(lv->hwnd, &client_rc);
    int cw = client_rc.right - client_rc.left;

    CmdCardGeometry g;
    build_cmd_card_geometry(lv, hdc, rc->top, rc->bottom - rc->top, cw, &g);

    RECT box = { g.box_left, g.box_top, g.box_right, g.box_bot };

    /* Background */
    HBRUSH bg_br = CreateSolidBrush(RGB_FROM_THEME(tc->cmd_bg));
    FillRect(hdc, &box, bg_br);
    DeleteObject(bg_br);

    /* Border */
    HPEN border_pen = CreatePen(PS_SOLID, g.border_w,
                                RGB_FROM_THEME(tc->cmd_border));
    HGDIOBJ old_pen = SelectObject(hdc, border_pen);
    HGDIOBJ old_br  = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, box.left, box.top, box.right, box.bottom, corner, corner);
    SelectObject(hdc, old_br);
    SelectObject(hdc, old_pen);
    DeleteObject(border_pen);

    SetBkMode(hdc, TRANSPARENT);

    /* ── Header: "N commands · M held" [— Permit write is off] ──────── */
    {
        RECT hdr_rc = { g.layout.header.x, g.layout.header.y,
                        g.layout.header.x + g.layout.header.w,
                        g.layout.header.y + g.layout.header.h };
        char hdr_buf[48];
        if (g.held_count > 0)
            snprintf(hdr_buf, sizeof(hdr_buf), "%d command%s \xC2\xB7 %d held",
                     g.n, g.n == 1 ? "" : "s", g.held_count);
        else
            snprintf(hdr_buf, sizeof(hdr_buf), "%d command%s",
                     g.n, g.n == 1 ? "" : "s");

        SetTextColor(hdc, RGB_FROM_THEME(tok->text_main));
        HGDIOBJ hf = SelectObject(hdc, lv->hBoldFont ? lv->hBoldFont
                                       : GetStockObject(DEFAULT_GUI_FONT));
        RECT calc_rc = hdr_rc;
        draw_text_utf8(hdc, hdr_buf, &calc_rc,
                       DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_CALCRECT);
        draw_text_utf8(hdc, hdr_buf, &hdr_rc,
                       DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_LEFT);
        SelectObject(hdc, hf);

        if (g.held_count > 0) {
            RECT reason_rc = hdr_rc;
            reason_rc.left = calc_rc.right;
            SetTextColor(hdc, RGB_FROM_THEME(tok->text_dim));
            HGDIOBJ rf = SelectObject(hdc, lv->hFont ? lv->hFont
                                           : GetStockObject(DEFAULT_GUI_FONT));
            draw_text_utf8(hdc, " \xE2\x80\x94 Permit write is off", &reason_rc,
                           DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_LEFT
                           | DT_END_ELLIPSIS);
            SelectObject(hdc, rf);
        }
    }

    /* ── Clipped scroll region for rows ────────────────────────────── */
    int saved_dc = SaveDC(hdc);
    IntersectClipRect(hdc, g.clip_left, g.clip_top, g.card_right, g.clip_bot);

    for (int i = 0; i < g.layout.n_rows; i++) {
        const ApprovalRowLayout *rowl = &g.layout.rows[i];
        if (rowl->tag.w <= 0 && rowl->text.w <= 0) continue;
        ChatMsgItem *citem = g.cmd_items[g.first_row + i];
        if (!citem) continue;
        paint_cmd_row(lv, hdc, citem, rowl, i);
    }

    RestoreDC(hdc, saved_dc);

    /* ── Themed scrollbar (inside box, right edge) ────────────────── */
    if (g.needs_scroll) {
        int sb_w = ns_scale(BASE_SCROLLBAR_W, dpi);
        int track_left  = g.box_right - g.border_w - sb_w - ns_scale(2, dpi);
        int track_top   = g.clip_top;
        int track_bot   = g.clip_bot;
        int track_h     = track_bot - track_top;

        RECT track_rc = { track_left, track_top, track_left + sb_w, track_bot };
        ns_draw_round_fill(hdc, &track_rc, sb_w / 2,
                           RGB_FROM_THEME(tc->cmd_border), 255);

        int max_cmd_scroll = lv->cmd_total_h - lv->cmd_visible_h;
        if (max_cmd_scroll < 1) max_cmd_scroll = 1;
        int thumb_h = track_h * lv->cmd_visible_h / lv->cmd_total_h;
        if (thumb_h < ns_scale(20, dpi)) thumb_h = ns_scale(20, dpi);
        int thumb_y = track_top +
            (lv->cmd_scroll_y * (track_h - thumb_h)) / max_cmd_scroll;
        RECT thumb_rc = { track_left, thumb_y, track_left + sb_w, thumb_y + thumb_h };
        ns_draw_round_fill(hdc, &thumb_rc, sb_w / 2,
                           RGB_FROM_THEME(lv->theme->accent), 255);
    }

    /* ── Card-wide actions: Deny all (ghost) | Run N selected (primary) ── */
    {
        RECT deny_rc = { g.layout.deny_all.x, g.layout.deny_all.y,
                         g.layout.deny_all.x + g.layout.deny_all.w,
                         g.layout.deny_all.y + g.layout.deny_all.h };
        RECT run_rc  = { g.layout.run_selected.x, g.layout.run_selected.y,
                         g.layout.run_selected.x + g.layout.run_selected.w,
                         g.layout.run_selected.y + g.layout.run_selected.h };

        int deny_hover = ns_hover_state_for(&lv->hover, CLV_CARD_HIT_ID(HIT_DENY_ALL));
        COLORREF deny_border = deny_hover ? RGB_FROM_THEME(tok->accent.base)
                                          : RGB_FROM_THEME(tok->border);
        ns_draw_round_stroke(hdc, &deny_rc, ns_scale(R_CTRL, dpi),
                             deny_border, STROKE_HAIRLINE);
        SetTextColor(hdc, RGB_FROM_THEME(tok->text_main));
        HGDIOBJ df = SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                       : GetStockObject(DEFAULT_GUI_FONT));
        DrawTextA(hdc, "Deny all", -1, &deny_rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, df);

        char run_label[32];
        snprintf(run_label, sizeof(run_label), "Run %d selected", g.checked_count);
        NsBtnState run_state = g.run_enabled
            ? (NsBtnState)ns_hover_state_for(&lv->hover, CLV_CARD_HIT_ID(HIT_RUN_SELECTED))
            : NS_BTN_DISABLED;
        ns_draw_button(hdc, &run_rc, &tok->success, run_state, 0,
                      run_label, lv->hSmallFont, dpi);
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
    text_rc.left  += ns_scale(BASE_SIDE_PAD, CLV_DPI(lv));
    text_rc.right -= ns_scale(BASE_SIDE_PAD, CLV_DPI(lv));
    draw_text_utf8(hdc, item->text, &text_rc,
                   DT_WORDBREAK | DT_NOPREFIX | DT_LEFT);
    SelectObject(hdc, old_font);
}

/* ── Empty/no-key/no-session state (ai_panel_states.h) ────────────────
 * Painted by on_paint() in place of the normal item walk whenever the
 * message list has zero items and a state has been set via
 * chat_listview_set_state() -- see AI Assist Panel task 4's "Empty and
 * blocked states" section. ─────────────────────────────────────────── */

static int chatlv_list_empty(const ChatListView *lv)
{
    return !lv || !lv->msg_list || lv->msg_list->count == 0;
}

/* Lay out the glyph circle, title, body and the chip row / action button
 * centred in `client_rc`. Cheap (no allocation, no caching) so painting
 * and hit-testing can each call it fresh and never disagree. */
static void build_empty_state_layout(ChatListView *lv, HDC hdc,
                                     const RECT *client_rc,
                                     EmptyStateLayout *out)
{
    memset(out, 0, sizeof(*out));
    if (!lv || lv->state_id < 0) return;

    const AiPanelState *st = ai_panel_state(lv->state_id);
    if (!st) return;

    int dpi = CLV_DPI(lv);
    int cw = client_rc->right - client_rc->left;
    int ch = client_rc->bottom - client_rc->top;
    int side_pad = ns_scale(BASE_SIDE_PAD, dpi);

    char body_buf[256];
    ai_panel_state_body(lv->state_id, lv->state_context_lines,
                        body_buf, sizeof(body_buf));

    /* Wrap width: ~28 characters of FONT_BODY, clamped to what actually
     * fits so a narrow docked panel never clips instead of wrapping. */
    int max_w = cw - 2 * side_pad;
    if (max_w < ns_scale(40, dpi)) max_w = cw > 0 ? cw : ns_scale(200, dpi);
    int wrap_w = max_w;
    HFONT body_font = ns_font(FONT_BODY, dpi);
    HFONT title_font = ns_font(FONT_TITLE, dpi);
    {
        HGDIOBJ old = SelectObject(hdc, body_font);
        TEXTMETRICA tm;
        GetTextMetricsA(hdc, &tm);
        int chars_w = tm.tmAveCharWidth * 28;
        if (chars_w > 0 && chars_w < wrap_w) wrap_w = chars_w;
        SelectObject(hdc, old);
    }

    /* Measure title (single line) and body (wrapped) heights */
    RECT title_calc = { 0, 0, wrap_w, 0 };
    HGDIOBJ old_tf = SelectObject(hdc, title_font);
    draw_text_utf8(hdc, st->title, &title_calc,
                   DT_CENTER | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
    SelectObject(hdc, old_tf);

    RECT body_calc = { 0, 0, wrap_w, 0 };
    HGDIOBJ old_bf = SelectObject(hdc, body_font);
    draw_text_utf8(hdc, body_buf, &body_calc,
                   DT_CENTER | DT_NOPREFIX | DT_WORDBREAK | DT_CALCRECT);
    SelectObject(hdc, old_bf);

    int icon_d = ns_scale(SZ_ICON, dpi) * 2;
    int gap_icon_title  = ns_scale(SP_MD, dpi);
    int gap_title_body  = ns_scale(SP_XS, dpi);
    int gap_body_action = ns_scale(SP_LG, dpi);

    int title_w = title_calc.right - title_calc.left;
    int title_h = title_calc.bottom - title_calc.top;
    int body_w  = body_calc.right - body_calc.left;
    int body_h  = body_calc.bottom - body_calc.top;

    /* ── Action row: three chips (EMPTY) or one button (NO_KEY/NO_SESSION) */
    int action_h;
    int chip_w[3] = {0, 0, 0};
    int n_chips = 0;
    HFONT chip_font = ns_font(FONT_CAPTION, dpi);
    int pad_chip = ns_scale(SP_MD, dpi);
    int gap_chip = ns_scale(SP_SM, dpi);
    int chip_h = ns_scale(SZ_CTRL_H, dpi);
    int btn_w = 0, btn_h = 0;

    out->is_button = !st->has_suggestions;

    if (out->is_button) {
        HFONT btn_font = ns_font(FONT_BODY, dpi);
        SIZE sz = {0, 0};
        if (st->action_label) {
            HGDIOBJ old = SelectObject(hdc, btn_font);
            GetTextExtentPoint32A(hdc, st->action_label,
                                  (int)strlen(st->action_label), &sz);
            SelectObject(hdc, old);
        }
        btn_h = ns_scale(SZ_CTRL_H, dpi);
        btn_w = sz.cx + 2 * ns_scale(SP_LG, dpi);
        int min_btn_w = ns_scale(SZ_BTN_MIN_W, dpi);
        if (btn_w < min_btn_w) btn_w = min_btn_w;
        action_h = btn_h;
    } else {
        int count;
        const char *const *sugg = ai_panel_suggestions(&count);
        n_chips = count < 3 ? count : 3;
        HGDIOBJ old = SelectObject(hdc, chip_font);
        for (int i = 0; i < n_chips; i++) {
            SIZE sz = {0, 0};
            GetTextExtentPoint32A(hdc, sugg[i], (int)strlen(sugg[i]), &sz);
            chip_w[i] = sz.cx + 2 * pad_chip;
        }
        SelectObject(hdc, old);
        action_h = chip_h;   /* one row unless it has to wrap (below) */
    }

    /* Chip rows: try to fit all n_chips on one row; fall back to
     * one-per-row when the docked panel is too narrow. Only two shapes
     * are possible for 3 chips, so a small cascade covers every case. */
    int chip_rows[3][3];   /* chip_rows[row][slot] = chip index, -1 = end */
    int n_rows = 0;
    if (!out->is_button) {
        int all_w = 0;
        for (int i = 0; i < n_chips; i++) all_w += chip_w[i];
        all_w += gap_chip * (n_chips > 0 ? n_chips - 1 : 0);

        if (n_chips == 0) {
            n_rows = 0;
        } else if (all_w <= max_w) {
            n_rows = 1;
            for (int i = 0; i < n_chips; i++) chip_rows[0][i] = i;
            for (int i = n_chips; i < 3; i++) chip_rows[0][i] = -1;
        } else if (n_chips == 3 &&
                  (chip_w[0] + gap_chip + chip_w[1]) <= max_w) {
            n_rows = 2;
            chip_rows[0][0] = 0; chip_rows[0][1] = 1; chip_rows[0][2] = -1;
            chip_rows[1][0] = 2; chip_rows[1][1] = -1; chip_rows[1][2] = -1;
        } else {
            n_rows = n_chips;
            for (int i = 0; i < n_chips; i++) {
                chip_rows[i][0] = i;
                chip_rows[i][1] = -1;
                chip_rows[i][2] = -1;
            }
        }
        action_h = n_rows > 0
            ? n_rows * chip_h + (n_rows - 1) * gap_chip
            : 0;
    }

    int total_h = icon_d + gap_icon_title + title_h + gap_title_body +
                  body_h + gap_body_action + action_h;
    int top = (ch - total_h) / 2;
    if (top < side_pad) top = side_pad;

    int cx = client_rc->left + cw / 2;

    out->icon.left   = cx - icon_d / 2;
    out->icon.top    = top;
    out->icon.right  = out->icon.left + icon_d;
    out->icon.bottom = out->icon.top + icon_d;

    out->title.left   = cx - title_w / 2;
    out->title.top    = out->icon.bottom + gap_icon_title;
    out->title.right  = out->title.left + title_w;
    out->title.bottom = out->title.top + title_h;

    out->body.left   = cx - body_w / 2;
    out->body.top    = out->title.bottom + gap_title_body;
    out->body.right  = out->body.left + body_w;
    out->body.bottom = out->body.top + body_h;

    int action_top = out->body.bottom + gap_body_action;

    if (out->is_button) {
        out->button.left   = cx - btn_w / 2;
        out->button.top    = action_top;
        out->button.right  = out->button.left + btn_w;
        out->button.bottom = out->button.top + btn_h;
    } else {
        out->n_chips = n_chips;
        for (int r = 0; r < n_rows; r++) {
            int row_w = 0, row_n = 0;
            for (int s = 0; s < 3 && chip_rows[r][s] >= 0; s++) {
                row_w += chip_w[chip_rows[r][s]];
                row_n++;
            }
            row_w += gap_chip * (row_n > 0 ? row_n - 1 : 0);
            int x = cx - row_w / 2;
            int y = action_top + r * (chip_h + gap_chip);
            for (int s = 0; s < row_n; s++) {
                int idx = chip_rows[r][s];
                out->chip[idx].left   = x;
                out->chip[idx].top    = y;
                out->chip[idx].right  = x + chip_w[idx];
                out->chip[idx].bottom = y + chip_h;
                x += chip_w[idx] + gap_chip;
            }
        }
    }
}

static void paint_empty_state(ChatListView *lv, HDC hdc, const RECT *client_rc)
{
    const AiPanelState *st = ai_panel_state(lv->state_id);
    if (!st) return;

    const ThemeTokens *tok = ns_tokens();
    int dpi = CLV_DPI(lv);

    EmptyStateLayout el;
    build_empty_state_layout(lv, hdc, client_rc, &el);

    SetBkMode(hdc, TRANSPARENT);

    /* Glyph in a border-stroked circle, accent foreground */
    int icon_d = el.icon.right - el.icon.left;
    ns_draw_round_stroke(hdc, &el.icon, icon_d / 2,
                         RGB_FROM_THEME(tok->border), STROKE_HAIRLINE);
    {
        int inner_d = ns_scale(SZ_ICON, dpi);
        RECT glyph_rc = {
            el.icon.left + (icon_d - inner_d) / 2,
            el.icon.top  + (icon_d - inner_d) / 2,
            0, 0
        };
        glyph_rc.right  = glyph_rc.left + inner_d;
        glyph_rc.bottom = glyph_rc.top + inner_d;
        ns_icon_draw(hdc, NS_ICON_AI, &glyph_rc,
                    RGB_FROM_THEME(tok->accent.base), (UINT)dpi);
    }

    /* Title (FONT_TITLE, text_main) */
    SetTextColor(hdc, RGB_FROM_THEME(tok->text_main));
    {
        HGDIOBJ old = SelectObject(hdc, ns_font(FONT_TITLE, dpi));
        RECT title_rc = el.title;
        draw_text_utf8(hdc, st->title, &title_rc,
                       DT_CENTER | DT_NOPREFIX | DT_WORDBREAK);
        SelectObject(hdc, old);
    }

    /* Body (FONT_BODY, text_dim) */
    {
        char body_buf[256];
        ai_panel_state_body(lv->state_id, lv->state_context_lines,
                            body_buf, sizeof(body_buf));
        SetTextColor(hdc, RGB_FROM_THEME(tok->text_dim));
        HGDIOBJ old = SelectObject(hdc, ns_font(FONT_BODY, dpi));
        RECT body_rc = el.body;
        draw_text_utf8(hdc, body_buf, &body_rc,
                       DT_CENTER | DT_NOPREFIX | DT_WORDBREAK);
        SelectObject(hdc, old);
    }

    if (el.is_button) {
        NsBtnState state = (NsBtnState)ns_hover_state_for(&lv->hover,
                                                           CLV_STATE_BUTTON_HIT);
        ns_draw_button(hdc, &el.button, &tok->accent, state, 0,
                      st->action_label, ns_font(FONT_BODY, dpi), dpi);
    } else {
        int count;
        const char *const *sugg = ai_panel_suggestions(&count);
        HFONT chip_font = ns_font(FONT_CAPTION, dpi);
        for (int i = 0; i < el.n_chips && i < count; i++) {
            int hover = ns_hover_state_for(&lv->hover, CLV_STATE_CHIP_HIT(i));
            COLORREF bg = hover ? RGB_FROM_THEME(tok->raised.hover)
                                : RGB_FROM_THEME(tok->raised.base);
            ns_draw_chip(hdc, &el.chip[i], bg, RGB_FROM_THEME(tok->text_main),
                        chip_font, sugg[i]);
        }
    }
}

/* Hit-test the empty/no-key/no-session state for hover and click purposes.
 * Returns CLV_STATE_CHIP_HIT(i) / CLV_STATE_BUTTON_HIT, or -1 when nothing
 * hittable is under (mx, my). Only meaningful while chatlv_list_empty()
 * and lv->state_id >= 0 -- callers are expected to check that first. */
static int chatlv_empty_state_hit(ChatListView *lv, int mx, int my, RECT *out_rc)
{
    RECT client_rc;
    GetClientRect(lv->hwnd, &client_rc);

    EmptyStateLayout el;
    HDC hdc = GetDC(lv->hwnd);
    build_empty_state_layout(lv, hdc, &client_rc, &el);
    if (hdc) ReleaseDC(lv->hwnd, hdc);

    if (el.is_button) {
        if (mx >= el.button.left && mx < el.button.right &&
            my >= el.button.top && my < el.button.bottom) {
            if (out_rc) *out_rc = el.button;
            return CLV_STATE_BUTTON_HIT;
        }
        return -1;
    }

    for (int i = 0; i < el.n_chips; i++) {
        if (mx >= el.chip[i].left && mx < el.chip[i].right &&
            my >= el.chip[i].top && my < el.chip[i].bottom) {
            if (out_rc) *out_rc = el.chip[i];
            return CLV_STATE_CHIP_HIT(i);
        }
    }
    return -1;
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

    int act_h  = ns_scale(BASE_ACTIVITY_H, CLV_DPI(lv));
    int dot_sz = ns_scale(BASE_DOT_SIZE, CLV_DPI(lv));
    int pad    = ns_scale(BASE_SIDE_PAD, CLV_DPI(lv));
    int indent = ns_scale(BASE_AI_INDENT, CLV_DPI(lv));

    COLORREF bg_clr = RGB_FROM_THEME(lv->theme->bg_primary);
    COLORREF dot_clr = activity_health_color(lv, lv->activity->health);
    if (lv->pulse_toggle && !ns_reduced_motion())
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
    text_rc.left   = dot_x + dot_sz + ns_scale(6, CLV_DPI(lv));
    text_rc.top    = y;
    text_rc.right  = cw - pad;
    text_rc.bottom = y + act_h;
    DrawTextA(hdc, status_buf, -1, &text_rc,
              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    /* Draw [Retry] link when stalled -- underlined while hot (ns_hover) */
    if (lv->activity->health == HEALTH_RED) {
        static const char retry_text[] = "[Retry]";
        SIZE sz;
        GetTextExtentPoint32A(hdc, status_buf, (int)strlen(status_buf), &sz);
        int retry_x = text_rc.left + sz.cx + ns_scale(10, CLV_DPI(lv));
        SetTextColor(hdc, RGB_FROM_THEME(ns_tokens()->link.base));
        RECT retry_rc;
        retry_rc.left   = retry_x;
        retry_rc.top    = y;
        retry_rc.right  = cw - pad;
        retry_rc.bottom = y + act_h;
        DrawTextA(hdc, retry_text, -1, &retry_rc,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

        if (ns_hover_state_for(&lv->hover, CLV_HOVER_RETRY) > 0) {
            SIZE rsz;
            GetTextExtentPoint32A(hdc, retry_text, (int)strlen(retry_text), &rsz);
            int text_top = y + (act_h - rsz.cy) / 2;
            int underline_y = text_top + rsz.cy - 1;
            HPEN u_pen = CreatePen(PS_SOLID, 1, RGB_FROM_THEME(ns_tokens()->link.base));
            HGDIOBJ old_u = SelectObject(hdc, u_pen);
            MoveToEx(hdc, retry_x, underline_y, NULL);
            LineTo(hdc, retry_x + rsz.cx, underline_y);
            SelectObject(hdc, old_u);
            DeleteObject(u_pen);
        }
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

    /* Empty/no-key/no-session state: replaces the normal item walk
     * entirely while the list has zero items and a state is set (AI
     * Assist Panel task 4). */
    if (chatlv_list_empty(lv) && lv->state_id >= 0) {
        paint_empty_state(lv, mem_dc, &client);

        BitBlt(hdc, 0, 0, cw, ch, mem_dc, 0, 0, SRCCOPY);
        SelectObject(mem_dc, old_bmp);
        DeleteObject(bmp);
        DeleteDC(mem_dc);
        EndPaint(lv->hwnd, &ps);
        return;
    }

    /* Walk items, skip those above viewport, stop after those below */
    int y = lv->msg_gap - lv->scroll_y;
    int side_pad = ns_scale(BASE_SIDE_PAD, CLV_DPI(lv));

    /* Track last user message for sticky pinning */
    ChatMsgItem *last_user = NULL;
    int last_user_y = 0;
    int last_user_h = 0;

    ChatMsgItem *item = lv->msg_list ? lv->msg_list->head : NULL;
    while (item) {
        int h = item->measured_height;

        /* Skip hidden items (h=0 command items absorbed by container) */
        if (h == 0) {
            item = item->next;
            continue;
        }

        /* Track the most recent user message position */
        if (item->type == CHAT_ITEM_USER) {
            last_user = item;
            last_user_y = y;
            last_user_h = h;
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
                /* A settled command paints as its own compact inline row.
                 * An unsettled one is part of the single live container
                 * (painted by whichever item currently absorbs the full
                 * container height -- see build_cmd_card_geometry). Both
                 * settled_row_layout() and approval_card_layout() are
                 * recomputed straight from recalc_layout()'s measured
                 * heights on every settle (settle_all_commands calls
                 * chat_listview_invalidate, which recalcs), so item_rc's
                 * height here always matches what's about to be painted. */
                if (item->u.cmd.settled)
                    paint_cmd_settled_row(lv, mem_dc, item, &item_rc);
                else
                    paint_cmd_container(lv, mem_dc, &item_rc);
                break;
            case CHAT_ITEM_TOOL_CALL:
            case CHAT_ITEM_TOOL_RESULT:
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

    /* Sticky user message: if the last user message has scrolled above
     * the viewport, re-paint it pinned to the top so the user always
     * sees the question the AI is addressing. */
    if (last_user && last_user_y + last_user_h <= 0) {
        /* Clear the sticky area with background */
        RECT sticky_bg;
        SetRect(&sticky_bg, 0, 0, cw, last_user_h + ns_scale(2, CLV_DPI(lv)));
        HBRUSH sbr = CreateSolidBrush(bg);
        FillRect(mem_dc, &sticky_bg, sbr);
        DeleteObject(sbr);

        /* Paint the user bubble at y=0 */
        RECT sticky_rc;
        sticky_rc.left   = side_pad;
        sticky_rc.top    = 0;
        sticky_rc.right  = cw - side_pad;
        sticky_rc.bottom = last_user_h;
        paint_user_item(lv, mem_dc, last_user, &sticky_rc);

        /* Subtle bottom shadow line to separate from scrolling content */
        HPEN shadow_pen = CreatePen(PS_SOLID, 1,
                                     RGB_FROM_THEME(lv->theme->chat.cmd_border));
        HGDIOBJ old_sp = SelectObject(mem_dc, shadow_pen);
        MoveToEx(mem_dc, 0, last_user_h + 1, NULL);
        LineTo(mem_dc, cw, last_user_h + 1);
        SelectObject(mem_dc, old_sp);
        DeleteObject(shadow_pen);
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
 *  Hover hit-testing: same geometry as clicks, but never triggers an
 *  action -- only identifies the painted element (if any) under the
 *  cursor and its rect, for ns_hover_move() and targeted invalidation.
 * ══════════════════════════════════════════════════════════════════════ */

/* Find the sole active (unsettled) command container item and the (y, h)
 * it paints at. Only one item in the list ever carries the container's
 * full measured_height at a time (the rest are absorbed with h = 0 --
 * see build_cmd_card_geometry's caller in on_paint), so this mirrors the
 * walk there and in on_lbuttondown. Returns NULL if no container is
 * showing right now (e.g. everything has been decided). */
static ChatMsgItem *find_active_cmd_container(ChatListView *lv,
                                              int *out_y, int *out_h)
{
    int y = lv->msg_gap - lv->scroll_y;
    ChatMsgItem *item = lv->msg_list ? lv->msg_list->head : NULL;
    while (item) {
        int h = item->measured_height;
        if (h == 0) { item = item->next; continue; }
        if (item->type == CHAT_ITEM_COMMAND && !item->u.cmd.settled) {
            if (out_y) *out_y = y;
            if (out_h) *out_h = h;
            return item;
        }
        y += h + lv->msg_gap;
        item = item->next;
    }
    return NULL;
}

/* The y coordinate just below the last visible item -- where the inline
 * activity indicator (and its [Retry] link) paints. Mirrors the trailing
 * walk in on_paint/on_lbuttondown. */
static int chatlv_items_bottom_y(ChatListView *lv)
{
    int y = lv->msg_gap - lv->scroll_y;
    ChatMsgItem *item = lv->msg_list ? lv->msg_list->head : NULL;
    while (item) {
        int h = item->measured_height;
        if (h != 0) y += h + lv->msg_gap;
        item = item->next;
    }
    return y;
}

/* Rect of the [Retry] link, matching paint_activity_indicator's layout.
 * Returns 0 (leaving *out untouched) when there is no stalled activity
 * to retry right now. The right edge deliberately matches the generous
 * hit area on_lbuttondown already accepts for a click (the whole
 * horizontal band to the right margin), so hover and click never
 * disagree about whether the link is present. */
static int activity_retry_rect(ChatListView *lv, HDC hdc, int y, int cw,
                               RECT *out)
{
    if (!lv->activity || lv->activity->phase == ACTIVITY_IDLE ||
        lv->activity->health != HEALTH_RED)
        return 0;

    int act_h  = ns_scale(BASE_ACTIVITY_H, CLV_DPI(lv));
    int dot_sz = ns_scale(BASE_DOT_SIZE, CLV_DPI(lv));
    int pad    = ns_scale(BASE_SIDE_PAD, CLV_DPI(lv));
    int indent = ns_scale(BASE_AI_INDENT, CLV_DPI(lv));
    int dot_x  = pad + indent;
    int text_left = dot_x + dot_sz + ns_scale(6, CLV_DPI(lv));

    char status_buf[128];
    float now = (float)GetTickCount() / 1000.0f;
    chat_activity_format(lv->activity, now, status_buf, sizeof(status_buf));

    HGDIOBJ old_font = SelectObject(hdc, lv->hSmallFont ? lv->hSmallFont
                                         : GetStockObject(DEFAULT_GUI_FONT));
    SIZE sz;
    GetTextExtentPoint32A(hdc, status_buf, (int)strlen(status_buf), &sz);
    SelectObject(hdc, old_font);

    out->left   = text_left + sz.cx + ns_scale(10, CLV_DPI(lv));
    out->top    = y;
    out->right  = cw - pad;
    out->bottom = y + act_h;
    return 1;
}

/* Decode a row-element id back into the rect approval_card_layout gave
 * it. Shared by chatlv_hover_hit() (current position) and
 * chatlv_hover_rect_for_id() (the previously-hot element, which may no
 * longer be under the cursor). */
static int cmd_card_rect_for_hit(const ApprovalCardLayout *l, int hit,
                                 int row, RECT *out)
{
    NsRect nr;
    if (hit == HIT_DENY_ALL) {
        nr = l->deny_all;
    } else if (hit == HIT_RUN_SELECTED) {
        nr = l->run_selected;
    } else if (hit == HIT_HEADER) {
        nr = l->header;
    } else if (row >= 0 && row < l->n_rows) {
        const ApprovalRowLayout *rl = &l->rows[row];
        switch (hit) {
        case HIT_TAG:      nr = rl->tag;      break;
        case HIT_TEXT:     nr = rl->text;     break;
        case HIT_CHECKBOX: nr = rl->checkbox; break;
        default: return 0;
        }
    } else {
        return 0;
    }
    if (nr.w <= 0 || nr.h <= 0) return 0;
    if (out) {
        out->left = nr.x; out->top = nr.y;
        out->right = nr.x + nr.w; out->bottom = nr.y + nr.h;
    }
    return 1;
}

/* Find the Thinking disclosure row (if any) under (mx, my), across every
 * AI item in the list -- not just the one the cursor's vertical span
 * belongs to would suggest, since a collapsed row is short but its item
 * may still be tall (reply text below it). Returns 0 when nothing
 * hittable is under the cursor. */
static int chatlv_thinking_row_hit(ChatListView *lv, int mx, int my,
                                   RECT *out_rc, int *out_item_id)
{
    int side_pad = ns_scale(BASE_SIDE_PAD, CLV_DPI(lv));
    RECT client_rc;
    GetClientRect(lv->hwnd, &client_rc);
    int box_left  = side_pad + lv->ai_indent;
    int box_right = (client_rc.right - client_rc.left) - side_pad;

    int y = lv->msg_gap - lv->scroll_y;
    ChatMsgItem *item = lv->msg_list ? lv->msg_list->head : NULL;
    while (item) {
        int h = item->measured_height;
        if (h == 0) { item = item->next; continue; }
        if (item->type == CHAT_ITEM_AI_TEXT && item->u.ai.thinking_text &&
            item->u.ai.thinking_text[0] && my >= y && my < y + h) {
            int content_top = y + ns_scale(BASE_ICON_SIZE, CLV_DPI(lv))
                               + ns_scale(4, CLV_DPI(lv));
            ThinkingLayout tl;
            HDC hdc = GetDC(lv->hwnd);
            build_thinking_layout(lv, hdc, item, box_left, box_right,
                                  content_top, &tl, NULL);
            if (hdc) ReleaseDC(lv->hwnd, hdc);

            if (mx >= tl.row.x && mx < tl.row.x + tl.row.w &&
                my >= tl.row.y && my < tl.row.y + tl.row.h) {
                if (out_rc)
                    SetRect(out_rc, tl.row.x, tl.row.y,
                           tl.row.x + tl.row.w, tl.row.y + tl.row.h);
                if (out_item_id) *out_item_id = item->id;
                return 1;
            }
            return 0;
        }
        y += h + lv->msg_gap;
        item = item->next;
    }
    return 0;
}

/* Hit-test the whole list view for hover purposes only -- never posts a
 * command, only reports which element (if any) is under (mx, my) via a
 * CLV_ROW_HIT_ID/CLV_CARD_HIT_ID/CLV_HOVER_RETRY/CLV_THINK_HOVER_ID id,
 * plus its rect for invalidation. Returns -1 when nothing hittable is
 * under the cursor. */
static int chatlv_hover_hit(ChatListView *lv, int mx, int my, RECT *out_rc)
{
    if (chatlv_list_empty(lv) && lv->state_id >= 0)
        return chatlv_empty_state_hit(lv, mx, my, out_rc);

    int cy, ch;
    ChatMsgItem *citem = find_active_cmd_container(lv, &cy, &ch);
    if (citem && my >= cy && my < cy + ch) {
        RECT client_rc;
        GetClientRect(lv->hwnd, &client_rc);
        int cw = client_rc.right - client_rc.left;

        HDC mdc = GetDC(lv->hwnd);
        CmdCardGeometry g;
        build_cmd_card_geometry(lv, mdc, cy, ch, cw, &g);
        if (mdc) ReleaseDC(lv->hwnd, mdc);

        int row_out = -1;
        int hit = approval_card_hit(&g.layout, mx, my, &row_out);
        if (hit != HIT_NONE &&
            cmd_card_rect_for_hit(&g.layout, hit, row_out, out_rc)) {
            return (row_out >= 0) ? CLV_ROW_HIT_ID(row_out, hit)
                                  : CLV_CARD_HIT_ID(hit);
        }
    }

    RECT think_rc;
    int think_item_id = -1;
    if (chatlv_thinking_row_hit(lv, mx, my, &think_rc, &think_item_id)) {
        if (out_rc) *out_rc = think_rc;
        return CLV_THINK_HOVER_ID(think_item_id);
    }

    /* [Retry] link, below all items */
    RECT client_rc;
    GetClientRect(lv->hwnd, &client_rc);
    int cw = client_rc.right - client_rc.left;
    int y = chatlv_items_bottom_y(lv);
    HDC hdc = GetDC(lv->hwnd);
    RECT retry_rc;
    int has_retry = activity_retry_rect(lv, hdc, y, cw, &retry_rc);
    if (hdc) ReleaseDC(lv->hwnd, hdc);
    if (has_retry && mx >= retry_rc.left && mx < retry_rc.right &&
        my >= retry_rc.top && my < retry_rc.bottom) {
        if (out_rc) *out_rc = retry_rc;
        return CLV_HOVER_RETRY;
    }

    return -1;
}

/* Recompute the rect for a previously-reported hover id -- used to
 * invalidate the element the cursor just left, which chatlv_hover_hit()
 * (keyed by the *new* position) cannot report any more. Returns 0 (rect
 * left untouched) if the element no longer exists, e.g. its container
 * was dismissed between hover events; the caller should fall back to a
 * full invalidate in that rare case. */
static int chatlv_hover_rect_for_id(ChatListView *lv, int id, RECT *out)
{
    if (id < 0) return 0;

    if (id >= CLV_STATE_CHIP_BASE) {
        if (!chatlv_list_empty(lv) || lv->state_id < 0) return 0;
        RECT client_rc;
        GetClientRect(lv->hwnd, &client_rc);
        EmptyStateLayout el;
        HDC hdc = GetDC(lv->hwnd);
        build_empty_state_layout(lv, hdc, &client_rc, &el);
        if (hdc) ReleaseDC(lv->hwnd, hdc);

        if (id == CLV_STATE_BUTTON_HIT) {
            if (!el.is_button) return 0;
            *out = el.button;
            return 1;
        }
        int i = id - CLV_STATE_CHIP_BASE;
        if (el.is_button || i < 0 || i >= el.n_chips) return 0;
        *out = el.chip[i];
        return 1;
    }

    if (id == CLV_HOVER_RETRY) {
        RECT client_rc;
        GetClientRect(lv->hwnd, &client_rc);
        int cw = client_rc.right - client_rc.left;
        int y = chatlv_items_bottom_y(lv);
        HDC hdc = GetDC(lv->hwnd);
        int ok = activity_retry_rect(lv, hdc, y, cw, out);
        if (hdc) ReleaseDC(lv->hwnd, hdc);
        return ok;
    }

    if (id >= CLV_THINK_HOVER_BASE) {
        int item_id = id - CLV_THINK_HOVER_BASE;
        int side_pad = ns_scale(BASE_SIDE_PAD, CLV_DPI(lv));
        RECT client_rc;
        GetClientRect(lv->hwnd, &client_rc);
        int box_left  = side_pad + lv->ai_indent;
        int box_right = (client_rc.right - client_rc.left) - side_pad;

        int y = lv->msg_gap - lv->scroll_y;
        ChatMsgItem *item = lv->msg_list ? lv->msg_list->head : NULL;
        while (item) {
            int h = item->measured_height;
            if (h == 0) { item = item->next; continue; }
            if (item->id == item_id && item->type == CHAT_ITEM_AI_TEXT &&
                item->u.ai.thinking_text && item->u.ai.thinking_text[0]) {
                int content_top = y + ns_scale(BASE_ICON_SIZE, CLV_DPI(lv))
                                   + ns_scale(4, CLV_DPI(lv));
                ThinkingLayout tl;
                HDC hdc = GetDC(lv->hwnd);
                build_thinking_layout(lv, hdc, item, box_left, box_right,
                                      content_top, &tl, NULL);
                if (hdc) ReleaseDC(lv->hwnd, hdc);
                SetRect(out, tl.row.x, tl.row.y,
                       tl.row.x + tl.row.w, tl.row.y + tl.row.h);
                return 1;
            }
            y += h + lv->msg_gap;
            item = item->next;
        }
        return 0;
    }

    int cy, ch;
    ChatMsgItem *citem = find_active_cmd_container(lv, &cy, &ch);
    if (!citem) return 0;

    RECT client_rc;
    GetClientRect(lv->hwnd, &client_rc);
    int cw = client_rc.right - client_rc.left;
    HDC mdc = GetDC(lv->hwnd);
    CmdCardGeometry g;
    build_cmd_card_geometry(lv, mdc, cy, ch, cw, &g);
    if (mdc) ReleaseDC(lv->hwnd, mdc);

    int hit, row;
    if (id == CLV_CARD_HIT_ID(HIT_DENY_ALL)) { hit = HIT_DENY_ALL; row = -1; }
    else if (id == CLV_CARD_HIT_ID(HIT_RUN_SELECTED)) { hit = HIT_RUN_SELECTED; row = -1; }
    else { row = id / 16; hit = id % 16; }

    return cmd_card_rect_for_hit(&g.layout, hit, row, out);
}

/* Is `id` a link/button the user can click -- i.e. should the cursor be
 * IDC_HAND rather than IDC_ARROW? Tag/text are hit-testable (for id
 * completeness) but not interactive. */
static int chatlv_hit_is_actionable(int id)
{
    if (id < 0) return 0;
    if (id >= CLV_STATE_CHIP_BASE) return 1;   /* empty-state chip/button */
    if (id == CLV_HOVER_RETRY) return 1;
    if (id >= CLV_THINK_HOVER_BASE) return 1;
    if (id == CLV_CARD_HIT_ID(HIT_DENY_ALL) || id == CLV_CARD_HIT_ID(HIT_RUN_SELECTED))
        return 1;
    int hit = id % 16;
    return hit == HIT_CHECKBOX;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Hit testing: convert mouse click to button action
 * ══════════════════════════════════════════════════════════════════════ */

static int on_lbuttondown(ChatListView *lv, int mx, int my)
{
    SetFocus(lv->hwnd);   /* Acquire keyboard focus so WM_KEYDOWN fires */

    /* Empty/no-key/no-session state: a suggestion chip sends its text as
     * a prompt (IDC_CHAT_SUGGESTION_BASE + i); the action button opens
     * Settings on Provider or the Session Manager (IDC_CHAT_STATE_ACTION)
     * -- both handled by ai_chat.c's WM_COMMAND (private 4xxx id block;
     * AI Assist Panel task 4). */
    if (chatlv_list_empty(lv) && lv->state_id >= 0) {
        RECT hit_rc;
        int hit = chatlv_empty_state_hit(lv, mx, my, &hit_rc);
        if (hit < 0) return 0;
        HWND state_parent = GetParent(lv->hwnd);
        if (!state_parent) return 1;
        if (hit == CLV_STATE_BUTTON_HIT) {
            PostMessage(state_parent, WM_COMMAND,
                       MAKEWPARAM(4023 /* IDC_CHAT_STATE_ACTION */, 0), 0);
        } else {
            int i = hit - CLV_STATE_CHIP_BASE;
            PostMessage(state_parent, WM_COMMAND,
                       MAKEWPARAM(4020 + i /* IDC_CHAT_SUGGESTION_BASE */, 0), 0);
        }
        return 1;
    }

    int side_pad = ns_scale(BASE_SIDE_PAD, CLV_DPI(lv));

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

        /* Check click on the Thinking disclosure row for AI items */
        if (item->type == CHAT_ITEM_AI_TEXT && my >= y && my < y + h
            && item->u.ai.thinking_text && item->u.ai.thinking_text[0]) {
            int box_left = side_pad + lv->ai_indent;
            RECT client_rc2;
            GetClientRect(lv->hwnd, &client_rc2);
            int box_right = (client_rc2.right - client_rc2.left) - side_pad;
            int content_top = y + ns_scale(BASE_ICON_SIZE, CLV_DPI(lv))
                               + ns_scale(4, CLV_DPI(lv));

            ThinkingLayout tl;
            HDC mdc0 = GetDC(lv->hwnd);
            build_thinking_layout(lv, mdc0, item, box_left, box_right,
                                  content_top, &tl, NULL);
            if (mdc0) ReleaseDC(lv->hwnd, mdc0);

            if (my >= tl.row.y && my < tl.row.y + tl.row.h &&
                mx >= tl.row.x && mx < tl.row.x + tl.row.w) {
                item->u.ai.thinking_collapsed =
                    !item->u.ai.thinking_collapsed;
                if (item->u.ai.thinking_collapsed) {
                    item->u.ai.thinking_scroll_y = 0;
                } else {
                    item->u.ai.thinking_autoscroll = 1;
                    /* The user opened it themselves -- remember for this
                     * session so a reply-start doesn't auto-collapse it
                     * (see ai_chat.c's WM_AI_STREAM handler). */
                    if (parent)
                        PostMessage(parent, WM_COMMAND,
                                    MAKEWPARAM(IDC_CHAT_THINKING_OPENED, 0), 0);
                }
                item->dirty = 1;
                recalc_layout(lv);
                InvalidateRect(lv->hwnd, NULL, FALSE);
                return 1;
            }
        }

        /* ── Command container hit testing ─────────────────────────── */
        if (item->type == CHAT_ITEM_COMMAND && !item->u.cmd.settled && my >= y && my < y + h) {
            RECT client_rc;
            GetClientRect(lv->hwnd, &client_rc);
            int cw2 = client_rc.right - client_rc.left;

            HDC mdc = GetDC(lv->hwnd);
            CmdCardGeometry g;
            build_cmd_card_geometry(lv, mdc, y, h, cw2, &g);
            if (mdc) ReleaseDC(lv->hwnd, mdc);

            /* ── Scrollbar click ──────────────────────────────────── */
            if (g.needs_scroll) {
                int sb_w = ns_scale(BASE_SCROLLBAR_W, CLV_DPI(lv));
                int sb_right = g.box_right - ns_scale(2, CLV_DPI(lv));
                int sb_left  = sb_right - sb_w;
                if (mx >= sb_left && mx <= sb_right &&
                    my >= g.clip_top && my < g.clip_bot) {
                    int track_h = lv->cmd_visible_h;
                    int max_cs = lv->cmd_total_h - lv->cmd_visible_h;
                    int rel = my - g.clip_top;
                    lv->cmd_scroll_y = (track_h > 0) ? (rel * max_cs) / track_h : 0;
                    if (lv->cmd_scroll_y < 0) lv->cmd_scroll_y = 0;
                    if (lv->cmd_scroll_y > max_cs) lv->cmd_scroll_y = max_cs;
                    InvalidateRect(lv->hwnd, NULL, FALSE);
                    return 1;
                }
            }

            int row_out = -1;
            int hit = approval_card_hit(&g.layout, mx, my, &row_out);

            if (hit == HIT_DENY_ALL) {
                if (parent)
                    PostMessage(parent, WM_COMMAND,
                                MAKEWPARAM(IDC_CMD_CANCEL_ALL, 0), 0);
                return 1;
            }
            if (hit == HIT_RUN_SELECTED) {
                /* Approve every checked, non-held, pending entry through
                 * the existing approve path (IDC_CMD_APPROVE_SEL also
                 * denies unselected pending rows -- exactly what "the
                 * rows you unchecked don't run" means) and start
                 * execution, same as Allow All did in v1. Disabled
                 * (nothing checked) still consumes the click, no-op. */
                if (g.run_enabled && parent)
                    PostMessage(parent, WM_COMMAND,
                                MAKEWPARAM(IDC_CMD_APPROVE_SEL, 0), 0);
                return 1;
            }
            if (row_out >= 0 && hit == HIT_CHECKBOX) {
                int real_idx = g.first_row + row_out;
                if (real_idx >= 0 && real_idx < g.n) {
                    ChatMsgItem *citem = g.cmd_items[real_idx];
                    if (citem && !citem->u.cmd.blocked && citem->u.cmd.approved == -1) {
                        citem->u.cmd.selected = !citem->u.cmd.selected;
                        InvalidateRect(lv->hwnd, NULL, FALSE);
                    }
                }
                return 1;
            }
            return 0;
        }

        y += h + lv->msg_gap;
        item = item->next;
    }

    /* Check click on [Retry] link in the activity indicator */
    if (lv->activity && lv->activity->phase != ACTIVITY_IDLE &&
        lv->activity->health == HEALTH_RED) {
        int act_h = ns_scale(BASE_ACTIVITY_H, CLV_DPI(lv));
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
        if (!lv->sel_active) {
            int mx = GET_X_LPARAM(lParam);
            int my_hov = GET_Y_LPARAM(lParam);
            RECT new_rc = {0, 0, 0, 0};
            int hit_id = chatlv_hover_hit(lv, mx, my_hov, &new_rc);
            NsHoverChange ch = ns_hover_move(&lv->hover, hit_id);
            if (ch.changed) {
                RECT old_rc;
                if (chatlv_hover_rect_for_id(lv, ch.old_id, &old_rc))
                    InvalidateRect(hwnd, &old_rc, FALSE);
                else if (ch.old_id >= 0)
                    InvalidateRect(hwnd, NULL, FALSE); /* element vanished */
                if (ch.new_id >= 0)
                    InvalidateRect(hwnd, &new_rc, FALSE);
            }
            if (hit_id >= 0 && !lv->hover_tracking) {
                TRACKMOUSEEVENT tme;
                tme.cbSize    = sizeof(tme);
                tme.dwFlags   = TME_LEAVE;
                tme.hwndTrack = hwnd;
                tme.dwHoverTime = 0;
                if (TrackMouseEvent(&tme)) lv->hover_tracking = 1;
            }
        }
        if (!lv->sel_active) break;
        int my = GET_Y_LPARAM(lParam);
        lv->sel_end_y = my + lv->scroll_y;
        /* Auto-scroll when dragging near edges */
        if (my < 0) {
            lv->scroll_y += my;  /* my is negative, scrolls up */
            clamp_scroll(lv);
            after_user_scroll(lv);
        } else if (my > lv->viewport_height) {
            lv->scroll_y += my - lv->viewport_height;
            clamp_scroll(lv);
            after_user_scroll(lv);
        }
        int sy = lv->sel_start_y, ey = lv->sel_end_y;
        if (sy > ey) { int tmp = sy; sy = ey; ey = tmp; }
        lv->sel_valid = (ey - sy > 4);  /* small threshold to avoid accidental select */
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSELEAVE: {
        lv->hover_tracking = 0;
        NsHoverChange ch = ns_hover_leave(&lv->hover);
        if (ch.changed) {
            RECT old_rc;
            if (chatlv_hover_rect_for_id(lv, ch.old_id, &old_rc))
                InvalidateRect(hwnd, &old_rc, FALSE);
            else
                InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            int hit_id = chatlv_hover_hit(lv, pt.x, pt.y, NULL);
            SetCursor(LoadCursor(NULL, chatlv_hit_is_actionable(hit_id)
                                        ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
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
            after_user_scroll(lv);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        /* Ctrl+Wheel: forward to AI chat parent for zoom. */
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            HWND parent = GetParent(hwnd);
            if (parent) SendMessage(parent, msg, wParam, lParam);
            return 0;
        }
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int scroll_amount = ns_scale(40, CLV_DPI(lv));  /* ~3 lines per notch */

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
                    && wi->u.ai.thinking_text[0]
                    && !wi->u.ai.thinking_collapsed
                    && pt.y >= wy && pt.y < wy + wh) {
                    int side_pad2 = ns_scale(BASE_SIDE_PAD, CLV_DPI(lv));
                    int box_left = side_pad2 + lv->ai_indent;
                    RECT crc;
                    GetClientRect(hwnd, &crc);
                    int box_right = (crc.right - crc.left) - side_pad2;
                    int content_top = wy + ns_scale(BASE_ICON_SIZE, CLV_DPI(lv))
                                       + ns_scale(4, CLV_DPI(lv));

                    ThinkingLayout tl;
                    int full_h = 0;
                    HDC tdc = GetDC(hwnd);
                    build_thinking_layout(lv, tdc, wi, box_left, box_right,
                                          content_top, &tl, &full_h);
                    if (tdc) ReleaseDC(hwnd, tdc);

                    int vis_h = tl.body.h;

                    /* Is cursor in the thinking body area and is there overflow? */
                    if (full_h > vis_h && pt.x >= tl.body.x && pt.x < tl.body.x + tl.body.w
                        && pt.y >= tl.body.y
                        && pt.y < tl.body.y + vis_h) {
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
            after_user_scroll(lv);
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
        /* Ctrl+= / Ctrl+- : forward to AI chat parent for zoom */
        if ((GetKeyState(VK_CONTROL) & 0x8000) &&
            (wParam == VK_OEM_PLUS  || wParam == (WPARAM)'='
          || wParam == VK_OEM_MINUS || wParam == (WPARAM)'-')) {
            HWND parent = GetParent(hwnd);
            if (parent) SendMessage(parent, msg, wParam, lParam);
            return 0;
        }
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
            after_user_scroll(lv);
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
