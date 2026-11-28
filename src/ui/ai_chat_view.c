/* The panel's view: layout helpers, panel state, busy indicator, display
 * rebuild, header, relayout and zoom. */
#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#endif

#include "ai_chat_internal.h"
#include "ai_header_layout.h"
#include "chat_rebuild.h"
#include "voice_config.h"
#include "voice_gate.h"
#include "voice_win.h"
#include "main_chord_win.h"

#ifdef _WIN32
#include <stdio.h>

/* Subclass for owner-drawn buttons: suppress WM_ERASEBKGND to prevent
 * white flicker during parent resize.  WM_DRAWITEM already fills the
 * entire button rect, so erasing is redundant. */
LRESULT CALLBACK ai_chat_btn_noerase_subclass(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    (void)dwRefData;
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_KEYDOWN && main_chord_now(wParam) != MCHORD_NONE) {
        /* A focused button hands the main-window chords (and Ctrl+0) to the
         * panel, as the input box and the list do. */
        HWND panel = GetParent(hwnd);
        if (panel) SendMessage(panel, msg, wParam, lParam);
        return 0;
    }
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, ai_chat_btn_noerase_subclass, uIdSubclass);
    if (msg == WM_SETTEXT) {
        /* The Send button flips to Stop and back as the assistant becomes
         * busy and idle: the mic button's disabled look follows. */
        LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
        HWND panel = GetParent(hwnd);
        AiChatData *pd = panel ? (AiChatData *)(LONG_PTR)GetWindowLongPtr(panel, GWLP_USERDATA)
                               : NULL;
        if (pd && pd->hSendBtn == hwnd) ai_chat_voice_busy_changed(pd);
        return r;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

/* Convert device pixels back to a 96-DPI base value (inverse of ns_scale,
 * round-half-up). dpi <= 0 is treated as 96. */
int ai_chat_unscale(int px, int dpi)
{
    if (dpi <= 0) dpi = 96;
    return (px * 96 + dpi / 2) / dpi;
}

/* The message box's height in device pixels: the stored 96-DPI base, scaled
 * and clamped to what the panel can hold (never under two input lines, never
 * over half the panel). The one place it is decided -- used by the layout,
 * ai_chat_relayout() and the placeholder at creation so they cannot drift. */
int ai_chat_input_h(const AiChatData *d)
{
    RECT rc = {0, 0, 1, 1};
    if (d->hwnd) GetClientRect(d->hwnd, &rc);
    int base = d->input_base_h > 0 ? d->input_base_h : AI_INPUT_BASE_H;
    return ai_composer_clamp_h(ns_scale(base, d->dpi), rc.bottom,
                               d->input_line_h, d->dpi);
}

/* Compute the panel's top-level tiling (header/thread/status/sash/composer)
 * for the current client size -- the one place composer_h is decided, shared
 * by ai_chat_relayout(), WM_PAINT and every status-line hit-test helper below so
 * they never disagree with each other. */
void ai_chat_compute_layout(AiChatData *d, AiPanelLayout *out)
{
    RECT rc = {0, 0, 1, 1};
    if (d->hwnd) GetClientRect(d->hwnd, &rc);
    NsRect panel = { 0, 0, rc.right, rc.bottom };

    int margin = ns_scale(5, d->dpi);
    int input_h = ai_chat_input_h(d);
    int composer_h = input_h + margin;
    ai_panel_layout(panel, d->dpi, composer_h, out);
}

/* Decide and push the empty/no-key/no-session state (ai_panel_states.h)
 * to the chat list view. chat_listview only paints/hit-tests it while
 * the message list actually has zero items, so it is harmless (and
 * cheap) to call this any time the inputs to the decision might have
 * changed: the panel is shown, the session changes, the API key changes,
 * or the conversation is reset. See the spec's "Empty and blocked
 * states (frame C)" section. */
void ai_chat_update_panel_state(AiChatData *d)
{
    if (!d || !d->hChatList) return;

    int state;
    if (d->forced_state >= 0)
        state = d->forced_state;
    else if (!d->active_io)
        state = AI_STATE_NO_SESSION;
    else if (d->api_key[0] == '\0')
        state = AI_STATE_NO_KEY;
    else
        state = AI_STATE_EMPTY;

    chat_listview_set_state(d->hChatList, state, d->context_lines);

    /* The voice hint: one more line under the empty state while voice is on. */
    char hint[160];
    hint[0] = '\0';
    if (state == AI_STATE_EMPTY && voice_gate_on()) {
        const Settings *vs = ui_current_settings();
        (void)ai_panel_voice_hint(vs ? vs->voice_ptt_mode : VOICE_MODE_TOGGLE,
                                  vs ? vs->voice_ptt_key : VOICE_PTT_RIGHT_SHIFT,
                                  vs ? vs->voice_auto_send : 0,
                                  hint, sizeof(hint));
    }
    chat_listview_set_state_hint(d->hChatList, hint);
}

/* Start (or replace) the busy-indicator text shown in place of the status
 * line's context meter numbers while a command/response is in flight (the
 * per-message activity dot + word in the header covers most of the same
 * ground, but callers here have more specific text, e.g. "waiting for
 * output"). */
void ai_chat_start_indicator(AiChatData *d, const char *base)
{
    if (!d) return;
    if (strcmp(base, "thinking") == 0)
        d->context_label[0] = '\0';   /* inline indicator shows timing */
    else
        snprintf(d->context_label, sizeof(d->context_label), "%c%s",
            base[0] >= 'a' && base[0] <= 'z' ? (char)(base[0]-32) : base[0],
            base + 1);
    ai_chat_header_invalidate(d);
}

/* Free all thinking history entries. */
void ai_chat_thinking_history_clear(AiChatData *d)
{
    for (int i = 0; i < AI_MAX_MESSAGES; i++) {
        free(d->thinking_history[i]);
        d->thinking_history[i] = NULL;
    }
    d->stream_thinking[0] = '\0';
    d->stream_thinking_len = 0;
}

/* Rebuild the chat display from the conversation history.
 * Used when switching sessions (and redocking) to replay the loaded
 * conversation. Which rows a stored turn yields is chat_rebuild_replay()'s
 * (src/core/chat_rebuild.c, native tests): the rows of every tool call and
 * its result, the replayed command rows of the session's run log, the live
 * batches' cards. This clears the list, calls it and invalidates the
 * ChatListView. */
void ai_chat_rebuild_display(AiChatData *d)
{
    if (!d) return;

    chat_msg_list_clear(&d->msg_list);
    d->stream_ai_item = NULL;

    /* No welcome placeholder any more -- an empty msg_list here (no real
     * messages replayed) is painted by chat_listview as the empty/no-key/
     * no-session state instead (see ai_chat_update_panel_state()). */
    chat_rebuild_replay(&d->msg_list, &d->conv, d->thinking_history,
                        d->active_state ? &d->active_state->run_log : NULL,
                        d->active_state ? &d->active_state->batches : NULL);

    if (d->hChatList) {
        chat_listview_invalidate(d->hChatList);
        chat_listview_scroll_to_bottom(d->hChatList);
    }
    ai_chat_update_context_bar(d);
}

/* Add a tooltip to a child control */
void ai_chat_add_tooltip(HWND hTooltip, HWND hCtrl, const char *text)
{
    if (!hTooltip || !hCtrl) return;
    TOOLINFO ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize   = sizeof(ti);
    ti.uFlags   = TTF_SUBCLASS | TTF_IDISHWND;
    ti.hwnd     = GetParent(hCtrl);
    ti.uId      = (UINT_PTR)hCtrl;
    ti.lpszText = (LPSTR)text;
    SendMessage(hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
}

/* Draw one of the header's square icon buttons (New chat / Save / Undock
 * or Dock): bg_secondary fill, border stroke, R_CTRL radius, a built-in
 * vector icon centred -- no label, per the frame-B header design. These
 * are real child windows, so hover comes from the themed_button subclass
 * rather than ns_hover. */
void ai_chat_draw_header_icon_button(LPDRAWITEMSTRUCT dis,
                                     const ThemeColors *theme, int dpi,
                                     NsIconId icon)
{
    if (!dis || !theme) return;
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    int pressed = (dis->itemState & ODS_SELECTED) != 0;
    themed_button_track_hover(dis->hwndItem);
    int hot = !pressed && themed_button_is_hot(dis->hwndItem);
    COLORREF bg = theme_cr(pressed ? theme->bg_primary
                           : hot ? ns_tokens()->bg_secondary.hover
                                 : theme->bg_secondary);
    COLORREF fg = theme_cr(theme->text_main);
    COLORREF bdr = theme_cr(theme->border);

    HBRUSH hBgBr = CreateSolidBrush(theme_cr(theme->bg_primary));
    FillRect(hdc, &rc, hBgBr);
    DeleteObject(hBgBr);

    int radius = ns_scale(R_CTRL, dpi);
    ns_draw_round_fill(hdc, &rc, radius, bg, 255);
    ns_draw_round_stroke(hdc, &rc, radius, bdr, STROKE_HAIRLINE);

    ns_icon_draw(hdc, icon, &rc, fg, (UINT)dpi);
}

/* The one-word activity status the header shows next to the session name;
 * "" while idle. */
static const char *ai_chat_header_word(const AiChatData *d)
{
    switch (d->activity.phase) {
    case ACTIVITY_PROCESSING: return "Processing";
    case ACTIVITY_THINKING:   return "Thinking";
    case ACTIVITY_RESPONDING: return "Responding";
    case ACTIVITY_EXECUTING:  return "Executing";
    case ACTIVITY_WAITING:    return "Waiting";
    default:                  return "";
    }
}

/* What the header's status slot says: a status note (context_label), else
 * the voice cue, else the activity word; NULL when there is none. *is_word
 * is 1 for the activity word (painted in the health colour, the notes in
 * text_dim). UTF-8. */
const char *ai_chat_header_text(const AiChatData *d, int *is_word)
{
    const char *word = ai_chat_header_word(d);
    const char *t = ai_header_status_pick(d->context_label, d->voice_cue, word);
    if (is_word) *is_word = (t != NULL && t == word);
    return t;
}

/* UTF-8 to a wide buffer of `cap` chars; returns the length without the
 * terminating NUL (0 on failure or empty). */
static int ai_chat_header_widen(const char *u, wchar_t *w, int cap)
{
    if (!u || cap < 1) return 0;
    w[0] = L'\0';
    int n = MultiByteToWideChar(CP_UTF8, 0, u, -1, w, cap);
    if (n <= 0) { w[cap - 1] = L'\0'; return 0; }
    return n - 1;
}

#define HEADER_TEXT_MAX 300

/* Width of the activity dot and the gap after it; 0 while idle. */
static int ai_chat_header_dot_w(const AiChatData *d)
{
    if (d->activity.phase == ACTIVITY_IDLE) return 0;
    return ns_scale(6, d->dpi) + ns_scale(SP_XS, d->dpi);
}

/* Header geometry, shared by ai_chat_relayout() (buttons and the session
 * label), ai_chat_header_sync() (the label and the status tooltip, as the
 * status comes and goes) and the header paint: measures the session name
 * (FONT_TITLE, the label's own font) and the status (the activity dot, if
 * any, plus the picked text in FONT_CAPTION), then lets ai_header_layout()
 * place the row so the status follows the name and can never land under the
 * buttons. `hdc` may be NULL (an unclipped name and no status are assumed);
 * pass a real one for accurate sizes. */
static void ai_chat_header_layout(AiChatData *d, NsRect header, HDC hdc,
                                  AiHeaderLayout *out)
{
    int pad_sm = ns_scale(SP_SM, d->dpi);
    AiHeaderMeasure m;
    m.header       = header;
    m.pad          = pad_sm;
    m.gap          = pad_sm;
    m.btn_h        = ns_scale(SZ_CTRL_H, d->dpi);
    m.buttons_w    = 3 * m.btn_h + 2 * pad_sm;
    m.name_w       = header.w;
    m.min_name_w   = 0;
    m.status_w     = 0;
    m.min_status_w = 0;

    if (hdc) {
        SIZE sz = {0, 0};
        HFONT title = ns_font(FONT_TITLE, d->dpi);
        HGDIOBJ old = title ? SelectObject(hdc, title) : NULL;

        /* A little slack so the label's own ellipsis never trims a name
         * that the layout has judged to fit. */
        int nlen = (int)strlen(d->session_name);
        if (nlen > 0) GetTextExtentPoint32A(hdc, d->session_name, nlen, &sz);
        m.name_w = sz.cx + ns_scale(SP_XS, d->dpi) / 2;
        /* The name is never narrowed below its first ~8 characters. */
        sz.cx = 0; sz.cy = 0;
        if (nlen > 0)
            GetTextExtentPoint32A(hdc, d->session_name, nlen < 8 ? nlen : 8, &sz);
        m.min_name_w = sz.cx;

        const char *text = ai_chat_header_text(d, NULL);
        if (text) {
            wchar_t w[HEADER_TEXT_MAX];
            int wl = ai_chat_header_widen(text, w, HEADER_TEXT_MAX);
            HFONT cap = ns_font(FONT_CAPTION, d->dpi);
            if (cap) SelectObject(hdc, cap);
            sz.cx = 0; sz.cy = 0;
            if (wl > 0) GetTextExtentPoint32W(hdc, w, wl, &sz);
            int dot_w = ai_chat_header_dot_w(d);
            m.status_w = dot_w + sz.cx + ns_scale(SP_XS, d->dpi) / 2;
            /* Dropped only below the dot, its gap and about three letters. */
            SIZE three = {0, 0};
            GetTextExtentPoint32W(hdc, L"mmm", 3, &three);
            m.min_status_w = dot_w + three.cx;
            if (m.min_status_w > m.status_w) m.min_status_w = m.status_w;
        }
        if (old) SelectObject(hdc, old);
    }
    ai_header_layout(&m, out);
}

/* Move the session label to the rect the header layout gives it (only when
 * it changed, so a paint that finds nothing to do moves nothing). */
static void ai_chat_header_place_label(AiChatData *d, const AiHeaderLayout *hl)
{
    if (!d->hSessionLabel) return;
    RECT cur;
    GetWindowRect(d->hSessionLabel, &cur);
    MapWindowPoints(NULL, d->hwnd, (POINT *)&cur, 2);
    if (cur.left == hl->name.x && cur.top == hl->name.y &&
        cur.right - cur.left == hl->name.w &&
        cur.bottom - cur.top == hl->name.h)
        return;
    MoveWindow(d->hSessionLabel, hl->name.x, hl->name.y, hl->name.w,
               hl->name.h, TRUE);
}

/* Move the header status's tooltip tool onto the status slot (an empty rect
 * while nothing is shown), only when it changed. The text comes from
 * TTN_GETDISPINFO, so it is always the full, unclipped status. */
static void ai_chat_header_tip_update(AiChatData *d, const AiHeaderLayout *hl)
{
    if (!d->hTooltip) return;
    NsRect r = { 0, 0, 0, 0 };
    if (hl->status_shown && hl->status.w > 0) r = hl->status;
    if (r.x == d->header_tip.x && r.y == d->header_tip.y &&
        r.w == d->header_tip.w && r.h == d->header_tip.h)
        return;
    d->header_tip = r;
    TOOLINFO ti;
    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.hwnd   = d->hwnd;
    ti.uId    = (UINT_PTR)(STATUS_TOOL_BASE + STATUS_TOOL_HEADER);
    if (r.w > 0 && r.h > 0) SetRect(&ti.rect, r.x, r.y, r.x + r.w, r.y + r.h);
    else SetRectEmpty(&ti.rect);
    SendMessage(d->hTooltip, TTM_NEWTOOLRECT, 0, (LPARAM)&ti);
}

/* Re-place the session label for the current status. Called before
 * the panel paints: the word (and so the name's room) changes with the
 * phase, between resizes. */
void ai_chat_header_sync(AiChatData *d)
{
    if (!d || !d->hwnd || !d->hSessionLabel) return;
    AiPanelLayout l;
    ai_chat_compute_layout(d, &l);
    HDC hdc = GetDC(d->hwnd);
    AiHeaderLayout hl;
    ai_chat_header_layout(d, l.header, hdc, &hl);
    if (hdc) ReleaseDC(d->hwnd, hdc);
    ai_chat_header_place_label(d, &hl);
    ai_chat_header_tip_update(d, &hl);
}

/* Reposition all child controls from ai_panel_layout()'s header/thread/
 * status/sash/composer tiling (ai_chat_compute_layout()). Called once at the
 * end of WM_CREATE and again on every WM_SIZE. */
void ai_chat_relayout(AiChatData *d)
{
    if (!d || !d->hwnd) return;

    int margin = ns_scale(5, d->dpi);
    int input_h = ai_chat_input_h(d);
    int send_w = ns_scale(40, d->dpi);

    AiPanelLayout l;
    ai_chat_compute_layout(d, &l);

    /* ---- Header: session name (left), the status (painted -- see
     * WM_PAINT) after it, three icon buttons right-aligned; all
     * placed by ai_header_layout(). ---- */
    {
        int btn_h = ns_scale(SZ_CTRL_H, d->dpi);
        int pad_sm = ns_scale(SP_SM, d->dpi);
        AiHeaderLayout hl;
        HDC hdc = GetDC(d->hwnd);
        ai_chat_header_layout(d, l.header, hdc, &hl);
        if (hdc) ReleaseDC(d->hwnd, hdc);

        int by = hl.buttons.y;
        int bx = hl.buttons.x + hl.buttons.w - btn_h;
        if (d->hUndockBtn)
            MoveWindow(d->hUndockBtn, bx, by, btn_h, btn_h, TRUE);
        bx -= pad_sm + btn_h;
        if (d->hSaveBtn)
            MoveWindow(d->hSaveBtn, bx, by, btn_h, btn_h, TRUE);
        bx -= pad_sm + btn_h;
        if (d->hNewChatBtn)
            MoveWindow(d->hNewChatBtn, bx, by, btn_h, btn_h, TRUE);
        ai_chat_header_place_label(d, &hl);
    }

    /* ---- Thread: the chat list fills l.thread. ---- */
    {
        int disp_w = l.thread.w - CSB_WIDTH;
        if (disp_w < 1) disp_w = 1;
        int disp_h = l.thread.h < 1 ? 1 : l.thread.h;
        if (d->hDisplay)
            MoveWindow(d->hDisplay, l.thread.x, l.thread.y, disp_w, disp_h, TRUE);
        if (d->hDisplayScrollbar)
            MoveWindow(d->hDisplayScrollbar, l.thread.x + disp_w, l.thread.y,
                       CSB_WIDTH, disp_h, TRUE);
        if (d->hChatList)
            chat_listview_relayout(d->hChatList);
    }

    /* ---- Composer: unchanged position and behaviour; keeps its own
     * height (composer_h in ai_chat_compute_layout() is exactly
     * input_h + margin, so l.composer.y lands where input_y always did). */
    {
        int input_y = l.composer.y;
        int input_w = l.composer.w - send_w - margin * 3 - CSB_WIDTH;
        if (d->hMicBtn) input_w -= send_w + margin;   /* the mic button, left of Send */
        if (input_w < 1) input_w = 1;
        if (d->hInput)
            MoveWindow(d->hInput, l.composer.x + margin, input_y,
                       input_w, input_h, TRUE);
        if (d->hInputScrollbar)
            MoveWindow(d->hInputScrollbar, l.composer.x + margin + input_w,
                       input_y, CSB_WIDTH, input_h, TRUE);
        if (d->hSendBtn)
            MoveWindow(d->hSendBtn, l.composer.x + l.composer.w - send_w - margin,
                       input_y, send_w, input_h, TRUE);
        if (d->hMicBtn)
            MoveWindow(d->hMicBtn, l.composer.x + l.composer.w - 2 * send_w - 2 * margin,
                       input_y, send_w, input_h, TRUE);
    }

    ai_chat_status_tools_update(d);
}

RECT ai_chat_to_RECT(NsRect r)
{
    RECT rc = { r.x, r.y, r.x + r.w, r.y + r.h };
    return rc;
}

/* Ask for a repaint of the header row without erasing it (the paint fills
 * the status strip itself). Call it when the header's status changes (the
 * activity word, a status note, the voice cue) or goes away with no
 * heartbeat left to repaint it. */
void ai_chat_header_invalidate(AiChatData *d)
{
    if (!d || !d->hwnd) return;
    AiPanelLayout l;
    ai_chat_compute_layout(d, &l);
    RECT rc = ai_chat_to_RECT(l.header);
    InvalidateRect(d->hwnd, &rc, FALSE);
}

/* Paint the header's status: while a session is active the activity dot,
 * then the status text (a note in text_dim, or the activity word in its
 * health colour), after the session label. Every rect comes from
 * ai_header_layout(): the slot follows the name and is left out when the
 * row has no room for it. */
void ai_chat_paint_header(AiChatData *d, HDC hdc)
{
    const ThemeTokens *tok = ns_tokens();
    AiPanelLayout l;
    ai_chat_compute_layout(d, &l);

    AiHeaderLayout hl;
    ai_chat_header_layout(d, l.header, hdc, &hl);

    /* The header is not erased on a repaint (the heartbeat invalidates it
     * with bErase FALSE, so as not to flicker the label and buttons), so
     * the strip the status can occupy -- from the end of the name to the
     * buttons -- is filled with the panel background here first. It is also
     * what clears the status when it goes away. */
    {
        int x0 = hl.name.x + hl.name.w;
        int x1 = hl.buttons.x;
        if (x1 > x0 && hl.status.h > 0) {
            RECT strip = { x0, hl.status.y, x1, hl.status.y + hl.status.h };
            FillRect(hdc, &strip, d->hBrBgPrimary);
        }
    }

    int is_word = 0;
    const char *text = ai_chat_header_text(d, &is_word);
    if (!text || !hl.status_shown) return;

    HGDIOBJ old_f = SelectObject(hdc, ns_font(FONT_CAPTION, d->dpi));
    int dot_w = ai_chat_header_dot_w(d);
    COLORREF text_clr = theme_cr(tok->text_dim);

    if (dot_w > 0) {
        int dot_sz = ns_scale(6, d->dpi);
        int dot_x = hl.status.x;
        int dot_y = hl.status.y + (hl.status.h - dot_sz) / 2;

        COLORREF dot_clr;
        switch (d->activity.health) {
        case HEALTH_YELLOW:
            dot_clr = RGB(((d->theme->chat.indicator_yellow) >> 16) & 0xFF,
                          ((d->theme->chat.indicator_yellow) >> 8) & 0xFF,
                          (d->theme->chat.indicator_yellow) & 0xFF);
            break;
        case HEALTH_RED:
            dot_clr = RGB(((d->theme->chat.indicator_red) >> 16) & 0xFF,
                          ((d->theme->chat.indicator_red) >> 8) & 0xFF,
                          (d->theme->chat.indicator_red) & 0xFF);
            break;
        default:
            dot_clr = RGB(((d->theme->chat.indicator_green) >> 16) & 0xFF,
                          ((d->theme->chat.indicator_green) >> 8) & 0xFF,
                          (d->theme->chat.indicator_green) & 0xFF);
            break;
        }

        /* Only the dot pulses; the activity word keeps the steady colour so
         * its contrast on bg_primary does not dip with the phase. */
        if (is_word) text_clr = dot_clr;
        if (d->pulse_toggle && !ns_reduced_motion()) {
            COLORREF bg_c = RGB(((d->theme->bg_primary) >> 16) & 0xFF,
                                ((d->theme->bg_primary) >> 8) & 0xFF,
                                (d->theme->bg_primary) & 0xFF);
            dot_clr = RGB((GetRValue(dot_clr) + GetRValue(bg_c)) / 2,
                          (GetGValue(dot_clr) + GetGValue(bg_c)) / 2,
                          (GetBValue(dot_clr) + GetBValue(bg_c)) / 2);
        }

        HBRUSH hDotBr = CreateSolidBrush(dot_clr);
        HPEN hDotPn = CreatePen(PS_SOLID, 1, dot_clr);
        HGDIOBJ ob = SelectObject(hdc, hDotBr);
        HGDIOBJ op = SelectObject(hdc, hDotPn);
        Ellipse(hdc, dot_x, dot_y, dot_x + dot_sz, dot_y + dot_sz);
        SelectObject(hdc, op);
        SelectObject(hdc, ob);
        DeleteObject(hDotPn);
        DeleteObject(hDotBr);
    }

    wchar_t w[HEADER_TEXT_MAX];
    if (ai_chat_header_widen(text, w, HEADER_TEXT_MAX) > 0) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, text_clr);
        RECT wrc;
        wrc.left   = hl.status.x + dot_w;
        wrc.top    = hl.status.y;
        wrc.right  = hl.status.x + hl.status.w;
        wrc.bottom = hl.status.y + hl.status.h;
        DrawTextW(hdc, w, -1, &wrc,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    if (old_f) SelectObject(hdc, old_f);
}

/* Zoom the AI chat content fonts. Resizes the chat ListView fonts
 * (user messages + AI responses) and the user input textbox font.
 * Buttons keep the UI font; context bar and session label keep the
 * small font. */
void ai_chat_apply_zoom(AiChatData *d, int delta)
{
    if (!d) return;
    ai_chat_zoom_to(d, app_font_zoom(d->ui_font_size, delta));
}

/* As ai_chat_apply_zoom, to an absolute size (Ctrl+0 resets to the default). */
void ai_chat_zoom_to(AiChatData *d, int new_size)
{
    if (!d) return;
    if (new_size == d->ui_font_size)
        return;
    d->ui_font_size = new_size;

    if (!d->hChatList)
        return;

    int h = -MulDiv(new_size, d->dpi, 72);

    if (d->hChatFont) DeleteObject(d->hChatFont);
    if (d->hBoldFont) DeleteObject(d->hBoldFont);
    if (d->hMonoFont) DeleteObject(d->hMonoFont);

    d->hChatFont = CreateFont(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_TT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_SWISS, APP_FONT_UI_FACE);
    d->hBoldFont = CreateFont(h, 0, 0, 0, FW_BOLD,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
        APP_FONT_UI_FACE);
    d->hMonoFont = CreateFont(h, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
        d->ai_font_name[0] ? d->ai_font_name : "Consolas");

    chat_listview_set_fonts(d->hChatList, d->hChatFont,
                            d->hMonoFont, d->hBoldFont,
                            d->hSmallFont);
    chat_listview_relayout(d->hChatList);

    /* Apply the same zoomable font to the user input textbox so its text
     * scales with the chat. Buttons, labels and the context bar continue
     * to use the fixed UI font (d->hFont). */
    if (d->hInput && d->hChatFont) {
        SendMessage(d->hInput, WM_SETFONT, (WPARAM)d->hChatFont, TRUE);
        HDC hdc_m = GetDC(d->hInput);
        if (hdc_m) {
            HGDIOBJ old_m = SelectObject(hdc_m, (HGDIOBJ)d->hChatFont);
            TEXTMETRIC tm_m;
            GetTextMetrics(hdc_m, &tm_m);
            d->input_line_h = tm_m.tmHeight + tm_m.tmExternalLeading;
            SelectObject(hdc_m, old_m);
            ReleaseDC(d->hInput, hdc_m);
        }
        ai_chat_input_sync_scroll(d);
    }
}

/* The panel moved to a monitor with another scale (WM_DPICHANGED, or the main
 * window telling it so): take the new DPI, re-fetch the cached UI fonts, and
 * rebuild the fonts the panel owns -- the small bold font and the three
 * zoomable chat fonts -- from the same point sizes, so the user's zoom level
 * is kept. The old handles are deleted once nothing uses them. Then lay out
 * again: paint, hit-testing and the tooltip rects all read d->dpi. */
void ai_chat_apply_dpi(AiChatData *d, int dpi)
{
    if (!d) return;
    if (dpi <= 0) dpi = 96;
    d->dpi = dpi;

    d->hFont = ns_font(FONT_BODY, dpi);
    if (d->hSessionLabel)
        SendMessage(d->hSessionLabel, WM_SETFONT,
                    (WPARAM)ns_font(FONT_TITLE, dpi), TRUE);

    HFONT old_small = d->hSmallFont;
    d->hSmallFont = CreateFont(app_font_px_height(8, dpi), 0, 0, 0, FW_BOLD,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                               "Segoe UI");

    HFONT old_chat = d->hChatFont, old_bold = d->hBoldFont, old_mono = d->hMonoFont;
    if (d->hChatList) {
        chat_listview_set_dpi(d->hChatList, (UINT)dpi);
        int h = app_font_px_height(d->ui_font_size, dpi);
        d->hChatFont = CreateFont(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_TT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_SWISS, APP_FONT_UI_FACE);
        d->hBoldFont = CreateFont(h, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_TT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_SWISS, APP_FONT_UI_FACE);
        d->hMonoFont = CreateFont(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_TT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  FIXED_PITCH | FF_MODERN,
                                  d->ai_font_name[0] ? d->ai_font_name : "Consolas");
        chat_listview_set_fonts(d->hChatList,
                                d->hChatFont ? d->hChatFont : d->hFont,
                                d->hMonoFont, d->hBoldFont, d->hSmallFont);
        chat_listview_relayout(d->hChatList);
    }

    /* The input box follows the chat font (as at creation and in zoom). */
    HFONT in_font = d->hChatFont ? d->hChatFont : d->hFont;
    if (d->hInput && in_font) {
        SendMessage(d->hInput, WM_SETFONT, (WPARAM)in_font, TRUE);
        HDC hdc_m = GetDC(d->hInput);
        if (hdc_m) {
            HGDIOBJ old_m = SelectObject(hdc_m, (HGDIOBJ)in_font);
            TEXTMETRIC tm_m;
            GetTextMetrics(hdc_m, &tm_m);
            d->display_line_h = tm_m.tmHeight + tm_m.tmExternalLeading;
            d->input_line_h = d->display_line_h;
            SelectObject(hdc_m, old_m);
            ReleaseDC(d->hInput, hdc_m);
        }
    }

    /* Nothing refers to the old handles any more. */
    if (old_small && old_small != d->hSmallFont) DeleteObject(old_small);
    if (old_chat && old_chat != d->hChatFont) DeleteObject(old_chat);
    if (old_bold && old_bold != d->hBoldFont) DeleteObject(old_bold);
    if (old_mono && old_mono != d->hMonoFont) DeleteObject(old_mono);

    ai_chat_relayout(d);
    ai_chat_input_sync_scroll(d);
    if (d->hwnd) InvalidateRect(d->hwnd, NULL, TRUE);
}

#endif /* _WIN32 */
