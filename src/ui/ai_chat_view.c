/* The panel's view: layout helpers, panel state, busy indicator, display
 * rebuild, header, relayout and zoom. */
#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#endif

#include "ai_chat_internal.h"

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
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, ai_chat_btn_noerase_subclass, uIdSubclass);
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
    ai_chat_invalidate_status_line(d);
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

/* Append one CHAT_ITEM_COMMAND per entry in a batch's queue, in order,
 * tagged with the batch's id (chat_msg_set_batch) -- the inverse of what
 * WM_AI_RESPONSE builds live, used to rebuild a pending card wherever the
 * command items themselves aren't carried across a rebuild: a session
 * switch (ai_chat_rebuild_display, below) and --ui-demo's "batches"/etc.
 * states (ai_chat_apply_demo_extras, which builds real CmdBatch entries
 * from the canned ApprovalQueue and lets this same replay draw them).
 * Status maps straight onto the fields the list view reads: PENDING/
 * BLOCKED stay unsettled (part of the active card, approved=-1, which
 * chat_msg_set_command already defaults to); DENIED/APPROVED/EXECUTING/
 * COMPLETED all render inline (settled=1) -- APPROVED is included because
 * a card settles the moment every entry is decided, well before the
 * dispatcher actually gets around to sending it (see
 * ai_chat_settle_batch_if_done()). */
static void append_batch_command_items(ChatMsgList *list, const CmdBatch *batch)
{
    if (!list || !batch) return;
    for (int i = 0; i < batch->q.count; i++) {
        const ApprovalEntry *e = &batch->q.entries[i];
        ChatMsgItem *item = chat_msg_append(list, CHAT_ITEM_COMMAND, "");
        if (!item) continue;
        chat_msg_set_command(item, e->command, e->safety,
                             e->status == APPROVE_BLOCKED);
        chat_msg_set_quoted(item, e->quoted);
        chat_msg_set_reason(item, e->reason);
        chat_msg_set_batch(item, batch->id);
        chat_msg_set_note(item, e->note);
        switch (e->status) {
        case APPROVE_APPROVED:
        case APPROVE_EXECUTING:
        case APPROVE_COMPLETED:
            item->u.cmd.approved = 1;
            item->u.cmd.settled = 1;
            break;
        case APPROVE_DENIED:
            item->u.cmd.approved = 0;
            item->u.cmd.settled = 1;
            break;
        case APPROVE_PENDING:
        case APPROVE_BLOCKED:
        default:
            break;  /* stays unsettled -- part of the active card */
        }
    }

    /* Run state comes from the same queue, so a rebuilt display (session
     * switch, --ui-demo=executing) shows "running"/"ran" exactly where the
     * live panel does instead of falling back to "queued". The batch is
     * alive here by definition -- a rebuild only ever replays batches still
     * in the set -- hence batch_ending = 0. */
    chat_msg_batch_sync_run(list, batch->id, &batch->q, 0);
}

/* Rebuild the chat display from the conversation history.
 * Used when switching sessions to replay the loaded conversation.
 * Populates the ChatMsgList and invalidates the ChatListView. */
