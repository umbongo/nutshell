/* The SSE stream: line parser, the posting helpers and the background
 * thread that talks to the provider and posts the results to the panel. */
#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#endif

#include "ai_chat_internal.h"
#include "json_parser.h"

#ifdef _WIN32
#include <stdio.h>

/* Context for SSE streaming callback */
typedef struct {
    AiStream *stream;            /* owns the abort flag and the post target */
    char line_buf[8192];     /* SSE line accumulation buffer */
    size_t line_len;
    char full_content[AI_MSG_MAX];   /* accumulated full content */
    size_t content_len;
    char full_thinking[AI_MSG_MAX];  /* accumulated full thinking */
    size_t thinking_len;
    int in_thinking;         /* 1 while receiving thinking chunks */
    int header_sent;         /* bitmask: 1=thinking header, 2=content header */
    /* Tool streaming state (NULL when no tools registered) */
    AiToolStreamState *tool_stream;
    char provider[64];
    int tool_stop;          /* set to 1 when tool_use stop detected */
    int cut_off;            /* 1 when the provider ended the reply at its output limit */
    int saw_end;            /* 1 once the reply's end arrived ([DONE] or a finish_reason) */
    int stream_error;       /* 1 when an in-stream {"error":...} event arrived */
    char error_msg[AI_STREAM_ERROR_MSG_MAX + 1];   /* its sanitised message */
    char finish[32];        /* finish_reason seen by a stream without tool state */
    int input_tokens;        /* actual input token count from API (0 until received) */
    int output_tokens;       /* actual output token count from API (0 until received) */
} StreamContext;

/* Post a text payload tagged with the stream's id. If the panel is gone the
 * post fails and the payload is freed here, so nothing leaks. */
static void stream_post_text(AiStream *s, UINT msg, WPARAM wp, const char *text)
{
    if (ai_stream_aborted(s)) return;   /* nobody is listening any more */
    AiStreamChunk *chunk = (AiStreamChunk *)calloc(1, sizeof(*chunk));
    if (!chunk) return;
    chunk->stream_id = s->id;
    chunk->delta = _strdup(text ? text : "");
    if (!chunk->delta ||
        !PostMessage((HWND)s->post_target, msg, wp, (LPARAM)chunk)) {
        free(chunk->delta);
        free(chunk);
    }
}

/* Post one AiToolDecideMsg to the panel. Returns 1 when the UI owns it, 0
 * when the allocation or the post failed (nothing leaks). */
static int stream_post_tool_decide(AiStream *s, unsigned gen, int loop_iter,
                                   int kind, ToolSafety safety,
                                   const AiToolCall *call)
{
    AiToolDecideMsg *m = (AiToolDecideMsg *)calloc(1, sizeof(*m));
    if (!m) return 0;
    m->stream_id = s->id;
    m->gen = gen;
    m->loop_iter = loop_iter;
    m->kind = kind;
    m->safety = safety;
    /* -1 (a web_fetch without a url) still fills a row the user can read. */
    /* The Details cap is CMD_NOTE_MAX, the size of the approval entry's note
     * it is copied into: ai_tool_card_text cuts on an escape boundary, and
     * chat_approval_add_tool then copies it whole (its own cut knows only
     * UTF-8 and would split a \UXXXXXXXX escape). */
    _Static_assert(CMD_NOTE_MAX <= sizeof(((AiToolDecideMsg *)0)->detail),
                   "the note cap must fit the message's detail buffer");
    (void)ai_tool_card_text(call, m->row, sizeof(m->row),
                            m->detail, CMD_NOTE_MAX);
    if (!PostMessage((HWND)s->post_target, WM_AI_TOOL_DECIDE, 0, (LPARAM)m)) {
        free(m);
        return 0;
    }
    return 1;
}

/* Ask the user about one non-SAFE tool call and wait for the answer
 * (spec decisions 15, 16, 20). Publishes before any abort check so the first
 * poll sees an abort that was already set; polls the slot every
 * AI_TOOL_DECIDE_POLL_MS (no Win32 event) until the UI answers, the stream is
 * aborted or the stream's tool_timeout_ms (copied at launch) passes; a
 * tool_timeout_ms of 0 means never, so only an answer or an abort ends the
 * wait. Returns the settled outcome, never PENDING. */
