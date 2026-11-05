#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#endif

#include "ai_chat_internal.h"

#ifdef _WIN32
#include <stdio.h>

static const char *AI_CHAT_CLASS = "Nutshell_AIChat";

/* Forward declarations */
static void do_session_switch(AiChatData *d,
                              AiSessionState *new_state,
                              Terminal *term, SessionIo *io,
                              const char *session_notes,
                              const char *system_notes,
                              const char *session_name);

#include "markdown.h"

static LRESULT CALLBACK AiChatWndProc(HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam)
{
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        LPCREATESTRUCT cs = (LPCREATESTRUCT)lParam;
        AiChatData *nd = (AiChatData *)cs->lpCreateParams;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)nd);
        nd->hwnd = hwnd;

        /* Start GDI+ for clipboard image conversion */
        {
            GdiplusStartupInput gdip_in = {1, NULL, FALSE, FALSE};
            GdiplusStartup(&nd->gdip_token, &gdip_in, NULL);
        }

        /* Get per-monitor DPI for layout scaling */
        nd->dpi = get_window_dpi(hwnd);

        /* All child-control geometry is decided by ai_chat_relayout() (called once
         * at the end of this handler, then again on every WM_SIZE) from
         * ai_panel_layout()'s header/thread/status/composer tiling -- the
         * positions/sizes given to CreateWindow here are placeholders. */
        int btn_h = ns_scale(SZ_CTRL_H, nd->dpi);

        /* New chat / Save / Undock: three square icon buttons in the
         * header, owner-drawn via ai_chat_draw_header_icon_button(). */
        nd->hNewChatBtn = CreateWindow("BUTTON", "",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            0, 0, btn_h, btn_h,
            hwnd, (HMENU)IDC_CHAT_NEWCHAT, NULL, NULL);

        nd->show_thinking = 0; /* default: collapsed (user must click '>' to expand) */
        nd->hThinkingBtn = NULL; /* Thinking button removed - now inline in chat */

        nd->hSaveBtn = CreateWindow("BUTTON", "",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            0, 0, btn_h, btn_h,
            hwnd, (HMENU)IDC_CHAT_SAVE, NULL, NULL);

        nd->hUndockBtn = CreateWindow("BUTTON", "",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            0, 0, btn_h, btn_h,
            hwnd, (HMENU)IDC_CHAT_UNDOCK, NULL, NULL);

        /* Suppress WM_ERASEBKGND on owner-drawn buttons to prevent
         * white flicker during splitter drag / parent resize. */
        SetWindowSubclass(nd->hNewChatBtn,  ai_chat_btn_noerase_subclass, BTN_NOERASE_SUBCLASS_ID, 0);
        SetWindowSubclass(nd->hSaveBtn,     ai_chat_btn_noerase_subclass, BTN_NOERASE_SUBCLASS_ID, 0);
        SetWindowSubclass(nd->hUndockBtn,   ai_chat_btn_noerase_subclass, BTN_NOERASE_SUBCLASS_ID, 0);

        /* Old floating Allow/Deny buttons removed — approval is now
         * handled inline via chat_listview command block buttons.
         * Initialize the approval queue. */
        chat_approval_init(&nd->approval_q);
        chat_activity_init(&nd->activity);
        ns_hover_init(&nd->status_hover);

        /* Session name label (left of the header, FONT_TITLE) -- the model
         * chip to its right is painted, not a child window. */
        nd->hSessionLabel = CreateWindow("STATIC",
            nd->session_name[0] ? nd->session_name : "",
            WS_VISIBLE | WS_CHILD | SS_LEFT | SS_ENDELLIPSIS,
            0, 0, 1, 1,
            hwnd, (HMENU)IDC_SESSION_LABEL, NULL, NULL);

        /* Chat display: owner-drawn ChatListView replaces RichEdit.
         * In docked mode the initial window may be 1x1, so clamp
         * all dimensions to >=1 — ai_chat_relayout() fixes them on first WM_SIZE. */
        int input_h = ai_chat_input_h(nd); /* placeholder; ai_chat_relayout() sets it */
        int margin = ns_scale(5, nd->dpi);
        int disp_w = 1, disp_h = 1;

        /* Initialize the message list */
        chat_msg_list_init(&nd->msg_list);

        /* Create ChatListView — hDisplay points to same HWND for layout compat */
        nd->hChatList = chat_listview_create(hwnd, 0, 0,
                                              disp_w, disp_h,
                                              &nd->msg_list, nd->theme);
        nd->hDisplay = nd->hChatList;  /* layout code uses hDisplay */
        if (nd->hChatList)
            chat_listview_set_activity(nd->hChatList, &nd->activity);

        /* Custom themed scrollbar for chat display (kept for visual consistency) */
        csb_register(GetModuleHandle(NULL));
        nd->hDisplayScrollbar = csb_create(hwnd,
            0, 0, CSB_WIDTH, disp_h,
            nd->theme, GetModuleHandle(NULL));

        /* Connect custom scrollbar to ChatListView */
        if (nd->hChatList && nd->hDisplayScrollbar)
            chat_listview_set_scrollbar(nd->hChatList, nd->hDisplayScrollbar);

        /* ThinkingBox no longer needed — thinking is inline in ChatListView */
        nd->hThinkingBox = NULL;
        nd->thinking_box_height = 0;
        nd->indicator_line_y = -1;
        nd->last_phase = -1;

        /* Input field: multiline, Enter sends via subclass, Shift+Enter = newline */
        int send_w = ns_scale(40, nd->dpi);
        nd->hInput = CreateWindow("EDIT", "",
            WS_VISIBLE | WS_CHILD | WS_BORDER |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            margin, 0, 1, input_h,
            hwnd, (HMENU)IDC_CHAT_INPUT, NULL, NULL);
        if (nd->hInput)
            SetWindowSubclass(nd->hInput, ai_chat_input_subclass_proc, 0, 0);

        /* Custom themed scrollbar for input */
        nd->hInputScrollbar = csb_create(hwnd,
            0, 0, CSB_WIDTH, input_h,
            nd->theme, GetModuleHandle(NULL));

        /* Send button (owner-drawn for theme) */
        nd->hSendBtn = CreateWindow("BUTTON", ">",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            0, 0, send_w, input_h,
            hwnd, (HMENU)IDC_CHAT_SEND, NULL, NULL);
        SetWindowSubclass(nd->hSendBtn, ai_chat_btn_noerase_subclass, BTN_NOERASE_SUBCLASS_ID, 0);

        /* Font — buttons/labels use the cached FONT_BODY role (Inter UI
         * font); the chat content font below is independently zoomable
         * (Ctrl+/Ctrl-), so it keeps its own CreateFont calls. */
        nd->ui_font_size = ns_type_font(FONT_BODY)->size_pt;
        int h = -MulDiv(nd->ui_font_size, nd->dpi, 72);
        nd->hFont = ns_font(FONT_BODY, nd->dpi);
        /* Small bold font — Segoe UI renders reliably at small sizes on all
         * Windows versions (hand-tuned hinting), unlike bundled Inter. */
        int sh = -MulDiv(8, nd->dpi, 72);
        nd->hSmallFont = CreateFont(sh, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_TT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_SWISS, "Segoe UI");

        if (nd->hFont) {
            /* Chat content uses its own zoomable fonts (separate from
             * the UI font used by buttons/labels). The input textbox
             * shares the chat font so its text scales on Ctrl+/Ctrl-. */
            if (nd->hChatList) {
                nd->hChatFont = CreateFont(h, 0, 0, 0, FW_NORMAL,
                    FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                    APP_FONT_UI_FACE);
                nd->hBoldFont = CreateFont(h, 0, 0, 0, FW_BOLD,
                    FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                    APP_FONT_UI_FACE);
                nd->hMonoFont = CreateFont(h, 0, 0, 0, FW_NORMAL,
                    FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                    OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN,
                    nd->ai_font_name[0] ? nd->ai_font_name
                                        : "Consolas");
                chat_listview_set_fonts(nd->hChatList,
                                        nd->hChatFont ? nd->hChatFont : nd->hFont,
                                        nd->hMonoFont, nd->hBoldFont,
                                        nd->hSmallFont);
                chat_listview_set_model(nd->hChatList, nd->conv.model);
            }
            HFONT hInputFont = nd->hChatFont ? nd->hChatFont : nd->hFont;
            SendMessage(nd->hInput, WM_SETFONT, (WPARAM)hInputFont, TRUE);
            /* Measure line height for scrollbar sync */
            HDC hdc_m = GetDC(nd->hInput);
            HGDIOBJ old_m = SelectObject(hdc_m, (HGDIOBJ)hInputFont);
            TEXTMETRIC tm_m;
            GetTextMetrics(hdc_m, &tm_m);
            nd->display_line_h = tm_m.tmHeight + tm_m.tmExternalLeading;
            nd->input_line_h = nd->display_line_h;
            SelectObject(hdc_m, old_m);
            ReleaseDC(nd->hInput, hdc_m);
        }
        /* Session name reads at FONT_TITLE per the header design. */
        SendMessage(nd->hSessionLabel, WM_SETFONT,
                    (WPARAM)ns_font(FONT_TITLE, nd->dpi), TRUE);

        /* Apply theme title bar + borders */
        if (nd->theme) {
            themed_apply_title_bar(hwnd, nd->theme);
            themed_apply_borders(hwnd, nd->theme);
        }

        /* Create tooltip control and add tips for all buttons, plus one
         * rect tool per status-line element (four cells, the Auto-approve
         * button, the context meter) -- their text is built on demand in
         * WM_NOTIFY (TTN_GETDISPINFO), so it always reflects current state
         * without us pushing updates. */
        nd->hTooltip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        if (nd->hTooltip) {
            SendMessage(nd->hTooltip, TTM_SETMAXTIPWIDTH, 0, 400);
            ai_chat_add_tooltip(nd->hTooltip, nd->hNewChatBtn,
                "New chat\nClear the conversation and start fresh.");
            ai_chat_add_tooltip(nd->hTooltip, nd->hSaveBtn,
                "Save chat\nSave the conversation as a text file.");
            if (nd->hUndockBtn)
                ai_chat_add_tooltip(nd->hTooltip, nd->hUndockBtn,
                    nd->docked ? "Undock\nOpen AI Assist in a separate window."
                               : "Dock\nDock AI Assist inside the main window.");
            ai_chat_add_tooltip(nd->hTooltip, nd->hSendBtn,
                "Send\nSend your message to the AI.\n"
                "Shortcut: press Enter in the input box.");

            /* The status line: one rect tool per element -- four cells,
             * the Auto-approve button and the context meter -- so Windows
             * shows and hides the tip per element on its own (spec
             * 2026-10-18 section 4). Rects are set by ai_chat_status_tools_update()
             * after every ai_chat_relayout; text comes from TTN_GETDISPINFO. */
            for (int t = 0; t < STATUS_TOOL_COUNT; t++) {
                TOOLINFO ti;
                memset(&ti, 0, sizeof(ti));
                ti.cbSize   = sizeof(ti);
                ti.uFlags   = TTF_SUBCLASS;
                ti.hwnd     = hwnd;
                ti.uId      = (UINT_PTR)(STATUS_TOOL_BASE + t);
                ti.lpszText = LPSTR_TEXTCALLBACK;
                SetRectEmpty(&ti.rect);
                SendMessage(nd->hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
            }
        }

        /* Replays any loaded conversation, or leaves msg_list empty for
         * chat_listview to paint the empty/no-key/no-session state
         * (set just below -- active_io isn't known yet at this
         * point, ai_chat_set_session() corrects it right after). */
        ai_chat_rebuild_display(nd);
        ai_chat_update_panel_state(nd);

        ai_chat_update_context_bar(nd);
        ai_chat_relayout(nd);
        SetFocus(nd->hInput);
        return 0;
    }

    case WM_SIZE: {
        if (!d) break;
        ai_chat_relayout(d);
        /* Force repaint of all owner-drawn buttons after layout change.
         * Erase background (TRUE) so newly exposed areas during the
         * slide-out animation get filled with the theme colour. */
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }

    case WM_NCHITTEST: {
        /* When docked, make the left edge transparent to mouse clicks
         * so the parent window can handle splitter dragging. The transparent
         * band must match the parent's right-side splitter pad so every
         * pixel that falls through actually lands inside the parent's
         * hit zone. */
        if (d && d->docked) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            if (pt.x < AI_DOCK_SPLITTER_PAD)
                return HTTRANSPARENT;
        }
        break;
    }

    case WM_NOTIFY: {
        /* EN_LINK handling removed — ChatListView handles thinking toggle
         * inline via its own click handling in the list view WndProc. */
        NMHDR *hdr = (NMHDR *)lParam;
        if (d && hdr && hdr->hwndFrom == d->hTooltip &&
            (hdr->code == TTN_GETDISPINFOA || hdr->code == TTN_NEEDTEXTA) &&
            hdr->idFrom >= (UINT_PTR)STATUS_TOOL_BASE &&
            hdr->idFrom < (UINT_PTR)(STATUS_TOOL_BASE + STATUS_TOOL_COUNT)) {
            /* Fetched fresh on every show, so the text always matches the
             * current policy and label set. */
            NMTTDISPINFOA *nm = (NMTTDISPINFOA *)lParam;
            int t = (int)(hdr->idFrom - (UINT_PTR)STATUS_TOOL_BASE);
            AiStatusPaint sp;
            ai_chat_get_status_paint(d, NULL, &sp);
            CmdPolicy pol = d->approval_q.policy;
            int on = cmd_policy_auto_on(pol);
            d->tooltip_buf[0] = '\0';
            if (t < NS_POLICY_STOPS) {
                cmd_policy_cell_tip(t, sp.sl.compact, on,
                                    d->tooltip_buf, sizeof(d->tooltip_buf));
            } else if (t == STATUS_HIT_AUTO) {
                cmd_policy_auto_tip(on, pol.allowed,
                                    d->tooltip_buf, sizeof(d->tooltip_buf));
            } else if (t == STATUS_TOOL_METER && sp.has_meter) {
                int actual = d->actual_input_tokens + d->actual_output_tokens;
                int est = (actual > 0) ? 0 : ai_context_estimate_tokens_ex(&d->conv, d->last_snapshot_len);
                ai_format_context_tooltip(d->actual_input_tokens,
                                          d->actual_output_tokens, est,
                                          d->context_limit, d->conv.model,
                                          d->tooltip_buf, sizeof(d->tooltip_buf));
            }
            nm->lpszText = d->tooltip_buf;
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE: {
        if (d && d->sash_dragging) {
            /* Dragging up grows the box: the new height is the starting
             * height minus how far the cursor has moved down. */
            RECT crc;
            GetClientRect(hwnd, &crc);
            int dy = GET_Y_LPARAM(lParam) - d->sash_drag_start_y;
            int want = ns_scale(d->sash_drag_start_base, d->dpi) - dy;
            int new_h = ai_composer_clamp_h(want, crc.bottom,
                                            d->input_line_h, d->dpi);
            int new_base = ai_chat_unscale(new_h, d->dpi);
            if (new_base != d->input_base_h) {
                d->input_base_h = new_base;
                ai_chat_relayout(d);
                InvalidateRect(hwnd, NULL, TRUE);
                UpdateWindow(hwnd);
            }
            return 0;
        }
        if (d) {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            int hit_id = ai_chat_status_hit(d, mx, my);
            NsHoverChange ch = ns_hover_move(&d->status_hover, hit_id);
            if (ch.changed) {
                RECT old_rc;
                if (ai_chat_status_rect(d, ch.old_id, &old_rc))
                    InvalidateRect(hwnd, &old_rc, FALSE);
                RECT new_rc;
                if (ch.new_id >= 0 && ai_chat_status_rect(d, ch.new_id, &new_rc))
                    InvalidateRect(hwnd, &new_rc, FALSE);
            }
            if (!d->status_hover_tracking) {
                TRACKMOUSEEVENT tme;
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                tme.dwHoverTime = 0;
                if (TrackMouseEvent(&tme)) d->status_hover_tracking = 1;
            }
            if (d->hTooltip) {
                MSG relay = { hwnd, WM_MOUSEMOVE, wParam, lParam, 0, {0, 0} };
                SendMessage(d->hTooltip, TTM_RELAYEVENT, 0, (LPARAM)&relay);
            }
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        if (d) {
            d->status_hover_tracking = 0;
            NsHoverChange ch = ns_hover_leave(&d->status_hover);
            if (ch.changed) {
                RECT old_rc;
                if (ai_chat_status_rect(d, ch.old_id, &old_rc))
                    InvalidateRect(hwnd, &old_rc, FALSE);
            }
        }
        return 0;

    case WM_SETCURSOR:
        if (d && LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (d->sash_dragging || ai_chat_sash_hit(d, pt.x, pt.y)) {
                SetCursor(LoadCursor(NULL, IDC_SIZENS));
                return TRUE;
            }
            int hit_id = ai_chat_status_hit(d, pt.x, pt.y);
            SetCursor(LoadCursor(NULL, hit_id >= 0 ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;

    case WM_LBUTTONDOWN:
        if (d) {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            if (ai_chat_sash_hit(d, mx, my)) {
                d->sash_dragging = 1;
                d->sash_drag_start_y = my;
                d->sash_drag_start_base =
                    d->input_base_h > 0 ? d->input_base_h : AI_INPUT_BASE_H;
                SetCapture(hwnd);
                return 0;
            }
            int hit_id = ai_chat_status_hit(d, mx, my);
            if (hit_id >= 0 && hit_id < NS_POLICY_STOPS) {
                PostMessage(hwnd, WM_COMMAND,
                            MAKEWPARAM(IDC_CHAT_POLICY_ALLOW_BASE + hit_id, 0), 0);
                return 0;
            }
            if (hit_id == STATUS_HIT_AUTO) {
                PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_CHAT_AUTOAPPROVE, 0), 0);
                return 0;
            }
        }
        break;

    case WM_LBUTTONUP:
        if (d && d->sash_dragging) {
            d->sash_dragging = 0;
            ReleaseCapture();
            return 0;
        }
        break;

    case WM_CAPTURECHANGED:
        if (d) d->sash_dragging = 0;
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CHAT_SEND:
            if (d && d->dispatch_active) {
                /* Stopping a batch by hand is an attended round. Marked
                 * before the cancel so its continue message resets the
                 * unattended-round count; reset directly as well, since a
                 * cancel that sends no continue message would not. */
                if (d->active_state) {
                    CmdBatch *running = cmd_batch_find(
                        &d->active_state->batches, d->dispatch_batch_id);
                    if (running) running->user_acted = 1;
                }
                ai_chat_dispatch_cancel(d, "[command queue stopped]", 1);
                ai_rounds_user_message(d->active_state);
                if (d->hChatList)
                    chat_listview_scroll_to_bottom(d->hChatList);
            } else if (d && ACTIVE_BUSY(d)) {
                ai_chat_cancel_active_stream(d);
                chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                    "[cancelled]");
                if (d->hChatList) {
                    chat_listview_invalidate(d->hChatList);
                    chat_listview_scroll_to_bottom(d->hChatList);
                }
            } else {
                ai_chat_send_user_message(d);
            }
            /* Only while the panel is on screen: a send finishing after the
             * panel was hidden must not pull keyboard focus back into it. */
            if (d && d->hInput && IsWindowVisible(hwnd))
                SetFocus(d->hInput);
            return 0;
        case IDC_CHAT_SUGGESTION_BASE:
        case IDC_CHAT_SUGGESTION_BASE + 1:
        case IDC_CHAT_SUGGESTION_BASE + 2: {
            /* Empty-state suggestion chip (chat_listview.c): drop its
             * text into the input box and send it via the normal path. */
            if (!d) return 0;
            int i = (int)LOWORD(wParam) - IDC_CHAT_SUGGESTION_BASE;
            int count = 0;
            const char *const *sugg = ai_panel_suggestions(&count);
            if (i >= 0 && i < count) {
                SetWindowText(d->hInput, sugg[i]);
                ai_chat_send_user_message(d);
                SetFocus(d->hInput);
            }
            return 0;
        }
        case IDC_CHAT_STATE_ACTION: {
            /* Empty-state action button (chat_listview.c): NO_KEY opens
             * Settings on the Provider page, NO_SESSION opens the
             * Session Manager -- both handled by the main window
             * (window.c), so post there rather than act locally. */
            if (!d) return 0;
            HWND main_hwnd = GetParent(hwnd);
            if (!main_hwnd) return 0;
            /* Same precedence as ai_chat_update_panel_state(): no transport means
             * NO_SESSION is showing (even if the key is also empty), so
             * check active_io first rather than api_key. */
            if (!d->active_io) {
                PostMessage(main_hwnd, WM_COMMAND,
                           MAKEWPARAM(IDM_FILE_CONNECT, 0), 0);
            } else {
                PostMessage(main_hwnd, WM_COMMAND,
                           MAKEWPARAM(IDM_EDIT_SETTINGS, 0),
                           (LPARAM)SETTINGS_PAGE_AI_PROVIDER);
            }
            return 0;
        }
        case IDC_CHAT_NEWCHAT:
            if (d) {
                /* Cancel any active stream or command dispatch first */
                if (ACTIVE_BUSY(d))
                    ai_chat_cancel_active_stream(d);
                if (d->dispatch_active)
                    ai_chat_dispatch_cancel(d, NULL, 0);

                /* Reset only the ACTIVE session's conversation.
                 * Other sessions' AiSessionState objects are untouched. */
                ai_conv_reset(&d->conv);
                if (d->active_state) {
                    ai_conv_reset(&d->active_state->conv);
                    d->active_state->valid = 1;
                    /* Drop every pending batch -- New Chat starts clean,
                     * same as it clears msg_list below. A tool card's call
                     * is answered first (cancelled). */
                    ai_chat_cancel_tool_batches(d, d->active_state);
                    cmd_batch_set_free(&d->active_state->batches);
                    cmd_batch_set_init(&d->active_state->batches);
                }
                d->indicator_pos = -1;
                d->pending_request[0] = '\0';
                d->stream_thinking[0] = '\0';
                d->stream_thinking_len = 0;
                d->stream_content[0] = '\0';
                d->stream_content_len = 0;
                d->stream_phase = 0;
                d->actual_input_tokens = 0;
                d->actual_output_tokens = 0;
                d->last_snapshot_len = 0;   /* an empty conversation carries no snapshot */
                d->show_thinking = 0;
                if (d->active_state) d->active_state->show_thinking = 0;
                KillTimer(hwnd, TIMER_HEARTBEAT);
                chat_activity_reset(&d->activity);
                ai_chat_thinking_history_clear(d);
                /* Clear display -- an empty msg_list is painted as the
                 * empty/no-key/no-session state (ai_chat_update_panel_state()). */
                chat_msg_list_clear(&d->msg_list);
                d->stream_ai_item = NULL;
                if (d->hChatList)
                    chat_listview_invalidate(d->hChatList);
                ai_chat_update_panel_state(d);
                /* Clear input field */
                SetWindowText(d->hInput, "");
                SetFocus(d->hInput);
                ai_chat_update_context_bar(d);
            }
            return 0;
        case IDC_CHAT_SAVE:
            if (d) {
                /* Build save text from conversation */
                char *save_buf = (char *)malloc(AI_BODY_MAX);
                if (save_buf) {
                    size_t n = ai_build_save_text(&d->conv,
                        d->thinking_history, d->show_thinking,
                        save_buf, AI_BODY_MAX);
                    if (n > 0) {
                        char fname[MAX_PATH] = "ai_chat.txt";
                        OPENFILENAME ofn;
                        memset(&ofn, 0, sizeof(ofn));
                        ofn.lStructSize = sizeof(ofn);
                        ofn.hwndOwner = hwnd;
                        ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0"
                                          "All Files (*.*)\0*.*\0";
                        ofn.lpstrFile = fname;
                        ofn.nMaxFile = MAX_PATH;
                        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST
                                    | OFN_NOCHANGEDIR;
                        ofn.lpstrDefExt = "txt";
                        if (GetSaveFileName(&ofn)) {
                            FILE *f = fopen(fname, "w");
                            if (f) {
                                fwrite(save_buf, 1, n, f);
                                fclose(f);
                            } else {
                                MessageBox(hwnd,
                                    "Failed to save file.",
                                    "Save Error",
                                    MB_OK | MB_ICONERROR);
                            }
                        }
                    }
                    free(save_buf);
                }
            }
            return 0;
        case IDC_CHAT_UNDOCK:
            if (d) {
                /* Post (not Send) so the AI window finishes its message
                 * loop before the main window destroys and recreates it. */
                HWND owner = GetParent(hwnd);
                if (!owner) owner = GetWindow(hwnd, GW_OWNER);
                if (owner)
                    PostMessage(owner, WM_COMMAND,
                                MAKEWPARAM(IDM_VIEW_AI_UNDOCK, 0), 0);
            }
            return 0;
        /* ── The policy row ─────────────────────────────────────────
         * Every gesture lands here and goes through ai_chat_request_policy_change(),
         * which asks before Critical runs unattended and is a no-op while
         * that box is up. apply_policy_change() then unblocks or re-blocks
         * pending rows against the new level. */
        case IDC_CHAT_PERMIT:
            if (d) {
                CmdPolicy next = d->approval_q.policy;
                cmd_policy_cycle_allowed(&next);
                ai_chat_request_policy_change(hwnd, d, next);
            }
            return 0;
        case IDC_CHAT_AUTOAPPROVE:
            if (d) {
                CmdPolicy next = d->approval_q.policy;
                cmd_policy_set_auto(&next, !cmd_policy_auto_on(next));
                ai_chat_request_policy_change(hwnd, d, next);
            }
            return 0;
        /* IDC_CHAT_THINKING removed - thinking toggle now inline in chat */

        /* ── Inline command approval from chat_listview ───────────── */
        default: {
            int ctl_id = LOWORD(wParam);

            /* IDC_CHAT_POLICY_ALLOW_BASE + stop -> put the level on that
             * stop; Auto-approve follows it when on (cmd_policy_set_allowed). */
            if (d && ctl_id >= IDC_CHAT_POLICY_ALLOW_BASE &&
                ctl_id < IDC_CHAT_POLICY_ALLOW_BASE + NS_POLICY_STOPS) {
                CmdPolicy next = d->approval_q.policy;
                cmd_policy_set_allowed(&next, ctl_id - IDC_CHAT_POLICY_ALLOW_BASE);
                ai_chat_request_policy_change(hwnd, d, next);
                return 0;
            }
            /* Retired unattended setters: swallowed, no effect. */
            if (ctl_id >= IDC_CHAT_POLICY_RETIRED_BASE &&
                ctl_id <= IDC_CHAT_POLICY_RETIRED_BASE + NS_POLICY_STOPS)
                return 0;

            /* IDC_CMD_APPROVE_BASE + index → approve single command.
             * lParam is the batch id (0 = oldest batch still needing the
             * user); idx is the command's position within that batch. */
            if (d && ctl_id >= IDC_CMD_APPROVE_BASE &&
                ctl_id < IDC_CMD_APPROVE_BASE + APPROVAL_MAX_CMDS) {
                int idx = ctl_id - IDC_CMD_APPROVE_BASE;
                CmdBatch *batch = ai_chat_resolve_batch(d, lParam);
                if (batch && cmd_batch_is_tool(batch)) {
                    /* A tool call's card: answer its slot (before anything
                     * is settled), never touch the command queue logic. */
                    ai_chat_tool_card_answer(d, batch, TOOL_DECISION_APPROVED);
                    SetFocus(d->hInput);
                    return 0;
                }
                if (batch) {
                    chat_approval_approve(&batch->q, idx);

                    /* Sync approval state back to the matching ChatMsgItem */
                    int ci = 0;
                    for (ChatMsgItem *it = d->msg_list.head; it; it = it->next) {
                        if (it->type != CHAT_ITEM_COMMAND || it->u.cmd.settled) continue;
                        if (it->u.cmd.batch != batch->id) continue;
                        if (ci == idx) { it->u.cmd.approved = 1; break; }
                        ci++;
                    }

                    if (d->hChatList) chat_listview_invalidate(d->hChatList);

                    /* Let the dispatcher pick this command up once the
                     * terminal is at a prompt (no-op if already running). */
                    batch->user_acted = 1;
                    ai_chat_dispatch_start(d, batch->id);

                    /* Check if all decided — if so, the card can settle now */
                    ai_chat_settle_batch_if_done(d, batch);
                }
                SetFocus(d->hInput);
                return 0;
            }

            /* IDC_CMD_DENY_BASE + index → deny single command */
            if (d && ctl_id >= IDC_CMD_DENY_BASE &&
                ctl_id < IDC_CMD_DENY_BASE + APPROVAL_MAX_CMDS) {
                int idx = ctl_id - IDC_CMD_DENY_BASE;
                CmdBatch *batch = ai_chat_resolve_batch(d, lParam);
                if (batch && cmd_batch_is_tool(batch)) {
                    /* A tool call's card: answer its slot (before anything
                     * is settled), never touch the command queue logic. */
                    ai_chat_tool_card_answer(d, batch, TOOL_DECISION_NOT_APPROVED);
                    SetFocus(d->hInput);
                    return 0;
                }
                if (batch) {
                    chat_approval_deny(&batch->q, idx);

                    /* Sync denial state back to the matching ChatMsgItem */
                    int ci = 0;
                    for (ChatMsgItem *it = d->msg_list.head; it; it = it->next) {
                        if (it->type != CHAT_ITEM_COMMAND || it->u.cmd.settled) continue;
                        if (it->u.cmd.batch != batch->id) continue;
                        if (ci == idx) { it->u.cmd.approved = 0; break; }
                        ci++;
                    }

                    if (d->hChatList) chat_listview_invalidate(d->hChatList);

                    /* Check if all decided */
                    if (chat_approval_all_decided(&batch->q)) {
                        ai_chat_settle_batch_if_done(d, batch);
                        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                                        "[some commands denied]");
                        if (d->hChatList)
                            chat_listview_invalidate(d->hChatList);
                    }
                }
                SetFocus(d->hInput);
                return 0;
            }

            /* IDC_CMD_APPROVE_ALL → approve all pending commands in one batch */
            if (d && ctl_id == IDC_CMD_APPROVE_ALL) {
                CmdBatch *batch = ai_chat_resolve_batch(d, lParam);
                if (batch && cmd_batch_is_tool(batch)) {
                    /* A tool call's card: answer its slot (before anything
                     * is settled), never touch the command queue logic. */
                    ai_chat_tool_card_answer(d, batch, TOOL_DECISION_APPROVED);
                    SetFocus(d->hInput);
                    return 0;
                }
                if (batch) {
                    chat_approval_approve_all(&batch->q);

                    /* Sync all to approved in the matching ChatMsgItems */
                    for (ChatMsgItem *it = d->msg_list.head; it; it = it->next) {
                        if (it->type == CHAT_ITEM_COMMAND && !it->u.cmd.settled &&
                            it->u.cmd.batch == batch->id &&
                            it->u.cmd.approved == -1 && !it->u.cmd.blocked)
                            it->u.cmd.approved = 1;
                    }

                    ai_chat_settle_batch_if_done(d, batch);
                    if (d->hChatList) chat_listview_invalidate(d->hChatList);

                    /* Let the dispatcher send the approved commands one at a
                     * time, only once the terminal is at a prompt. */
                    batch->user_acted = 1;
                    ai_chat_dispatch_start(d, batch->id);
                }
                SetFocus(d->hInput);
                return 0;
            }

            /* IDC_CMD_APPROVE_SEL → approve only selected (ticked) commands
             * in one batch */
            if (d && ctl_id == IDC_CMD_APPROVE_SEL) {
                CmdBatch *batch = ai_chat_resolve_batch(d, lParam);
                if (batch && cmd_batch_is_tool(batch)) {
                    /* A tool call's card: answer its slot (before anything
                     * is settled), never touch the command queue logic. */
                    ai_chat_tool_card_answer(d, batch, TOOL_DECISION_APPROVED);
                    SetFocus(d->hInput);
                    return 0;
                }
                if (batch) {
                    int ci = 0;
                    int any_approved = 0;
                    for (ChatMsgItem *it = d->msg_list.head; it; it = it->next) {
                        if (it->type != CHAT_ITEM_COMMAND || it->u.cmd.settled) continue;
                        if (it->u.cmd.batch != batch->id) continue;
                        if (it->u.cmd.selected && it->u.cmd.approved == -1
                            && !it->u.cmd.blocked) {
                            it->u.cmd.approved = 1;
                            chat_approval_approve(&batch->q, ci);
                            any_approved = 1;
                        }
                        ci++;
                    }

                    /* Deny unselected pending commands */
                    ci = 0;
                    for (ChatMsgItem *it = d->msg_list.head; it; it = it->next) {
                        if (it->type != CHAT_ITEM_COMMAND || it->u.cmd.settled) continue;
                        if (it->u.cmd.batch != batch->id) continue;
                        if (it->u.cmd.approved == -1 && !it->u.cmd.blocked) {
                            it->u.cmd.approved = 0;
                            chat_approval_deny(&batch->q, ci);
                        }
                        ci++;
                    }

                    if (d->hChatList) chat_listview_invalidate(d->hChatList);

                    if (any_approved) {
                        /* Let the dispatcher send the approved commands one
                         * at a time, only once the terminal is at a prompt. */
                        batch->user_acted = 1;
                        ai_chat_dispatch_start(d, batch->id);
                    } else {
                        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                                        "[no commands selected]");
                        if (d->hChatList)
                            chat_listview_invalidate(d->hChatList);
                    }
                    ai_chat_settle_batch_if_done(d, batch);
                }
                SetFocus(d->hInput);
                return 0;
            }

            /* IDC_CMD_CANCEL_ALL → deny all pending commands in one batch */
            if (d && ctl_id == IDC_CMD_CANCEL_ALL) {
                CmdBatch *batch = ai_chat_resolve_batch(d, lParam);
                if (batch && cmd_batch_is_tool(batch)) {
                    /* A tool call's card: answer its slot (before anything
                     * is settled), never touch the command queue logic. */
                    ai_chat_tool_card_answer(d, batch, TOOL_DECISION_NOT_APPROVED);
                    SetFocus(d->hInput);
                    return 0;
                }
                if (batch) {
                    int ci = 0;
                    for (ChatMsgItem *it = d->msg_list.head; it; it = it->next) {
                        if (it->type != CHAT_ITEM_COMMAND || it->u.cmd.settled) continue;
                        if (it->u.cmd.batch != batch->id) continue;
                        if (it->u.cmd.approved == -1 && !it->u.cmd.blocked) {
                            it->u.cmd.approved = 0;
                            chat_approval_deny(&batch->q, ci);
                        }
                        ci++;
                    }
                    ai_chat_settle_batch_if_done(d, batch);
                    if (d->hChatList) chat_listview_invalidate(d->hChatList);
                    chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                                    "[all commands cancelled]");
                    if (d->hChatList)
                        chat_listview_invalidate(d->hChatList);
                }
                SetFocus(d->hInput);
                return 0;
            }

            /* IDC_ACTIVITY_RETRY → cancel current request and resend */
            if (d && ctl_id == IDC_ACTIVITY_RETRY) {
                /* Cancel current stream if possible, reset activity */
                ai_chat_abort_session_stream(d, d->active_state);
                KillTimer(hwnd, TIMER_HEARTBEAT);
                chat_activity_reset(&d->activity);
                if (d->hChatList) {
                    chat_listview_set_pulse(d->hChatList, 0);
                    chat_listview_invalidate(d->hChatList);
                }

                /* Remove the streaming AI item (incomplete response) */
                if (d->stream_ai_item) {
                    chat_msg_remove(&d->msg_list, d->stream_ai_item);
                    d->stream_ai_item = NULL;
                }
                d->stream_phase = 0;
                d->stream_content[0] = '\0';
                d->stream_content_len = 0;
                d->stream_thinking[0] = '\0';
                d->stream_thinking_len = 0;

                /* Remove the last assistant message from conv if added */
                if (d->conv.msg_count > 0 &&
                    d->conv.messages[d->conv.msg_count - 1].role == AI_ROLE_ASSISTANT)
                    ai_msg_free(&d->conv.messages[--d->conv.msg_count]);

                /* Re-launch stream */
                ai_chat_start_indicator(d, "retrying");
                ai_chat_launch_stream_thread(d, "retry");
                return 0;
            }

            /* IDC_CHAT_THINKING_OPENED → user manually expanded a Thinking
             * disclosure this session; remember it so a reply-start doesn't
             * auto-collapse it (see the WM_AI_STREAM handler above). */
            if (d && ctl_id == IDC_CHAT_THINKING_OPENED) {
                d->show_thinking = 1;
                return 0;
            }

            /* IDC_CHAT_THINKING_CLOSED → user manually collapsed a Thinking
             * disclosure this session. Clear show_thinking (mirrored into
             * active_state the way IDC_CHAT_NEWCHAT does above) so a later
             * reply's block still starts open while it streams -- but the
             * item the user just closed is protected from re-opening by
             * its own thinking_user_set flag, set at the click site in
             * chat_listview.c. */
            if (d && ctl_id == IDC_CHAT_THINKING_CLOSED) {
                d->show_thinking = 0;
                if (d->active_state) d->active_state->show_thinking = 0;
                return 0;
            }

            /* IDC_CMD_EXPAND_ALL → toggle command list expand/collapse */
            if (d && ctl_id == IDC_CMD_EXPAND_ALL) {
                if (d->hChatList) chat_listview_toggle_cmd_expand(d->hChatList);
                return 0;
            }

            /* IDC_CMD_DETAIL → open the details popup for one command row.
             * lParam is the ChatMsgItem id (the list view posts it rather
             * than opening the popup itself, like every other card action).
             * The popup copies command, note, reason and safety before it runs its
             * own modal loop, so nothing it shows points into msg_list --
             * a stream chunk or rebuild that fires while it is open can
             * free the items freely. */
            if (d && ctl_id == IDC_CMD_DETAIL) {
                int detail_id = (int)lParam;
                for (ChatMsgItem *it = d->msg_list.head; it; it = it->next) {
                    if (it->id != detail_id || it->type != CHAT_ITEM_COMMAND)
                        continue;
                    const char *dcmd = it->u.cmd.command ? it->u.cmd.command
                                                         : it->text;
                    HWND top = GetAncestor(hwnd, GA_ROOT);
                    cmd_detail_show(top ? top : hwnd, dcmd ? dcmd : "",
                                    it->u.cmd.note ? it->u.cmd.note : "",
                                    it->u.cmd.reason,
                                    it->u.cmd.safety, it->u.cmd.tool,
                                    get_window_dpi(hwnd));
                    break;
                }
                return 0;
            }

            /* IDC_CHATLIST_PASTE → right-click paste from chat listview */
            if (d && ctl_id == IDC_CHATLIST_PASTE) {
                if (d->hInput && ai_chat_input_paste_text(d->hInput, hwnd))
                    SetFocus(d->hInput);
                return 0;
            }
            break;
        }
        case IDC_CHAT_INPUT:
            /* Handle Enter key in input (via EN_CHANGE notification is wrong;
             * we handle it via WM_KEYDOWN subclass or default button) */
            break;
        }
        break;

    case WM_AI_TOOL_MSG: {
        /* Tool call/result notification from background thread.
         * wParam: ChatItemType (CHAT_ITEM_TOOL_CALL or CHAT_ITEM_TOOL_RESULT or CHAT_ITEM_STATUS)
         * lParam: heap AiStreamChunk (stream id + text; we must free) */
        AiStreamChunk *tmsg = (AiStreamChunk *)lParam;
        if (!tmsg) return 0;
        void *tool_owner = NULL;
        int tool_live = d && ai_stream_table_find(&d->streams, tmsg->stream_id,
                                                  &tool_owner) != NULL;
        /* A stale stream's line, or a background session's, is dropped:
         * msg_list is the displayed session's thread. */
        if (tool_live && tool_owner == (void *)d->active_state && tmsg->delta) {
            ChatItemType tool_type = (ChatItemType)(WPARAM)wParam;
            chat_msg_append(&d->msg_list, tool_type, tmsg->delta);
        }
        free(tmsg->delta);
        free(tmsg);
        if (!tool_live || tool_owner != (void *)d->active_state) return 0;
        /* Following the tool call/result into view (if the user hasn't
         * scrolled away) is now automatic: chat_listview_invalidate()
         * recalcs layout, which applies the list's stick-to-bottom state. */
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
        return 0;
    }

    case WM_AI_STREAM: {
        /* Realtime streaming chunk: wParam 0=thinking, 1=content.
         *
         * Coalesce: drain ALL pending WM_AI_STREAM messages from the
         * queue and accumulate them before doing the expensive UI
         * update (remeasure + repaint) once.  This prevents the UI
         * thread from being starved when tokens arrive faster than
         * we can repaint. */
        if (!d) {
            AiStreamChunk *orphan = (AiStreamChunk *)lParam;
            if (orphan) { free(orphan->delta); free(orphan); }
            break;
        }

        int display_dirty = 0;

        /* Process this chunk and all queued WM_AI_STREAM messages */
        WPARAM cur_wp = wParam;
        LPARAM cur_lp = lParam;
        for (;;) {
            AiStreamChunk *chunk = (AiStreamChunk *)cur_lp;
            if (!chunk) goto next_coalesce;
            char *delta = chunk->delta;
            void *chunk_owner = NULL;
            AiStream *chunk_stream = ai_stream_table_find(&d->streams,
                                                          chunk->stream_id,
                                                          &chunk_owner);
            free(chunk);
            if (!delta) goto next_coalesce;

            /* Stale: stopped, retried, or its session is gone. */
            if (!chunk_stream) { free(delta); goto next_coalesce; }
            AiSessionState *src = (AiSessionState *)chunk_owner;

            /* Accumulate to source session buffers */
            if (src) {
                size_t dlen = strlen(delta);
                if (cur_wp == 0) {
                    if (src->stream_thinking &&
                        src->stream_thinking_len + dlen < AI_MSG_MAX - 1) {
                        memcpy(src->stream_thinking + src->stream_thinking_len,
                               delta, dlen);
                        src->stream_thinking_len += dlen;
                        src->stream_thinking[src->stream_thinking_len] = '\0';
                    }
                    if (src->stream_phase == 0)
                        src->stream_phase = 1;
                } else {
                    if (src->stream_content &&
                        src->stream_content_len + dlen < AI_MSG_MAX - 1) {
                        memcpy(src->stream_content + src->stream_content_len,
                               delta, dlen);
                        src->stream_content_len += dlen;
                        src->stream_content[src->stream_content_len] = '\0';
                    }
                    if (src->stream_phase < 2)
                        src->stream_phase = 2;
                }
            }

            /* Accumulate to display buffers if active session */
            if (src == d->active_state) {
                size_t dlen = strlen(delta);
                if (cur_wp == 0) {
                    if (d->stream_thinking_len + dlen < AI_MSG_MAX - 1) {
                        memcpy(d->stream_thinking + d->stream_thinking_len,
                               delta, dlen);
                        d->stream_thinking_len += dlen;
                        d->stream_thinking[d->stream_thinking_len] = '\0';
                    }
                    if (d->stream_phase == 0)
                        d->stream_phase = 1;
                } else {
                    if (d->stream_content_len + dlen < AI_MSG_MAX - 1) {
                        memcpy(d->stream_content + d->stream_content_len,
                               delta, dlen);
                        d->stream_content_len += dlen;
                        d->stream_content[d->stream_content_len] = '\0';
                    }
                }
                display_dirty = 1;
            }

            free(delta);

next_coalesce:;
            /* Peek for more WM_AI_STREAM messages */
            MSG peeked;
            if (PeekMessage(&peeked, hwnd, WM_AI_STREAM, WM_AI_STREAM,
                            PM_REMOVE)) {
                cur_wp = peeked.wParam;
                cur_lp = peeked.lParam;
            } else {
                break;  /* No more queued — proceed to UI update */
            }
        }

        /* Single UI update for all coalesced chunks */
        if (display_dirty) {
            float now = (float)GetTickCount() / 1000.0f;
            if (d->stream_phase == 1) {
                if (d->activity.phase != ACTIVITY_THINKING)
                    chat_activity_set_phase(&d->activity,
                                            ACTIVITY_THINKING, now);
            } else if (d->stream_phase >= 2) {
                if (d->activity.phase != ACTIVITY_RESPONDING)
                    chat_activity_set_phase(&d->activity,
                                            ACTIVITY_RESPONDING, now);
            }
            chat_activity_token(&d->activity, now);

            if (d->stream_ai_item) {
                /* Thinking disclosure: starts (and stays) collapsed, even
                 * while reasoning is still streaming in -- most people
                 * don't want to see the raw reasoning (maintainer request,
                 * 2026-09-24). chat_msg_append() already defaults every new
                 * item's thinking_collapsed to 1, so there is nothing to do
                 * for that case here. The one exception is a session-wide
                 * preference: once the user has manually opened a Thinking
                 * disclosure this session (d->show_thinking), later replies
                 * open already-expanded too, so they aren't re-clicking the
                 * same disclosure every turn. A manual per-item toggle
                 * (thinking_user_set) always wins over this -- once set, it
                 * must never be touched again from here. */
                if (d->stream_thinking_len > 0 &&
                    !d->stream_ai_item->u.ai.thinking_user_set &&
                    d->show_thinking &&
                    d->stream_ai_item->u.ai.thinking_collapsed) {
                    d->stream_ai_item->u.ai.thinking_collapsed = 0;
                    d->stream_ai_item->dirty = 1;
                }
                /* Always update thinking text if we have any -- the box's
                 * own follow-vs-pinned scroll (stick_scroll_on_layout(),
                 * reused from the outer chat list) is settled inside
                 * build_thinking_layout() itself (chat_listview.c) the next
                 * time its geometry is computed, so there is no scroll_y
                 * bookkeeping to do here. */
                if (d->stream_thinking_len > 0) {
                    chat_msg_set_thinking(d->stream_ai_item,
                                          d->stream_thinking);
                }
                /* Always update content text if we have any */
                if (d->stream_content_len > 0) {
                    chat_msg_set_text(d->stream_ai_item,
                                      d->stream_content);
                }
                /* Following the growing reply into view (if the user
                 * hasn't scrolled away) is now automatic: invalidate's
                 * recalc_layout applies the list's stick-to-bottom state
                 * after total_height has already grown, so there's no
                 * stale-margin race the way a per-chunk scroll_to_bottom
                 * had. */
                if (d->hChatList) chat_listview_invalidate(d->hChatList);
            }
        }

        return 0;
    }

    case WM_AI_TOOL_DECIDE:
        /* A stream thread needs the user's say on a tool call. The handler
         * never waits and frees the message on every path, a stale stream
         * included. */
        ai_chat_on_tool_decide(d, (AiToolDecideMsg *)lParam);
        return 0;

    case WM_AI_GET_POLICY:
        return d ? (LRESULT)cmd_policy_status_word(d->approval_q.policy) : 0;

    case WM_AI_RESPONSE: {
        AiResponseMsg *rmsg = (AiResponseMsg *)lParam;
        if (!rmsg) break;
        void *resp_owner = NULL;
        AiStream *resp_stream = d ? ai_stream_table_find(&d->streams,
                                                         rmsg->stream_id,
                                                         &resp_owner) : NULL;
        if (!resp_stream) {
            /* Stale: the reply was stopped or retried, its tab closed, or
             * the panel rebuilt. Nothing of it may touch a session. */
            free(rmsg->content);
            free(rmsg->thinking);
            free(rmsg);
            return 0;
        }
        AiSessionState *src = (AiSessionState *)resp_owner;

        /* A reply for a session that isn't displayed: the thread's own
         * conversation (the send-time copy plus any tool turns and, on a
         * normal end, the reply) becomes the session's, so nothing the
         * thread wrote is lost. When the tool loop stopped at its limit or
         * on a failed rebuild it ends on the tool results instead -- the
         * same as before this conversation was the thread's own. */
        if (src != d->active_state && wParam == 2) {
            ai_conv_take(&src->conv, &resp_stream->conv);
            src->valid = 1;
        }
        /* The done/error message is the thread's last: drop our reference. */
        ai_stream_table_finish(&d->streams, rmsg->stream_id);
        resp_stream = NULL;
        /* The session's current stream has ended, so any tool card it left
         * (the thread's EXPIRED post can fail) is stale: answer and drop it
         * now, so no tool batch outlives its stream or is evicted unanswered
         * by cmd_batch_add below. The slot is gone with the stream, so the
         * rows read "cancelled, not run". */
        if (src) ai_chat_cancel_tool_batches(d, src);

        /* Free per-session stream accumulators */
        if (src) {
            free(src->stream_content);
            src->stream_content = NULL;
            src->stream_content_len = 0;
            free(src->stream_thinking);
            src->stream_thinking = NULL;
            src->stream_thinking_len = 0;
            src->stream_phase = 0;
        }

        /* If this response is for a different session than the one displayed,
         * the thread already committed its result to src->conv. Build a real
         * batch in that (background) session's own set -- there's no live
         * msg_list to post cards into while it isn't displayed, but
         * ai_chat_rebuild_display() replays every batch in the set the moment
         * the user switches to it, same as an active-session reply. */
        if (src != d->active_state) {
            if (wParam == 2 && src && rmsg->content) {
                char cmds[16][1024];
                char notes[16][CMD_NOTE_MAX];
                int quoted[16] = { 0 };
                int rejected = 0, rejected_long = 0;
                int ncmds = ai_extract_commands_full(rmsg->content, cmds,
                                                     notes, quoted, 16,
                                                     &rejected,
                                                     &rejected_long);
                int nquoted = 0;
                for (int qi = 0; qi < ncmds; qi++)
                    if (quoted[qi]) nquoted++;
                if (ncmds > 0) {
                    /* Built while the tab is hidden: Auto-approve forced
                     * off, so every row is PENDING or BLOCKED and nothing
                     * runs before the user has looked (spec 2026-10-18
                     * section 6). Before this, an all-approved batch built
                     * here ran, stale, after an unrelated later prompt. */
                    ApprovalQueue bg_defaults;
                    chat_approval_init(&bg_defaults);
                    bg_defaults.policy = cmd_policy_attended(src->policy);

                    CmdBatch *batch = cmd_batch_add(&src->batches,
                                                    &bg_defaults, NULL);
                    if (batch) {
                        batch->conv_mark = src->conv.msg_count;
                        for (int ci = 0; ci < ncmds; ci++) {
                            /* -1 means the queue refused it (full, blank,
                             * control characters, or too long) -- not
                             * added, and no card is ever built for it. */
                            int bidx = quoted[ci]
                                ? chat_approval_add_quoted(&batch->q,
                                    cmds[ci], (CmdPlatform)src->platform,
                                    src->platform_contradicted)
                                : chat_approval_add_session(&batch->q,
                                    cmds[ci], (CmdPlatform)src->platform,
                                    src->platform_contradicted);
                            /* The note goes on the index the queue returned,
                             * never the loop index: a refused command shifts
                             * every later one down. */
                            if (bidx >= 0)
                                chat_approval_set_note(&batch->q, bidx,
                                                       notes[ci]);
                        }
                    }
                }
                if (rejected_long > 0) {
                    char lnote[256];
                    snprintf(lnote, sizeof(lnote),
                        "NOTE: %d command(s) were NOT run because they are "
                        "longer than the %d-byte limit for a single EXEC "
                        "block. Do not claim they ran. Break the work into "
                        "smaller steps (write a file a few lines at a time) "
                        "and send them again.", rejected_long, 1023);
                    ai_conv_add(&src->conv, AI_ROLE_USER, lnote);
                }
                if (rejected > 0) {
                    char note[256];
                    snprintf(note, sizeof(note),
                        "NOTE: %d EXEC block(s) were rejected because they "
                        "contained control characters (newline, tab, "
                        "escape, etc.) or bidirectional text formatting "
                        "characters. Put exactly one single-line command "
                        "in each EXEC block.", rejected);
                    ai_conv_add(&src->conv, AI_ROLE_USER, note);
                }
                if (nquoted > 0) {
                    char qnote[256];
                    snprintf(qnote, sizeof(qnote),
                        "NOTE: %d [EXEC] marker(s) were inside a code block "
                        "or inline code and were NOT run. Put each marker on "
                        "its own line, outside any code fence.", nquoted);
                    ai_conv_add(&src->conv, AI_ROLE_USER, qnote);
                }
            }
            if (src) {
                src->busy = 0;
                src->activity_phase = (int)ACTIVITY_IDLE;
            }
            free(rmsg->content);
            free(rmsg->thinking);
            free(rmsg);
            return 0;
        }

        KillTimer(hwnd, TIMER_THINKING);
        KillTimer(hwnd, TIMER_HEARTBEAT);
        chat_activity_reset(&d->activity);
        if (d->hChatList) {
            chat_listview_set_pulse(d->hChatList, 0);
            chat_listview_invalidate(d->hChatList);
        }
        d->indicator_pos = -1;

        if (wParam == 2) {
            /* Streaming complete — finalize AI item and extract commands */
            char *text = rmsg->content;

            /* Store actual token counts when the API provided them */
            if (rmsg->input_tokens > 0)
                d->actual_input_tokens = rmsg->input_tokens;
            if (rmsg->output_tokens > 0)
                d->actual_output_tokens = rmsg->output_tokens;

            /* The thread committed the assistant message to src->conv.
             * Sync d->conv (the working copy) so it includes the response. */
            if (text) {
                ai_conv_add(&d->conv, AI_ROLE_ASSISTANT, text);
            }

            /* Save thinking for this assistant message in history. */
            if (d->stream_thinking_len > 0 &&
                d->conv.msg_count > 0) {
                int idx = d->conv.msg_count - 1;
                free(d->thinking_history[idx]);
                d->thinking_history[idx] =
                    _strdup(d->stream_thinking);
            }
            d->stream_thinking[0] = '\0';
            d->stream_thinking_len = 0;

            /* Extract commands from the full accumulated content */
            char cmds[16][1024];
            char notes[16][CMD_NOTE_MAX];
            int quoted[16] = { 0 };
            int rejected = 0, rejected_long = 0;
            int ncmds = text ? ai_extract_commands_full(text, cmds, notes,
                                                        quoted, 16,
                                                        &rejected,
                                                        &rejected_long) : 0;
            int nquoted = 0;
            for (int qi = 0; qi < ncmds; qi++)
                if (quoted[qi]) nquoted++;

            /* Finalize the AI item text.  When commands were found, show
             * only the pre-command portion — the summary/analysis after
             * [/EXEC] is speculative (commands haven't run yet) and will
             * be regenerated by the continue-message flow after execution. */
            if (d->stream_ai_item && text) {
                if (ncmds > 0) {
                    char pre_text[AI_MSG_MAX];
                    ai_response_split(text, pre_text, sizeof(pre_text),
                                      NULL, 0);
                    chat_msg_set_text(d->stream_ai_item, pre_text);
                } else {
                    chat_msg_set_text(d->stream_ai_item, text);
                }
                if (d->thinking_history[d->conv.msg_count - 1])
                    chat_msg_set_thinking(d->stream_ai_item,
                        d->thinking_history[d->conv.msg_count - 1]);
                d->stream_ai_item->u.ai.thinking_complete = 1;
            }
            d->stream_ai_item = NULL;
            d->stream_display_start = -1;
            d->stream_phase = 0;

            /* C1: tell both the user and the model when one or more
             * [EXEC] blocks were dropped for containing control
             * characters (a newline is the classic way to smuggle a
             * second, unclassified command past the approval card). */
            /* An EXEC block too long for ApprovalEntry.command used to be
             * truncated mid-argument and run anyway (a printf losing its
             * closing quote left the shell on its continuation prompt).
             * It is skipped now -- no card, no queue entry -- and both the
             * user and the model are told, with the limit, so the model can
             * break the work into smaller steps. */
            if (rejected_long > 0) {
                char status_text[160];
                snprintf(status_text, sizeof(status_text),
                    "[%d command(s) not run: over the %d-byte limit; ask for "
                    "them in smaller steps]", rejected_long, 1023);
                chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS, status_text);
                if (d->hChatList) chat_listview_invalidate(d->hChatList);

                char long_note[256];
                snprintf(long_note, sizeof(long_note),
                    "NOTE: %d command(s) were NOT run because they are longer "
                    "than the %d-byte limit for a single EXEC block. Do not "
                    "claim they ran. Break the work into smaller steps (write "
                    "a file a few lines at a time) and send them again.",
                    rejected_long, 1023);
                ai_conv_add(&d->conv, AI_ROLE_USER, long_note);
            }

            if (rejected > 0) {
                char status_text[160];
                snprintf(status_text, sizeof(status_text),
                    "[%d command(s) rejected: control characters inside "
                    "an EXEC block]", rejected);
                chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS, status_text);
                if (d->hChatList) chat_listview_invalidate(d->hChatList);

                char rej_note[256];
                snprintf(rej_note, sizeof(rej_note),
                    "NOTE: %d EXEC block(s) were rejected because they "
                    "contained control characters (newline, tab, escape, "
                    "etc.) or bidirectional text formatting characters. "
                    "Put exactly one single-line command in each EXEC "
                    "block.", rejected);
                ai_conv_add(&d->conv, AI_ROLE_USER, rej_note);
            }

            /* A marker the model wrote inside a code block or inline code
             * is shown on the card as "quoted, not run" and never runs.
             * The model is told too, so it does not claim the command ran
             * and puts its next marker outside the fence. */
            if (nquoted > 0) {
                char quote_note[256];
                snprintf(quote_note, sizeof(quote_note),
                    "NOTE: %d [EXEC] marker(s) were inside a code block or "
                    "inline code and were NOT run. Put each marker on its "
                    "own line, outside any code fence.", nquoted);
                ai_conv_add(&d->conv, AI_ROLE_USER, quote_note);
            }

            /* Pending command batches: earlier cards are never touched by
             * a new reply -- each reply that yields commands gets its own
             * batch and its own card (see docs/superpowers/specs/
             * 2026-09-09-pending-command-batches.md). No settle-all here. */

            if (ncmds > 0 && d->active_state) {
                /* New batch for this reply. approval_q's CmdPolicy is the
                 * only field cmd_batch_add() reads from `defaults` -- it
                 * seeds the new batch's queue. */
                int evicted_id = 0;
                /* After AI_AUTO_ROUNDS_MAX unattended rounds this one batch
                 * is built attended, so its rows arrive PENDING on an
                 * ordinary card. The session's policy and the Auto-approve
                 * button are untouched (guard-rails spec section 1). */
                ApprovalQueue batch_defaults;
                chat_approval_init(&batch_defaults);
                batch_defaults.policy = ai_rounds_batch_policy(
                    d->active_state, d->approval_q.policy);
                int rounds_paused = ai_rounds_paused(d->active_state,
                                                     d->approval_q.policy);
                CmdBatch *batch = cmd_batch_add(&d->active_state->batches,
                                                &batch_defaults, &evicted_id);

                if (batch) {
                    /* Record how many conversation messages exist right
                     * after this reply -- compared against the live count
                     * when the batch finishes running to decide whether
                     * the continue message needs to name it explicitly
                     * (ai_build_continue_text's newer_exchanges). */
                    batch->conv_mark = d->conv.msg_count;

                    if (evicted_id > 0) {
                        chat_msg_batch_settle(&d->msg_list, evicted_id);
                        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                            "[earlier command batch skipped]");
                    }

                    if (rounds_paused) {
                        char pause_text[160];
                        snprintf(pause_text, sizeof(pause_text),
                            "[Auto-approve paused after %d automatic rounds "
                            "\xE2\x80\x94 Run the card to go on]",
                            AI_AUTO_ROUNDS_MAX);
                        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                                        pause_text);
                    }

                    /* Classify and queue every extracted command in order
                     * -- including ones above the policy ceiling, which
                     * chat_approval_add marks APPROVE_BLOCKED rather than
                     * silently dropping. This keeps item order == queue
                     * order always, so index-based execution (APPROVE_SEL
                     * etc.) can never run the wrong command, and a batch of
                     * nothing but held commands still shows up as a card the
                     * user can run after raising the ceiling (see
                     * chat_approval_needs_user below). */
                    for (int ci = 0; ci < ncmds; ci++) {
                        int idx = quoted[ci]
                            ? chat_approval_add_quoted(&batch->q, cmds[ci],
                                (CmdPlatform)d->active_state->platform,
                                d->active_state->platform_contradicted)
                            : chat_approval_add_session(&batch->q, cmds[ci],
                                (CmdPlatform)d->active_state->platform,
                                d->active_state->platform_contradicted);
                        if (idx < 0) continue;  /* queue full or blank -- drop it */
                        chat_approval_set_note(&batch->q, idx, notes[ci]);

                        ChatMsgItem *cmd_item = chat_msg_append(
                            &d->msg_list, CHAT_ITEM_COMMAND, "");
                        if (cmd_item) {
                            chat_msg_set_command(cmd_item, cmds[ci],
                                batch->q.entries[idx].safety,
                                batch->q.entries[idx].status == APPROVE_BLOCKED);
                            chat_msg_set_quoted(cmd_item,
                                                batch->q.entries[idx].quoted);
                            chat_msg_set_reason(cmd_item,
                                                batch->q.entries[idx].reason);
                            chat_msg_set_batch(cmd_item, batch->id);
                            chat_msg_set_note(cmd_item,
                                              batch->q.entries[idx].note);
                        }
                    }

                    /* Tell the AI which commands (if any) were blocked by
                     * the read-only policy -- classified straight from the
                     * queue, which is the single source of truth for
                     * write/critical classification (cmd_classify, via
                     * chat_approval_add). */
                    {
                        int nblocked = 0;
                        for (int qi = 0; qi < batch->q.count; qi++)
                            if (batch->q.entries[qi].status == APPROVE_BLOCKED &&
                                !batch->q.entries[qi].quoted)
                                nblocked++;
                        if (nblocked > 0) {
                            char list[1536];
                            size_t lp = 0;
                            list[0] = '\0';
                            for (int qi = 0; qi < batch->q.count; qi++) {
                                if (batch->q.entries[qi].status == APPROVE_BLOCKED &&
                                    !batch->q.entries[qi].quoted) {
                                    if (!str_append_fmt(list, sizeof(list), &lp,
                                            "  - %s\n", batch->q.entries[qi].command))
                                        break;
                                }
                            }
                            char bmsg[2048];
                            if (ai_build_policy_blocked_note(list, bmsg, sizeof(bmsg)) > 0) {
                                ai_conv_add(&d->conv, AI_ROLE_USER, bmsg);
                            }
                        }
                    }

                    if (!chat_approval_needs_user(&batch->q)) {
                        /* Auto-approve already decided all commands —
                         * skip approval UI and execute immediately */
                        int ci2 = 0;
                        ChatMsgItem *it = d->msg_list.head;
                        while (it) {
                            if (it->type == CHAT_ITEM_COMMAND &&
                                !it->u.cmd.settled &&
                                it->u.cmd.batch == batch->id) {
                                if (batch->q.entries[ci2].status == APPROVE_APPROVED)
                                    it->u.cmd.approved = 1;
                                ci2++;
                            }
                            it = it->next;
                        }
                        /* Every entry was decided by policy alone -- settle
                         * the batch immediately so its rows show "queued"/
                         * "running"/"ran" instead of sitting on the pending
                         * card's un-settled styling while it runs. */
                        ai_chat_settle_batch_if_done(d, batch);
                        /* Let the dispatcher send the approved commands one
                         * at a time, only once the terminal is at a prompt. */
                        ai_chat_dispatch_start(d, batch->id);
                    } else {
                        /* Leave the card pending and reset its container's
                         * scroll -- it stays interactive for the life of
                         * the session (rule 5). */
                        if (d->hChatList)
                            chat_listview_reset_cmd_expand(d->hChatList);
                    }
                    ai_chat_relayout(d);
                    /* A reply finishing is not a user action: only move
                     * the caret to the input if the focus is already
                     * somewhere in this panel. Never take it from the
                     * terminal while the user is typing there. */
                    {
                        HWND f = GetFocus();
                        if (f && (f == hwnd || IsChild(hwnd, f)))
                            SetFocus(d->hInput);
                    }
                }
            }

            /* Following the finished reply/approval card into view (if the
             * user hasn't scrolled away) is now automatic via stick-to-
             * bottom in recalc_layout. */
            if (d->hChatList) chat_listview_invalidate(d->hChatList);

            free(text);
            free(rmsg->thinking);
            free(rmsg);
            ai_chat_update_context_bar(d);
        } else {
            /* Error */
            char *text = rmsg->content;
            d->stream_phase = 0;
            d->stream_ai_item = NULL;
            if (text) {
                chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                                text);
                if (d->hChatList) chat_listview_invalidate(d->hChatList);
                free(text);
            }
            free(rmsg->thinking);
            free(rmsg);
        }

        if (src) src->busy = 0;
        /* Restore Send button from Stop */
        if (d->hSendBtn) {
            SetWindowText(d->hSendBtn, ">");
            InvalidateRect(d->hSendBtn, NULL, TRUE);
        }
        /* This reply is done (whether it was the original request or a
         * batch's continue message) -- if another batch has approved
         * commands still waiting to run, start it now (rule 3's "queued,
         * runs next"). No-op if the dispatcher is already busy or another
         * stream is in flight. */
        if (src == d->active_state)
            ai_chat_maybe_start_next_batch(d);
        return 0;
    }

    case WM_TIMER:
        if (!d) return 0;
        if (wParam == TIMER_CMD_QUEUE) {
            ai_chat_dispatch_tick(d);
        } else if (wParam == TIMER_STREAM_REAP) {
            /* A reply whose thread ended without its final message getting
             * through (out of memory, or the post failed) would leave its
             * session busy for ever: end it here as if Stop were pressed. */
            void *lost;
            while ((lost = ai_stream_table_lost_owner(&d->streams)) != NULL) {
                if (lost == (void *)d->active_state) {
                    ai_chat_cancel_active_stream(d);
                    chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                                    "[the AI reply was lost]");
                    if (d->hChatList) chat_listview_invalidate(d->hChatList);
                } else {
                    ai_chat_abort_session_stream(d, (AiSessionState *)lost);
                }
            }
            if (d->streams.count == 0)
                KillTimer(hwnd, TIMER_STREAM_REAP);
        } else if (wParam == TIMER_HEARTBEAT) {
            /* Activity monitor heartbeat: tick health + toggle pulse */
            float now = (float)GetTickCount() / 1000.0f;
            chat_activity_tick(&d->activity, now);
            d->pulse_toggle = !d->pulse_toggle;

            /* Tick thinking elapsed time on the streaming AI item */
            if (d->stream_ai_item && d->stream_phase == 1
                && d->stream_ai_item->u.ai.thinking_text) {
                d->stream_ai_item->u.ai.thinking_elapsed += 1.0f;
                d->stream_ai_item->dirty = 1;
            }

            if (d->hChatList) {
                chat_listview_set_pulse(d->hChatList, d->pulse_toggle);
                InvalidateRect(d->hChatList, NULL, FALSE);
            }
            /* Also repaint header area for the header bar indicator */
            {
                RECT hdr_rc;
                GetClientRect(hwnd, &hdr_rc);
                hdr_rc.bottom = ns_scale(4, d->dpi) + ns_scale(24, d->dpi) + ns_scale(4, d->dpi) + ns_scale(16, d->dpi) + ns_scale(4, d->dpi);
                InvalidateRect(hwnd, &hdr_rc, FALSE);
            }
        } else if (wParam == TIMER_SCROLL_SYNC) {
            /* ChatListView handles its own scroll; only sync input */
            ai_chat_input_sync_scroll(d);
        }
        return 0;

    case WM_VSCROLL:
        if (d && d->hDisplayScrollbar &&
            (HWND)lParam == d->hDisplayScrollbar) {
            /* ChatListView handles its own scroll; forward WM_VSCROLL */
            if (d->hChatList)
                SendMessage(d->hChatList, WM_VSCROLL, wParam, 0);
            return 0;
        }
        if (d && d->hInputScrollbar &&
            (HWND)lParam == d->hInputScrollbar) {
            WORD code = LOWORD(wParam);
            int first = (int)SendMessage(d->hInput,
                            EM_GETFIRSTVISIBLELINE, 0, 0);
            int delta = 0;
            RECT erc_i;
            GetClientRect(d->hInput, &erc_i);
            int vis = edit_scroll_visible_lines(erc_i.bottom - erc_i.top,
                          d->input_line_h > 0 ? d->input_line_h : 1);
            switch (code) {
            case SB_LINEUP:    delta = -1;   break;
            case SB_LINEDOWN:  delta =  1;   break;
            case SB_PAGEUP:    delta = -vis; break;
            case SB_PAGEDOWN:  delta =  vis; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                delta = edit_scroll_line_delta(
                    csb_get_trackpos(d->hInputScrollbar), first);
                break;
            case SB_TOP:    delta = -first;  break;
            case SB_BOTTOM: delta =  99999;  break;
            }
            if (delta != 0)
                SendMessage(d->hInput, EM_LINESCROLL, 0, (LPARAM)delta);
            ai_chat_input_sync_scroll(d);
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (d && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (wParam == VK_OEM_PLUS || wParam == (WPARAM)'=') {
                ai_chat_apply_zoom(d, 1);
                return 0;
            }
            if (wParam == VK_OEM_MINUS || wParam == (WPARAM)'-') {
                ai_chat_apply_zoom(d, -1);
                return 0;
            }
        }
        break;

    case WM_MOUSEWHEEL:
        if (d) {
            int zdelta = GET_WHEEL_DELTA_WPARAM(wParam);
            /* Ctrl+Scroll zooms the font */
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                ai_chat_apply_zoom(d, zdelta > 0 ? 1 : -1);
                return 0;
            }
            /* Forward wheel to ChatListView if it exists */
            if (d->hChatList)
                return SendMessage(d->hChatList, WM_MOUSEWHEEL,
                                   wParam, lParam);
        }
        break;

    case WM_PAINT:
        if (d && d->theme) {
            /* The header's model chip and the status line are painted
             * elements (not child windows), so they need repainting on
             * every WM_PAINT regardless of activity; the activity dot +
             * one-word status inside ai_chat_paint_header() is the only piece
             * that's conditional on d->activity.phase. */
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            ai_chat_paint_header(d, hdc);
            ai_chat_paint_status_line(d, hdc);
            ai_chat_paint_sash(d, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        if (d && d->theme) {
            HDC hdc_bg = (HDC)wParam;
            RECT rc_bg;
            GetClientRect(hwnd, &rc_bg);
            FillRect(hdc_bg, &rc_bg, d->hBrBgPrimary);
            return 1;
        }
        break;

    case WM_DRAWITEM:
        if (d && d->theme) {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if ((int)dis->CtlID == IDC_CHAT_SEND) {
                /* Custom send button: pastel blue bg + larger icon */
                {
                    HDC hdc = dis->hDC;
                    RECT rc = dis->rcItem;
                    int pressed = (dis->itemState & ODS_SELECTED) != 0;
                    wchar_t btn_text[64];
                    GetWindowTextW(dis->hwndItem, btn_text, 64);
                    int is_stop = (btn_text[0] == 0x25A0); /* ■ */
                    unsigned int bg_rgb = is_stop
                        ? d->theme->chat.stop_btn
                        : d->theme->chat.send_btn;
                    if (pressed) {
                        unsigned int r = (bg_rgb >> 16) & 0xFF;
                        unsigned int g = (bg_rgb >> 8)  & 0xFF;
                        unsigned int b =  bg_rgb        & 0xFF;
                        r = r * 4 / 5; g = g * 4 / 5; b = b * 4 / 5;
                        bg_rgb = (r << 16) | (g << 8) | b;
                    }
                    /* 3D depth colors */
                    unsigned int hi_r = ((bg_rgb >> 16) & 0xFF);
                    unsigned int hi_g = ((bg_rgb >> 8)  & 0xFF);
                    unsigned int hi_b = ( bg_rgb        & 0xFF);
                    hi_r = hi_r + (255 - hi_r) * 2 / 5;
                    hi_g = hi_g + (255 - hi_g) * 2 / 5;
                    hi_b = hi_b + (255 - hi_b) * 2 / 5;
                    unsigned int hi_rgb = (hi_r << 16) | (hi_g << 8) | hi_b;
                    unsigned int sh_r = ((bg_rgb >> 16) & 0xFF) * 3 / 5;
                    unsigned int sh_g = ((bg_rgb >> 8)  & 0xFF) * 3 / 5;
                    unsigned int sh_b = ( bg_rgb        & 0xFF) * 3 / 5;
                    unsigned int sh_rgb = (sh_r << 16) | (sh_g << 8) | sh_b;

                    /* Clear corners, draw round-rect body */
                    HBRUSH hBgBr = CreateSolidBrush(theme_cr(d->theme->bg_primary));
                    FillRect(hdc, &rc, hBgBr);
                    DeleteObject(hBgBr);
                    {
                        int radius = ns_scale(R_CTRL, d->dpi);
                        ns_draw_round_fill(hdc, &rc, radius, theme_cr(bg_rgb), 255);
                        ns_draw_round_stroke(hdc, &rc, radius,
                                             theme_cr(d->theme->border),
                                             STROKE_HAIRLINE);
                    }

                    /* Top highlight line (inset 1px) */
                    HPEN hHiPen = CreatePen(PS_SOLID, 1, theme_cr(hi_rgb));
                    HGDIOBJ oP = SelectObject(hdc, hHiPen);
                    MoveToEx(hdc, rc.left + 3, rc.top + 1, NULL);
                    LineTo(hdc, rc.right - 3, rc.top + 1);
                    /* Left highlight line */
                    MoveToEx(hdc, rc.left + 1, rc.top + 3, NULL);
                    LineTo(hdc, rc.left + 1, rc.bottom - 3);
                    SelectObject(hdc, oP);
                    DeleteObject(hHiPen);

                    /* Bottom shadow line (inset 1px) */
                    HPEN hShPen = CreatePen(PS_SOLID, 1, theme_cr(sh_rgb));
                    oP = SelectObject(hdc, hShPen);
                    MoveToEx(hdc, rc.left + 3, rc.bottom - 2, NULL);
                    LineTo(hdc, rc.right - 3, rc.bottom - 2);
                    /* Right shadow line */
                    MoveToEx(hdc, rc.right - 2, rc.top + 3, NULL);
                    LineTo(hdc, rc.right - 2, rc.bottom - 3);
                    SelectObject(hdc, oP);
                    DeleteObject(hShPen);

                    /* Draw send (paper plane) or stop (square) icon
                     * from the built-in vector set. Choose by BUSY state
                     * so the icon stays in sync with the button label
                     * even if the caller forgets to refresh text. */
                    int busy = ACTIVE_BUSY(d) || is_stop;
                    ns_icon_draw(hdc, busy ? NS_ICON_STOP : NS_ICON_SEND,
                                 &rc, theme_cr(0xFFFFFF), (UINT)d->dpi);
                }
            } else if ((int)dis->CtlID == IDC_CHAT_SAVE) {
                ai_chat_draw_header_icon_button(dis, d->theme, d->dpi, NS_ICON_SAVE);
            } else if ((int)dis->CtlID == IDC_CHAT_UNDOCK) {
                ai_chat_draw_header_icon_button(dis, d->theme, d->dpi,
                    d->docked ? NS_ICON_UNDOCK : NS_ICON_DOCK);
            } else if ((int)dis->CtlID == IDC_CHAT_NEWCHAT) {
                ai_chat_draw_header_icon_button(dis, d->theme, d->dpi, NS_ICON_NEW_CHAT);
            }
            /* Old IDC_CHAT_ALLOW / IDC_CHAT_DENY draw code removed —
             * approval buttons are now inline in chat_listview. The
             * owner-drawn Permit Write / Auto Approve "tab" buttons are
             * gone too -- the one policy control replaced both, painted in
             * the status line (WM_PAINT / ai_chat_status_hit()). */
            return TRUE;
        }
        break;

    case WM_CTLCOLOREDIT:
        if (d && d->theme) {
            SetTextColor((HDC)wParam, theme_cr(d->theme->text_main));
            SetBkColor((HDC)wParam, theme_cr(d->theme->bg_secondary));
            return (LRESULT)d->hBrBgSecondary;
        }
        break;

    case WM_CTLCOLORSTATIC:
        if (d && d->theme) {
            SetTextColor((HDC)wParam, theme_cr(d->theme->text_main));
            SetBkColor((HDC)wParam, theme_cr(d->theme->bg_primary));
            return (LRESULT)d->hBrBgPrimary;
        }
        break;

    case WM_DESTROY:
        if (d) {
            /* Abort and orphan every reply in flight -- the active session's
             * and any background tab's -- and put those sessions back to
             * idle: the threads free their own streams, and a panel built
             * later (undock/redock) starts with no stale busy flag.
             *
             * A table entry can have a NULL owner (defensive: nothing
             * today launches a stream with d->active_state NULL, but
             * nothing guarantees it never will). ai_chat_abort_session_stream()
             * no-ops on a NULL state without touching the table at all, so
             * looping on d->streams.count while resolving owner-at-slot-0
             * would spin forever the moment slot 0 held one: remove such
             * an entry directly instead of routing it through
             * ai_chat_abort_session_stream() (there is no session to update
             * anyway). Either branch always shrinks the table by at least
             * one entry, so the loop always terminates. */
            while (d->streams.count > 0) {
                AiSessionState *owner =
                    (AiSessionState *)ai_stream_table_owner_at(&d->streams, 0);
                if (owner)
                    ai_chat_abort_session_stream(d, owner);
                else
                    ai_stream_table_abort_owner(&d->streams, NULL);
            }
            ai_stream_table_abort_all(&d->streams);   /* nothing left; belt and braces */
            if (d->active_state) d->active_state->busy = 0;
            ai_chat_drain_stream_messages(hwnd);

            KillTimer(hwnd, TIMER_SCROLL_SYNC);
            KillTimer(hwnd, TIMER_HEARTBEAT);
            KillTimer(hwnd, TIMER_CMD_QUEUE);
            KillTimer(hwnd, TIMER_STREAM_REAP);
            d->dispatch_active = 0;
            /* Save conversation back to session before cleanup */
            if (d->active_state) {
                ai_conv_take(&d->active_state->conv, &d->conv);
                d->active_state->valid = 1;
            }
            ai_conv_reset(&d->conv);   /* no session: nothing may leak */
            ai_chat_thinking_history_clear(d);
            ai_attachment_free(&d->pending_attachment);
            chat_msg_list_clear(&d->msg_list);
            d->stream_ai_item = NULL;
            /* d->hFont comes from the ns_font cache — owned there, not here. */
            if (d->hSmallFont) DeleteObject(d->hSmallFont);
            if (d->hChatFont) DeleteObject(d->hChatFont);
            if (d->hBoldFont) DeleteObject(d->hBoldFont);
            if (d->hMonoFont) DeleteObject(d->hMonoFont);
            if (d->hTooltip) DestroyWindow(d->hTooltip);
            if (d->hBrBgPrimary)   DeleteObject(d->hBrBgPrimary);
            if (d->hBrBgSecondary) DeleteObject(d->hBrBgSecondary);
            GdiplusShutdown(d->gdip_token);
            free(d->ctx_term);
            free(d->ctx_prompt);
            free(d);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)NULL);
        }
        return 0;

    case WM_CLOSE:
        if (d && d->docked) {
            /* Docked: ask parent to close the panel (preserves state) */
            SendMessage(GetParent(hwnd), WM_COMMAND,
                        MAKEWPARAM(IDM_VIEW_AI_CHAT, 0), 0);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void ai_chat_init(HINSTANCE hInstance)
{
    /* Load RichEdit control library (still needed for input field).
     * LoadLibraryExW + LOAD_LIBRARY_SEARCH_SYSTEM32 (DLL search-order
     * hardening): search System32 only, never the exe's own directory or
     * the current directory, so a same-named DLL planted in either can't be
     * loaded instead of the real one. */
    LoadLibraryExW(L"Riched20.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    LoadLibraryExW(L"Msftedit.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

    /* Register the ChatListView window class */
    chat_listview_register(hInstance);

    WNDCLASSEX wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = AiChatWndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = AI_CHAT_CLASS;
    RegisterClassEx(&wc);
}

HWND ai_chat_show(HWND parent, const char *api_key, const char *provider,
                  const char *custom_url, const char *custom_model,
                  int paste_delay_ms, const char *font_name,
                  const char *ai_font,
                  const char *colour_scheme,
                  const char *session_notes, const char *system_notes,
                  AiSessionState *initial_state,
                  const char *session_name,
                  int docked)
{
    AiChatData *d = (AiChatData *)calloc(1, sizeof(AiChatData));
    if (!d) return NULL;

    d->indicator_pos = -1;
    d->stream_display_start = -1;
    d->forced_state = -1;
    d->paste_delay_ms = paste_delay_ms;
    d->context_lines = AI_CONTEXT_LINES_DEFAULT;
    d->tool_timeout_sec = AI_TOOL_TIMEOUT_SEC_DEFAULT;
    if (font_name && font_name[0])
        strncpy(d->font_name, font_name, sizeof(d->font_name) - 1);
    else
        strncpy(d->font_name, APP_FONT_DEFAULT, sizeof(d->font_name) - 1);
    if (ai_font && ai_font[0])
        strncpy(d->ai_font_name, ai_font, sizeof(d->ai_font_name) - 1);
    else
        strncpy(d->ai_font_name, APP_FONT_AI_DEFAULT, sizeof(d->ai_font_name) - 1);

    /* Theme lookup */
    {
        int idx = ui_theme_find(colour_scheme ? colour_scheme : "");
        d->theme = ui_theme_get(idx);
        d->hBrBgPrimary   = CreateSolidBrush(theme_cr(d->theme->bg_primary));
        d->hBrBgSecondary = CreateSolidBrush(theme_cr(d->theme->bg_secondary));
    }

    if (api_key)
        strncpy(d->api_key, api_key, sizeof(d->api_key) - 1);
    if (provider)
        strncpy(d->provider, provider, sizeof(d->provider) - 1);
    if (custom_url)
        strncpy(d->custom_url, custom_url, sizeof(d->custom_url) - 1);
    if (custom_model)
        strncpy(d->custom_model, custom_model, sizeof(d->custom_model) - 1);

    /* Initialize conversation with user-selected model (if set),
     * otherwise fall back to provider default */
    const char *model = (custom_model && custom_model[0])
                        ? custom_model : ai_provider_model(provider);
    ai_conv_init(&d->conv, model ? model : "deepseek-chat");
    d->context_limit = ai_model_context_limit(model ? model : "deepseek-chat");

    /* Load existing conversation from session state if available */
    d->active_state = initial_state;
    if (initial_state && initial_state->valid) {
        ai_conv_take(&d->conv, &initial_state->conv);
    }

    if (session_notes)
        strncpy(d->session_notes, session_notes, sizeof(d->session_notes) - 1);
    if (system_notes)
        strncpy(d->system_notes, system_notes, sizeof(d->system_notes) - 1);
    if (session_name)
        strncpy(d->session_name, session_name, sizeof(d->session_name) - 1);

    d->docked = docked;

    /* Scale window size for DPI */
    int pdpi = get_window_dpi(parent);

    DWORD style = docked
        ? (WS_CHILD | WS_CLIPCHILDREN)       /* hidden until first resize */
        : (WS_OVERLAPPEDWINDOW | WS_VISIBLE);

    HWND hwnd = CreateWindowEx(
        0, AI_CHAT_CLASS, "AI Assist",
        style,
        docked ? 0 : CW_USEDEFAULT,
        docked ? 0 : CW_USEDEFAULT,
        docked ? 1 : ns_scale(500, pdpi),
        docked ? 1 : ns_scale(600, pdpi),
        parent, NULL, GetModuleHandle(NULL), d);

    if (!hwnd) {
        /* No panel: the session keeps its conversation. (WM_CREATE never
         * fails, so a NULL here means it never ran and nothing else was
         * set up.) */
        if (initial_state && initial_state->valid)
            ai_conv_take(&initial_state->conv, &d->conv);
        ai_conv_reset(&d->conv);
        free(d);
    }

    return hwnd;
}

void ai_chat_set_session(HWND hwnd, Terminal *term, SessionIo *io)
{
    if (!hwnd) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    d->active_term = term;
    d->active_io = io;
    d->last_snapshot_len = 0;   /* the old terminal's snapshot is not this one's */
    ai_chat_update_panel_state(d);
}

void ai_chat_set_shell_name(HWND hwnd, const char *shell_name)
{
    if (!hwnd) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    (void)snprintf(d->shell_name, sizeof(d->shell_name), "%s",
                   shell_name ? shell_name : "");
}

void ai_chat_force_state(HWND hwnd, int state_id)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    d->forced_state = state_id;
    ai_chat_update_panel_state(d);
}

void ai_chat_update_key(HWND hwnd, const char *api_key, const char *provider,
                        const char *custom_url, const char *custom_model)
{
    if (!hwnd) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;

    if (api_key)
        snprintf(d->api_key, sizeof(d->api_key), "%s", api_key);
    if (custom_url)
        snprintf(d->custom_url, sizeof(d->custom_url), "%s", custom_url);
    if (custom_model)
        snprintf(d->custom_model, sizeof(d->custom_model), "%s", custom_model);
    if (provider) {
        snprintf(d->provider, sizeof(d->provider), "%s", provider);
        const char *model = (custom_model && custom_model[0])
                            ? custom_model : ai_provider_model(provider);
        if (model)
            snprintf(d->conv.model, sizeof(d->conv.model), "%s", model);
    }

    /* Update model name on chat listview */
    if (d->hChatList)
        chat_listview_set_model(d->hChatList, d->conv.model);

    ai_chat_update_panel_state(d);
}

void ai_chat_update_tools(HWND hwnd,
                          const char *search_provider,
                          const char *search_url,
                          int max_search_results,
                          int web_fetch_enabled)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    ai_chat_register_tools(d,
                        search_provider ? search_provider : "none",
                        search_url,
                        max_search_results,
                        web_fetch_enabled);
}

