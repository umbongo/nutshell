/* The send path: the tool registry, the stream's launch, abort and cancel,
 * and the user and continue turns that start a reply. */
#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#endif

#include "ai_chat_internal.h"

#ifdef _WIN32
#include <stdio.h>

/* 1 when the next reply will carry read_debug_log (ai_chat_launch_stream_thread adds
 * it under the same two conditions), so the system prompt may mention it. */
static int debug_tool_active(const AiChatData *d)
{
    return d && dlog_enabled() && ai_provider_supports_tools(d->provider);
}

/* Initialise the tool registry from the configured search/fetch settings. */
void ai_chat_register_tools(AiChatData *d,
                                const char *search_provider,
                                const char *search_url,
                                int max_search_results,
                                int web_fetch_enabled)
{
    ai_tools_init(&d->tool_registry);

    if (search_provider && strcmp(search_provider, "none") != 0) {
        snprintf(d->search_ctx.search_provider,
                 sizeof(d->search_ctx.search_provider),
                 "%s", search_provider);
        snprintf(d->search_ctx.search_url,
                 sizeof(d->search_ctx.search_url),
                 "%s", search_url ? search_url : "");
        d->search_ctx.max_search_results =
            max_search_results > 0 ? max_search_results : 7;

        AiToolDef search_tool;
        memset(&search_tool, 0, sizeof(search_tool));
        snprintf(search_tool.name, sizeof(search_tool.name), "web_search");
        snprintf(search_tool.description, sizeof(search_tool.description),
                 "Search the web for current information. Use this when the user "
                 "asks about recent events, current data, or anything that requires "
                 "up-to-date information beyond your training data.");
        snprintf(search_tool.input_schema_json,
                 sizeof(search_tool.input_schema_json),
                 "{\"type\":\"object\","
                 "\"properties\":{\"query\":{\"type\":\"string\","
                 "\"description\":\"The search query\"}},"
                 "\"required\":[\"query\"]}");
        search_tool.safety = TOOL_SAFE;
        search_tool.execute = tool_web_search_execute;
        search_tool.tool_data = &d->search_ctx;
        ai_tools_register(&d->tool_registry, &search_tool);
    }

    if (web_fetch_enabled) {
        d->fetch_ctx.timeout_ms = 10000;

        AiToolDef fetch_tool;
        memset(&fetch_tool, 0, sizeof(fetch_tool));
        snprintf(fetch_tool.name, sizeof(fetch_tool.name), "web_fetch");
        snprintf(fetch_tool.description, sizeof(fetch_tool.description),
                 "Fetch the contents of a specific web page URL. Use this to retrieve "
                 "detailed information from a URL found via web search or provided by the user.");
        snprintf(fetch_tool.input_schema_json,
                 sizeof(fetch_tool.input_schema_json),
                 "{\"type\":\"object\","
                 "\"properties\":{\"url\":{\"type\":\"string\","
                 "\"description\":\"The URL to fetch\"}},"
                 "\"required\":[\"url\"]}");
        fetch_tool.safety = TOOL_SAFE;
        fetch_tool.execute = tool_web_fetch_execute;
        fetch_tool.tool_data = &d->fetch_ctx;
        ai_tools_register(&d->tool_registry, &fetch_tool);
    }

    /* Reset notification flag whenever tools are reconfigured */
    d->tool_support_notified = 0;
    d->tools_gen++;
}

/* A reply that never started: stop the thinking indicator the send path
 * started, and say why in a status line. */
static void launch_failed(AiChatData *d, const char *why)
{
    KillTimer(d->hwnd, TIMER_THINKING);
    d->indicator_pos = -1;
    chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS, why);
    if (d->hChatList) chat_listview_invalidate(d->hChatList);
    ai_chat_update_context_bar(d);
}

static int ctx_buffers_ensure(AiChatData *d);