static ToolDecision stream_ask_tool(AiStream *s, const AiToolCall *call,
                                    ToolSafety safety, int loop_iter)
{
    const unsigned gen = tool_decision_publish(&s->decision);
    if (gen == 0) return TOOL_DECISION_NOT_APPROVED;

    if (!stream_post_tool_decide(s, gen, loop_iter, AI_TOOL_DECIDE_ASK,
                                 safety, call))
        (void)tool_decision_answer(&s->decision, gen,
                                   TOOL_DECISION_NOT_APPROVED);

    const ULONGLONG t0 = GetTickCount64();
    ToolDecision d;
    for (;;) {
        d = tool_decision_poll(&s->decision, gen, ai_stream_aborted(s),
                               (unsigned long long)(GetTickCount64() - t0),
                               s->tool_timeout_ms);
        if (d != TOOL_DECISION_PENDING) break;
        Sleep(AI_TOOL_DECIDE_POLL_MS);
    }

    if (d == TOOL_DECISION_EXPIRED)   /* a failed post here just frees it */
        (void)stream_post_tool_decide(s, gen, loop_iter,
                                      AI_TOOL_DECIDE_EXPIRED, safety, call);
    /* Tool name, stream and outcome only: never the URL. */
    DLOG(DLOG_AI, DLOG_INFO, 0, "tool.decide", "stream=%u gen=%u tool=%s outcome=%d",
         s->id, gen, call->name, (int)d);
    return d;
}

/* Pin an approved web_fetch to the host the user saw (spec decision 23).
 * An unparseable url stays unpinned: the fetch refuses it itself. */
static void stream_pin_fetch_host(AiStream *s, const AiToolCall *call)
{
    s->fetch.pinned_host[0] = '\0';
    JsonNode *input = json_parse(call->input_json);
    if (!input) return;
    const char *url = json_obj_str(input, "url");
    NetUrlParts parts;
    if (url && net_url_parse(url, &parts) == 0)
        snprintf(s->fetch.pinned_host, sizeof(s->fetch.pinned_host),
                 "%s", parts.host);
    json_free(input);
}

/* Process a single SSE line from the stream */
static void stream_process_line(StreamContext *ctx, const char *line, size_t len)
{
    /* Skip empty lines */
    if (len == 0) return;

    /* SSE lines start with "data: " */
    if (len < 6 || strncmp(line, "data: ", 6) != 0) return;

    /* An error event already ended the stream; nothing after it counts. */
    if (ctx->stream_error) return;

    const char *json = line + 6;
    if (strncmp(json, "[DONE]", 6) == 0) ctx->saw_end = 1;
    /* An in-stream error event ({"error":{"message":...}}): the stream ends
     * and the reply says why, instead of finishing silently. */
    if (strstr(json, "\"error\"") &&
        ai_stream_chunk_error(ctx->stream->provider, json,
                              ctx->error_msg, sizeof(ctx->error_msg))) {
        ctx->stream_error = 1;
        return;
    }
    /* Only a chunk that carries the word can be a "length" finish: skip the
     * extra JSON parse for every other chunk of the stream. */
    if (strstr(json, "\"length\"") && ai_stream_chunk_cut_off(json))
        ctx->cut_off = 1;
    char content_delta[1024] = "";
    char thinking_delta[1024] = "";
    int chunk_input_tokens = 0, chunk_output_tokens = 0;

    int rc;
    if (ctx->tool_stream) {
        rc = ai_parse_stream_chunk_ex(json,
                                     content_delta, sizeof(content_delta),
                                     thinking_delta, sizeof(thinking_delta),
                                     ctx->tool_stream, ctx->provider);
        /* Tool-use stop: noted for the round's log line. The chunk's text
         * is still read below: it can ride with the finish_reason. */
        if (rc == 2) ctx->tool_stop = 1;
    } else {
        rc = ai_parse_stream_chunk(json, content_delta, sizeof(content_delta),
                                   thinking_delta, sizeof(thinking_delta),
                                   &chunk_input_tokens, &chunk_output_tokens);
        /* Remember the finish_reason for the round's log line. */
        if (strstr(json, "\"finish_reason\":\"") ||
            strstr(json, "\"finish_reason\": \"")) {
            JsonNode *fr_root = json_parse(json);
            if (fr_root) {
                JsonNode *chs = json_obj_get(fr_root, "choices");
                if (chs && chs->type == JSON_ARRAY && vec_size(&chs->as.arr) > 0) {
                    JsonNode *c0 = (JsonNode *)vec_get(&chs->as.arr, 0);
                    const char *fr = (c0 && c0->type == JSON_OBJECT)
                                   ? json_obj_str(c0, "finish_reason") : NULL;
                    if (fr) snprintf(ctx->finish, sizeof(ctx->finish), "%s", fr);
                }
                json_free(fr_root);
            }
        }
    }

    /* Accumulate actual token counts when provided by the API */
    if (chunk_input_tokens > 0)
        ctx->input_tokens += chunk_input_tokens;
    if (chunk_output_tokens > 0)
        ctx->output_tokens += chunk_output_tokens;

    /* The reply's end has arrived: a read failure after this is harmless. */
    if (rc == 1 || rc == 2 || ctx->finish[0] ||
        (ctx->tool_stream && ctx->tool_stream->finish[0]))
        ctx->saw_end = 1;

    /* rc == 1 is [DONE] (no text) or a "stop" finish, whose own text is in
     * content_delta and is posted below with every other delta. */
    if (rc < 0) return;

    /* Post thinking delta to UI */
    if (thinking_delta[0]) {
        /* Accumulate */
        size_t dlen = strlen(thinking_delta);
        if (ctx->thinking_len + dlen < AI_MSG_MAX - 1) {
            memcpy(ctx->full_thinking + ctx->thinking_len, thinking_delta, dlen);
            ctx->thinking_len += dlen;
            ctx->full_thinking[ctx->thinking_len] = '\0';
        }
        /* Post to UI for realtime display */
        stream_post_text(ctx->stream, WM_AI_STREAM, 0, thinking_delta);
    }

    /* Post content delta to UI */
    if (content_delta[0]) {
        /* Accumulate */
        size_t dlen = strlen(content_delta);
        if (ctx->content_len + dlen < AI_MSG_MAX - 1) {
            memcpy(ctx->full_content + ctx->content_len, content_delta, dlen);
            ctx->content_len += dlen;
            ctx->full_content[ctx->content_len] = '\0';
        }
        /* Post to UI for realtime display */
        stream_post_text(ctx->stream, WM_AI_STREAM, 1, content_delta);
    }
}