void ai_chat_rebuild_display(AiChatData *d)
{
    if (!d) return;

    chat_msg_list_clear(&d->msg_list);
    d->stream_ai_item = NULL;

    /* No welcome placeholder any more -- an empty msg_list here (no real
     * messages replayed below) is painted by chat_listview as the empty/
     * no-key/no-session state instead (see ai_chat_update_panel_state()). */

    /* Replay messages, skipping the system prompt at index 0 */
    for (int i = 1; i < d->conv.msg_count; i++) {
        const AiMessage *msg = &d->conv.messages[i];

        if (msg->role == AI_ROLE_USER) {
            chat_msg_append(&d->msg_list, CHAT_ITEM_USER,
                            ai_msg_content(msg));
        } else if (msg->role == AI_ROLE_ASSISTANT) {
            /* Tool-use blocks first, matching the live "using tool: <name>"
             * status row posted while the call is in flight. */
            if (msg->n_tool_calls > 0 && msg->tool_calls) {
                for (int ti = 0; ti < msg->n_tool_calls; ti++) {
                    char tool_call_text[256];
                    char fetch_desc[160] = "";
                    if (strcmp(msg->tool_calls[ti].name, "web_fetch") == 0)
                        (void)web_fetch_describe(msg->tool_calls[ti].input_json,
                                                 fetch_desc, sizeof(fetch_desc));
                    if (fetch_desc[0])
                        snprintf(tool_call_text, sizeof(tool_call_text),
                                 "using tool: %s - %s",
                                 msg->tool_calls[ti].name, fetch_desc);
                    else
                        snprintf(tool_call_text, sizeof(tool_call_text),
                                 "using tool: %s", msg->tool_calls[ti].name);
                    chat_msg_append(&d->msg_list, CHAT_ITEM_TOOL_CALL,
                                    tool_call_text);
                }
            }
            const char *content = ai_msg_content(msg);
            if (content && content[0] != '\0') {
                ChatMsgItem *item = chat_msg_append(&d->msg_list,
                                                     CHAT_ITEM_AI_TEXT,
                                                     content);
                if (item && d->thinking_history[i] &&
                    d->thinking_history[i][0]) {
                    chat_msg_set_thinking(item, d->thinking_history[i]);
                    item->u.ai.thinking_complete = 1;
                }
            }
        } else if (msg->role == AI_ROLE_TOOL) {
            const char *content = ai_msg_content(msg);
            size_t clen = content ? strlen(content) : 0;
            char preview[256];
            size_t plen = clen < 200 ? clen : 200;
            memcpy(preview, content ? content : "", plen);
            preview[plen] = '\0';
            chat_msg_append(&d->msg_list, CHAT_ITEM_TOOL_RESULT, preview);
            if (clen > 200) {
                char warn[256];
                agentic_truncation_warning(msg->tool_name, warn, sizeof(warn));
                chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS, warn);
            }
        }
        /* Skip system messages injected mid-conversation */
    }

    /* Pending command batches: rebuild one card per batch still in the
     * active session's set, oldest first -- the interleaving with earlier
     * conversation turns isn't reconstructed (every batch's cards land
     * after the full replay above), only which cards exist and their
     * state (see docs/superpowers/specs/
     * 2026-09-09-pending-command-batches.md, "Session switch"). */
    if (d->active_state) {
        for (int bi = 0; bi < d->active_state->batches.count; bi++)
            append_batch_command_items(&d->msg_list, d->active_state->batches.b[bi]);
    }

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

/* Model-chip geometry, shared by ai_chat_relayout() (to size the session label)
 * and the header paint (to draw the chip itself): sized to the current
 * model name's measured text width (FONT_CAPTION + 2*SP_SM padding) and
 * placed directly left of the header's icon buttons; the session label
 * takes the remaining space to its left. `hdc` may be NULL (a fixed
 * fallback width is used); pass a real one for an accurate size. */
static void ai_chat_header_chip(AiChatData *d, NsRect header, int buttons_left,
                                 HDC hdc, NsRect *out_chip, RECT *out_label)
{
    int pad_sm = ns_scale(SP_SM, d->dpi);
    int chip_h = ns_scale(SZ_TAG_H, d->dpi);
    int chip_w = chip_h * 2;

    if (hdc) {
        HFONT font = ns_font(FONT_CAPTION, d->dpi);
        HGDIOBJ old = font ? SelectObject(hdc, font) : NULL;
        const char *model = d->conv.model;
        int mlen = (int)strlen(model);
        SIZE sz = {0, 0};
        if (mlen > 0) GetTextExtentPoint32A(hdc, model, mlen, &sz);
        if (old) SelectObject(hdc, old);
        chip_w = sz.cx + 2 * pad_sm;
    }

    int chip_x = buttons_left - chip_w;
    int chip_y = header.y + (header.h - chip_h) / 2;
    if (out_chip) {
        out_chip->x = chip_x; out_chip->y = chip_y;
        out_chip->w = chip_w; out_chip->h = chip_h;
    }
    if (out_label) {
        int label_left = header.x + pad_sm;
        int label_right = chip_x - pad_sm;
        if (label_right < label_left) label_right = label_left;
        SetRect(out_label, label_left, header.y, label_right,
                header.y + header.h);
    }
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

    /* ---- Header: session name (left), model chip (painted -- see
     * WM_PAINT), three icon buttons right-aligned. ---- */
    int btn_h = ns_scale(SZ_CTRL_H, d->dpi);
    int pad_sm = ns_scale(SP_SM, d->dpi);
    int by = l.header.y + (l.header.h - btn_h) / 2;
    int bx = l.header.x + l.header.w - pad_sm - btn_h;
    if (d->hUndockBtn)
        MoveWindow(d->hUndockBtn, bx, by, btn_h, btn_h, TRUE);
    bx -= pad_sm + btn_h;
    if (d->hSaveBtn)
        MoveWindow(d->hSaveBtn, bx, by, btn_h, btn_h, TRUE);
    bx -= pad_sm + btn_h;
    if (d->hNewChatBtn)
        MoveWindow(d->hNewChatBtn, bx, by, btn_h, btn_h, TRUE);
    int buttons_left = bx - pad_sm;

    {
        NsRect chip;
        RECT label_rc;
        HDC hdc = GetDC(d->hwnd);
        ai_chat_header_chip(d, l.header, buttons_left, hdc, &chip, &label_rc);
        if (hdc) ReleaseDC(d->hwnd, hdc);
        if (d->hSessionLabel)
            MoveWindow(d->hSessionLabel, label_rc.left, label_rc.top,
                       label_rc.right - label_rc.left,
                       label_rc.bottom - label_rc.top, TRUE);
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
    }

    ai_chat_status_tools_update(d);
}