/* Build an AiStream from the current AiChatData state -- its own copies of
 * the key, provider, request body, tool registry and conversation -- and
 * launch the background thread on it. Sets active_state->busy = 1. */
void ai_chat_launch_stream_thread(AiChatData *d, const char *src)
{
    if (!d || !d->active_state) return;

    AiStream *s = ai_stream_new();
    if (!s) {
        launch_failed(d, "[out of memory starting the AI request]");
        return;
    }
    s->post_target = (uintptr_t)d->hwnd;
    snprintf(s->src, sizeof(s->src), "%s", src ? src : "user");
    s->panel_visible = (d->hwnd && IsWindowVisible(d->hwnd)) ? 1 : 0;

    strncpy(s->api_key, d->api_key, sizeof(s->api_key) - 1);
    strncpy(s->provider, d->provider, sizeof(s->provider) - 1);
    strncpy(s->custom_url, d->custom_url, sizeof(s->custom_url) - 1);
    int conv_ok = ai_conv_copy(&s->conv, &d->conv) == 0;

    /* One terminal snapshot per launch (spec 2026-11-05 decision 2), taken
     * here on the UI thread and handed to the stream, which only reads it.
     * Every tool-loop rebuild reuses it. A failed allocation sends the
     * request without the block rather than aborting it. */
    d->last_snapshot_len = 0;
    if (d->active_term && ctx_buffers_ensure(d)) {
        size_t n = term_extract_last_n(d->active_term, d->context_lines,
                                       d->ctx_term, d->ctx_term_cap);
        if (n > 0) {
            s->snapshot = (char *)malloc(n + 1);
            if (s->snapshot) {
                memcpy(s->snapshot, d->ctx_term, n);
                s->snapshot[n] = '\0';
                d->last_snapshot_len = n;
            }
        }
    }
    {
        /* A fresh delimiter for every request body. */
        char delim[AI_DELIM_MAX];
        ai_delim_make(crypto_random_bytes, NULL, delim);
        AiBodyFrame frame;
        frame.snapshot = s->snapshot;
        frame.delim = delim;
        int last_msg = d->conv.msg_count - 1;
        const AiAttachment *att = (last_msg >= 0)
            ? d->conv.messages[last_msg].attachment : NULL;

        /* Use tool-aware request builder when tools are registered and provider supports them */
        int has_tools = (d->tool_registry.count > 0) || dlog_enabled();
        int provider_supports = ai_provider_supports_tools(d->provider);

        if (has_tools && provider_supports) {
            /* Snapshot the registry: the thread runs the tools from its own
             * copy, never the panel's (which settings can change, and which
             * dies with the panel). */
            /* read_debug_log joins the snapshot only while debugging is on
             * right now, whatever the panel's own registry holds; its call
             * counter is the stream's copy (one per user message). */
            /* The registry is ~42 KB: on the heap, not the UI thread's stack. */
            AiToolRegistry *launch_reg = (AiToolRegistry *)malloc(sizeof(*launch_reg));
            if (!launch_reg) {
                ai_stream_discard(s);
                launch_failed(d, "[out of memory starting the AI request]");
                return;
            }
            *launch_reg = d->tool_registry;
            DebugLogToolContext debug_ctx;
            ai_debug_tool_ctx_init(&debug_ctx);
            int with_debug = ai_debug_tool_register_if_enabled(launch_reg, &debug_ctx);
            ai_stream_copy_tools(s, launch_reg, d->tools_gen,
                                 &d->search_ctx, &d->fetch_ctx,
                                 with_debug ? &debug_ctx : NULL);
            free(launch_reg);
            /* The stream's own fetch copy reports each hop as a chat row. */
            s->fetch.on_hop = ai_chat_fetch_on_hop;
            s->fetch.user = s;
            s->has_tools = 1;
            /* Serialize tool definitions for this provider */
            s->tools_json[0] = '\0';
            if (strcmp(d->provider, "anthropic") == 0)
                ai_tools_serialize_anthropic(&s->tools, s->tools_json,
                                             sizeof(s->tools_json));
            else
                ai_tools_serialize_openai(&s->tools, s->tools_json,
                                          sizeof(s->tools_json));

            s->body_len = ai_build_request_body_frame(&d->conv, att,
                                                      s->tools_json[0] ? s->tools_json : NULL,
                                                      &frame,
                                                      s->body, AI_BODY_MAX,
                                                      1, s->provider);
        } else {
            s->has_tools = 0;
            s->body_len = ai_build_request_body_frame(&d->conv, att, NULL,
                                                      &frame, s->body,
                                                      AI_BODY_MAX, 1,
                                                      s->provider);
        }
    }

    if (!conv_ok) {
        ai_stream_discard(s);
        launch_failed(d, "[out of memory starting the AI request]");
        return;
    }

    {
        char json_err[256] = "";
        if (s->body_len == 0 ||
            !json_validate(s->body, s->body_len, json_err, sizeof(json_err))) {
            ai_stream_discard(s);
            launch_failed(d, json_err[0] ? json_err : "JSON build failed");
            return;
        }
    }

    /* Register it for this session. Any stream the session still had (a
     * retry) is aborted and orphaned here, so its late messages are stale. */
    if (ai_stream_table_add(&d->streams, s, d->active_state) != 0) {
        ai_stream_discard(s);
        launch_failed(d, "[too many AI replies in progress]");
        return;
    }
    unsigned stream_id = s->id;

    HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, ai_chat_stream_thread_proc, s, 0, NULL);
    if (!hThread) {
        /* No thread: drop its reference for it, then ours. */
        ai_stream_release(s);
        ai_stream_table_finish(&d->streams, stream_id);
        launch_failed(d, "[could not start the AI request thread]");
        return;
    }
    CloseHandle(hThread);
    s = NULL;   /* the thread may free it from here on; only the id is ours */
    SetTimer(d->hwnd, TIMER_STREAM_REAP, STREAM_REAP_MS, NULL);

    /* One-time notification when tools are configured but provider doesn't support them */
    if (d->tool_registry.count > 0 && !ai_provider_supports_tools(d->provider)
        && !d->tool_support_notified) {
        d->tool_support_notified = 1;
        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
            "[Note: web search/fetch tools require Anthropic, OpenAI, or DeepSeek provider]");
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
    }

    /* Allocate per-session stream accumulators */
    free(d->active_state->stream_content);
    d->active_state->stream_content = (char *)calloc(1, AI_MSG_MAX);
    d->active_state->stream_content_len = 0;
    free(d->active_state->stream_thinking);
    d->active_state->stream_thinking = (char *)calloc(1, AI_MSG_MAX);
    d->active_state->stream_thinking_len = 0;
    d->active_state->stream_phase = 0;

    d->active_state->busy = 1;

    /* Start activity monitor */
    {
        float now = (float)GetTickCount() / 1000.0f;
        chat_activity_set_phase(&d->activity, ACTIVITY_PROCESSING, now);
        d->pulse_toggle = 0;
        SetTimer(d->hwnd, TIMER_HEARTBEAT, HEARTBEAT_MS, NULL);
        if (d->hChatList) {
            chat_listview_set_activity(d->hChatList, &d->activity);
            chat_listview_set_pulse(d->hChatList, 0);
        }
    }

    /* Reset display-side stream buffers */
    d->stream_thinking[0] = '\0';
    d->stream_thinking_len = 0;
    d->stream_content[0] = '\0';
    d->stream_content_len = 0;
    d->stream_phase = 0;
    d->last_phase = 0;

    /* Create a placeholder AI item in the ChatMsgList for streaming */
    d->stream_ai_item = chat_msg_append(&d->msg_list, CHAT_ITEM_AI_TEXT,
                                         "");
    if (d->hChatList) {
        chat_listview_invalidate(d->hChatList);
        chat_listview_scroll_to_bottom(d->hChatList);
    }

    /* Switch Send button to Stop while streaming */
    if (d->hSendBtn) {
        SetWindowTextW(d->hSendBtn, L"\x25A0"); /* ■ solid square = stop */
        InvalidateRect(d->hSendBtn, NULL, TRUE);
    }
}