void ai_chat_update_notes(HWND hwnd, const char *session_notes,
                          const char *system_notes)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    if (session_notes)
        strncpy(d->session_notes, session_notes, sizeof(d->session_notes) - 1);
    if (system_notes)
        strncpy(d->system_notes, system_notes, sizeof(d->system_notes) - 1);
}

void ai_chat_refresh_fonts(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;

    d->hFont = ns_font(FONT_BODY, d->dpi);
    /* The session label's font comes from the same cache (ns_font_flush()
     * invalidates the handle it was last given), so re-fetch it here. */
    if (d->hSessionLabel)
        SendMessage(d->hSessionLabel, WM_SETFONT,
                    (WPARAM)ns_font(FONT_TITLE, d->dpi), TRUE);
    InvalidateRect(hwnd, NULL, TRUE);
}

void ai_chat_set_theme(HWND hwnd, const char *colour_scheme)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;

    int idx = ui_theme_find(colour_scheme ? colour_scheme : "");
    d->theme = ui_theme_get(idx);

    /* Recreate cached brushes */
    if (d->hBrBgPrimary)   DeleteObject(d->hBrBgPrimary);
    if (d->hBrBgSecondary) DeleteObject(d->hBrBgSecondary);
    d->hBrBgPrimary   = CreateSolidBrush(theme_cr(d->theme->bg_primary));
    d->hBrBgSecondary = CreateSolidBrush(theme_cr(d->theme->bg_secondary));

    /* Update ChatListView theme */
    if (d->hChatList)
        chat_listview_set_theme(d->hChatList, d->theme);

    /* Update custom scrollbar themes */
    if (d->hDisplayScrollbar)
        csb_set_theme(d->hDisplayScrollbar, d->theme);
    if (d->hInputScrollbar)
        csb_set_theme(d->hInputScrollbar, d->theme);

    /* Update title bar and borders */
    themed_apply_title_bar(hwnd, d->theme);
    themed_apply_borders(hwnd, d->theme);

    /* Force full repaint */
    InvalidateRect(hwnd, NULL, TRUE);
}