RECT ai_chat_to_RECT(NsRect r)
{
    RECT rc = { r.x, r.y, r.x + r.w, r.y + r.h };
    return rc;
}

/* Paint the header's model chip and (while a session is active) the
 * activity dot + one-word status, next to the session label. */
void ai_chat_paint_header(AiChatData *d, HDC hdc)
{
    const ThemeTokens *tok = ns_tokens();
    AiPanelLayout l;
    ai_chat_compute_layout(d, &l);

    int btn_h = ns_scale(SZ_CTRL_H, d->dpi);
    int pad_sm = ns_scale(SP_SM, d->dpi);
    int buttons_left = l.header.x + l.header.w - pad_sm
                      - 3 * btn_h - 2 * pad_sm - pad_sm;

    NsRect chip;
    ai_chat_header_chip(d, l.header, buttons_left, hdc, &chip, NULL);
    if (chip.w > 0 && chip.h > 0) {
        RECT chip_rc = ai_chat_to_RECT(chip);
        HFONT font = ns_font(FONT_CAPTION, d->dpi);
        ns_draw_chip(hdc, &chip_rc, theme_cr(tok->raised.base),
                    theme_cr(tok->text_dim), font, d->conv.model);
    }

    if (d->activity.phase == ACTIVITY_IDLE) return;

    RECT rc_lbl = {0, 0, 0, 0};
    if (d->hSessionLabel) {
        GetWindowRect(d->hSessionLabel, &rc_lbl);
        MapWindowPoints(NULL, d->hwnd, (POINT *)&rc_lbl, 2);
    }
    SIZE sz_lbl = {0, 0};
    /* Measure with the session label's own font (FONT_TITLE) so the dot
     * lands right after its actually-rendered text, not an approximation. */
    HGDIOBJ old_f = SelectObject(hdc, ns_font(FONT_TITLE, d->dpi));
    char lbl_text[256] = "";
    if (d->hSessionLabel)
        GetWindowTextA(d->hSessionLabel, lbl_text, (int)sizeof(lbl_text));
    GetTextExtentPoint32A(hdc, lbl_text, (int)strlen(lbl_text), &sz_lbl);
    SelectObject(hdc, d->hSmallFont ? d->hSmallFont : GetStockObject(DEFAULT_GUI_FONT));

    int dot_sz = ns_scale(6, d->dpi);
    int dot_x = rc_lbl.left + sz_lbl.cx + ns_scale(6, d->dpi);
    int dot_y = rc_lbl.top + ((rc_lbl.bottom - rc_lbl.top) - dot_sz) / 2;

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

    /* Only the dot pulses; the activity word keeps the steady colour so its
     * contrast on bg_primary does not dip with the phase. */
    const COLORREF word_clr = dot_clr;
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

    const char *word;
    switch (d->activity.phase) {
    case ACTIVITY_PROCESSING: word = "Processing"; break;
    case ACTIVITY_THINKING:   word = "Thinking";   break;
    case ACTIVITY_RESPONDING: word = "Responding"; break;
    case ACTIVITY_EXECUTING:  word = "Executing";  break;
    case ACTIVITY_WAITING:    word = "Waiting";    break;
    default:                  word = "";           break;
    }
    if (word[0]) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, word_clr);
        RECT wrc;
        wrc.left   = dot_x + dot_sz + ns_scale(4, d->dpi);
        wrc.top    = rc_lbl.top;
        wrc.right  = l.header.x + l.header.w;
        wrc.bottom = rc_lbl.bottom;
        DrawTextA(hdc, word, -1, &wrc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }
    SelectObject(hdc, old_f);
}

/* Zoom the AI chat content fonts. Resizes the chat ListView fonts
 * (user messages + AI responses) and the user input textbox font.
 * Buttons keep the UI font; context bar and session label keep the
 * small font. */
void ai_chat_apply_zoom(AiChatData *d, int delta)
{
    if (!d) return;
    int new_size = app_font_zoom(d->ui_font_size, delta);
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

#endif /* _WIN32 */