/* Abort and orphan the reply `state` has in flight, and put the session
 * back to idle: its busy flag and its stream accumulators. Touches no
 * panel UI -- see ai_chat_cancel_active_stream() for that. Safe with no stream. */
void ai_chat_abort_session_stream(AiChatData *d, AiSessionState *state)
{
    if (!d || !state) return;
    ai_stream_table_abort_owner(&d->streams, state);
    state->busy = 0;
    free(state->stream_content);
    state->stream_content = NULL;
    state->stream_content_len = 0;
    free(state->stream_thinking);
    state->stream_thinking = NULL;
    state->stream_thinking_len = 0;
    state->stream_phase = 0;
    state->activity_phase = (int)ACTIVITY_IDLE;
}

/* Free the payloads of stream messages still queued for a panel that is
 * being destroyed; the system would drop them unread once the window is
 * gone. Their streams are already orphaned, so nothing else is owed. */
void ai_chat_drain_stream_messages(HWND hwnd)
{
    MSG m;
    while (PeekMessage(&m, hwnd, WM_AI_RESPONSE, WM_AI_TOOL_MSG, PM_REMOVE)) {
        if (m.message == WM_AI_RESPONSE) {
            AiResponseMsg *r = (AiResponseMsg *)m.lParam;
            if (r) { free(r->content); free(r->thinking); free(r); }
        } else if (m.message == WM_AI_STREAM || m.message == WM_AI_TOOL_MSG) {
            AiStreamChunk *c = (AiStreamChunk *)m.lParam;
            if (c) { free(c->delta); free(c); }
        }
    }
}