void ai_chat_set_markdown(HWND hwnd, int enabled)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    if (d->hChatList)
        chat_listview_set_render_markdown(d->hChatList, enabled);
}

/* Seed state->policy from d->policy_default if (and only if) this session's
 * policy has never been set -- a session the user has already moved a marker
 * on (or that was already seeded) keeps its own choice. */
static void ai_chat_seed_policy(AiChatData *d, AiSessionState *state)
{
    if (!state || state->policy_seeded) return;
    state->policy = d->policy_default;
    cmd_policy_clamp(&state->policy);
    state->policy_seeded = 1;
}

void ai_chat_set_policy_default(HWND hwnd, CmdPolicy policy)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    cmd_policy_clamp(&policy);
    d->policy_default = policy;
    /* Apply immediately to the active session if it hasn't been seeded yet
     * (e.g. the panel was just created and this is the first call). A
     * session the user has already touched is left alone. */
    if (d->active_state) {
        ai_chat_seed_policy(d, d->active_state);
        d->approval_q.policy = d->active_state->policy;
        ai_chat_invalidate_status_line(d);
    }
}

void ai_chat_set_context_lines(HWND hwnd, int lines)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    d->context_lines = ai_context_clamp_lines(lines);
}

void ai_chat_set_tool_timeout(HWND hwnd, int seconds)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    /* 0 = never expire; tool_decision_timeout_ms() clamps the rest */
    if (seconds != AI_TOOL_TIMEOUT_SEC_NEVER && seconds < AI_TOOL_TIMEOUT_SEC_MIN)
        seconds = AI_TOOL_TIMEOUT_SEC_MIN;
    if (seconds > AI_TOOL_TIMEOUT_SEC_MAX) seconds = AI_TOOL_TIMEOUT_SEC_MAX;
    d->tool_timeout_sec = seconds;
}

