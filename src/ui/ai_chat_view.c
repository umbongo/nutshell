/* The panel's view: layout helpers, panel state, busy indicator, display
 * rebuild, header, relayout and zoom. */
#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#endif

#include "ai_chat_internal.h"
#include "ai_header_layout.h"

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
 * Status maps onto the fields the list view reads: approved follows the
 * click (APPROVED/EXECUTING/COMPLETED 1, DENIED 0, PENDING/BLOCKED -1, which
 * chat_msg_set_command already defaults to). Whether the rows are settled
 * (inline, off the card) is the batch's, not each row's, and not derived from
 * the queue: the live view settles a batch's rows together
 * (ai_chat_batch_settle_rows(), which sets CmdBatch.settled), and a ceiling
 * change moves entries between PENDING and BLOCKED without settling or
 * unsettling a row, so the queue alone cannot say which the live view shows.
 * A batch not yet settled comes back as one whole card, and its summary line
 * (needs a settled row) as absent as it was live. */
void ai_chat_append_batch_command_items(ChatMsgList *list, const CmdBatch *batch)
{
    if (!list || !batch) return;
    const int settled = batch->settled;
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
        /* A tool call's row (outcome PENDING until its slot is answered). */
        if (e->tool) chat_msg_set_tool(item, 1);
        /* Approved/denied as the click left them (APPROVED, EXECUTING and
         * COMPLETED read approved, DENIED denied), whether or not the card
         * is still up. */
        switch (e->status) {
        case APPROVE_APPROVED:
        case APPROVE_EXECUTING:
        case APPROVE_COMPLETED:
            item->u.cmd.approved = 1;
            break;
        case APPROVE_DENIED:
            item->u.cmd.approved = 0;
            break;
        case APPROVE_PENDING:
        case APPROVE_BLOCKED:
        default:
            break;
        }
        item->u.cmd.settled = settled;
    }

    /* Run state comes from the same queue, so a rebuilt display (session
     * switch, --ui-demo=executing) shows "running"/"ran" exactly where the
     * live panel does instead of falling back to "queued". The batch is
     * alive here by definition -- a rebuild only ever replays batches still
     * in the set -- hence batch_ending = 0. */
    chat_msg_batch_sync_run(list, batch->id, &batch->q, 0);
}

/* Append a recorded batch (cmd_run_log.h) as settled CHAT_ITEM_COMMAND rows,
 * tagged with `batch_id` (negative: live batch ids start at 1, so no
 * chat_msg_batch_sync_run() or settle ever touches a replayed row). The
 * fields are the ones the rows had when the batch ended, so the same painter
 * draws the same chips; `run` is set directly because nothing syncs these
 * rows. */