/* SSE stream callback — accumulates lines and processes them */
static int stream_callback(const char *data, size_t len, void *userdata)
{
    StreamContext *ctx = (StreamContext *)userdata;

    /* Stop at once if this stream was aborted (Stop, New Chat, tab closed,
     * panel closed or undocked, app exit). */
    if (ai_stream_aborted(ctx->stream))
        return 1; /* abort stream */
    if (ctx->stream_error)
        return 1; /* an error event ended the stream */

    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n') {
            /* Strip trailing \r */
            if (ctx->line_len > 0 && ctx->line_buf[ctx->line_len - 1] == '\r')
                ctx->line_len--;
            ctx->line_buf[ctx->line_len] = '\0';
            stream_process_line(ctx, ctx->line_buf, ctx->line_len);
            ctx->line_len = 0;
            if (ctx->stream_error)
                return 1;   /* stop at once: nothing after the error counts */
        } else {
            if (ctx->line_len < sizeof(ctx->line_buf) - 1)
                ctx->line_buf[ctx->line_len++] = c;
        }
    }
    return 0;
}

/* Post a WM_AI_RESPONSE. The handler clears the session's busy flag -- if
 * this stream is still the session's current one. */
static void post_response(AiStream *s, WPARAM kind, const char *content,
                          const char *thinking, int in_tok, int out_tok)
{
    AiResponseMsg *rmsg = (AiResponseMsg *)calloc(1, sizeof(*rmsg));
    if (!rmsg) return;
    rmsg->stream_id = s->id;
    rmsg->content = _strdup(content ? content : "");
    rmsg->thinking = (thinking && thinking[0]) ? _strdup(thinking) : NULL;
    rmsg->input_tokens = in_tok;
    rmsg->output_tokens = out_tok;
    if (!PostMessage((HWND)s->post_target, WM_AI_RESPONSE, kind, (LPARAM)rmsg)) {
        free(rmsg->content);
        free(rmsg->thinking);
        free(rmsg);
        return;
    }
    ai_stream_note_delivered(s);
}

static void post_error_response(AiStream *s, const char *msg)
{
    post_response(s, 0, msg, NULL, 0, 0);
}