/* Internal helper: perform the actual session switch (save/load/rebuild).
 * Safe to call even while busy — each thread targets its own session. */
static void do_session_switch(AiChatData *d,
                              AiSessionState *new_state,
                              Terminal *term, SessionIo *io,
                              const char *session_notes,
                              const char *system_notes,
                              const char *session_name)
{
    /* A Critical confirmation still up from the old session must not apply
     * to this one (ai_chat_request_policy_change()). */
    d->session_gen++;
    /* Save current conversation to old session state. A busy session's
     * thread works on its own copy, so this is safe mid-reply; when the
     * reply lands, WM_AI_RESPONSE replaces it with the thread's copy. */
    if (d->active_state && d->active_state != new_state) {
        ai_conv_take(&d->active_state->conv, &d->conv);
        d->active_state->valid = 1;
    }

    /* Kill command timers — they belong to the old session. The dispatcher
     * itself is per-panel, not per-session, so switching away just stops
     * it silently (no "[command queue stopped]" -- that's the Stop button's
     * message); ai_chat_dispatch_cancel() marks whatever was left APPROVED in the
     * old session's running batch "not run" on its card, settles and
     * removes just that one batch (so a later ai_chat_dispatch_start() can't
     * resurrect it), and restores the Send button. Every other pending batch in the old session stays
     * in its CmdBatchSet untouched -- ai_chat_rebuild_display() re-creates
     * their cards when the user switches back (rule 6). */
    KillTimer(d->hwnd, TIMER_CMD_QUEUE);
    ai_chat_dispatch_cancel(d, NULL, 0);

    /* Save policy, show-thinking and activity phase to old session */
    if (d->active_state && d->active_state != new_state) {
        d->active_state->policy = d->approval_q.policy;
        d->active_state->policy_seeded = 1;
        d->active_state->show_thinking = d->show_thinking;
        d->active_state->activity_phase = (int)d->activity.phase;
    }

    /* Clear thinking history — it belongs to the old session */
    ai_chat_thinking_history_clear(d);

    /* Load new session's conversation */
    if (new_state && new_state != d->active_state) {
        if (new_state->valid) {
            ai_conv_take(&d->conv, &new_state->conv);
        } else {
            ai_conv_reset(&d->conv);   /* frees, keeps the model */
        }
    }

    /* The snapshot length belongs to the session whose request carried it;
     * zero it when the displayed session changes (the meter then falls back
     * to the conversation alone until the next launch). */
    if (new_state != d->active_state)
        d->last_snapshot_len = 0;
    d->active_state = new_state;
    d->active_term = term;
    d->active_io = io;

    /* Update notes */
    if (session_notes)
        strncpy(d->session_notes, session_notes, sizeof(d->session_notes) - 1);
    else
        d->session_notes[0] = '\0';
    if (system_notes)
        strncpy(d->system_notes, system_notes, sizeof(d->system_notes) - 1);
    else
        d->system_notes[0] = '\0';

    /* Update session name label */
    if (session_name)
        strncpy(d->session_name, session_name, sizeof(d->session_name) - 1);
    else
        d->session_name[0] = '\0';
    if (d->hSessionLabel)
        SetWindowText(d->hSessionLabel, d->session_name);

    /* Reset transient UI state */
    d->indicator_pos = -1;
    d->pending_request[0] = '\0';
    d->stream_phase = 0;

    /* Restore policy, show-thinking and activity phase from new session.
     * A session that has never had its policy set (fresh tab) is seeded
     * from the configured default first. */
    if (new_state) {
        ai_chat_seed_policy(d, new_state);
        d->approval_q.policy = new_state->policy;
        d->show_thinking = new_state->show_thinking;
        chat_activity_set_phase(&d->activity,
                                (ActivityPhase)new_state->activity_phase, 0.0f);
    } else {
        d->approval_q.policy = cmd_policy_default();
        d->show_thinking = 0;
        chat_activity_reset(&d->activity);
    }

    ai_chat_relayout(d);

    /* ai_chat_rebuild_display() replays the conversation and re-creates a
     * card for every batch still in new_state->batches (rule 6) --
     * ai_build_confirm_text()'s old single-snapshot summary line is gone
     * along with it. */
    ai_chat_rebuild_display(d);
    ai_chat_update_panel_state(d);

    /* Pick up the newly displayed session's queue at once rather than
     * after the user's next prompt. Hidden-tab batches are all pending, so
     * they wait for a click; a batch the user had approved on this tab and
     * that the switch away cancelled was removed by ai_chat_dispatch_cancel(), so
     * only batches still holding approved, unrun rows start here. */
    ai_chat_maybe_start_next_batch(d);

    /* If switching to a session that's still streaming,
     * re-append any accumulated content so the user sees progress.
     * Use the per-session stream buffers (populated by WM_AI_STREAM). */
    if (new_state && new_state->busy) {
        /* Copy session stream buffers into display-side buffers */
        d->stream_thinking[0] = '\0';
        d->stream_thinking_len = 0;
        d->stream_content[0] = '\0';
        d->stream_content_len = 0;
        if (new_state->stream_thinking && new_state->stream_thinking_len > 0) {
            size_t tlen = new_state->stream_thinking_len;
            if (tlen >= AI_MSG_MAX) tlen = AI_MSG_MAX - 1;
            memcpy(d->stream_thinking, new_state->stream_thinking, tlen);
            d->stream_thinking_len = tlen;
            d->stream_thinking[tlen] = '\0';
        }
        if (new_state->stream_content && new_state->stream_content_len > 0) {
            size_t clen = new_state->stream_content_len;
            if (clen >= AI_MSG_MAX) clen = AI_MSG_MAX - 1;
            memcpy(d->stream_content, new_state->stream_content, clen);
            d->stream_content_len = clen;
            d->stream_content[clen] = '\0';
        }
        d->stream_phase = new_state->stream_phase;

        /* Create an AI item in the msg_list for the in-progress stream */
        d->stream_ai_item = chat_msg_append(&d->msg_list,
                                             CHAT_ITEM_AI_TEXT,
                                             d->stream_content);
        if (d->stream_ai_item && d->stream_thinking_len > 0)
            chat_msg_set_thinking(d->stream_ai_item,
                                  d->stream_thinking);
    }

    /* Send/Stop follows the displayed session's own stream: Stop must never
     * be offered for another tab's reply, and a busy tab shows it again. */
    if (d->hSendBtn) {
        if (new_state && new_state->busy)
            SetWindowTextW(d->hSendBtn, L"\x25A0"); /* ■ solid square = stop */
        else
            SetWindowText(d->hSendBtn, ">");
        InvalidateRect(d->hSendBtn, NULL, TRUE);
    }

    if (d->hChatList) {
        chat_listview_invalidate(d->hChatList);
        chat_listview_scroll_to_bottom(d->hChatList);
    }
}