/* Cancel the active AI stream: signal abort, clear busy, reset UI state.
 * Safe to call even when no stream is active. */
void ai_chat_cancel_active_stream(AiChatData *d)
{
    if (!d) return;

    /* Abort and orphan this session's stream only -- another tab's reply
     * keeps running -- and put the session back to idle. */
    ai_chat_abort_session_stream(d, d->active_state);

    /* Kill timers */
    KillTimer(d->hwnd, TIMER_HEARTBEAT);
    KillTimer(d->hwnd, TIMER_CMD_QUEUE);
    d->dispatch_active = 0;
    chat_activity_reset(&d->activity);
    if (d->hChatList) {
        chat_listview_set_pulse(d->hChatList, 0);
        chat_listview_invalidate(d->hChatList);
    }

    /* Remove the incomplete streaming AI item */
    if (d->stream_ai_item) {
        chat_msg_remove(&d->msg_list, d->stream_ai_item);
        d->stream_ai_item = NULL;
    }

    /* Reset streaming state */
    d->stream_phase = 0;
    d->stream_content[0] = '\0';
    d->stream_content_len = 0;
    d->stream_thinking[0] = '\0';
    d->stream_thinking_len = 0;

    /* Remove the last assistant message from conv if partially added */
    if (d->conv.msg_count > 0 &&
        d->conv.messages[d->conv.msg_count - 1].role == AI_ROLE_ASSISTANT)
        ai_msg_free(&d->conv.messages[--d->conv.msg_count]);

    /* Update UI */
    ai_chat_update_context_bar(d);
    if (d->hChatList) chat_listview_invalidate(d->hChatList);

    /* Restore Send button from Stop */
    if (d->hSendBtn) {
        SetWindowText(d->hSendBtn, ">");
        InvalidateRect(d->hSendBtn, NULL, TRUE);
    }
}

/* Grow the context scratch buffers to fit the configured line budget.
 * Buffers only ever grow, so a shrinking setting keeps the larger block.
 * On allocation failure the previous (smaller) buffer stays usable; only a
 * first-call failure leaves a NULL, which callers must tolerate.
 * Returns non-zero when both buffers are usable. */