/* Post a tool-related status message to the UI thread.
 * type is CHAT_ITEM_TOOL_CALL, CHAT_ITEM_TOOL_RESULT or CHAT_ITEM_STATUS. */
static void post_tool_msg(AiStream *s, ChatItemType type, const char *text)
{
    stream_post_text(s, WM_AI_TOOL_MSG, (WPARAM)type, text);
}

/* web_fetch progress (WebFetchContext.on_hop): runs on the stream thread, so it
 * only posts a row (stream_post_text copies the text; display_url is the fetch
 * loop's stack buffer). user is the stream, which outlives the tool call. */
void ai_chat_fetch_on_hop(void *user, const char *display_url, int refused)
{
    AiStream *s = (AiStream *)user;
    char row[256];
    if (!s) return;
    if (refused)
        snprintf(row, sizeof(row), "fetch refused: %s (local or private address)",
                 display_url ? display_url : "");
    else
        snprintf(row, sizeof(row), "fetching: %s",
                 display_url ? display_url : "");
    post_tool_msg(s, CHAT_ITEM_TOOL_CALL, row);
}

/* Background thread: streaming AI API call.
 *
 * Owns one reference to its AiStream and reads and writes nothing else: no
 * AiChatData, no session. It drops its reference on every way out; if the
 * panel orphaned the stream meanwhile, that frees it. When s->has_tools,
 * runs the agentic tool-use loop on the stream's own conversation. */