void ai_chat_switch_session(HWND hwnd,
                            AiSessionState *new_state,
                            Terminal *term, SessionIo *io,
                            const char *session_notes,
                            const char *system_notes,
                            const char *session_name)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;

    /* Kill any indicator timer — it belongs to the old session's display */
    if (d->indicator_pos >= 0) {
        KillTimer(hwnd, TIMER_THINKING);
        d->indicator_pos = -1;
    }

    /* Switch immediately — any running thread continues in the background
     * targeting its own session directly. */
    do_session_switch(d, new_state, term, io,
                      session_notes, system_notes, session_name);
}

void ai_chat_notify_session_closed(HWND hwnd, AiSessionState *state)
{
    if (!hwnd || !IsWindow(hwnd) || !state) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;

    /* If the dispatcher is running a batch that belongs to this session
     * (it's the one currently displayed), stop it before the session's
     * batch set is freed out from under it -- otherwise the next
     * TIMER_CMD_QUEUE tick would dereference a batch that's about to be
     * freed below. */
    if (d->active_state == state) {
        /* Stream first: ai_chat_cancel_active_stream() ends by starting the
         * next approved batch, which the dispatch cancel below must then
         * stop before the set is freed. */
        if (state->busy) ai_chat_cancel_active_stream(d);
        KillTimer(d->hwnd, TIMER_CMD_QUEUE);
        ai_chat_dispatch_cancel(d, NULL, 0);
    }

    /* Abort and orphan its reply in flight, if any: the thread frees its
     * own stream, and its late messages no longer resolve to this state.
     * Also frees the session's stream buffers and clears busy. */
    ai_chat_abort_session_stream(d, state);

    /* Free every pending batch (the caller frees the AiSessionState itself
     * right after this call returns). */
    cmd_batch_set_free(&state->batches);

    if (d->active_state == state) {
        d->active_state = NULL;
        d->last_snapshot_len = 0;
        d->session_gen++;   /* a confirmation box still up must not apply */
    }
}

