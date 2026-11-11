/* The SSE stream: line parser, the posting helpers and the background
 * thread that talks to the provider and posts the results to the panel. */
#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#endif

#include "ai_chat_internal.h"
#include "ai_conv_guard.h"
#include "chat_rebuild.h"
#include "run_command.h"
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
    int thinking_cut;        /* 1 once any reasoning of this reply was dropped (spec 23) */
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

/* Store a reply as a plain assistant turn together with its reasoning (every
 * turn's is kept, spec decision 23: only DeepSeek's builder reads a plain
 * turn's). The turn is marked reasoning_cut when the accumulator dropped some,
 * the AI_REASONING_MAX cap cut it or its copy failed. Touches only `conv`, the
 * stream's own. Returns 1 when the reply was stored (ai_conv_add took it). */
static int store_plain_reply(AiConversation *conv, const StreamContext *ctx)
{
    if (ai_conv_add(conv, AI_ROLE_ASSISTANT, ctx->full_content) != 0)
        return 0;
    AiMessage *turn = &conv->messages[conv->msg_count - 1];
    (void)ai_msg_set_reasoning(turn, ctx->full_thinking);   /* -1 sets the flag itself */
    if (ctx->thinking_cut) turn->reasoning_cut = 1;
    return 1;
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
    /* Only a chunk that carries the word can be a "length" (OpenAI style) or
     * "max_tokens" (Anthropic) finish: skip the extra JSON parse for every
     * other chunk of the stream. */
    if ((strstr(json, "\"length\"") || strstr(json, "\"max_tokens\"")) &&
        ai_stream_chunk_cut_off(json))
        ctx->cut_off = 1;
    char content_delta[1024] = "";
    char thinking_delta[1024] = "";
    int chunk_input_tokens = 0, chunk_output_tokens = 0;

    int rc;
    if (ctx->tool_stream) {
        rc = ai_parse_stream_chunk_ex(json,
                                     content_delta, sizeof(content_delta),
                                     thinking_delta, sizeof(thinking_delta),
                                     ctx->tool_stream, ctx->provider,
                                     &chunk_input_tokens, &chunk_output_tokens);
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
        } else {
            /* DeepSeek's reasoning goes back whole or not at all: note that
             * this reply's is no longer whole (the turn is marked below) */
            ctx->thinking_cut = 1;
        }
        /* a delta that filled its buffer to the brim may have been cut by the
         * parser */
        if (dlen >= sizeof(thinking_delta) - 1)
            ctx->thinking_cut = 1;
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
            else
                ctx->thinking_cut = 1;   /* a line cut short may have held reasoning */
        }
    }
    return 0;
}

/* Free a response message and everything it owns. Every site that drops an
 * AiResponseMsg calls this, so the calls a run_command stop hands over are
 * freed with it. NULL-safe. */
void ai_response_msg_free(AiResponseMsg *m)
{
    if (!m) return;
    free(m->content);
    free(m->thinking);
    free(m->calls);
    free(m);
}

/* Post a WM_AI_RESPONSE. The handler clears the session's busy flag -- if
 * this stream is still the session's current one. `calls` (n_calls of them,
 * heap-allocated; NULL for none) become the message's: they are freed here
 * when nothing could be posted. `exec_inert` is the stream's run_command
 * flag on every message, and `fetch_registered` says whether its registry
 * held web_fetch. */
static void post_response_ex(AiStream *s, WPARAM kind, const char *content,
                             const char *thinking, int in_tok, int out_tok,
                             int reply_stored, AiToolCall *calls, int n_calls,
                             int fetch_registered)
{
    AiResponseMsg *rmsg = (AiResponseMsg *)calloc(1, sizeof(*rmsg));
    if (!rmsg) {
        free(calls);
        return;
    }
    rmsg->stream_id = s->id;
    rmsg->content = _strdup(content ? content : "");
    /* A NULL content is still posted: the handler tolerates it, and a taken
     * run_command turn (calls) must still reach its answer (spec decision 25). */
    if (!rmsg->content)
        DLOG(DLOG_AI, DLOG_WARN, 0, "stream.post_oom",
             "stream=%u kind=%d calls=%d", s->id, (int)kind, calls ? n_calls : 0);
    rmsg->thinking = (thinking && thinking[0]) ? _strdup(thinking) : NULL;
    rmsg->input_tokens = in_tok;
    rmsg->output_tokens = out_tok;
    rmsg->reply_stored = reply_stored;
    rmsg->calls = calls;
    rmsg->n_calls = calls ? n_calls : 0;
    rmsg->exec_inert = s->run_command;
    rmsg->fetch_registered = fetch_registered;
    if (!PostMessage((HWND)s->post_target, WM_AI_RESPONSE, kind, (LPARAM)rmsg)) {
        ai_response_msg_free(rmsg);
        return;
    }
    ai_stream_note_delivered(s);
}