unsigned __stdcall ai_chat_stream_thread_proc(void *raw_arg)
{
    AiStream *s = (AiStream *)raw_arg;
    dlog_set_thread_tag("ai");

    /* Debug log (spec section 2): the provider and model by name, never the
     * request, the reply or a tool's arguments or result. */
    const ULONGLONG t_stream = GetTickCount64();
    const unsigned stream_id = s->id;
    int stream_failed = 0;
    DLOG(DLOG_AI, DLOG_INFO, 0, "stream.start",
         "stream=%u provider=%s model=%s tools=%d src=%s panel_visible=%d",
         stream_id, s->provider, s->conv.model, s->has_tools ? s->tools.count : 0,
         s->src[0] ? s->src : "user", s->panel_visible);

    const char *url = ai_provider_url(s->provider);
    if (!url && strcmp(s->provider, "custom") == 0 && s->custom_url[0])
        url = s->custom_url;
    if (!url) {
        DLOG(DLOG_AI, DLOG_WARN, 0, "stream.error", "stream=%u class=unknown_provider ms=%llu",
             stream_id, (unsigned long long)(GetTickCount64() - t_stream));
        post_error_response(s, "Error: unknown AI provider");
        ai_stream_mark_finished(s);
        ai_stream_release(s);
        return 0;
    }

    char hdr0[300], hdr1[64];
    const char *req_headers[3];
    ai_build_auth_headers(s->provider, s->api_key,
                          hdr0, sizeof(hdr0), hdr1, sizeof(hdr1),
                          req_headers);

    /* Agentic state (only used when tools are enabled) */
    AgenticState agentic;
    agentic_state_reset(&agentic);

    /* Tool streaming state (heap-allocated so we can reset between loops) */
    AiToolStreamState tool_stream_state;
    AiToolStreamState *tool_stream_ptr = NULL;
    if (s->has_tools) {
        ai_tools_stream_init(&tool_stream_state);
        tool_stream_ptr = &tool_stream_state;
    }

    /* Agentic loop — runs once for non-tool responses, multiple times with tools */
    char final_content[AI_MSG_MAX] = "";
    char final_thinking[AI_MSG_MAX] = "";
    int total_input_tokens = 0, total_output_tokens = 0;
    int loop_done = 0;

    while (!loop_done) {
        StreamContext ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.stream = s;
        ctx.tool_stream = tool_stream_ptr;
        if (tool_stream_ptr)
            strncpy(ctx.provider, s->provider, sizeof(ctx.provider) - 1);

        /* Reset tool stream state for this loop iteration */
        if (tool_stream_ptr)
            ai_tools_stream_reset(tool_stream_ptr);

        int status = 0;
        char errbuf[256] = "";
        int rc = ai_http_post_stream(url, req_headers, s->body, s->body_len,
                                     stream_callback, &ctx,
                                     &status, errbuf, sizeof(errbuf));

        /* If stream was aborted (user cancelled), discard partial results */
        if (ai_stream_aborted(s))
            goto out;

        /* An error event inside the stream (HTTP 200, then {"error":...}). */
        if (ctx.stream_error) {
            char msg[1024];
            snprintf(msg, sizeof(msg), "Provider error: %s", ctx.error_msg);
            stream_failed = 1;
            /* The class only: the message stays out of the log. */
            DLOG(DLOG_AI, DLOG_WARN, 0, "stream.error",
                 "stream=%u class=provider status=%d ms=%llu", stream_id,
                 status, (unsigned long long)(GetTickCount64() - t_stream));
            post_error_response(s, msg);
            goto out;
        }

        /* A read failure after the reply's end ([DONE] or a finish_reason)
         * lost nothing: the round arrived whole, so it ends cleanly. */
        int lost_after_end = rc != 0 && status >= 200 && status < 300 &&
                             ctx.saw_end;
        if (lost_after_end)
            DLOG(DLOG_AI, DLOG_INFO, 0, "stream.read_failed_after_end",
                 "stream=%u iter=%d status=%d", stream_id, agentic.loop_iter,
                 status);

        if ((rc != 0 && !lost_after_end) || status < 200 || status >= 300) {
            char msg[1024];
            if (rc != 0 && status >= 200 && status < 300 && errbuf[0])
                /* The body broke after a 200: the text says so itself. */
                snprintf(msg, sizeof(msg), "%s", errbuf);
            else if (errbuf[0])
                snprintf(msg, sizeof(msg), "HTTP %d: %s", status, errbuf);
            else
                snprintf(msg, sizeof(msg), "HTTP %d: streaming request failed", status);
            stream_failed = 1;
            DLOG(DLOG_AI, DLOG_WARN, 0, "stream.error",
                 "stream=%u class=%s status=%d ms=%llu", stream_id,
                 (rc != 0 && status >= 200 && status < 300) ? "connection_lost"
                     : rc != 0 ? "transport" : "http", status,
                 (unsigned long long)(GetTickCount64() - t_stream));
            post_error_response(s, msg);
            goto out;
        }

        /* Accumulate final content/thinking across all iterations */
        if (ctx.content_len > 0) {
            size_t existing = strlen(final_content);
            size_t space = sizeof(final_content) - existing - 1;
            size_t copy_n = ctx.content_len < space ? ctx.content_len : space;
            memcpy(final_content + existing, ctx.full_content, copy_n);
            final_content[existing + copy_n] = '\0';
        }
        if (ctx.thinking_len > 0 && final_thinking[0] == '\0') {
            /* Only capture first thinking block */
            size_t copy_n = ctx.thinking_len < sizeof(final_thinking) - 1
                          ? ctx.thinking_len : sizeof(final_thinking) - 1;
            memcpy(final_thinking, ctx.full_thinking, copy_n);
            final_thinking[copy_n] = '\0';
        }

        /* Slots a provider's indices skipped (no name, no id) are no calls:
         * drop them so they are not run as "Unknown tool". */
        int dropped_slots = tool_stream_ptr
            ? ai_tools_stream_compact(tool_stream_ptr) : 0;
        if (dropped_slots > 0)
            DLOG(DLOG_AI, DLOG_WARN, 0, "stream.tool_slots_dropped",
                 "stream=%u iter=%d empty_slots=%d", stream_id,
                 agentic.loop_iter, dropped_slots);
        int pending_n = tool_stream_ptr ? tool_stream_ptr->pending_tool_count : 0;

        /* finish_reason "length": the reply stops mid-sentence (or mid
         * [EXEC], so no command card): say so instead of leaving it a mystery.
         * A call cut off with it is not run: say that too. */
        if (ctx.cut_off) {
            DLOG(DLOG_AI, DLOG_WARN, 0, "stream.cut_off",
                 "stream=%u finish_reason=length", stream_id);
            post_tool_msg(s, CHAT_ITEM_STATUS,
                          pending_n > 0
                          ? "reply cut off at the provider's output limit; a tool call in it was not run"
                          : "reply cut off at the provider's output limit");
        }

        /* Accumulate token counts (last non-zero value wins) */
        if (ctx.input_tokens > 0)
            total_input_tokens = ctx.input_tokens;
        if (ctx.output_tokens > 0)
            total_output_tokens = ctx.output_tokens;

        /* The round's outcome in one line: what the provider said it
         * finished with, and whether the tool loop runs. tool_stop is the
         * raw "finish says tool_calls/tool_use" flag; run is the decision.
         * Counts and names only: no URL, no arguments, no text. */
        const char *finish_seen = tool_stream_ptr ? tool_stream_ptr->finish
                                                  : ctx.finish;
        int run_tools = tool_stream_ptr &&
            ai_tool_round_should_run(s->provider, ctx.tool_stop, pending_n,
                                     ctx.cut_off);
        DLOG(DLOG_AI, DLOG_INFO, 0, "stream.round",
             "stream=%u iter=%d finish=%s tool_stop=%d pending=%d run=%d",
             stream_id, agentic.loop_iter,
             finish_seen[0] ? finish_seen : "none", ctx.tool_stop, pending_n,
             run_tools);
        if (tool_stream_ptr && tool_stream_ptr->index_defaulted > 0)
            DLOG(DLOG_AI, DLOG_WARN, 0, "stream.tool_index_missing",
                 "stream=%u iter=%d deltas=%d (read as index 0)", stream_id,
                 agentic.loop_iter, tool_stream_ptr->index_defaulted);

        /* ---- Tool-use branch: whenever calls are pending, whatever the
         * finish_reason said (spec: Kimi trace, Task 17) ---- */
        if (run_tools) {

            /* Add assistant message with tool_use blocks */
            /* ctx.full_thinking is this turn's reasoning; the builder sends it
             * back with the tool calls for provider "moonshot" only. */
            if (agentic_add_assistant_tool_msg_ex(&s->conv,
                                                  ctx.full_content,
                                                  ctx.full_thinking,
                                                  tool_stream_ptr->pending_tool_calls,
                                                  tool_stream_ptr->pending_tool_count) != 0) {
                /* Results must never go out without their tool-call turn. */
                stream_failed = 1;
                DLOG(DLOG_AI, DLOG_WARN, 0, "stream.error", "stream=%u class=oom ms=%llu",
                     stream_id, (unsigned long long)(GetTickCount64() - t_stream));
                post_tool_msg(s, CHAT_ITEM_STATUS, "[tool loop stopped: out of memory]");
                post_error_response(s, "Error: out of memory for the tool call");
                goto out;
            }

            /* Execute each tool call */
            AiToolResult *results = (AiToolResult *)calloc(
                (size_t)tool_stream_ptr->pending_tool_count, sizeof(AiToolResult));
            if (!results) {
                stream_failed = 1;
                DLOG(DLOG_AI, DLOG_WARN, 0, "stream.error", "stream=%u class=oom ms=%llu",
                     stream_id, (unsigned long long)(GetTickCount64() - t_stream));
                post_error_response(s, "Error: out of memory for tool results");
                goto out;
            }

            for (int ti = 0; ti < tool_stream_ptr->pending_tool_count; ti++) {
                AiToolCall *call = &tool_stream_ptr->pending_tool_calls[ti];

                /* A web_fetch call while Permit Web Fetch is off: no card and
                 * no "using tool" row. A status row says why, and the model
                 * gets Nutshell's note (not an error) so it can tell the
                 * user. Nothing is counted against the rate limit. */
                if (tool_dispatch_rule(call->name,
                        ai_tools_find(&s->tools, call->name) != NULL) ==
                    TOOL_DISPATCH_FETCH_OFF) {
                    results[ti].tool_use_id[0] = '\0';
                    strncpy(results[ti].tool_use_id, call->id,
                            sizeof(results[ti].tool_use_id) - 1);
                    snprintf(results[ti].tool_note,
                             sizeof(results[ti].tool_note), "%s",
                             tool_fetch_off_note());
                    results[ti].content = NULL;
                    results[ti].is_error = 0;
                    DLOG(DLOG_AI, DLOG_INFO, 0, "tool.call",
                         "stream=%u tool=%s ok=0 fetch_off=1",
                         stream_id, call->name);
                    post_tool_msg(s, CHAT_ITEM_STATUS, tool_fetch_off_row());
                    continue;
                }

                /* Rate limit check */
                char rate_err[256] = "";
                if (!agentic_check_rate_limit(&agentic, call->name,
                                              rate_err, sizeof(rate_err))) {
                    results[ti].tool_use_id[0] = '\0';
                    strncpy(results[ti].tool_use_id, call->id,
                            sizeof(results[ti].tool_use_id) - 1);
                    /* Nutshell's own text: a note, no data block. */
                    snprintf(results[ti].tool_note,
                             sizeof(results[ti].tool_note), "%s", rate_err);
                    results[ti].is_error = 1;
                    DLOG(DLOG_AI, DLOG_INFO, 0, "tool.call", "stream=%u tool=%s ok=0 rate_limited=1",
                         stream_id, call->name);
                    post_tool_msg(s, CHAT_ITEM_TOOL_CALL, "[tool rate-limited]");
                    continue;
                }

                /* Notify UI of tool call (include provider for web_search) */
                char tool_call_text[256];
                const AiToolDef *tdef = ai_tools_find(&s->tools, call->name);
                const char *provider_suffix = NULL;
                if (tdef && tdef->tool_data &&
                    strcmp(call->name, "web_search") == 0) {
                    const WebSearchContext *wsc =
                        (const WebSearchContext *)tdef->tool_data;
                    if (wsc->search_provider[0])
                        provider_suffix = wsc->search_provider;
                }
                /* read_debug_log: the query in words, not just the name. */
                char debug_desc[160];
                if (!provider_suffix && strcmp(call->name, DLOG_TOOL_NAME) == 0) {
                    (void)ai_debug_tool_describe(call->input_json, debug_desc,
                                                 sizeof(debug_desc));
                    if (debug_desc[0]) provider_suffix = debug_desc;
                }
                /* web_fetch: the requested URL (display form). */
                char fetch_desc[160];
                if (!provider_suffix && strcmp(call->name, "web_fetch") == 0) {
                    (void)web_fetch_describe(call->input_json, fetch_desc,
                                             sizeof(fetch_desc));
                    if (fetch_desc[0]) provider_suffix = fetch_desc;
                }
                if (provider_suffix)
                    snprintf(tool_call_text, sizeof(tool_call_text),
                             "using tool: %s - %s", call->name,
                             provider_suffix);
                else
                    snprintf(tool_call_text, sizeof(tool_call_text),
                             "using tool: %s", call->name);
                /* A SAFE call is announced at once; any other waits for the
                 * user's say and is announced only when it actually runs. */
                const int asks = tdef && tdef->safety != TOOL_SAFE;
                if (!asks)
                    post_tool_msg(s, CHAT_ITEM_TOOL_CALL, tool_call_text);

                int pinned = 0;
                if (asks) {
                    const ToolDecision dec = stream_ask_tool(
                        s, call, tdef->safety, agentic.loop_iter);
                    if (dec != TOOL_DECISION_RUN &&
                        dec != TOOL_DECISION_APPROVED) {
                        /* Not approved: Nutshell's note, no tool run, and
                         * not an error (spec decision 16). */
                        const char *note = tool_decision_note(dec);
                        results[ti].tool_use_id[0] = '\0';
                        strncpy(results[ti].tool_use_id, call->id,
                                sizeof(results[ti].tool_use_id) - 1);
                        snprintf(results[ti].tool_note,
                                 sizeof(results[ti].tool_note), "%s",
                                 note ? note : "");
                        results[ti].content = NULL;
                        results[ti].is_error = 0;
                        continue;
                    }
                    post_tool_msg(s, CHAT_ITEM_TOOL_CALL, tool_call_text);
                    if (dec == TOOL_DECISION_APPROVED &&
                        strcmp(call->name, "web_fetch") == 0) {
                        stream_pin_fetch_host(s, call);
                        pinned = 1;
                    }
                }

                /* Execute (the stream's own registry snapshot and abort flag) */
                const ULONGLONG t_tool = GetTickCount64();
                ai_tool_execute(&s->tools, call, ai_stream_abort_flag(s),
                                &results[ti]);
                if (pinned) s->fetch.pinned_host[0] = '\0';
                agentic_record_tool_call(&agentic, call->name);
                /* The tool's name and outcome only: never its arguments or result. */
                DLOG(DLOG_AI, DLOG_INFO, 0, "tool.call", "stream=%u tool=%s ok=%d ms=%llu",
                     stream_id, call->name, results[ti].is_error ? 0 : 1,
                     (unsigned long long)(GetTickCount64() - t_tool));

                /* Truncation warning */
                if (results[ti].was_truncated) {
                    char warn[256];
                    agentic_truncation_warning(call->name, warn, sizeof(warn));
                    post_tool_msg(s, CHAT_ITEM_STATUS, warn);
                }

                /* Show brief result in UI (skip error results) */
                if (results[ti].content && results[ti].content_len > 0
                    && !results[ti].is_error) {
                    char preview[256];
                    if (strcmp(call->name, DLOG_TOOL_NAME) == 0) {
                        /* The header line only: the log lines are for the
                         * model, not for the chat. */
                        ai_debug_tool_summary_line(results[ti].content,
                                                   results[ti].content_len,
                                                   preview, 200);
                    } else {
                        size_t plen = results[ti].content_len < 200
                                    ? results[ti].content_len : 200;
                        memcpy(preview, results[ti].content, plen);
                        preview[plen] = '\0';
                    }
                    post_tool_msg(s, CHAT_ITEM_TOOL_RESULT, preview);
                }
            }

            /* Add tool results to conversation */
            agentic_add_tool_results(&s->conv,
                                     tool_stream_ptr->pending_tool_calls,
                                     results,
                                     tool_stream_ptr->pending_tool_count);

            /* Free tool results */
            for (int ti = 0; ti < tool_stream_ptr->pending_tool_count; ti++)
                free(results[ti].content);
            free(results);

            ai_tools_stream_reset(tool_stream_ptr);
            agentic.loop_iter++;

            if (ai_stream_aborted(s))
                goto out;

            /* Check if we can continue */
            if (!agentic_can_continue(&agentic)) {
                post_tool_msg(s, CHAT_ITEM_STATUS,
                    "[tool loop limit reached — stopping]");
                loop_done = 1;
            } else {
                /* Rebuild request body with updated conversation (including tool results) */
                int last_msg = s->conv.msg_count - 1;
                const AiAttachment *att2 = (last_msg >= 0)
                    ? s->conv.messages[last_msg].attachment : NULL;
                /* A fresh delimiter for this request, the launch's snapshot. */
                char delim2[AI_DELIM_MAX];
                ai_delim_make(crypto_random_bytes, NULL, delim2);
                AiBodyFrame frame2;
                frame2.snapshot = s->snapshot;
                frame2.delim = delim2;
                s->body_len = ai_build_request_body_frame(
                    &s->conv, att2,
                    s->tools_json[0] ? s->tools_json : NULL,
                    &frame2,
                    s->body, AI_BODY_MAX,
                    1, s->provider);
                {
                    char json_err[256] = "";
                    if (s->body_len == 0 ||
                        !json_validate(s->body, s->body_len, json_err, sizeof(json_err))) {
                        post_tool_msg(s, CHAT_ITEM_STATUS,
                            json_err[0] ? json_err : "JSON rebuild failed in tool loop");
                        loop_done = 1;
                    }
                }
                /* loop continues */
            }
        } else {
            /* Normal end (no tool-use stop) — add assistant message and exit loop */
            loop_done = 1;
            ai_conv_add(&s->conv, AI_ROLE_ASSISTANT, ctx.full_content);
        }
    } /* end agentic loop */

    /* Signal stream done — wParam=2 means "streaming complete, do command
     * extraction". This is the thread's last message: after it, the UI may
     * take s->conv. busy is cleared by the WM_AI_RESPONSE handler on the UI
     * thread to prevent a race where the user sends a new message before
     * cleanup completes. */
    if (!ai_stream_aborted(s))
        post_response(s, 2, final_content, final_thinking,
                      total_input_tokens, total_output_tokens);

out:
    if (!stream_failed) {
        if (ai_stream_aborted(s))
            DLOG(DLOG_AI, DLOG_INFO, 0, "stream.cancel", "stream=%u ms=%llu", stream_id,
                 (unsigned long long)(GetTickCount64() - t_stream));
        else
            DLOG(DLOG_AI, DLOG_INFO, 0, "stream.done",
                 "stream=%u ms=%llu tokens_in=%d tokens_out=%d", stream_id,
                 (unsigned long long)(GetTickCount64() - t_stream),
                 total_input_tokens, total_output_tokens);
    }
    if (tool_stream_ptr) ai_tools_stream_reset(tool_stream_ptr);
    ai_stream_mark_finished(s);   /* the panel reaps it if nothing got through */
    ai_stream_release(s);
    return 0;
}

#endif /* _WIN32 */