void ai_chat_cancel_tool_batches_for(HWND hwnd, AiSessionState *state)
{
    if (!hwnd || !IsWindow(hwnd) || !state) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    ai_chat_cancel_tool_batches(d, state);
}

void ai_chat_apply_demo_extras(HWND hwnd, const char *state,
                               const ApprovalQueue *approval,
                               const ApprovalQueue *approval2)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;

    /* Attach the canned "thinking" block to the "chat" state's reply
     * (index 2: system, user, assistant; "all" builds chat's turns first,
     * so the index lines up there too) -- thinking text lives outside
     * AiConversation, so ui_demo_build() can't set it and a plain rebuild
     * from d->conv alone would leave it off. Every other state also has an
     * assistant message at index 2, but with unrelated content, so this
     * must not fire for those or the thinking block would show up on the
     * wrong reply. */
    if (state && (strcmp(state, "chat") == 0 || strcmp(state, "all") == 0) &&
        d->conv.msg_count > 2 &&
        d->conv.messages[2].role == AI_ROLE_ASSISTANT) {
        free(d->thinking_history[2]);
        d->thinking_history[2] = _strdup(ui_demo_thinking_text());
    }
    /* "thinking": same reply, but with the much longer reasoning block --
     * see ui_demo_thinking_text_long()'s comment for why (the 50-line
     * scrollable Thinking box). */
    if (state && strcmp(state, "thinking") == 0 &&
        d->conv.msg_count > 2 &&
        d->conv.messages[2].role == AI_ROLE_ASSISTANT) {
        free(d->thinking_history[2]);
        d->thinking_history[2] = _strdup(ui_demo_thinking_text_long());
    }

    /* Pending command batches: build real CmdBatch entries (real, unique
     * ids) in the demo session's own batch set from the canned queues --
     * approval for the state's own batch, approval2 for "batches"'s
     * second one -- instead of touching d->approval_q. ai_chat_rebuild_display()
     * below replays every batch still in the set exactly the way a live
     * session switch does (ai_chat_append_batch_command_items()), so this reuses
     * the same code path a real reply's cards come from. */
    int any_executing = 0;
    if (d->active_state) {
        ai_chat_cancel_tool_batches(d, d->active_state);
        cmd_batch_set_free(&d->active_state->batches);
        cmd_batch_set_init(&d->active_state->batches);

        const ApprovalQueue *queues[2] = { approval, approval2 };
        for (int qi = 0; qi < 2; qi++) {
            const ApprovalQueue *src = queues[qi];
            if (!src || src->count == 0) continue;
            CmdBatch *batch = cmd_batch_add(&d->active_state->batches, NULL, NULL);
            if (!batch) continue;
            batch->q = *src;  /* only overwrites q -- batch->id stays */
            for (int i = 0; i < batch->q.count; i++)
                if (batch->q.entries[i].status == APPROVE_EXECUTING)
                    any_executing = 1;
        }
    }

    /* Rebuilds msg_list from d->conv (now including the thinking block
     * just attached, plus any tool_call/tool_result items) and appends a
     * card for every batch just built above. */
    ai_chat_rebuild_display(d);

    /* "cmddetail": the "approval" state with the details popup already open
     * on its longest command (the demo's long, truncated row), so the
     * gallery can capture the popup without a click. Posted, not called:
     * the popup runs its own modal loop and must not start inside this
     * demo setup. */
    if (state && strcmp(state, "cmddetail") == 0) {
        ChatMsgItem *longest = NULL;
        size_t longest_len = 0;
        for (ChatMsgItem *it = d->msg_list.head; it; it = it->next) {
            if (it->type != CHAT_ITEM_COMMAND || !it->u.cmd.command) continue;
            size_t cl = strlen(it->u.cmd.command);
            if (cl > longest_len) { longest = it; longest_len = cl; }
        }
        if (longest)
            PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_CMD_DETAIL, 0),
                        (LPARAM)longest->id);
    }

    /* The demo session has no channel (window.c's create_demo_session
     * passes NULL), so ai_chat_update_panel_state() would otherwise read every
     * demo state as AI_STATE_NO_SESSION. "empty", "nokey" and "nosession"
     * are the only states that actually leave msg_list empty (every other
     * state's replayed turns make the forced state moot -- chat_listview
     * only paints it when the list has zero items), so force the matching
     * AiPanelStateId for those three and leave every other state
     * undecided-but-irrelevant. */
    int forced = -1;
    if (state) {
        if (strcmp(state, "empty") == 0) forced = AI_STATE_EMPTY;
        else if (strcmp(state, "nokey") == 0) forced = AI_STATE_NO_KEY;
        else if (strcmp(state, "nosession") == 0) forced = AI_STATE_NO_SESSION;
    }
    ai_chat_force_state(hwnd, forced);

    /* Gallery: "chat" and "all" show the Thinking disclosure expanded --
     * "chat" because its selection crosses the Thinking body (spec
     * 2026-10-18 section 6) -- and "thinking" shows it collapsed, so the
     * review set still covers both forms per the design spec's "Thought
     * process" section. Setting the item's own flag (rather than
     * d->show_thinking) is the smallest change -- ai_chat_rebuild_display()
     * always creates it collapsed. */
    if (state && (strcmp(state, "all") == 0 || strcmp(state, "chat") == 0)) {
        ChatMsgItem *ti = d->msg_list.head;
        while (ti) {
            if (ti->type == CHAT_ITEM_AI_TEXT && ti->u.ai.thinking_text &&
                ti->u.ai.thinking_text[0]) {
                ti->u.ai.thinking_collapsed = 0;
                ti->dirty = 1;
                break;
            }
            ti = ti->next;
        }
    }

    float now = (float)GetTickCount() / 1000.0f;
    if (any_executing) {
        chat_activity_set_phase(&d->activity, ACTIVITY_EXECUTING, now);
        chat_activity_set_exec(&d->activity, 1, 1);
    }

    /* "error"/"all": a plain AiConversation can show the two user turns
     * that led nowhere, but not *why* -- the HTTP-error [Retry] link and
     * "cancelled" note come from ActivityState, so add them here. */
    if (state && (strcmp(state, "error") == 0 || strcmp(state, "all") == 0)) {
        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
            "Error: HTTP 503 Service Unavailable -- the AI provider could "
            "not complete the request.");
        chat_activity_set_phase(&d->activity, ACTIVITY_WAITING, now);
        chat_activity_connection_lost(&d->activity);
        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                        "Stream cancelled by user.");
    }

    ai_chat_relayout(d);
    if (d->hChatList) {
        /* The integration harness asks the list where words were painted
         * (CLV_MSG_QUERY_WORD) -- under --ui-demo only. */
        chat_listview_enable_query(d->hChatList);
        chat_listview_invalidate(d->hChatList);
        /* ai_chat_rebuild_display() above always leaves the list scrolled to
         * the bottom; ui_demo_scrolls_to_top() (src/core/ui_demo.c) names
         * the states whose canned reply is long enough that this would
         * push the Thinking disclosure off the top of the thread, and
         * pulls it back for those so the gallery review set actually shows
         * the disclosure. See that function's own comment for why
         * "thinking" had to be added to the set on 2026-09-24 (a stale
         * paint, not just a scroll position, was riding on this). */
        if (ui_demo_scrolls_to_top(state))
            chat_listview_scroll_to_top(d->hChatList);
    }

    /* "chat": a character selection from the user bubble, across the
     * Thinking body, into the reply's last line -- the screenshot the
     * maintainer reviews in the four themes (spec 2026-10-18 section 6). */
    if (state && strcmp(state, "chat") == 0 && d->hChatList) {
        ChatMsgItem *u = NULL, *ai = NULL;
        for (ChatMsgItem *it = d->msg_list.head; it; it = it->next) {
            if (!u && it->type == CHAT_ITEM_USER) u = it;
            if (!ai && it->type == CHAT_ITEM_AI_TEXT) ai = it;
        }
        if (u && ai) {
            int uo = 0, ro = 0;
            ui_demo_chat_selection(&uo, &ro);
            ChatSelPos a = { u->id, TR_PART_MAIN, uo };
            ChatSelPos b = { ai->id, TR_PART_MAIN, ro };
            chat_listview_set_selection(d->hChatList, a, b);
        }
    }
}