static int ctx_buffers_ensure(AiChatData *d)
{
    int cols = (d->active_term && d->active_term->cols > 0)
               ? d->active_term->cols : 80;
    size_t need_term   = ai_context_buf_size(d->context_lines, cols);
    size_t need_prompt = need_term + AI_MSG_MAX;

    if (d->ctx_term_cap < need_term) {
        char *nb = (char *)realloc(d->ctx_term, need_term);
        if (nb) { d->ctx_term = nb; d->ctx_term_cap = need_term; }
    }
    if (d->ctx_prompt_cap < need_prompt) {
        char *nb = (char *)realloc(d->ctx_prompt, need_prompt);
        if (nb) { d->ctx_prompt = nb; d->ctx_prompt_cap = need_prompt; }
    }

    /* Last resort on a failed first allocation: take whatever we can get. */
    if (!d->ctx_term) {
        d->ctx_term = (char *)malloc((size_t)AI_MSG_MAX);
        if (d->ctx_term) d->ctx_term_cap = (size_t)AI_MSG_MAX;
    }
    if (!d->ctx_prompt) {
        d->ctx_prompt = (char *)malloc((size_t)AI_MSG_MAX);
        if (d->ctx_prompt) d->ctx_prompt_cap = (size_t)AI_MSG_MAX;
    }

    return (d->ctx_term && d->ctx_prompt) ? 1 : 0;
}

/* The message box's text as UTF-8 with every CR LF turned into LF (spec
 * 2026-10-18 section 2). Read as UTF-16 rather than with the ANSI
 * GetWindowText, so a user item never holds code-page bytes the selection's
 * UTF-8 boundary rules would misread. Heap (the caller frees it); NULL when
 * memory runs out. */
static char *input_text_utf8(HWND hInput)
{
    int wlen = GetWindowTextLengthW(hInput);
    if (wlen < 0) wlen = 0;
    size_t wbytes = ((size_t)wlen + 1) * sizeof(wchar_t);
    wchar_t *w = (wchar_t *)malloc(wbytes);
    if (!w) return NULL;
    int got = GetWindowTextW(hInput, w, wlen + 1);
    if (got < 0) got = 0;
    if (got > wlen) got = wlen;
    w[got] = L'\0';
    int need = WideCharToMultiByte(CP_UTF8, 0, w, got + 1, NULL, 0, NULL, NULL);
    char *u = need > 0 ? (char *)malloc((size_t)need) : NULL;
    if (u && WideCharToMultiByte(CP_UTF8, 0, w, got + 1, u, need,
                                 NULL, NULL) <= 0)
        u[0] = '\0';
    secure_zero(w, wbytes);
    free(w);
    if (u) text_crlf_to_lf(u);
    return u;
}