static void append_run_record_items(ChatMsgList *list, const CmdRunRecord *rec,
                                    int batch_id)
{
    for (int i = 0; i < rec->n; i++) {
        const CmdRunRow *r = &rec->rows[i];
        ChatMsgItem *item = chat_msg_append(list, CHAT_ITEM_COMMAND, "");
        if (!item) continue;
        chat_msg_set_command(item, r->command ? r->command : "", r->safety,
                             r->blocked);
        chat_msg_set_quoted(item, r->quoted);
        chat_msg_set_reason(item, r->reason);
        chat_msg_set_batch(item, batch_id);
        chat_msg_set_note(item, r->note);
        item->u.cmd.approved = r->approved;
        item->u.cmd.run = r->run;
        item->u.cmd.settled = 1;
    }
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
            /* Records of the displayed session's run log that belong to this
             * reply (cmd_run_record_matches(): same index, still an
             * assistant turn, same text). Such a reply shows only its
             * pre-command text, as it does live, and its commands follow as
             * the recorded rows -- so each command appears once. */
            const CmdRunLog *rlog = d->active_state ? &d->active_state->run_log
                                                    : NULL;
            int has_record = 0;
            for (int ri = 0; rlog && ri < rlog->count; ri++)
                if (cmd_run_record_matches(rlog->rec[ri], i, 1, content,
                                           msg->content_len))
                    has_record = 1;

            char pre_text[AI_MSG_MAX];
            const char *shown = content;
            if (has_record && content) {
                ai_response_split(content, pre_text, sizeof(pre_text), NULL, 0);
                shown = pre_text;
            }
            int has_thinking = d->thinking_history[i] &&
                               d->thinking_history[i][0];
            if (shown && (shown[0] != '\0' || (has_record && has_thinking))) {
                ChatMsgItem *item = chat_msg_append(&d->msg_list,
                                                     CHAT_ITEM_AI_TEXT,
                                                     shown);
                if (item && has_thinking) {
                    chat_msg_set_thinking(item, d->thinking_history[i]);
                    item->u.ai.thinking_complete = 1;
                }
            }
            for (int ri = 0; has_record && ri < rlog->count; ri++)
                if (cmd_run_record_matches(rlog->rec[ri], i, 1, content,
                                           msg->content_len))
                    append_run_record_items(&d->msg_list, rlog->rec[ri],
                                            -(ri + 1));
        } else if (msg->role == AI_ROLE_TOOL) {
            /* One results turn: a row per part (its first 200 bytes), and a
             * truncation row for a part longer than that. */
            for (int pi = 0; msg->parts && pi < msg->n_parts; pi++) {
                const AiToolPart *part = &msg->parts[pi];
                size_t clen = part->content ? part->content_len : 0;
                char preview[256];
                size_t plen = clen < 200 ? clen : 200;
                memcpy(preview, part->content ? part->content : "", plen);
                preview[plen] = '\0';
                chat_msg_append(&d->msg_list, CHAT_ITEM_TOOL_RESULT, preview);
                if (clen > 200) {
                    char warn[256];
                    agentic_truncation_warning(part->tool_name, 0, warn,
                                               sizeof(warn));
                    chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS, warn);
                }
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
            ai_chat_append_batch_command_items(&d->msg_list,
                                               d->active_state->batches.b[bi]);
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

/* Header geometry, shared by ai_chat_relayout() (buttons and the session
 * label), ai_chat_header_sync() (the label, as the status word comes and
 * goes) and the header paint (chip, dot and word): measures the model name
 * (FONT_CAPTION chip text + 2*SP_SM padding), the session name (FONT_TITLE,
 * the label's own font) and the activity word (the small font the paint
 * uses), then lets ai_header_layout() lay the row out right to left so the
 * word can never land under the chip or the buttons. `hdc` may be NULL (a
 * fixed chip width and an unclipped name are assumed); pass a real one for
 * accurate sizes. */
static void ai_chat_header_layout(AiChatData *d, NsRect header, HDC hdc,
                                  AiHeaderLayout *out)
{
    int pad_sm = ns_scale(SP_SM, d->dpi);
    AiHeaderMeasure m;
    m.header    = header;
    m.pad       = pad_sm;
    m.gap       = pad_sm;
    m.btn_h     = ns_scale(SZ_CTRL_H, d->dpi);
    m.buttons_w = 3 * m.btn_h + 2 * pad_sm;
    m.chip_h    = ns_scale(SZ_TAG_H, d->dpi);
    m.chip_w    = m.chip_h * 2;
    m.name_w    = header.w;
    m.status_w  = 0;

    if (hdc) {
        SIZE sz = {0, 0};
        HFONT cap = ns_font(FONT_CAPTION, d->dpi);
        HGDIOBJ old = cap ? SelectObject(hdc, cap) : NULL;
        int mlen = (int)strlen(d->conv.model);
        if (mlen > 0) GetTextExtentPoint32A(hdc, d->conv.model, mlen, &sz);
        m.chip_w = sz.cx + 2 * pad_sm;

        /* A little slack so the label's own ellipsis never trims a name
         * that the layout has judged to fit. */
        sz.cx = 0; sz.cy = 0;
        SelectObject(hdc, ns_font(FONT_TITLE, d->dpi));
        int nlen = (int)strlen(d->session_name);
        if (nlen > 0) GetTextExtentPoint32A(hdc, d->session_name, nlen, &sz);
        m.name_w = sz.cx + ns_scale(SP_XS, d->dpi) / 2;

        const char *word = ai_chat_header_word(d);
        if (word[0]) {
            sz.cx = 0; sz.cy = 0;
            SelectObject(hdc, d->hSmallFont ? d->hSmallFont
                                            : GetStockObject(DEFAULT_GUI_FONT));
            GetTextExtentPoint32A(hdc, word, (int)strlen(word), &sz);
            m.status_w = ns_scale(6, d->dpi) + ns_scale(SP_XS, d->dpi) + sz.cx;
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

/* Re-place the session label for the current activity word. Called before
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

    /* ---- Header: session name (left), the status word and model chip
     * (painted -- see WM_PAINT), three icon buttons right-aligned; all
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
 * activity dot + one-word status, next to the session label. Every rect
 * comes from ai_header_layout(): the word sits between the session name
 * and the chip, and is left out when the row has no room for it. */
void ai_chat_paint_header(AiChatData *d, HDC hdc)
{
    const ThemeTokens *tok = ns_tokens();
    AiPanelLayout l;
    ai_chat_compute_layout(d, &l);

    AiHeaderLayout hl;
    ai_chat_header_layout(d, l.header, hdc, &hl);

    if (hl.chip.w > 0 && hl.chip.h > 0) {
        RECT chip_rc = ai_chat_to_RECT(hl.chip);
        HFONT font = ns_font(FONT_CAPTION, d->dpi);
        ns_draw_chip(hdc, &chip_rc, theme_cr(tok->raised.base),
                    theme_cr(tok->text_dim), font, d->conv.model);
    }

    if (d->activity.phase == ACTIVITY_IDLE || !hl.status_shown) return;

    HGDIOBJ old_f = SelectObject(hdc, d->hSmallFont ? d->hSmallFont
                                                    : GetStockObject(DEFAULT_GUI_FONT));

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

    const char *word = ai_chat_header_word(d);
    if (word[0]) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, word_clr);
        RECT wrc;
        wrc.left   = dot_x + dot_sz + ns_scale(SP_XS, d->dpi);
        wrc.top    = hl.status.y;
        wrc.right  = hl.status.x + hl.status.w;
        wrc.bottom = hl.status.y + hl.status.h;
        DrawTextA(hdc, word, -1, &wrc,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
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