int ai_chat_copy_selection(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return 0;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d || !d->hChatList) return 0;
    return chat_listview_copy_selection(d->hChatList);
}

void ai_chat_close(HWND hwnd)
{
    if (hwnd && IsWindow(hwnd))
        DestroyWindow(hwnd);
}

/* True once no AiStream is left live process-wide (ai_stream_live_count()
 * only drops once a worker thread has actually released its reference and
 * exited, not merely been asked to cancel). */
static int ai_streams_done(void)
{
    return ai_stream_live_count() <= 0;
}

void ai_chat_wait_for_streams(DWORD timeout_ms)
{
    /* Pumps this thread's inbound sent messages while waiting, instead of
     * blocking in Sleep(): called from window.c's WM_DESTROY, after the
     * panel has orphaned every stream it knew about, to give those worker
     * threads a moment to actually exit before WSACleanup/OpenSSL's exit
     * handlers run. A worker can still be blocked showing a host-key or
     * credential prompt owned by the main window, and Windows delivers
     * that dialog's own housekeeping to its owner via a cross-thread
     * SendMessage -- which only unblocks once this thread services its
     * message queue. A plain Sleep loop here never does that, so it can
     * turn a dismissable prompt into a full-timeout hang on every exit. */
    DWORD start = GetTickCount();
    for (;;) {
        if (ai_streams_done()) return;
        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= timeout_ms) return;
        DWORD slice = timeout_ms - elapsed;
        if (slice > 20) slice = 20;

        MsgWaitForMultipleObjects(0, NULL, FALSE, slice, QS_SENDMESSAGE);

        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE | PM_QS_SENDMESSAGE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

int ai_chat_has_content(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return 0;
    AiChatData *d = (AiChatData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return 0;
    /* At least one user or assistant message beyond the system prompt */
    return d->conv.msg_count > 1;
}

const char *ai_chat_focus_role(HWND panel, HWND focus)
{
    if (!panel || !focus || !IsWindow(panel)) return NULL;
    if (focus != panel && !IsChild(panel, focus)) return NULL;
    AiChatData *d = (AiChatData *)GetWindowLongPtr(panel, GWLP_USERDATA);
    if (d && focus == d->hInput) return "ai_input";
    if (d && focus == d->hChatList) return "ai_list";
    return "ai_panel";
}

#endif /* _WIN32 */