static void post_response(AiStream *s, WPARAM kind, const char *content,
                          const char *thinking, int in_tok, int out_tok,
                          int reply_stored)
{
    post_response_ex(s, kind, content, thinking, in_tok, out_tok,
                     reply_stored, NULL, 0, 0);
}

static void post_error_response(AiStream *s, const char *msg)
{
    post_response(s, 0, msg, NULL, 0, 0, 0);
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
    int reply_stored = 0;   /* the normal end's ai_conv_add() of the reply took */
    /* A run_command reply's calls, copied from the stored turn and handed to
     * the panel in the done message (post_response_ex takes them). */
    AiToolCall *handoff_calls = NULL;
    int handoff_n = 0;
    int handoff_fetch_registered = 0;

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
            /* A provider that refused the request over its tools: the error
             * row, then a row that says run_command can be turned off. The
             * error response is the thread's last message and ends the stream
             * (a row posted after it would find no stream), so the error goes
             * out as a status row and the hint as the response. In-stream
             * error objects never get it (status 0 above). */
            if (s->run_command &&
                runcmd_reject_hint_applies(status, errbuf)) {
                post_tool_msg(s, CHAT_ITEM_STATUS, msg);
                post_error_response(s, runcmd_reject_hint());
            } else {
                post_error_response(s, msg);
            }
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

            /* A call whose input overflowed AI_TOOL_INPUT_MAX keeps no part
             * of it, and an input that is not exactly one valid JSON object
             * (a reply cut off mid-call, trailing bytes) is not stored
             * either: both become "{}". The conversation, the card and the
             * executor then see an empty input, never a truncated or spliced
             * one that could read as a different command or URL; the executor
             * answers with its own "missing argument" error. */
            const int n_calls = tool_stream_ptr->pending_tool_count;
            AiToolCall *pending = tool_stream_ptr->pending_tool_calls;
            const int replaced = agentic_sanitize_tool_inputs(pending, n_calls);
            for (int oi = 0; oi < n_calls; oi++) {
                if (pending[oi].overflowed)
                    DLOG(DLOG_AI, DLOG_WARN, 0, "stream.tool_input_overflow",
                         "stream=%u iter=%d call=%d", stream_id,
                         agentic.loop_iter, oi);
            }
            if (replaced > 0)
                DLOG(DLOG_AI, DLOG_WARN, 0, "stream.tool_input_sanitized",
                     "stream=%u iter=%d replaced=%d", stream_id,
                     agentic.loop_iter, replaced);

            /* No room for a round (the assistant turn, the results and the
             * reply that follows): the text is stored as a plain turn and
             * nothing runs, a web tool included. The thread never reads the
             * owner's note queue, so it passes NULL (R45). */
            const RunCmdRound plan = runcmd_round_plan(
                agentic_room_for_tool_round(&s->conv, NULL), s->run_command,
                pending, n_calls);

            /* Add assistant message with tool_use blocks */
            /* ctx.full_thinking is this turn's reasoning; the builder sends it
             * back with the tool calls for provider "moonshot" and, with every
             * turn's, for "deepseek". It makes
             * up an empty or repeated call id (call_<n>): read ids from the
             * stored turn, never from `pending`. */
            AgenticStep turn_step = AGENTIC_STEP_FULL;
            if (plan != RUNCMD_ROUND_FULL)
                turn_step = agentic_turn_step(
                    agentic_add_assistant_tool_msg_ex(&s->conv,
                                                      ctx.full_content,
                                                      ctx.full_thinking,
                                                      s->provider, s->conv.model,
                                                      pending, n_calls));
            if (turn_step == AGENTIC_STEP_FAIL) {
                /* Results must never go out without their tool-call turn. */
                stream_failed = 1;
                DLOG(DLOG_AI, DLOG_WARN, 0, "stream.error", "stream=%u class=oom ms=%llu",
                     stream_id, (unsigned long long)(GetTickCount64() - t_stream));
                post_tool_msg(s, CHAT_ITEM_STATUS, "[tool loop stopped: out of memory]");
                post_error_response(s, "Error: out of memory for the tool call");
                goto out;
            }
            if (turn_step == AGENTIC_STEP_FULL) {
                /* The text of the reply is kept as a plain assistant turn (an
                 * empty one is not: no provider takes an empty turn). */
                char full_row[128];
                if (ctx.full_content[0])
                    reply_stored = store_plain_reply(&s->conv, &ctx);
                (void)agentic_full_row(n_calls, full_row, sizeof(full_row));
                DLOG(DLOG_AI, DLOG_WARN, 0, "stream.round_full",
                     "stream=%u iter=%d calls=%d stage=turn", stream_id,
                     agentic.loop_iter, n_calls);
                post_tool_msg(s, CHAT_ITEM_STATUS, full_row);
                loop_done = 1;
                continue;
            }

            /* The turn is stored: say so when its reasoning is not the whole
             * of what the model streamed (the accumulator dropped some). */
            if (ctx.thinking_cut && s->conv.msg_count > 0)
                s->conv.messages[s->conv.msg_count - 1].reasoning_cut = 1;

            /* A run_command call ends the stream: nothing runs here, no card
             * is asked and no "using tool" row is posted. The stored turn's
             * calls (their ids, as stored) go to the panel in the done message
             * and the panel plans the batch (Task B15). */
            if (plan == RUNCMD_ROUND_STOP) {
                const AiMessage *turn = &s->conv.messages[s->conv.msg_count - 1];
                const int stored_n = turn->n_tool_calls;
                handoff_calls = stored_n > 0
                    ? (AiToolCall *)calloc((size_t)stored_n, sizeof(AiToolCall))
                    : NULL;
                if (!handoff_calls) {
                    /* Nobody would answer the calls: take the turn back out. */
                    (void)ai_conv_drop_open_tool_turn(&s->conv);
                    stream_failed = 1;
                    DLOG(DLOG_AI, DLOG_WARN, 0, "stream.error",
                         "stream=%u class=oom ms=%llu", stream_id,
                         (unsigned long long)(GetTickCount64() - t_stream));
                    post_error_response(s, "Error: out of memory for the tool call");
                    goto out;
                }
                memcpy(handoff_calls, turn->tool_calls,
                       (size_t)stored_n * sizeof(AiToolCall));
                handoff_n = stored_n;
                handoff_fetch_registered =
                    ai_tools_find(&s->tools, "web_fetch") != NULL;
                for (int fi = 0; fi < stored_n; fi++) {
                    if (tool_dispatch_rule(handoff_calls[fi].name,
                            ai_tools_find(&s->tools, handoff_calls[fi].name) != NULL) ==
                        TOOL_DISPATCH_FETCH_OFF)
                        post_tool_msg(s, CHAT_ITEM_STATUS, tool_fetch_off_row());
                }
                DLOG(DLOG_AI, DLOG_INFO, 0, "stream.runcmd_stop",
                     "stream=%u iter=%d calls=%d", stream_id,
                     agentic.loop_iter, stored_n);
                loop_done = 1;
                continue;
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
                char fetch_desc[CHAT_REBUILD_FETCH_DESC_MAX];
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
                    agentic_truncation_warning(call->name, results[ti].omitted_bytes,
                                               warn, sizeof(warn));
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

            /* Add tool results to conversation. -1 and -2 stored nothing and
             * already dropped the open turn (R37), so the conversation is
             * well-formed: end the loop here, or the next round would run the
             * same tool again and fail to store it again. 1 stored a
             * notes-only answer: carry on and say so. */
            const int n_round = tool_stream_ptr->pending_tool_count;
            const AgenticStep results_step = agentic_results_step(
                agentic_add_tool_results(&s->conv,
                                         tool_stream_ptr->pending_tool_calls,
                                         results, n_round));

            /* Free tool results */
            for (int ti = 0; ti < n_round; ti++)
                free(results[ti].content);
            free(results);

            ai_tools_stream_reset(tool_stream_ptr);
            agentic.loop_iter++;

            if (ai_stream_aborted(s))
                goto out;

            if (results_step == AGENTIC_STEP_FULL) {
                char full_row[128];
                (void)agentic_full_row(n_round, full_row, sizeof(full_row));
                DLOG(DLOG_AI, DLOG_WARN, 0, "stream.round_full",
                     "stream=%u iter=%d calls=%d stage=results", stream_id,
                     agentic.loop_iter, n_round);
                post_tool_msg(s, CHAT_ITEM_STATUS, full_row);
                loop_done = 1;
                continue;
            }
            if (results_step == AGENTIC_STEP_NOTE)
                post_tool_msg(s, CHAT_ITEM_STATUS, AGENTIC_REDUCED_ROW);

            /* Check if we can continue */
            if (!agentic_can_continue(&agentic)) {
                post_tool_msg(s, CHAT_ITEM_STATUS,
                    "[tool loop limit reached — stopping]");
                loop_done = 1;
            } else {
                /* Rebuild request body with updated conversation (including tool results) */
                /* A conversation that does not pair its tool calls with
                 * their results is never sent: say why, as an error. */
                if (!ai_conv_tool_pairs_well_formed(&s->conv)) {
                    stream_failed = 1;
                    DLOG(DLOG_AI, DLOG_WARN, 0, "stream.error",
                         "stream=%u class=pairing ms=%llu", stream_id,
                         (unsigned long long)(GetTickCount64() - t_stream));
                    post_error_response(s, ai_body_failure_text(&s->conv));
                    goto out;
                }
                int last_msg = s->conv.msg_count - 1;
                const AiAttachment *att2 = (last_msg >= 0)
                    ? s->conv.messages[last_msg].attachment : NULL;
                /* A fresh delimiter for this request, the launch's snapshot
                 * (frozen in s->conv, on the same turn the launch put it). */
                char delim2[AI_DELIM_MAX];
                ai_delim_make(crypto_random_bytes, NULL, delim2);
                AiBodyFrame frame2;
                frame2.delim = delim2;
                ai_conv_snapshot_frame(&s->conv, &frame2);
                s->body_len = ai_build_request_body_frame(
                    &s->conv, att2,
                    s->tools_json[0] ? s->tools_json : NULL,
                    &frame2,
                    s->body, AI_BODY_MAX,
                    1, s->provider);
                {
                    char json_err[256] = "";
                    if (s->body_len == 0) {
                        /* pairing, overflow or allocation: the row says which */
                        post_tool_msg(s, CHAT_ITEM_STATUS,
                                      ai_body_failure_text_for(&s->conv,
                                                               s->provider,
                                                               s->tools_json));
                        loop_done = 1;
                    } else if (!json_validate(s->body, s->body_len, json_err,
                                              sizeof(json_err))) {
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
            reply_stored = store_plain_reply(&s->conv, &ctx);
            if (!reply_stored)   /* the reply stays on screen, but not in the conversation */
                post_tool_msg(s, CHAT_ITEM_STATUS,
                              "[reply not stored: conversation full; start a New Chat]");
        }
    } /* end agentic loop */

    /* Signal stream done — wParam=2 means "streaming complete, do command
     * extraction". This is the thread's last message: after it, the UI may
     * take s->conv. busy is cleared by the WM_AI_RESPONSE handler on the UI
     * thread to prevent a race where the user sends a new message before
     * cleanup completes. */
    if (!ai_stream_aborted(s)) {
        post_response_ex(s, 2, final_content, final_thinking,
                         total_input_tokens, total_output_tokens, reply_stored,
                         handoff_calls, handoff_n, handoff_fetch_registered);
        handoff_calls = NULL;   /* the message owns them now, posted or freed */
    }

out:
    free(handoff_calls);   /* aborted before the post: nobody took them */
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