void ai_chat_send_user_message(AiChatData *d)
{
    /* Pending command batches: a card never blocks the input (rule 1) --
     * only a streaming reply or an active dispatcher does, same as the
     * IDC_CHAT_SEND handler's Send/Stop toggle already enforces for the
     * primary UI path. */
    if (!d || !d->active_state || d->active_state->busy || d->dispatch_active)
        return;

    /* The user is talking again: the unattended-round count starts over
     * (guard-rails spec 2026-11-04 section 1). */
    ai_rounds_user_message(d->active_state);

    char *input = input_text_utf8(d->hInput);
    if (!input) return;
    /* The text as read: the prefix strip below shortens the string, but the
     * bytes past the new end still hold the original, so the wipes cover
     * this length. */
    const size_t input_len = strlen(input);

    /* Strip any "[Image attached] " prefixes inserted by clipboard paste */
    {
        const char *img_prefix = "[Image attached] ";
        size_t pfx_len = strlen(img_prefix);
        char *text_start = input;
        while (strstr(text_start, img_prefix) == text_start)
            text_start += pfx_len;
        if (text_start != input)
            memmove(input, text_start, strlen(text_start) + 1);
    }

    if (input[0] == '\0') {
        secure_zero(input, input_len);
        free(input);
        return;
    }

    SetWindowText(d->hInput, "");

    /* Display user message in ChatListView */
    chat_msg_append(&d->msg_list, CHAT_ITEM_USER, input);
    if (d->hChatList) {
        chat_listview_invalidate(d->hChatList);
        chat_listview_scroll_to_bottom(d->hChatList);
    }

    /* The system prompt holds settings only (spec 2026-11-05 decision 1): the
     * terminal text is no longer part of it. It is built even with no active
     * terminal, and carries the tools paragraph on every path. */
    int ctx_ok = ctx_buffers_ensure(d);
    if (ctx_ok) {
        char  *sys_prompt     = d->ctx_prompt;
        size_t sys_prompt_cap = d->ctx_prompt_cap;
        ai_build_system_prompt_ex(sys_prompt, sys_prompt_cap,
                                  d->session_notes, d->system_notes,
                                  active_session_kind(d), active_shell_name(d),
                                  debug_tool_active(d),
                                  ai_provider_supports_tools(d->provider)
                                      ? &d->tool_registry : NULL);
        if (d->conv.msg_count == 0)
            ai_conv_add(&d->conv, AI_ROLE_SYSTEM, sys_prompt);
        else
            ai_conv_set_system(&d->conv, sys_prompt);
    }

    ai_conv_add(&d->conv, AI_ROLE_USER, input);

    /* Attach pending image to the user message just added */
    if (d->pending_attachment) {
        int last = d->conv.msg_count - 1;
        if (last >= 0 && d->conv.messages[last].role == AI_ROLE_USER) {
            d->conv.messages[last].attachment =
                ai_attachment_dup(d->pending_attachment);
        }
    }


    ai_chat_update_context_bar(d);

    d->stream_display_start = -1;
    ai_chat_start_indicator(d, "thinking");

    ai_chat_launch_stream_thread(d, "user");

    /* Clear pending attachment after thread is launched */
    ai_attachment_free(&d->pending_attachment);

    secure_zero(input, input_len);
    free(input);
}

/* Send a follow-up user turn after a batch finishes running and launch a
 * fresh stream for it. msg_text is the continue message body (built by
 * the caller via ai_build_continue_text() -- rule 4). `unattended` is 1 when
 * the batch that just finished ran without the user acting on it by hand
 * (CmdBatch.user_acted == 0): it counts towards the Auto-approve round cap,
 * while an attended batch resets the count (ai_rounds_continue()). */
void ai_chat_send_continue_message(AiChatData *d, const char *msg_text,
                                  int unattended)
{
    if (!d || !d->active_state || d->active_state->busy) return;
    if (!msg_text || !msg_text[0]) return;

    ai_rounds_continue(d->active_state, unattended);

    /* Refresh the system prompt (settings and tools only; the terminal text
     * is added to the last user turn when the request body is built). */
    if (ctx_buffers_ensure(d) && d->conv.msg_count > 0) {
        char  *sys_prompt     = d->ctx_prompt;
        size_t sys_prompt_cap = d->ctx_prompt_cap;
        ai_build_system_prompt_ex(sys_prompt, sys_prompt_cap,
                                  d->session_notes, d->system_notes,
                                  active_session_kind(d), active_shell_name(d),
                                  debug_tool_active(d),
                                  ai_provider_supports_tools(d->provider)
                                      ? &d->tool_registry : NULL);
        ai_conv_set_system(&d->conv, sys_prompt);
    }

    ai_conv_add(&d->conv, AI_ROLE_USER, msg_text);


    ai_chat_start_indicator(d, "continuing");

    ai_chat_launch_stream_thread(d, "continue");
}

#endif /* _WIN32 */
