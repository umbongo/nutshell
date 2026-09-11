#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#endif

#include "ai_chat.h"

#ifdef _WIN32

#include "ai_prompt.h"
#include "ai_tools.h"
#include "ai_tool_web_search.h"
#include "ai_tool_web_fetch.h"
#include "ai_agentic.h"
#include "ai_http.h"
#include "app_font.h"
#include "ns_font.h"
#include "ns_draw.h"
#include "ns_tokens.h"
#include "ns_scale.h"
#include "ns_type.h"
#include "ns_reduced_motion.h"
#include "ns_hover.h"
#include "ai_panel_layout.h"
#include "ai_panel_states.h"
#include "settings_layout.h"
#include "ui_theme.h"
#include "themed_button.h"
#include "custom_scrollbar.h"
#include "edit_scroll.h"
#include "term_extract.h"
#include "ssh_channel.h"
#include "resource.h"
#include "ai_dock.h"
#include "string_utils.h"
#include "chat_msg.h"
#include "chat_thinking.h"
#include "chat_activity.h"
#include "chat_approval.h"
#include "cmd_batch.h"
#include "chat_listview.h"
#include "ui_demo.h"
#include "icons.h"
#include "dpi_util.h"
#include <windowsx.h>  /* GET_X_LPARAM, GET_Y_LPARAM */
#include <stdio.h>
#include <string.h>
#include <process.h>
#include <richedit.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>
#include "base64.h"
#include "json_validate.h"

/* GDI+ flat API declarations (C-compatible -- no C++ headers) */
typedef int GpStatus;
typedef void GpBitmap;
typedef void GpImage;
typedef struct { UINT32 Data1; UINT16 Data2; UINT16 Data3; BYTE Data4[8]; } GPCLSID;
typedef struct { UINT32 Num; UINT32 Size; } EncoderParameters;

extern GpStatus __stdcall GdiplusStartup(ULONG_PTR *token, const void *input,
                                          void *output);
extern void     __stdcall GdiplusShutdown(ULONG_PTR token);
extern GpStatus __stdcall GdipCreateBitmapFromHBITMAP(HBITMAP hbm, HPALETTE hpal,
                                                       GpBitmap **bitmap);
extern GpStatus __stdcall GdipSaveImageToStream(GpImage *image, IStream *stream,
                                                 const GPCLSID *clsid,
                                                 const EncoderParameters *params);
extern GpStatus __stdcall GdipDisposeImage(GpImage *image);
extern GpStatus __stdcall GdipGetImageWidth(GpImage *image, UINT *width);
extern GpStatus __stdcall GdipGetImageHeight(GpImage *image, UINT *height);

typedef struct {
    UINT32 GdiplusVersion;
    void *DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GdiplusStartupInput;

/* PNG encoder CLSID: {557CF406-1A04-11D3-9A73-0000F81EF32E} */
static const GPCLSID CLSID_PNG = {
    0x557CF406, 0x1A04, 0x11D3,
    {0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E}
};

/* Move an AiConversation from src to dst.  Attachment ownership transfers
 * to dst; src attachment pointers are NULLed to prevent double-free. */
static void ai_conv_move(AiConversation *dst, AiConversation *src)
{
    memcpy(dst, src, sizeof(AiConversation));
    for (int i = 0; i < src->msg_count; i++)
        src->messages[i].attachment = NULL;
}

static const char *AI_CHAT_CLASS = "Nutshell_AIChat";

#define IDC_CHAT_DISPLAY  4001
#define IDC_CHAT_INPUT    4002
#define IDC_CHAT_SEND     4003
#define IDC_CHAT_NEWCHAT  4004
#define IDC_CHAT_PERMIT   4005
#define IDC_CHAT_THINKING 4006
/* 4007/4008 (IDC_CONTEXT_BAR/IDC_CONTEXT_LABEL) retired with the boxed
 * context label -- the meter is painted in the status line now. */
#define IDC_SESSION_LABEL 4009
#define IDC_CHAT_SAVE     4010
#define IDC_CHAT_ALLOW    4011
#define IDC_THINKING_BOX  4014
#define IDC_CHAT_DENY     4012
#define IDC_CHAT_UNDOCK   4013
#define IDC_CHAT_AUTOAPPROVE 4015
/* Empty/no-key/no-session state (ai_panel_states.h), posted by
 * chat_listview.c when the message list is empty -- see
 * chatlv_empty_state_hit()/on_lbuttondown() there (AI Assist Panel
 * task 4). IDC_CHAT_SUGGESTION_BASE + 0..2 are the three suggestion
 * chips (AI_STATE_EMPTY); IDC_CHAT_STATE_ACTION is the single action
 * button (AI_STATE_NO_KEY/AI_STATE_NO_SESSION). */
#define IDC_CHAT_SUGGESTION_BASE 4020  /* 4020..4022 */
#define IDC_CHAT_STATE_ACTION    4023

#define WM_AI_RESPONSE   (WM_USER + 100)
#define WM_AI_CONTINUE   (WM_USER + 101)
#define WM_AI_STREAM     (WM_USER + 102)  /* wParam: 0=thinking, 1=content; lParam: char* */
#define WM_AI_TOOL_MSG   (WM_USER + 103)  /* wParam: ChatItemType; lParam: heap char* text */

#define TIMER_CMD_QUEUE   2     /* Command dispatcher poll (prompt-gated) */
#define TIMER_SCROLL_SYNC 4     /* Sync custom scrollbar with RichEdit */
#define TIMER_THINKING    5     /* Animated thinking indicator */
#define TIMER_HEARTBEAT   6     /* Activity monitor heartbeat (1s) */
#define THINKING_ANIM_MS  400   /* Dot animation interval */
#define HEARTBEAT_MS      1000  /* Heartbeat interval */
#define CMD_QUEUE_POLL_MS 250   /* Dispatcher tick interval */
#define PROMPT_QUIET_MS   400   /* Terminal must be quiet this long at a prompt */

/* Forward declaration for input subclass */
static LRESULT CALLBACK InputSubclassProc(HWND hwnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam,
                                           UINT_PTR uIdSubclass,
                                           DWORD_PTR dwRefData);

/* Subclass for owner-drawn buttons: suppress WM_ERASEBKGND to prevent
 * white flicker during parent resize.  WM_DRAWITEM already fills the
 * entire button rect, so erasing is redundant. */
#define BTN_NOERASE_SUBCLASS_ID 43
static LRESULT CALLBACK btn_noerase_subclass(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    (void)dwRefData;
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, btn_noerase_subclass, uIdSubclass);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

typedef struct {
    HWND hwnd;
    HWND hDisplay;
    HWND hThinkingBox;  /* Embedded textbox for thinking/processing content */
    HWND hInput;
    HWND hSendBtn;
    HWND hNewChatBtn;
    HWND hSaveBtn;
    HWND hUndockBtn;
    /* Old floating Allow/Deny buttons, and the owner-drawn Permit Write /
     * Auto Approve tab buttons, are gone — approval is inline in
     * chat_listview and the two modes are shown/clicked in the status
     * line (painted, not child windows; see ai_chat_status_hit()). */
    /* Pending command batches: approval_q.entries/count are unused --
     * decisions live per-batch in d->active_state->batches (CmdBatchSet).
     * approval_q survives only as the panel-wide template of
     * auto_approve/auto_approve_level (copied into every new batch by
     * cmd_batch_add()) and as the auto_approve_confirming/confirm_start_time
     * state for the status line's "click twice to confirm" flow. */
    ApprovalQueue approval_q;
    int auto_approve_default;  /* settings.ai_auto_approve_default, 0..3: seeds
                                 * a fresh session's auto_approve/level (see
                                 * ai_chat_set_auto_approve_default()). */
    HWND hThinkingBtn;
    HWND hTooltip;        /* Win32 tooltip control */
    int permit_write;     /* 0 = read-only (red), 1 = read/write (green) */
    /* Hover/press tracking for the painted status line (mode segments,
     * auto-approve text) -- see ai_chat_status_hit()/ai_chat_status_rect(). */
    NsHover status_hover;
    int status_hover_tracking;  /* TrackMouseEvent armed for WM_MOUSELEAVE */
    int show_thinking;    /* 1 = user has manually opened a Thinking
                            * disclosure this session -- suppresses
                            * auto-collapse at reply-start (chat_listview.c)
                            * and is also passed to ai_build_save_text() so
                            * a session where the user looked at reasoning
                            * saves it too. Mirrors auto_approve: persisted
                            * per-session in AiSessionState, reset on New Chat. */
    HFONT hFont;
    HFONT hSmallFont;     /* small bold font for indicator label */
    char font_name[64];
    char ai_font_name[64];   /* font for AI markdown code blocks */
    const ThemeColors *theme;
    HBRUSH hBrBgPrimary;
    HBRUSH hBrBgSecondary;

    /* AI state */
    AiConversation conv;
    char api_key[256];
    char provider[64];
    char custom_url[256];
    char custom_model[256];

    /* Active session references */
    Terminal   *active_term;
    SSHChannel *active_channel;

    /* Background thread */
    CRITICAL_SECTION cs;

    /* Auto-continue: when AI only gives partial commands, re-prompt */
    char pending_request[2048]; /* original user request for context */

    /* Position of the animated indicator so we can remove/update it */
    int indicator_pos;  /* char offset where indicator starts, or -1 */
    int indicator_line_y; /* Y pixel position of indicator line for thinking box placement */
    int thinking_tick;  /* animation frame counter 0-2 for dot cycling */
    char indicator_base[64]; /* base text without dots, e.g. "thinking" */
    int thinking_box_height; /* Current height of thinking box for auto-resize */
    int last_phase; /* Track phase transitions (0=processing, 1=thinking) */

    /* Batch command execution with paste delay */
    int paste_delay_ms;
    int dpi;

    /* Prompt-gated command dispatcher (TIMER_CMD_QUEUE): sends approved
     * commands from one batch's queue (d->active_state->batches) at a
     * time, only once active_term is back at a shell prompt. Any number
     * of batches may be pending at once (see src/core/cmd_batch.h and
     * docs/superpowers/specs/2026-09-09-pending-command-batches.md); the
     * dispatcher only ever runs one at a time -- dispatch_batch_id says
     * which. See dispatch_start()/dispatch_tick(). */
    int dispatch_active;             /* 1 while the dispatcher is running */
    int dispatch_batch_id;           /* CmdBatch id currently being dispatched, or 0 */
    unsigned long dispatch_seq;      /* last-seen active_term->write_seq */
    int dispatch_await_echo;         /* 1 until the terminal changes after a send */
    DWORD dispatch_last_change_tick; /* GetTickCount() of the last write_seq change */
    int dispatch_last_idx;           /* batch queue index last set EXECUTING, or -1 */
    int dispatch_sent_count;         /* commands sent so far in this dispatch run */
    int stream_phase;  /* 0=not started, 1=in thinking, 2=in content */

    /* AI notes for system prompt context */
    char session_notes[2560];
    char system_notes[2560];

    /* How many terminal lines are sent to the AI as context (1-50000).
     * Always clamped via ai_context_clamp_lines(); never 0. */
    int context_lines;

    /* Scratch buffers for the terminal context and the system prompt built
     * from it.  They live as long as the panel and grow with context_lines,
     * so the send paths never allocate and cannot leak on an early return.
     * Freed in WM_DESTROY. */
    char  *ctx_term;
    size_t ctx_term_cap;
    char  *ctx_prompt;
    size_t ctx_prompt_cap;

    /* Per-session conversation tracking */
    AiSessionState *active_state;     /* points to current session's ai_state */

    /* Session name label */
    HWND hSessionLabel;
    char session_name[256];

    /* Custom scrollbar for chat display */
    HWND hDisplayScrollbar;
    int  display_line_h;   /* cached line height in px */

    /* Context window usage meter, painted in the status line (no child
     * window any more -- see ai_chat_get_status_paint()). */
    int  context_limit;       /* token limit for model, 0=unknown */
    /* Busy-indicator override text ("Waiting for output", "Continuing"...):
     * when non-empty, painted in place of the meter's used/limit numbers.
     * Set by start_indicator(), cleared by update_context_bar(). */
    char context_label[64];
    int  actual_input_tokens;  /* last known input tokens from API (0 if unavailable) */
    int  actual_output_tokens; /* last known output tokens from API (0 if unavailable) */

    /* Buffer for the status-line meter's hover tooltip text. Populated on
     * each TTN_GETDISPINFO callback so the tip always reflects the
     * latest token state. */
    char tooltip_buf[512];

    /* Thinking history: per-assistant-message thinking text.
     * Indexed by conv.messages[] index (only meaningful for ASSISTANT roles).
     * Heap-allocated strings; NULL if no thinking for that message. */
    char *thinking_history[AI_MAX_MESSAGES];

    /* Live stream thinking accumulation (UI thread only) */
    char stream_thinking[AI_MSG_MAX];
    size_t stream_thinking_len;

    /* Live stream content accumulation (for rebuild mid-stream) */
    char stream_content[AI_MSG_MAX];
    size_t stream_content_len;

    /* RichEdit char position where AI response text began (for markdown re-render) */
    int stream_display_start;

    /* Wheel delta accumulator for high-precision scroll devices */
    int wheel_accum;

    /* Current UI font size (points) for zoom — starts at APP_FONT_UI_SIZE */
    int ui_font_size;

    /* Custom scrollbar for input text box */
    HWND hInputScrollbar;
    int  input_line_h;     /* cached line height in px for input */

    /* Docked mode: 1 = child window in main frame, 0 = floating */
    int docked;

    /* New chat list view fields */
    ChatMsgList msg_list;           /* Message item linked list */
    HWND hChatList;                 /* Owner-drawn chat list view */
    ChatMsgItem *stream_ai_item;    /* Current AI item being streamed into */

    /* Owned fonts for ChatListView (caller manages lifetime).
     * hChatFont/hBoldFont/hMonoFont are zoomable and apply to chat
     * content (user messages + AI responses) and the user input
     * textbox. The UI font (d->hFont) and d->hSmallFont stay fixed so
     * buttons, context bar, and indicator labels are not affected by
     * Ctrl+/Ctrl- zoom. */
    HFONT hChatFont;
    HFONT hBoldFont;
    HFONT hMonoFont;

    /* Activity monitor state */
    ActivityState activity;
    int pulse_toggle;          /* 0/1 for pulsing dot animation */

    /* Stream abort: UI thread sets to 1, stream callback checks it */
    volatile int abort_stream;

    /* GDI+ token for image conversion */
    ULONG_PTR gdip_token;

    /* Pending image attachment for the next send */
    AiAttachment *pending_attachment;

    /* Tool use */
    AiToolRegistry tool_registry;
    WebSearchContext search_ctx;
    WebFetchContext  fetch_ctx;
    int tool_support_notified;  /* 1 after showing "tools unavailable" message */

    /* Empty/no-key/no-session state (ai_panel_states.h), pushed to
     * hChatList whenever it might change -- see update_panel_state()
     * below (AI Assist Panel task 4). -1 = no forced override: the state
     * is decided from active_channel/api_key. --ui-demo's "empty" state
     * forces AI_STATE_EMPTY via ai_chat_force_state() even though the
     * demo session has no channel (it would otherwise read as
     * AI_STATE_NO_SESSION). */
    int forced_state;
} AiChatData;

/* Helper: check if the currently active session has a busy AI stream */
#define ACTIVE_BUSY(d) ((d)->active_state && (d)->active_state->busy)

/* Forward declarations */
static void do_session_switch(AiChatData *d,
                              AiSessionState *new_state,
                              Terminal *term, SSHChannel *channel,
                              const char *session_notes,
                              const char *system_notes,
                              const char *session_name);

#include "markdown.h"

/* Heap-allocated struct to pass both content and thinking from thread to UI.
 * Also carries the target session so the UI thread can route the response
 * to the correct session when multiple streams run concurrently. */
typedef struct {
    AiSessionState *session;   /* which session this response is for */
    char *content;
    char *thinking;
    int input_tokens;          /* actual input token count from API (0 if unavailable) */
    int output_tokens;         /* actual output token count from API (0 if unavailable) */
} AiResponseMsg;

/* Heap-allocated chunk posted via WM_AI_STREAM.  Replaces the old plain
 * char* lParam so the UI thread can tell which session the chunk belongs to. */
typedef struct {
    AiSessionState *session;
    char *delta;
} AiStreamChunk;

/* Thread argument: everything the background thread needs, decoupled from
 * AiChatData so multiple threads can run for different sessions. */
typedef struct {
    HWND hwnd;                      /* target window for PostMessage */
    AiSessionState *target;         /* session this request is for */
    CRITICAL_SECTION *cs;           /* shared CS for conv writes */
    volatile int *abort_flag;       /* set to 1 by UI thread to cancel stream */
    char api_key[256];
    char provider[64];
    char custom_url[256];
    char body[AI_BODY_MAX];         /* pre-built JSON request body */
    size_t body_len;
    /* Tool use — non-NULL when tools are registered */
    AiToolRegistry *tool_registry;  /* pointer into AiChatData (valid while window alive) */
    char tools_json[AI_TOOL_SCHEMA_MAX * AI_TOOL_MAX]; /* serialized tool defs */
} AiStreamThreadArg;

/* Context for SSE streaming callback */
typedef struct {
    HWND hwnd;                   /* target window for PostMessage */
    AiSessionState *target;      /* session this stream belongs to */
    volatile int *abort_flag;    /* checked each chunk — non-zero aborts */
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
    int input_tokens;        /* actual input token count from API (0 until received) */
    int output_tokens;       /* actual output token count from API (0 until received) */
} StreamContext;

/* Process a single SSE line from the stream */
static void stream_process_line(StreamContext *ctx, const char *line, size_t len)
{
    /* Skip empty lines */
    if (len == 0) return;

    /* SSE lines start with "data: " */
    if (len < 6 || strncmp(line, "data: ", 6) != 0) return;

    const char *json = line + 6;
    char content_delta[1024] = "";
    char thinking_delta[1024] = "";
    int chunk_input_tokens = 0, chunk_output_tokens = 0;

    int rc;
    if (ctx->tool_stream) {
        rc = ai_parse_stream_chunk_ex(json,
                                     content_delta, sizeof(content_delta),
                                     thinking_delta, sizeof(thinking_delta),
                                     ctx->tool_stream, ctx->provider);
        if (rc == 2) {
            /* Tool-use stop: signal to outer loop */
            ctx->tool_stop = 1;
            return;
        }
    } else {
        rc = ai_parse_stream_chunk(json, content_delta, sizeof(content_delta),
                                   thinking_delta, sizeof(thinking_delta),
                                   &chunk_input_tokens, &chunk_output_tokens);
    }

    /* Accumulate actual token counts when provided by the API */
    if (chunk_input_tokens > 0)
        ctx->input_tokens += chunk_input_tokens;
    if (chunk_output_tokens > 0)
        ctx->output_tokens += chunk_output_tokens;

    if (rc == 1) {
        /* [DONE] — stream finished */
        return;
    }
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
        AiStreamChunk *chunk = calloc(1, sizeof(*chunk));
        if (chunk) {
            chunk->session = ctx->target;
            chunk->delta = _strdup(thinking_delta);
            PostMessage(ctx->hwnd, WM_AI_STREAM, 0, (LPARAM)chunk);
        }
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
        AiStreamChunk *chunk = calloc(1, sizeof(*chunk));
        if (chunk) {
            chunk->session = ctx->target;
            chunk->delta = _strdup(content_delta);
            PostMessage(ctx->hwnd, WM_AI_STREAM, 1, (LPARAM)chunk);
        }
    }
}

/* SSE stream callback — accumulates lines and processes them */
static int stream_callback(const char *data, size_t len, void *userdata)
{
    StreamContext *ctx = (StreamContext *)userdata;

    /* Check abort flag (set by UI thread on Cancel / New Chat / window close) */
    if (ctx->abort_flag && *ctx->abort_flag)
        return 1; /* abort stream */

    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n') {
            /* Strip trailing \r */
            if (ctx->line_len > 0 && ctx->line_buf[ctx->line_len - 1] == '\r')
                ctx->line_len--;
            ctx->line_buf[ctx->line_len] = '\0';
            stream_process_line(ctx, ctx->line_buf, ctx->line_len);
            ctx->line_len = 0;
        } else {
            if (ctx->line_len < sizeof(ctx->line_buf) - 1)
                ctx->line_buf[ctx->line_len++] = c;
        }
    }
    return 0;
}

/* Helper: post an error AiResponseMsg tagged with the target session.
 * The WM_AI_RESPONSE handler is responsible for setting busy = 0. */
static void post_error_response(HWND hwnd, AiSessionState *target, const char *msg)
{
    AiResponseMsg *rmsg = (AiResponseMsg *)calloc(1, sizeof(*rmsg));
    if (rmsg) {
        rmsg->session = target;
        rmsg->content = _strdup(msg);
        PostMessage(hwnd, WM_AI_RESPONSE, 0, (LPARAM)rmsg);
    } else {
        /* Fallback: can't allocate — clear busy here since handler won't run */
        target->busy = 0;
    }
}

/* Post a tool-related status message to the UI thread.
 * type is CHAT_ITEM_TOOL_CALL or CHAT_ITEM_TOOL_RESULT.
 * Heap-allocates the text; the WM_AI_TOOL_MSG handler frees it. */
static void post_tool_msg(HWND hwnd, ChatItemType type, const char *text)
{
    char *dup = _strdup(text ? text : "");
    if (dup)
        PostMessage(hwnd, WM_AI_TOOL_MSG, (WPARAM)type, (LPARAM)dup);
}

/* Background thread: streaming AI API call.
 * Receives a heap-allocated AiStreamThreadArg and frees it before returning.
 * When arg->tool_registry is non-NULL, runs the agentic tool-use loop. */
static unsigned __stdcall ai_stream_thread_proc(void *raw_arg)
{
    AiStreamThreadArg *arg = (AiStreamThreadArg *)raw_arg;

    if (arg->body_len == 0) {
        post_error_response(arg->hwnd, arg->target, "Error: failed to build request");
        free(arg);
        return 0;
    }

    const char *url = ai_provider_url(arg->provider);
    if (!url && strcmp(arg->provider, "custom") == 0 && arg->custom_url[0])
        url = arg->custom_url;
    if (!url) {
        post_error_response(arg->hwnd, arg->target, "Error: unknown AI provider");
        free(arg);
        return 0;
    }

    char hdr0[300], hdr1[64];
    const char *req_headers[3];
    ai_build_auth_headers(arg->provider, arg->api_key,
                          hdr0, sizeof(hdr0), hdr1, sizeof(hdr1),
                          req_headers);

    /* Agentic state (only used when tools are enabled) */
    AgenticState agentic;
    agentic_state_reset(&agentic);

    /* Tool streaming state (heap-allocated so we can reset between loops) */
    AiToolStreamState tool_stream_state;
    AiToolStreamState *tool_stream_ptr = NULL;
    if (arg->tool_registry) {
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
        ctx.hwnd = arg->hwnd;
        ctx.target = arg->target;
        ctx.abort_flag = arg->abort_flag;
        ctx.tool_stream = tool_stream_ptr;
        if (tool_stream_ptr)
            strncpy(ctx.provider, arg->provider, sizeof(ctx.provider) - 1);

        /* Reset tool stream state for this loop iteration */
        if (tool_stream_ptr)
            ai_tools_stream_reset(tool_stream_ptr);

        int status = 0;
        char errbuf[256] = "";
        int rc = ai_http_post_stream(url, req_headers, arg->body, arg->body_len,
                                     stream_callback, &ctx,
                                     &status, errbuf, sizeof(errbuf));

        /* If stream was aborted (user cancelled), discard partial results */
        if (arg->abort_flag && *arg->abort_flag) {
            if (tool_stream_ptr) ai_tools_stream_reset(tool_stream_ptr);
            free(arg);
            return 0;
        }

        if (rc != 0 || status < 200 || status >= 300) {
            char msg[1024];
            if (errbuf[0])
                snprintf(msg, sizeof(msg), "HTTP %d: %s", status, errbuf);
            else
                snprintf(msg, sizeof(msg), "HTTP %d: streaming request failed", status);
            if (tool_stream_ptr) ai_tools_stream_reset(tool_stream_ptr);
            post_error_response(arg->hwnd, arg->target, msg);
            free(arg);
            return 0;
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

        /* Accumulate token counts (last non-zero value wins) */
        if (ctx.input_tokens > 0)
            total_input_tokens = ctx.input_tokens;
        if (ctx.output_tokens > 0)
            total_output_tokens = ctx.output_tokens;

        /* ---- Tool-use branch ---- */
        if (ctx.tool_stop && tool_stream_ptr &&
            tool_stream_ptr->pending_tool_count > 0) {

            /* Add assistant message with tool_use blocks */
            EnterCriticalSection(arg->cs);
            agentic_add_assistant_tool_msg(&arg->target->conv,
                                          ctx.full_content,
                                          tool_stream_ptr->pending_tool_calls,
                                          tool_stream_ptr->pending_tool_count);
            arg->target->valid = 1;
            LeaveCriticalSection(arg->cs);

            /* Execute each tool call */
            AiToolResult *results = (AiToolResult *)calloc(
                (size_t)tool_stream_ptr->pending_tool_count, sizeof(AiToolResult));
            if (!results) {
                ai_tools_stream_reset(tool_stream_ptr);
                post_error_response(arg->hwnd, arg->target,
                                    "Error: out of memory for tool results");
                free(arg);
                return 0;
            }

            for (int ti = 0; ti < tool_stream_ptr->pending_tool_count; ti++) {
                AiToolCall *call = &tool_stream_ptr->pending_tool_calls[ti];

                /* Rate limit check */
                char rate_err[256] = "";
                if (!agentic_check_rate_limit(&agentic, call->name,
                                              rate_err, sizeof(rate_err))) {
                    results[ti].tool_use_id[0] = '\0';
                    strncpy(results[ti].tool_use_id, call->id,
                            sizeof(results[ti].tool_use_id) - 1);
                    results[ti].content = _strdup(rate_err);
                    results[ti].content_len = results[ti].content
                                           ? strlen(results[ti].content) : 0;
                    results[ti].is_error = 1;
                    post_tool_msg(arg->hwnd, CHAT_ITEM_TOOL_CALL,
                                  "[tool rate-limited]");
                    continue;
                }

                /* Notify UI of tool call (include provider for web_search) */
                char tool_call_text[256];
                const AiToolDef *tdef = ai_tools_find(arg->tool_registry,
                                                      call->name);
                const char *provider_suffix = NULL;
                if (tdef && tdef->tool_data &&
                    strcmp(call->name, "web_search") == 0) {
                    const WebSearchContext *wsc =
                        (const WebSearchContext *)tdef->tool_data;
                    if (wsc->search_provider[0])
                        provider_suffix = wsc->search_provider;
                }
                if (provider_suffix)
                    snprintf(tool_call_text, sizeof(tool_call_text),
                             "using tool: %s - %s", call->name,
                             provider_suffix);
                else
                    snprintf(tool_call_text, sizeof(tool_call_text),
                             "using tool: %s", call->name);
                post_tool_msg(arg->hwnd, CHAT_ITEM_TOOL_CALL, tool_call_text);

                /* Execute */
                ai_tool_execute(arg->tool_registry, call, arg->abort_flag,
                                &results[ti]);
                agentic_record_tool_call(&agentic, call->name);

                /* Truncation warning */
                if (results[ti].was_truncated) {
                    char warn[256];
                    agentic_truncation_warning(call->name, warn, sizeof(warn));
                    post_tool_msg(arg->hwnd, CHAT_ITEM_STATUS, warn);
                }

                /* Show brief result in UI (skip error results) */
                if (results[ti].content && results[ti].content_len > 0
                    && !results[ti].is_error) {
                    char preview[256];
                    size_t plen = results[ti].content_len < 200
                                ? results[ti].content_len : 200;
                    memcpy(preview, results[ti].content, plen);
                    preview[plen] = '\0';
                    post_tool_msg(arg->hwnd, CHAT_ITEM_TOOL_RESULT, preview);
                }
            }

            /* Add tool results to conversation */
            EnterCriticalSection(arg->cs);
            agentic_add_tool_results(&arg->target->conv,
                                     tool_stream_ptr->pending_tool_calls,
                                     results,
                                     tool_stream_ptr->pending_tool_count);
            LeaveCriticalSection(arg->cs);

            /* Free tool results */
            for (int ti = 0; ti < tool_stream_ptr->pending_tool_count; ti++)
                free(results[ti].content);
            free(results);

            ai_tools_stream_reset(tool_stream_ptr);
            agentic.loop_iter++;

            /* Check if we can continue */
            if (!agentic_can_continue(&agentic)) {
                post_tool_msg(arg->hwnd, CHAT_ITEM_STATUS,
                    "[tool loop limit reached — stopping]");
                loop_done = 1;
            } else {
                /* Rebuild request body with updated conversation (including tool results) */
                EnterCriticalSection(arg->cs);
                int last_msg = arg->target->conv.msg_count - 1;
                const AiAttachment *att2 = (last_msg >= 0)
                    ? arg->target->conv.messages[last_msg].attachment : NULL;
                arg->body_len = ai_build_request_body_tools(
                    &arg->target->conv, att2,
                    arg->tools_json[0] ? arg->tools_json : NULL,
                    arg->body, sizeof(arg->body),
                    1, arg->provider);
                LeaveCriticalSection(arg->cs);
                {
                    char json_err[256] = "";
                    if (arg->body_len == 0 ||
                        !json_validate(arg->body, arg->body_len, json_err, sizeof(json_err))) {
                        post_tool_msg(arg->hwnd, CHAT_ITEM_STATUS,
                            json_err[0] ? json_err : "JSON rebuild failed in tool loop");
                        loop_done = 1;
                    }
                }
                /* loop continues */
            }
        } else {
            /* Normal end (no tool-use stop) — add assistant message and exit loop */
            loop_done = 1;
            EnterCriticalSection(arg->cs);
            ai_conv_add(&arg->target->conv, AI_ROLE_ASSISTANT, ctx.full_content);
            arg->target->valid = 1;
            LeaveCriticalSection(arg->cs);
        }
    } /* end agentic loop */

    if (tool_stream_ptr) ai_tools_stream_reset(tool_stream_ptr);

    /* Signal stream done — wParam=2 means "streaming complete, do command extraction".
     * busy is cleared by the WM_AI_RESPONSE handler on the UI thread to prevent
     * a race where the user sends a new message before cleanup completes. */
    AiResponseMsg *rmsg = (AiResponseMsg *)calloc(1, sizeof(*rmsg));
    if (rmsg) {
        rmsg->session = arg->target;
        rmsg->content = _strdup(final_content);
        rmsg->thinking = (final_thinking[0] != '\0') ? _strdup(final_thinking) : NULL;
        rmsg->input_tokens = total_input_tokens;
        rmsg->output_tokens = total_output_tokens;
        PostMessage(arg->hwnd, WM_AI_RESPONSE, 2, (LPARAM)rmsg);
    } else {
        /* Fallback: can't allocate — clear busy here */
        arg->target->busy = 0;
    }
    free(arg);
    return 0;
}


/* Compute the panel's top-level tiling (header/thread/status/composer) for
 * the current client size -- the one place composer_h is decided, shared by
 * relayout(), WM_PAINT and every status-line hit-test helper below so they
 * never disagree with each other. */
static void ai_chat_compute_layout(AiChatData *d, AiPanelLayout *out)
{
    RECT rc = {0, 0, 1, 1};
    if (d->hwnd) GetClientRect(d->hwnd, &rc);
    NsRect panel = { 0, 0, rc.right, rc.bottom };

    int margin = ns_scale(5, d->dpi);
    int input_h = ns_scale(46, d->dpi);
    int composer_h = input_h + margin;
    ai_panel_layout(panel, d->dpi, composer_h, out);
}

/* Invalidate just the status line -- called whenever permit_write,
 * auto-approve or the context numbers change. */
static void invalidate_status_line(AiChatData *d)
{
    if (!d || !d->hwnd) return;
    AiPanelLayout l;
    ai_chat_compute_layout(d, &l);
    RECT r = { l.status.x, l.status.y, l.status.x + l.status.w,
               l.status.y + l.status.h };
    InvalidateRect(d->hwnd, &r, FALSE);
}

/* Recompute the context meter/usage state and clear any busy-indicator
 * override text (see start_indicator()) so the status line goes back to
 * showing the normal used/limit numbers. The numbers themselves are
 * measured fresh from d->context_limit/actual_*_tokens/d->conv at paint
 * time (ai_chat_get_status_paint()) -- this just triggers that repaint. */
static void update_context_bar(AiChatData *d)
{
    if (!d) return;
    d->context_label[0] = '\0';
    invalidate_status_line(d);
}

/* Decide and push the empty/no-key/no-session state (ai_panel_states.h)
 * to the chat list view. chat_listview only paints/hit-tests it while
 * the message list actually has zero items, so it is harmless (and
 * cheap) to call this any time the inputs to the decision might have
 * changed: the panel is shown, the session changes, the API key changes,
 * or the conversation is reset. See the spec's "Empty and blocked
 * states (frame C)" section. */
static void update_panel_state(AiChatData *d)
{
    if (!d || !d->hChatList) return;

    int state;
    if (d->forced_state >= 0)
        state = d->forced_state;
    else if (!d->active_channel)
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
static void start_indicator(AiChatData *d, const char *base)
{
    if (!d) return;
    if (strcmp(base, "thinking") == 0)
        d->context_label[0] = '\0';   /* inline indicator shows timing */
    else
        snprintf(d->context_label, sizeof(d->context_label), "%c%s",
            base[0] >= 'a' && base[0] <= 'z' ? (char)(base[0]-32) : base[0],
            base + 1);
    invalidate_status_line(d);
}

/* Free all thinking history entries. */
static void thinking_history_clear(AiChatData *d)
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
 * switch (chat_rebuild_display, below) and --ui-demo's "batches"/etc.
 * states (ai_chat_apply_demo_extras, which builds real CmdBatch entries
 * from the canned ApprovalQueue and lets this same replay draw them).
 * Status maps straight onto the fields the list view reads: PENDING/
 * BLOCKED stay unsettled (part of the active card, approved=-1, which
 * chat_msg_set_command already defaults to); DENIED/APPROVED/EXECUTING/
 * COMPLETED all render inline (settled=1) -- APPROVED is included because
 * a card settles the moment every entry is decided, well before the
 * dispatcher actually gets around to sending it (see
 * settle_batch_if_done()). */
static void append_batch_command_items(ChatMsgList *list, const CmdBatch *batch)
{
    if (!list || !batch) return;
    for (int i = 0; i < batch->q.count; i++) {
        const ApprovalEntry *e = &batch->q.entries[i];
        ChatMsgItem *item = chat_msg_append(list, CHAT_ITEM_COMMAND, "");
        if (!item) continue;
        chat_msg_set_command(item, e->command, e->safety,
                             e->status == APPROVE_BLOCKED);
        chat_msg_set_batch(item, batch->id);
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
}

/* Rebuild the chat display from the conversation history.
 * Used when switching sessions to replay the loaded conversation.
 * Populates the ChatMsgList and invalidates the ChatListView. */
static void chat_rebuild_display(AiChatData *d)
{
    if (!d) return;

    chat_msg_list_clear(&d->msg_list);
    d->stream_ai_item = NULL;

    /* No welcome placeholder any more -- an empty msg_list here (no real
     * messages replayed below) is painted by chat_listview as the empty/
     * no-key/no-session state instead (see update_panel_state()). */

    /* Replay messages, skipping the system prompt at index 0 */
    for (int i = 1; i < d->conv.msg_count; i++) {
        const AiMessage *msg = &d->conv.messages[i];

        if (msg->role == AI_ROLE_USER) {
            chat_msg_append(&d->msg_list, CHAT_ITEM_USER, msg->content);
        } else if (msg->role == AI_ROLE_ASSISTANT) {
            /* Tool-use blocks first, matching the live "using tool: <name>"
             * status row posted while the call is in flight. */
            if (msg->n_tool_calls > 0 && msg->tool_calls) {
                for (int ti = 0; ti < msg->n_tool_calls; ti++) {
                    char tool_call_text[128];
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
    update_context_bar(d);
}

/* Initialise the tool registry from the configured search/fetch settings. */
static void chat_register_tools(AiChatData *d,
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
}

/* Build an AiStreamThreadArg from current AiChatData state under the CS,
 * and launch the background thread.  Sets active_state->busy = 1. */
static void launch_stream_thread(AiChatData *d)
{
    AiStreamThreadArg *arg = (AiStreamThreadArg *)calloc(1, sizeof(*arg));
    if (!arg) return;

    arg->hwnd = d->hwnd;
    arg->target = d->active_state;
    arg->cs = &d->cs;
    d->abort_stream = 0;  /* reset before launching */
    arg->abort_flag = &d->abort_stream;

    EnterCriticalSection(&d->cs);
    strncpy(arg->api_key, d->api_key, sizeof(arg->api_key) - 1);
    strncpy(arg->provider, d->provider, sizeof(arg->provider) - 1);
    strncpy(arg->custom_url, d->custom_url, sizeof(arg->custom_url) - 1);
    {
        int last_msg = d->conv.msg_count - 1;
        const AiAttachment *att = (last_msg >= 0)
            ? d->conv.messages[last_msg].attachment : NULL;

        /* Use tool-aware request builder when tools are registered and provider supports them */
        int has_tools = (d->tool_registry.count > 0);
        int provider_supports = ai_provider_supports_tools(d->provider);

        if (has_tools && provider_supports) {
            /* Serialize tool definitions for this provider */
            arg->tools_json[0] = '\0';
            if (strcmp(d->provider, "anthropic") == 0)
                ai_tools_serialize_anthropic(&d->tool_registry, arg->tools_json,
                                             sizeof(arg->tools_json));
            else
                ai_tools_serialize_openai(&d->tool_registry, arg->tools_json,
                                          sizeof(arg->tools_json));

            arg->tool_registry = &d->tool_registry;
            arg->body_len = ai_build_request_body_tools(&d->conv, att,
                                                         arg->tools_json[0] ? arg->tools_json : NULL,
                                                         arg->body, sizeof(arg->body),
                                                         1, arg->provider);
        } else {
            arg->tool_registry = NULL;
            arg->body_len = ai_build_request_body_ex(&d->conv, att, arg->body,
                                                      sizeof(arg->body), 1,
                                                      arg->provider);
        }
    }

    /* Sync conversation to session state so the agentic loop can rebuild
     * follow-up requests from target->conv (which needs model, system
     * prompt, and user messages — not just the tool messages added later). */
    {
        /* Free any existing messages in the session conv */
        for (int ci = 0; ci < d->active_state->conv.msg_count; ci++)
            ai_msg_free(&d->active_state->conv.messages[ci]);

        /* Copy struct (model, msg_count, fixed-size content arrays) */
        memcpy(&d->active_state->conv, &d->conv, sizeof(AiConversation));

        /* Duplicate heap resources so both convs can be freed independently */
        for (int ci = 0; ci < d->active_state->conv.msg_count; ci++) {
            AiMessage *m = &d->active_state->conv.messages[ci];
            if (m->content_overflow) {
                char *dup = (char *)malloc(m->content_len + 1);
                if (dup) memcpy(dup, m->content_overflow, m->content_len + 1);
                m->content_overflow = dup;
            }
            m->attachment = m->attachment
                ? ai_attachment_dup(d->conv.messages[ci].attachment) : NULL;
            if (m->tool_calls && m->n_tool_calls > 0) {
                size_t tc_sz = (size_t)m->n_tool_calls * sizeof(AiToolCall);
                AiToolCall *tc = (AiToolCall *)malloc(tc_sz);
                if (tc) memcpy(tc, d->conv.messages[ci].tool_calls, tc_sz);
                m->tool_calls = tc;
            }
        }
        d->active_state->valid = 1;
    }

    LeaveCriticalSection(&d->cs);

    {
        char json_err[256] = "";
        if (arg->body_len == 0 ||
            !json_validate(arg->body, arg->body_len, json_err, sizeof(json_err))) {
            chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                json_err[0] ? json_err : "JSON build failed");
            if (d->hChatList) chat_listview_invalidate(d->hChatList);
            free(arg);
            return;
        }
    }

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

    HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, ai_stream_thread_proc, arg, 0, NULL);
    if (hThread) CloseHandle(hThread);

    /* Switch Send button to Stop while streaming */
    if (d->hSendBtn) {
        SetWindowTextW(d->hSendBtn, L"\x25A0"); /* ■ solid square = stop */
        InvalidateRect(d->hSendBtn, NULL, TRUE);
    }
}

/* Resolve which batch a card-action WM_COMMAND targets: lParam is the
 * batch id the card posted (chat_listview.c tags every card action with
 * item->u.cmd.batch), or 0 to mean "the oldest batch still needing the
 * user" -- used by handlers that can't supply a specific id (the
 * integration harness; see docs/superpowers/specs/
 * 2026-09-09-pending-command-batches.md). NULL if there's no active
 * session or no such batch. */
static CmdBatch *resolve_batch(AiChatData *d, LPARAM lParam)
{
    if (!d || !d->active_state) return NULL;
    int id = (int)lParam;
    return id ? cmd_batch_find(&d->active_state->batches, id)
              : cmd_batch_first_pending(&d->active_state->batches);
}

/* Once every entry in a batch has been decided (chat_approval_all_decided
 * -- PENDING is the only thing that blocks this; BLOCKED counts as
 * decided), render its command items inline (chat_msg_batch_settle) and,
 * if nothing in it is left to run, remove the batch itself. A batch with
 * an approved-but-not-yet-sent entry stays in the set -- dispatch_tick()
 * removes it once execution actually finishes, since its ApprovalQueue is
 * still needed to drive that. Always follows up with a re-layout
 * (chat_listview_invalidate), since WM_PAINT does not recalc_layout on
 * its own. */
static void settle_batch_if_done(AiChatData *d, CmdBatch *batch)
{
    if (!d || !batch) return;
    if (!chat_approval_all_decided(&batch->q)) return;

    int batch_id = batch->id;
    int has_runnable = chat_approval_next_approved(&batch->q) >= 0;

    chat_msg_batch_settle(&d->msg_list, batch_id);
    if (!has_runnable && d->active_state)
        cmd_batch_remove(&d->active_state->batches, batch_id);
    if (d->hChatList) chat_listview_invalidate(d->hChatList);
}

/* Cancel the active AI stream: signal abort, clear busy, reset UI state.
 * Safe to call even when no stream is active. */
static void cancel_active_stream(AiChatData *d)
{
    if (!d) return;

    /* Signal the background thread to abort */
    d->abort_stream = 1;

    /* Force-clear busy so we can proceed */
    if (d->active_state) d->active_state->busy = 0;

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
    EnterCriticalSection(&d->cs);
    if (d->conv.msg_count > 0 &&
        d->conv.messages[d->conv.msg_count - 1].role == AI_ROLE_ASSISTANT)
        d->conv.msg_count--;
    LeaveCriticalSection(&d->cs);

    /* Update UI */
    update_context_bar(d);
    if (d->hChatList) chat_listview_invalidate(d->hChatList);

    /* Free per-session stream accumulators (normally freed in WM_AI_RESPONSE,
     * but stale response may not arrive after cancel) */
    if (d->active_state) {
        free(d->active_state->stream_content);
        d->active_state->stream_content = NULL;
        d->active_state->stream_content_len = 0;
        free(d->active_state->stream_thinking);
        d->active_state->stream_thinking = NULL;
        d->active_state->stream_thinking_len = 0;
        d->active_state->stream_phase = 0;
    }

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

static void send_user_message(AiChatData *d)
{
    /* Pending command batches: a card never blocks the input (rule 1) --
     * only a streaming reply or an active dispatcher does, same as the
     * IDC_CHAT_SEND handler's Send/Stop toggle already enforces for the
     * primary UI path. */
    if (!d || !d->active_state || d->active_state->busy || d->dispatch_active)
        return;

    char input[2048];
    GetWindowText(d->hInput, input, (int)sizeof(input));

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

    if (input[0] == '\0') return;

    SetWindowText(d->hInput, "");

    /* Display user message in ChatListView */
    chat_msg_append(&d->msg_list, CHAT_ITEM_USER, input);
    if (d->hChatList) {
        chat_listview_invalidate(d->hChatList);
        chat_listview_scroll_to_bottom(d->hChatList);
    }

    /* Extract terminal context into the panel's persistent scratch buffer */
    const char *term_text = "";
    int ctx_ok = ctx_buffers_ensure(d);
    if (ctx_ok) {
        d->ctx_term[0] = '\0';
        if (d->active_term) {
            term_extract_last_n(d->active_term, d->context_lines,
                                d->ctx_term, d->ctx_term_cap);
        }
        term_text = d->ctx_term;
    }

    EnterCriticalSection(&d->cs);

    /* On first message, add system prompt */
    if (ctx_ok && d->conv.msg_count == 0) {
        char  *sys_prompt     = d->ctx_prompt;
        size_t sys_prompt_cap = d->ctx_prompt_cap;
        ai_build_system_prompt(sys_prompt, sys_prompt_cap, term_text,
                               d->session_notes, d->system_notes);
        /* Append tool descriptions if tools are registered and provider supports them */
        if (d->tool_registry.count > 0 && ai_provider_supports_tools(d->provider)) {
            size_t len = strlen(sys_prompt);
            if (str_append_fmt(sys_prompt, sys_prompt_cap, &len,
                "\n\nYou have access to the following tools that will be called "
                "automatically via the API tool-use mechanism:\n\n")) {
                for (int ti = 0; ti < d->tool_registry.count; ti++) {
                    if (!str_append_fmt(sys_prompt, sys_prompt_cap, &len,
                            "- %s: %s\n",
                            d->tool_registry.tools[ti].name,
                            d->tool_registry.tools[ti].description))
                        break;
                }
                str_append_fmt(sys_prompt, sys_prompt_cap, &len,
                    "\nYou do NOT need to use [EXEC] markers for these tools. Simply "
                    "request them via the tool-use API and the results will be provided.\n\n"
                    "Continue to use [EXEC]...[/EXEC] markers for SSH terminal commands as before.\n");
            }
        }
        ai_conv_add(&d->conv, AI_ROLE_SYSTEM, sys_prompt);
    } else if (ctx_ok && d->active_term) {
        /* Update system prompt with fresh terminal context */
        char  *sys_prompt     = d->ctx_prompt;
        size_t sys_prompt_cap = d->ctx_prompt_cap;
        ai_build_system_prompt(sys_prompt, sys_prompt_cap, term_text,
                               d->session_notes, d->system_notes);
        /* Append tool descriptions */
        if (d->tool_registry.count > 0 && ai_provider_supports_tools(d->provider)) {
            size_t len = strlen(sys_prompt);
            if (str_append_fmt(sys_prompt, sys_prompt_cap, &len,
                "\n\nYou have access to the following tools that will be called "
                "automatically via the API tool-use mechanism:\n\n")) {
                for (int ti = 0; ti < d->tool_registry.count; ti++) {
                    if (!str_append_fmt(sys_prompt, sys_prompt_cap, &len,
                            "- %s: %s\n",
                            d->tool_registry.tools[ti].name,
                            d->tool_registry.tools[ti].description))
                        break;
                }
                str_append_fmt(sys_prompt, sys_prompt_cap, &len,
                    "\nYou do NOT need to use [EXEC] markers for these tools. Simply "
                    "request them via the tool-use API and the results will be provided.\n\n"
                    "Continue to use [EXEC]...[/EXEC] markers for SSH terminal commands as before.\n");
            }
        }
        /* Replace the first (system) message */
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

    LeaveCriticalSection(&d->cs);

    update_context_bar(d);

    d->stream_display_start = -1;
    start_indicator(d, "thinking");

    launch_stream_thread(d);

    /* Clear pending attachment after thread is launched */
    ai_attachment_free(&d->pending_attachment);
}

/* Defense in depth against C1: even though ai_extract_commands() and
 * chat_approval_add() already refuse a command containing a raw control
 * byte, refuse to write one to the channel here too -- this is the last
 * point before the bytes reach the remote shell. */
static int command_has_control_char(const char *cmd)
{
    for (const unsigned char *p = (const unsigned char *)cmd; *p; p++) {
        if (*p < 0x20 || *p == 0x7F) return 1;
    }
    return 0;
}

static void execute_command(AiChatData *d, const char *cmd)
{
    if (!d || !cmd || !cmd[0]) return;
    if (!d->active_channel) {
        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                        "[error: no active SSH channel]");
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
        return;
    }
    if (command_has_control_char(cmd)) {
        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
            "[command rejected: control characters inside an EXEC block]");
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
        return;
    }

    /* Clear any existing text on the line before pasting:
       Ctrl+E (end of line) + Ctrl+U (kill to start of line) */
    ssh_channel_write(d->active_channel, "\x05\x15", 2);

    /* Send command + CR to SSH channel (CR = Enter key, same as WM_CHAR) */
    ssh_channel_write(d->active_channel, cmd, (size_t)strlen(cmd));
    ssh_channel_write(d->active_channel, "\r", 1);

}

/* Send a follow-up user turn after a batch finishes running and launch a
 * fresh stream for it. msg_text is the continue message body (built by
 * the caller via ai_build_continue_text() -- rule 4). */
static void send_continue_message(AiChatData *d, const char *msg_text)
{
    if (!d || !d->active_state || d->active_state->busy) return;
    if (!msg_text || !msg_text[0]) return;

    /* Extract fresh terminal context after command execution */
    const char *term_text = "";
    int ctx_ok = ctx_buffers_ensure(d);
    if (ctx_ok) {
        d->ctx_term[0] = '\0';
        if (d->active_term) {
            term_extract_last_n(d->active_term, d->context_lines,
                                d->ctx_term, d->ctx_term_cap);
        }
        term_text = d->ctx_term;
    }

    EnterCriticalSection(&d->cs);

    /* Update system prompt with fresh terminal context */
    if (ctx_ok && d->conv.msg_count > 0 && d->active_term) {
        char  *sys_prompt     = d->ctx_prompt;
        size_t sys_prompt_cap = d->ctx_prompt_cap;
        ai_build_system_prompt(sys_prompt, sys_prompt_cap, term_text,
                               d->session_notes, d->system_notes);
        ai_conv_set_system(&d->conv, sys_prompt);
    }

    ai_conv_add(&d->conv, AI_ROLE_USER, msg_text);

    LeaveCriticalSection(&d->cs);

    start_indicator(d, "continuing");

    launch_stream_thread(d);
}

/* Start the command dispatcher on one specific batch: sends every command
 * in its queue with status APPROVE_APPROVED, in order, one at a time,
 * only once active_term is sitting at a shell prompt (see
 * term_at_prompt()). Does not send anything itself -- the TIMER_CMD_QUEUE
 * tick (dispatch_tick(), driven from AiChatWndProc's WM_TIMER) does the
 * actual sending. Safe to call repeatedly (e.g. once per single-command
 * Approve click, or from a different batch's Run while this one is
 * already running): a no-op while any dispatch is active or the batch
 * has nothing approved yet -- another card's Run while the dispatcher is
 * busy just leaves its commands APPROVED, and cmd_batch_next_runnable()
 * picks the batch up once the current one finishes (see
 * maybe_start_next_batch()). */
static void dispatch_start(AiChatData *d, int batch_id)
{
    if (!d || !d->active_state || d->dispatch_active) return;

    CmdBatch *batch = cmd_batch_find(&d->active_state->batches, batch_id);
    if (!batch || chat_approval_next_approved(&batch->q) < 0) return;

    d->dispatch_active = 1;
    d->dispatch_batch_id = batch->id;
    d->dispatch_seq = d->active_term ? d->active_term->write_seq : 0;
    d->dispatch_await_echo = 0;
    d->dispatch_last_change_tick = GetTickCount();
    d->dispatch_last_idx = -1;
    d->dispatch_sent_count = 0;

    SetTimer(d->hwnd, TIMER_CMD_QUEUE, CMD_QUEUE_POLL_MS, NULL);

    /* Send button shows Stop while the dispatcher is running, same as
     * while an AI stream is in flight. */
    if (d->hSendBtn) {
        SetWindowTextW(d->hSendBtn, L"\x25A0");
        InvalidateRect(d->hSendBtn, NULL, TRUE);
    }
}

/* If nothing is currently dispatching or streaming, start the oldest
 * batch that has an approved-but-unsent command (rule 3: a batch whose
 * Run was clicked while another was dispatching "is queued and runs
 * next"). Called once a stream finishes (the reply itself, or a batch's
 * continue message) so a queued batch picks up right away instead of
 * waiting for another user action. */
static void maybe_start_next_batch(AiChatData *d)
{
    if (!d || !d->active_state || d->dispatch_active || ACTIVE_BUSY(d)) return;
    CmdBatch *nb = cmd_batch_next_runnable(&d->active_state->batches);
    if (nb) dispatch_start(d, nb->id);
}

/* Stop the dispatcher: kill the timer, deny any commands in the running
 * batch that were approved but not yet sent (so a later dispatch_start()
 * can't resurrect them), settle and remove just that batch's card, and
 * restore the Send button. Other pending batches are untouched -- "Stop
 * cancels the running batch only" (rule 3). When status_msg is non-NULL
 * it is appended as a status line (the Stop button path); a session
 * switch or panel close cancels silently (NULL). A no-op when the
 * dispatcher isn't running. */
static void dispatch_cancel(AiChatData *d, const char *status_msg)
{
    if (!d || !d->dispatch_active) return;

    KillTimer(d->hwnd, TIMER_CMD_QUEUE);
    d->dispatch_active = 0;

    CmdBatch *batch = d->active_state
        ? cmd_batch_find(&d->active_state->batches, d->dispatch_batch_id)
        : NULL;
    if (batch) {
        for (int i = 0; i < batch->q.count; i++) {
            if (batch->q.entries[i].status == APPROVE_APPROVED)
                batch->q.entries[i].status = APPROVE_DENIED;
        }
        chat_msg_batch_settle(&d->msg_list, batch->id);
        cmd_batch_remove(&d->active_state->batches, batch->id);
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
    }
    d->dispatch_batch_id = 0;

    if (status_msg) {
        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS, status_msg);
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
    }

    if (d->hSendBtn) {
        SetWindowText(d->hSendBtn, ">");
        InvalidateRect(d->hSendBtn, NULL, TRUE);
    }
}

/* TIMER_CMD_QUEUE tick: advance the dispatcher (on d->dispatch_batch_id)
 * by at most one command. Tracks "quiet" (no terminal writes) and
 * "changed since the last send" (dispatch_await_echo) off
 * active_term->write_seq so the prompt that was on screen before a
 * command runs can never be mistaken for the next prompt. See
 * docs/superpowers/specs/2026-09-07-command-dispatch-and-
 * auto-approve-levels.md, section A. */
static void dispatch_tick(AiChatData *d)
{
    if (!d || !d->dispatch_active) return;

    if (!d->active_term || !d->active_channel) {
        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                        "[error: no active SSH channel]");
        if (d->hChatList) chat_listview_invalidate(d->hChatList);
        dispatch_cancel(d, NULL);
        return;
    }

    CmdBatch *batch = d->active_state
        ? cmd_batch_find(&d->active_state->batches, d->dispatch_batch_id)
        : NULL;
    if (!batch) {
        /* The batch vanished from under the dispatcher (e.g. the session
         * it belonged to was switched away from and its dispatch was
         * cancelled, but this stale tick still fired) -- stop cleanly. */
        KillTimer(d->hwnd, TIMER_CMD_QUEUE);
        d->dispatch_active = 0;
        d->dispatch_batch_id = 0;
        if (d->hSendBtn) {
            SetWindowText(d->hSendBtn, ">");
            InvalidateRect(d->hSendBtn, NULL, TRUE);
        }
        return;
    }

    unsigned long seq = d->active_term->write_seq;
    if (seq != d->dispatch_seq) {
        d->dispatch_seq = seq;
        d->dispatch_last_change_tick = GetTickCount();
        d->dispatch_await_echo = 0;
    }

    int ready = !d->dispatch_await_echo &&
                term_at_prompt(d->active_term) &&
                (GetTickCount() - d->dispatch_last_change_tick) >= PROMPT_QUIET_MS;
    if (!ready) return;

    int idx = chat_approval_next_approved(&batch->q);
    if (idx >= 0) {
        if (d->dispatch_last_idx >= 0)
            chat_approval_set_completed(&batch->q, d->dispatch_last_idx);

        /* N = commands already sent plus everything still APPROVED right
         * now (idx included) -- so the label tracks correctly even when
         * more commands get approved after the dispatcher started. */
        int approved_now = 0;
        for (int i = 0; i < batch->q.count; i++)
            if (batch->q.entries[i].status == APPROVE_APPROVED)
                approved_now++;
        int total = d->dispatch_sent_count + approved_now;

        chat_approval_set_executing(&batch->q, idx);
        execute_command(d, batch->q.entries[idx].command);
        d->dispatch_last_idx = idx;
        d->dispatch_sent_count++;
        d->dispatch_await_echo = 1;

        float now = (float)GetTickCount() / 1000.0f;
        chat_activity_set_phase(&d->activity, ACTIVITY_EXECUTING, now);
        chat_activity_set_exec(&d->activity, d->dispatch_sent_count, total);

        char prog[80];
        snprintf(prog, sizeof(prog), "running %d/%d \xC2\xB7 waiting for prompt",
                 d->dispatch_sent_count, total);
        start_indicator(d, prog);
    } else {
        /* Nothing left to send in this batch -- settle it, remove it, and
         * tell the AI which batch just ran (rule 4) before letting it
         * continue. */
        if (d->dispatch_last_idx >= 0)
            chat_approval_set_completed(&batch->q, d->dispatch_last_idx);

        int batch_id = batch->id;
        int newer_exchanges = (d->conv.msg_count > batch->conv_mark) ? 1 : 0;
        char first_cmd[1024] = "";
        if (batch->q.count > 0)
            snprintf(first_cmd, sizeof(first_cmd), "%s",
                     batch->q.entries[0].command);

        KillTimer(d->hwnd, TIMER_CMD_QUEUE);
        d->dispatch_active = 0;
        d->dispatch_batch_id = 0;

        chat_msg_batch_settle(&d->msg_list, batch_id);
        cmd_batch_remove(&d->active_state->batches, batch_id);
        if (d->hChatList) chat_listview_invalidate(d->hChatList);

        if (d->hSendBtn) {
            SetWindowText(d->hSendBtn, ">");
            InvalidateRect(d->hSendBtn, NULL, TRUE);
        }

        char continue_text[1024];
        ai_build_continue_text(newer_exchanges, first_cmd,
                               continue_text, sizeof(continue_text));
        send_continue_message(d, continue_text);
    }
}

/* Convert a clipboard bitmap to a base64-encoded PNG AiAttachment.
 * Returns NULL if the clipboard does not contain a bitmap or on error. */
static AiAttachment *clipboard_to_png_attachment(HWND owner)
{
    if (!IsClipboardFormatAvailable(CF_BITMAP))
        return NULL;
    if (!OpenClipboard(owner))
        return NULL;

    HBITMAP hbm = (HBITMAP)GetClipboardData(CF_BITMAP);
    if (!hbm) { CloseClipboard(); return NULL; }

    BITMAP bm_info;
    GetObject(hbm, sizeof(bm_info), &bm_info);

    GpBitmap *gpbmp = NULL;
    if (GdipCreateBitmapFromHBITMAP(hbm, NULL, &gpbmp) != 0 || !gpbmp) {
        CloseClipboard();
        return NULL;
    }
    CloseClipboard();

    IStream *stm = SHCreateMemStream(NULL, 0);
    if (!stm) {
        GdipDisposeImage((GpImage *)gpbmp);
        return NULL;
    }

    if (GdipSaveImageToStream((GpImage *)gpbmp, stm, &CLSID_PNG, NULL) != 0) {
        stm->lpVtbl->Release(stm);
        GdipDisposeImage((GpImage *)gpbmp);
        return NULL;
    }
    GdipDisposeImage((GpImage *)gpbmp);

    STATSTG stat;
    stm->lpVtbl->Stat(stm, &stat, STATFLAG_NONAME);
    size_t png_len = (size_t)stat.cbSize.QuadPart;

    unsigned char *png_buf = (unsigned char *)malloc(png_len);
    if (!png_buf) { stm->lpVtbl->Release(stm); return NULL; }

    LARGE_INTEGER zero_pos = {{0}};
    stm->lpVtbl->Seek(stm, zero_pos, STREAM_SEEK_SET, NULL);
    ULONG bytes_read = 0;
    stm->lpVtbl->Read(stm, png_buf, (ULONG)png_len, &bytes_read);
    stm->lpVtbl->Release(stm);

    if (bytes_read != (ULONG)png_len) { free(png_buf); return NULL; }

    size_t b64_size = ((png_len + 2) / 3) * 4 + 1;
    char *b64 = (char *)malloc(b64_size);
    if (!b64) { free(png_buf); return NULL; }

    size_t b64_len = base64_encode(png_buf, png_len, b64, b64_size);
    free(png_buf);
    if (b64_len == 0) { free(b64); return NULL; }

    const char *prefix = "data:image/png;base64,";
    size_t prefix_len = strlen(prefix);
    char *url = (char *)malloc(prefix_len + b64_len + 1);
    if (!url) { free(b64); return NULL; }
    memcpy(url, prefix, prefix_len);
    memcpy(url + prefix_len, b64, b64_len + 1);
    free(b64);

    AiAttachment *att = (AiAttachment *)calloc(1, sizeof(*att));
    if (!att) { free(url); return NULL; }
    att->base64_url = url;
    att->width = bm_info.bmWidth;
    att->height = bm_info.bmHeight;
    return att;
}

static void input_sync_scroll(AiChatData *d);

/* Subclass proc for the multiline input: Enter sends, Shift+Enter inserts newline */
static LRESULT CALLBACK InputSubclassProc(HWND hwnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam,
                                           UINT_PTR uIdSubclass,
                                           DWORD_PTR dwRefData)
{
    if (msg == WM_KEYDOWN || msg == WM_CHAR) {
        session_mark_user_active();
    }
    if (msg == WM_PASTE) {
        HWND parent = GetParent(hwnd);
        AiChatData *pd = parent
            ? (AiChatData *)GetWindowLongPtr(parent, GWLP_USERDATA) : NULL;
        if (pd && IsClipboardFormatAvailable(CF_BITMAP)) {
            AiAttachment *att = clipboard_to_png_attachment(parent);
            if (att) {
                ai_attachment_free(&pd->pending_attachment);
                pd->pending_attachment = att;
                SendMessageA(hwnd, EM_REPLACESEL, TRUE,
                             (LPARAM)"[Image attached] ");
                return 0;
            }
        }
        /* Fall through to default text paste */
    }
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        int shift = GetKeyState(VK_SHIFT) & 0x8000;
        AiInputAction action = ai_input_key_action(1, shift ? 1 : 0);
        if (action == AI_INPUT_SEND) {
            /* Trigger the send button */
            HWND parent = GetParent(hwnd);
            PostMessage(parent, WM_COMMAND,
                        MAKEWPARAM(IDC_CHAT_SEND, BN_CLICKED), 0);
            return 0; /* eat the Enter key */
        }
        /* AI_INPUT_NEWLINE: fall through to default (inserts newline) */
    }
    if (msg == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
        if (wParam == VK_OEM_PLUS  || wParam == (WPARAM)'='
         || wParam == VK_OEM_MINUS || wParam == (WPARAM)'-') {
            HWND parent = GetParent(hwnd);
            if (parent) SendMessage(parent, msg, wParam, lParam);
            return 0;
        }
    }
    if (msg == WM_MOUSEWHEEL) {
        int zdelta = GET_WHEEL_DELTA_WPARAM(wParam);
        /* Ctrl+Wheel: forward to parent so chat_apply_zoom runs */
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            HWND parent = GetParent(hwnd);
            if (parent) SendMessage(parent, msg, wParam, lParam);
            return 0;
        }
        int scroll = edit_scroll_wheel_delta(zdelta, WHEEL_DELTA, 3);
        SendMessage(hwnd, EM_LINESCROLL, 0, (LPARAM)scroll);
        /* Sync input scrollbar */
        HWND parent = GetParent(hwnd);
        if (parent) {
            AiChatData *d = (AiChatData *)GetWindowLongPtr(parent,
                                                           GWLP_USERDATA);
            if (d) input_sync_scroll(d);
        }
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, InputSubclassProc, uIdSubclass);
    }
    (void)dwRefData;
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

/* Add a tooltip to a child control */
static void add_tooltip(HWND hTooltip, HWND hCtrl, const char *text)
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
static void draw_header_icon_button(LPDRAWITEMSTRUCT dis,
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

/* Model-chip geometry, shared by relayout() (to size the session label)
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
 * status/composer tiling (ai_chat_compute_layout()). Called once at the
 * end of WM_CREATE and again on every WM_SIZE. */
static void relayout(AiChatData *d)
{
    if (!d || !d->hwnd) return;

    int margin = ns_scale(5, d->dpi);
    int input_h = ns_scale(46, d->dpi);
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
}

/* ── Status line: geometry, hit-test, tooltip, paint ──────────────────
 * The mode segments (Read-only / Read + write) and the auto-approve text
 * are painted directly on the panel's own client area (not child
 * windows), so hover/click/tooltip all go through a hit-test against a
 * freshly measured AiStatusLayout rather than window messages. See
 * docs/superpowers/specs/2026-09-07-ai-assist-panel-design.md "Structure
 * (frame B)".
 */

enum { STATUS_HIT_SEG0 = 0, STATUS_HIT_SEG1 = 1, STATUS_HIT_AUTO = 2 };

typedef struct {
    NsRect status;
    AiStatusLayout sl;
    char auto_text[64];
    char meter_text[32];
    int has_meter;   /* context_limit > 0 -- meter numbers are meaningful */
} AiStatusPaint;

/* Format "<used> / <limit>" the same way the old boxed label did (e.g.
 * "1.2k / 200k"), just without the "Context: " prefix or "(N%)" suffix --
 * the meter bar shows the fraction visually now. */
static void ai_chat_format_meter_text(int tokens, int limit,
                                       char *buf, size_t cap)
{
    if (!buf || cap == 0) return;
    char tok_str[16], lim_str[16];
    if (tokens >= 1000)
        snprintf(tok_str, sizeof(tok_str), "%.1fk", tokens / 1000.0);
    else
        snprintf(tok_str, sizeof(tok_str), "%d", tokens);
    if (limit >= 1000)
        snprintf(lim_str, sizeof(lim_str), "%dk", limit / 1000);
    else
        snprintf(lim_str, sizeof(lim_str), "%d", limit);
    snprintf(buf, cap, "%s / %s", tok_str, lim_str);
}

static NsRect ai_chat_status_rect_for_id(const AiStatusLayout *sl, int id)
{
    switch (id) {
    case STATUS_HIT_SEG0: return sl->seg[0];
    case STATUS_HIT_SEG1: return sl->seg[1];
    case STATUS_HIT_AUTO: return sl->auto_label;
    default: { NsRect z = {0, 0, 0, 0}; return z; }
    }
}

static int ai_chat_pt_in_rect(NsRect r, int x, int y)
{
    return r.w > 0 && r.h > 0 &&
           x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* Measure the status line's variable-width text (segment labels,
 * auto-approve text, meter numbers) and lay it out via ai_status_layout().
 * Self-contained: borrows its own HDC when the caller doesn't have one
 * handy (hit-testing/tooltips), or reuses the caller's paint HDC. */
static void ai_chat_get_status_paint(AiChatData *d, HDC hdc, AiStatusPaint *out)
{
    memset(out, 0, sizeof(*out));

    AiPanelLayout l;
    ai_chat_compute_layout(d, &l);
    out->status.x = l.status.x; out->status.y = l.status.y;
    out->status.w = l.status.w; out->status.h = l.status.h;

    HDC own_hdc = hdc ? NULL : GetDC(d->hwnd);
    HDC use_hdc = hdc ? hdc : own_hdc;
    if (!use_hdc) return;

    HFONT font = ns_font(FONT_CAPTION, d->dpi);
    HGDIOBJ old = font ? SelectObject(use_hdc, font) : NULL;

    SIZE sz;
    static const char seg0_label[] = "Read-only";
    static const char seg1_label[] = "Read + write";
    GetTextExtentPoint32A(use_hdc, seg0_label, (int)strlen(seg0_label), &sz);
    int seg0_w = sz.cx;
    GetTextExtentPoint32A(use_hdc, seg1_label, (int)strlen(seg1_label), &sz);
    int seg1_w = sz.cx;

    snprintf(out->auto_text, sizeof(out->auto_text), "Auto approve: %s",
            ai_modes_label(d->approval_q.auto_approve, d->approval_q.auto_approve_level));
    GetTextExtentPoint32A(use_hdc, out->auto_text, (int)strlen(out->auto_text), &sz);
    int auto_w = sz.cx;

    int meter_w = 0;
    out->has_meter = d->context_limit > 0;
    if (out->has_meter) {
        int actual = d->actual_input_tokens + d->actual_output_tokens;
        int tokens = actual > 0 ? actual : ai_context_estimate_tokens(&d->conv);
        ai_chat_format_meter_text(tokens, d->context_limit,
                                  out->meter_text, sizeof(out->meter_text));
        GetTextExtentPoint32A(use_hdc, out->meter_text,
                              (int)strlen(out->meter_text), &sz);
        meter_w = sz.cx;
    }

    if (old) SelectObject(use_hdc, old);
    if (own_hdc) ReleaseDC(d->hwnd, own_hdc);

    ai_status_layout(out->status, d->dpi, seg0_w, seg1_w, auto_w, meter_w,
                     &out->sl);
}

/* Hit-test a client point against the status line's clickable elements.
 * Returns STATUS_HIT_SEG0/SEG1/AUTO, or -1 for nothing hittable. */
static int ai_chat_status_hit(AiChatData *d, int x, int y)
{
    AiStatusPaint sp;
    ai_chat_get_status_paint(d, NULL, &sp);
    static const int ids[3] = { STATUS_HIT_SEG0, STATUS_HIT_SEG1, STATUS_HIT_AUTO };
    for (int i = 0; i < 3; i++) {
        NsRect r = ai_chat_status_rect_for_id(&sp.sl, ids[i]);
        if (ai_chat_pt_in_rect(r, x, y)) return ids[i];
    }
    return -1;
}

/* Client rect for a given status-line hit id (for targeted invalidation
 * when hover moves off an element). Returns 0 if the id is out of range or
 * the element is currently zero-size (squeezed out on a narrow panel). */
static int ai_chat_status_rect(AiChatData *d, int id, RECT *out_rc)
{
    if (id < 0) return 0;
    AiStatusPaint sp;
    ai_chat_get_status_paint(d, NULL, &sp);
    NsRect r = ai_chat_status_rect_for_id(&sp.sl, id);
    if (r.w <= 0 || r.h <= 0) return 0;
    SetRect(out_rc, r.x, r.y, r.x + r.w, r.y + r.h);
    return 1;
}

static RECT ai_chat_to_RECT(NsRect r)
{
    RECT rc = { r.x, r.y, r.x + r.w, r.y + r.h };
    return rc;
}

/* Paint the header's model chip and (while a session is active) the
 * activity dot + one-word status, next to the session label. */
static void paint_header(AiChatData *d, HDC hdc)
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
        SetTextColor(hdc, dot_clr);
        RECT wrc;
        wrc.left   = dot_x + dot_sz + ns_scale(4, d->dpi);
        wrc.top    = rc_lbl.top;
        wrc.right  = l.header.x + l.header.w;
        wrc.bottom = rc_lbl.bottom;
        DrawTextA(hdc, word, -1, &wrc, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    }
    SelectObject(hdc, old_f);
}

/* Paint the status line: bg_secondary fill with a border rule on top, the
 * mode segmented control, the auto-approve text (or, while a
 * start_indicator() busy override is set, that text instead of the
 * meter), and the context meter + used/limit numbers. */
static void paint_status_line(AiChatData *d, HDC hdc)
{
    const ThemeTokens *tok = ns_tokens();
    AiStatusPaint sp;
    ai_chat_get_status_paint(d, hdc, &sp);
    RECT status_rc = ai_chat_to_RECT(sp.status);

    HBRUSH bg_br = CreateSolidBrush(theme_cr(tok->bg_secondary.base));
    FillRect(hdc, &status_rc, bg_br);
    DeleteObject(bg_br);
    ns_draw_separator(hdc, status_rc.left, status_rc.right, status_rc.top,
                      theme_cr(tok->border));

    HFONT cap_font = ns_font(FONT_CAPTION, d->dpi);
    HGDIOBJ old_font = cap_font ? SelectObject(hdc, cap_font) : NULL;
    int old_bk = SetBkMode(hdc, TRANSPARENT);

    /* Mode segmented control */
    {
        const char *labels[2] = { "Read-only", "Read + write" };
        int hover_state[2] = {
            ns_hover_state_for(&d->status_hover, STATUS_HIT_SEG0),
            ns_hover_state_for(&d->status_hover, STATUS_HIT_SEG1)
        };
        NsRect seg[2] = { sp.sl.seg[0], sp.sl.seg[1] };
        ns_draw_segmented(hdc, seg, labels, d->permit_write, d->permit_write,
                          tok, hover_state, cap_font, d->dpi);
    }

    /* Auto-approve text: "Auto approve: " in text_dim, the state word in
     * text_main when on / text_dim when off; underlined while hovered. */
    if (sp.sl.auto_label.w > 0) {
        RECT rc = ai_chat_to_RECT(sp.sl.auto_label);
        const char *prefix = "Auto approve: ";
        const char *state = sp.auto_text + strlen(prefix);
        SIZE prefix_sz;
        GetTextExtentPoint32A(hdc, prefix, (int)strlen(prefix), &prefix_sz);

        SetTextColor(hdc, theme_cr(tok->text_dim));
        DrawTextA(hdc, prefix, -1, &rc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        RECT state_rc = rc;
        state_rc.left += prefix_sz.cx;
        SetTextColor(hdc, d->approval_q.auto_approve
                          ? theme_cr(tok->text_main) : theme_cr(tok->text_dim));
        DrawTextA(hdc, state, -1, &state_rc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        if (ns_hover_state_for(&d->status_hover, STATUS_HIT_AUTO) > 0) {
            SIZE full_sz;
            GetTextExtentPoint32A(hdc, sp.auto_text, (int)strlen(sp.auto_text), &full_sz);
            int text_top = rc.top + ((rc.bottom - rc.top) - full_sz.cy) / 2;
            int underline_y = text_top + full_sz.cy - 1;
            HPEN pen = CreatePen(PS_SOLID, 1, theme_cr(tok->text_dim));
            HGDIOBJ old_pen = SelectObject(hdc, pen);
            MoveToEx(hdc, rc.left, underline_y, NULL);
            LineTo(hdc, rc.left + full_sz.cx, underline_y);
            SelectObject(hdc, old_pen);
            DeleteObject(pen);
        }
    }

    /* Busy override, or the context meter + used/limit numbers */
    if (d->context_label[0]) {
        RECT rc = ai_chat_to_RECT(sp.sl.meter_bar.w > 0 ? sp.sl.meter_bar
                                                        : sp.sl.meter_text);
        rc.right = status_rc.right - ns_scale(SP_SM, d->dpi);
        SetTextColor(hdc, theme_cr(tok->text_main));
        DrawTextA(hdc, d->context_label, -1, &rc,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    } else if (sp.has_meter) {
        if (sp.sl.meter_bar.w > 0) {
            int actual = d->actual_input_tokens + d->actual_output_tokens;
            int tokens = actual > 0 ? actual : ai_context_estimate_tokens(&d->conv);
            double frac = d->context_limit > 0
                        ? (double)tokens / (double)d->context_limit : 0.0;
            ns_draw_meter(hdc, &sp.sl.meter_bar, frac, tok);
        }
        if (sp.sl.meter_text.w > 0) {
            RECT rc = ai_chat_to_RECT(sp.sl.meter_text);
            SetTextColor(hdc, theme_cr(tok->text_main));
            DrawTextA(hdc, sp.meter_text, -1, &rc,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }

    SetBkMode(hdc, old_bk);
    if (old_font) SelectObject(hdc, old_font);
}

/* Sync the input scrollbar with the EDIT control's scroll state */
static void input_sync_scroll(AiChatData *d)
{
    if (!d || !d->hInputScrollbar) return;
    int lh = d->input_line_h > 0 ? d->input_line_h : 1;
    csb_sync_edit(d->hInput, d->hInputScrollbar, lh);
}

/* Zoom the AI chat content fonts. Resizes the chat ListView fonts
 * (user messages + AI responses) and the user input textbox font.
 * Buttons keep the UI font; context bar and session label keep the
 * small font. */
static void chat_apply_zoom(AiChatData *d, int delta)
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
        input_sync_scroll(d);
    }
}

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

        /* All child-control geometry is decided by relayout() (called once
         * at the end of this handler, then again on every WM_SIZE) from
         * ai_panel_layout()'s header/thread/status/composer tiling -- the
         * positions/sizes given to CreateWindow here are placeholders. */
        int btn_h = ns_scale(SZ_CTRL_H, nd->dpi);

        /* New chat / Save / Undock: three square icon buttons in the
         * header, owner-drawn via draw_header_icon_button(). */
        nd->hNewChatBtn = CreateWindow("BUTTON", "",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            0, 0, btn_h, btn_h,
            hwnd, (HMENU)IDC_CHAT_NEWCHAT, NULL, NULL);

        nd->permit_write = 0; /* default: read-only; shown/toggled in the status line */

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
        SetWindowSubclass(nd->hNewChatBtn,  btn_noerase_subclass, BTN_NOERASE_SUBCLASS_ID, 0);
        SetWindowSubclass(nd->hSaveBtn,     btn_noerase_subclass, BTN_NOERASE_SUBCLASS_ID, 0);
        SetWindowSubclass(nd->hUndockBtn,   btn_noerase_subclass, BTN_NOERASE_SUBCLASS_ID, 0);

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
         * all dimensions to >=1 — relayout() fixes them on first WM_SIZE. */
        int input_h = ns_scale(46, nd->dpi); /* ~2 lines for multiline input */
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
            SetWindowSubclass(nd->hInput, InputSubclassProc, 0, 0);

        /* Custom themed scrollbar for input */
        nd->hInputScrollbar = csb_create(hwnd,
            0, 0, CSB_WIDTH, input_h,
            nd->theme, GetModuleHandle(NULL));

        /* Send button (owner-drawn for theme) */
        nd->hSendBtn = CreateWindow("BUTTON", ">",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            0, 0, send_w, input_h,
            hwnd, (HMENU)IDC_CHAT_SEND, NULL, NULL);
        SetWindowSubclass(nd->hSendBtn, btn_noerase_subclass, BTN_NOERASE_SUBCLASS_ID, 0);

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
         * whole-window callback-text tool for the painted status line
         * (mode segments / auto-approve / context meter) -- its text is
         * built on demand in WM_NOTIFY by hit-testing the cursor position
         * via ai_chat_status_hit(), so it always reflects current state
         * without us pushing updates. */
        nd->hTooltip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        if (nd->hTooltip) {
            SendMessage(nd->hTooltip, TTM_SETMAXTIPWIDTH, 0, 400);
            add_tooltip(nd->hTooltip, nd->hNewChatBtn,
                "New chat\nClear the conversation and start fresh.");
            add_tooltip(nd->hTooltip, nd->hSaveBtn,
                "Save chat\nSave the conversation as a text file.");
            if (nd->hUndockBtn)
                add_tooltip(nd->hTooltip, nd->hUndockBtn,
                    nd->docked ? "Undock\nOpen AI Assist in a separate window."
                               : "Dock\nDock AI Assist inside the main window.");
            add_tooltip(nd->hTooltip, nd->hSendBtn,
                "Send\nSend your message to the AI.\n"
                "Shortcut: press Enter in the input box.");

            TOOLINFO ti;
            memset(&ti, 0, sizeof(ti));
            ti.cbSize   = sizeof(ti);
            ti.uFlags   = TTF_SUBCLASS | TTF_IDISHWND;
            ti.hwnd     = hwnd;
            ti.uId      = (UINT_PTR)hwnd;
            ti.lpszText = LPSTR_TEXTCALLBACK;
            GetClientRect(hwnd, &ti.rect);
            SendMessage(nd->hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
        }

        /* Replays any loaded conversation, or leaves msg_list empty for
         * chat_listview to paint the empty/no-key/no-session state
         * (set just below -- active_channel isn't known yet at this
         * point, ai_chat_set_session() corrects it right after). */
        chat_rebuild_display(nd);
        update_panel_state(nd);

        update_context_bar(nd);
        relayout(nd);
        SetFocus(nd->hInput);
        return 0;
    }

    case WM_SIZE: {
        if (!d) break;
        relayout(d);
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
            hdr->idFrom == (UINT_PTR)hwnd) {
            /* The one whole-window callback tool covers the painted status
             * line -- hit-test the cursor to pick the right tip. */
            NMTTDISPINFOA *nm = (NMTTDISPINFOA *)lParam;
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            int hit = ai_chat_status_hit(d, pt.x, pt.y);
            if (hit == STATUS_HIT_SEG0) {
                nm->lpszText = (LPSTR)"Commands can only read.";
                return 0;
            }
            if (hit == STATUS_HIT_SEG1) {
                nm->lpszText = (LPSTR)"Commands may change the system.";
                return 0;
            }
            if (hit == STATUS_HIT_AUTO) {
                nm->lpszText = (LPSTR)"Approve safe commands automatically.";
                return 0;
            }
            /* Otherwise: is the cursor over the context meter? */
            AiStatusPaint sp;
            ai_chat_get_status_paint(d, NULL, &sp);
            NsRect meter = sp.sl.meter_bar.w > 0 ? sp.sl.meter_bar : sp.sl.meter_text;
            if (sp.has_meter && ai_chat_pt_in_rect(meter, pt.x, pt.y)) {
                int actual = d->actual_input_tokens + d->actual_output_tokens;
                int est = (actual > 0) ? 0
                                       : ai_context_estimate_tokens(&d->conv);
                ai_format_context_tooltip(
                    d->actual_input_tokens,
                    d->actual_output_tokens,
                    est,
                    d->context_limit,
                    d->conv.model,
                    d->tooltip_buf, sizeof(d->tooltip_buf));
                nm->lpszText = d->tooltip_buf;
                return 0;
            }
            nm->lpszText = (LPSTR)"";
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE: {
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
            int hit_id = ai_chat_status_hit(d, pt.x, pt.y);
            SetCursor(LoadCursor(NULL, hit_id >= 0 ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;

    case WM_LBUTTONDOWN:
        if (d) {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            int hit_id = ai_chat_status_hit(d, mx, my);
            if (hit_id == STATUS_HIT_SEG0) {
                if (d->permit_write != 0)
                    PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_CHAT_PERMIT, 0), 0);
                return 0;
            }
            if (hit_id == STATUS_HIT_SEG1) {
                if (d->permit_write != 1)
                    PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_CHAT_PERMIT, 0), 0);
                return 0;
            }
            if (hit_id == STATUS_HIT_AUTO) {
                PostMessage(hwnd, WM_COMMAND, MAKEWPARAM(IDC_CHAT_AUTOAPPROVE, 0), 0);
                return 0;
            }
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CHAT_SEND:
            if (d && d->dispatch_active) {
                dispatch_cancel(d, "[command queue stopped]");
                if (d->hChatList)
                    chat_listview_scroll_to_bottom(d->hChatList);
            } else if (d && ACTIVE_BUSY(d)) {
                cancel_active_stream(d);
                chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                    "[cancelled]");
                if (d->hChatList) {
                    chat_listview_invalidate(d->hChatList);
                    chat_listview_scroll_to_bottom(d->hChatList);
                }
            } else {
                send_user_message(d);
            }
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
                send_user_message(d);
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
            /* Same precedence as update_panel_state(): no channel means
             * NO_SESSION is showing (even if the key is also empty), so
             * check active_channel first rather than api_key. */
            if (!d->active_channel) {
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
                    cancel_active_stream(d);
                if (d->dispatch_active)
                    dispatch_cancel(d, NULL);

                /* Reset only the ACTIVE session's conversation.
                 * Other sessions' AiSessionState objects are untouched. */
                ai_conv_reset(&d->conv);
                if (d->active_state) {
                    ai_conv_reset(&d->active_state->conv);
                    d->active_state->valid = 1;
                    /* Drop every pending batch -- New Chat starts clean,
                     * same as it clears msg_list below. */
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
                d->show_thinking = 0;
                if (d->active_state) d->active_state->show_thinking = 0;
                KillTimer(hwnd, TIMER_HEARTBEAT);
                chat_activity_reset(&d->activity);
                thinking_history_clear(d);
                /* Clear display -- an empty msg_list is painted as the
                 * empty/no-key/no-session state (update_panel_state()). */
                chat_msg_list_clear(&d->msg_list);
                d->stream_ai_item = NULL;
                if (d->hChatList)
                    chat_listview_invalidate(d->hChatList);
                update_panel_state(d);
                /* Clear input field */
                SetWindowText(d->hInput, "");
                SetFocus(d->hInput);
                update_context_bar(d);
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
                        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
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
        case IDC_CHAT_PERMIT:
            if (d) {
                d->permit_write = !d->permit_write;
                invalidate_status_line(d);
                if (d->permit_write) {
                    /* Enabling: unblock all blocked commands, in every
                     * pending batch (rule: this must cover every card, not
                     * just the newest). */
                    if (d->active_state)
                        for (int bi = 0; bi < d->active_state->batches.count; bi++)
                            chat_approval_unblock_all(&d->active_state->batches.b[bi]->q);
                    ChatMsgItem *it = d->msg_list.head;
                    while (it) {
                        if (it->type == CHAT_ITEM_COMMAND &&
                            it->u.cmd.blocked) {
                            it->u.cmd.blocked = 0;
                            it->u.cmd.approved = -1;
                            it->u.cmd.selected = 1;  /* no longer held -- starts checked */
                        }
                        it = it->next;
                    }
                    /* Inject a corrective note so the AI knows that any
                     * previously-blocked commands are now allowed.  Without
                     * this, the AI sees the stale "blocked by read-only
                     * policy" message in its history and keeps telling the
                     * user to enable Permit Write even though they just did. */
                    if (d->conv.msg_count > 0) {
                        EnterCriticalSection(&d->cs);
                        ai_conv_add(&d->conv, AI_ROLE_USER,
                            "NOTE: The user has enabled 'Permit Write'. "
                            "Write commands are now allowed and will no longer be blocked. "
                            "Do not reference any previous security policy blocks.");
                        LeaveCriticalSection(&d->cs);
                    }
                } else {
                    /* Disabling: re-block pending write/critical commands,
                     * in every pending batch. */
                    if (d->active_state)
                        for (int bi = 0; bi < d->active_state->batches.count; bi++)
                            chat_approval_block_pending_writes(&d->active_state->batches.b[bi]->q);
                    ChatMsgItem *it = d->msg_list.head;
                    while (it) {
                        if (it->type == CHAT_ITEM_COMMAND &&
                            !it->u.cmd.settled &&
                            it->u.cmd.approved == -1 &&
                            it->u.cmd.safety > CMD_SAFE) {
                            it->u.cmd.blocked = 1;
                        }
                        it = it->next;
                    }
                }
                if (d->hChatList)
                    chat_listview_invalidate(d->hChatList);
            }
            return 0;
        case IDC_CHAT_AUTOAPPROVE:
            /* Cycle: off -> safe only -> safe + write -> all -> off. */
            if (d) {
                if (!d->approval_q.auto_approve) {
                    d->approval_q.auto_approve = 1;
                    d->approval_q.auto_approve_level = AUTO_APPROVE_SAFE;
                } else if (d->approval_q.auto_approve_level < AUTO_APPROVE_ALL) {
                    d->approval_q.auto_approve_level++;
                } else {
                    d->approval_q.auto_approve = 0;
                    d->approval_q.auto_approve_level = AUTO_APPROVE_SAFE;
                }
                invalidate_status_line(d);
                if (d->hChatList) chat_listview_invalidate(d->hChatList);
            }
            return 0;

        /* IDC_CHAT_THINKING removed - thinking toggle now inline in chat */

        /* ── Inline command approval from chat_listview ───────────── */
        default: {
            int ctl_id = LOWORD(wParam);

            /* IDC_CMD_APPROVE_BASE + index → approve single command.
             * lParam is the batch id (0 = oldest batch still needing the
             * user); idx is the command's position within that batch. */
            if (d && ctl_id >= IDC_CMD_APPROVE_BASE &&
                ctl_id < IDC_CMD_APPROVE_BASE + APPROVAL_MAX_CMDS) {
                int idx = ctl_id - IDC_CMD_APPROVE_BASE;
                CmdBatch *batch = resolve_batch(d, lParam);
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
                    dispatch_start(d, batch->id);

                    /* Check if all decided — if so, the card can settle now */
                    settle_batch_if_done(d, batch);
                }
                SetFocus(d->hInput);
                return 0;
            }

            /* IDC_CMD_DENY_BASE + index → deny single command */
            if (d && ctl_id >= IDC_CMD_DENY_BASE &&
                ctl_id < IDC_CMD_DENY_BASE + APPROVAL_MAX_CMDS) {
                int idx = ctl_id - IDC_CMD_DENY_BASE;
                CmdBatch *batch = resolve_batch(d, lParam);
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
                        settle_batch_if_done(d, batch);
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
                CmdBatch *batch = resolve_batch(d, lParam);
                if (batch) {
                    chat_approval_approve_all(&batch->q);

                    /* Sync all to approved in the matching ChatMsgItems */
                    for (ChatMsgItem *it = d->msg_list.head; it; it = it->next) {
                        if (it->type == CHAT_ITEM_COMMAND && !it->u.cmd.settled &&
                            it->u.cmd.batch == batch->id &&
                            it->u.cmd.approved == -1 && !it->u.cmd.blocked)
                            it->u.cmd.approved = 1;
                    }

                    settle_batch_if_done(d, batch);
                    if (d->hChatList) chat_listview_invalidate(d->hChatList);

                    /* Let the dispatcher send the approved commands one at a
                     * time, only once the terminal is at a prompt. */
                    dispatch_start(d, batch->id);
                }
                SetFocus(d->hInput);
                return 0;
            }

            /* IDC_CMD_APPROVE_SEL → approve only selected (ticked) commands
             * in one batch */
            if (d && ctl_id == IDC_CMD_APPROVE_SEL) {
                CmdBatch *batch = resolve_batch(d, lParam);
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
                        dispatch_start(d, batch->id);
                    } else {
                        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                                        "[no commands selected]");
                        if (d->hChatList)
                            chat_listview_invalidate(d->hChatList);
                    }
                    settle_batch_if_done(d, batch);
                }
                SetFocus(d->hInput);
                return 0;
            }

            /* IDC_CMD_CANCEL_ALL → deny all pending commands in one batch */
            if (d && ctl_id == IDC_CMD_CANCEL_ALL) {
                CmdBatch *batch = resolve_batch(d, lParam);
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
                    settle_batch_if_done(d, batch);
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
                if (d->active_state) d->active_state->busy = 0;
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
                EnterCriticalSection(&d->cs);
                if (d->conv.msg_count > 0 &&
                    d->conv.messages[d->conv.msg_count - 1].role == AI_ROLE_ASSISTANT)
                    d->conv.msg_count--;
                LeaveCriticalSection(&d->cs);

                /* Re-launch stream */
                start_indicator(d, "retrying");
                launch_stream_thread(d);
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

            /* IDC_AUTO_APPROVE → toggle session auto-approve */
            if (d && ctl_id == IDC_AUTO_APPROVE) {
                float now = (float)GetTickCount() / 1000.0f;
                chat_approval_auto_approve_click(&d->approval_q, now, 3.0f);
                if (d->hChatList) chat_listview_invalidate(d->hChatList);
                return 0;
            }

            /* IDC_CMD_EXPAND_ALL → toggle command list expand/collapse */
            if (d && ctl_id == IDC_CMD_EXPAND_ALL) {
                if (d->hChatList) chat_listview_toggle_cmd_expand(d->hChatList);
                return 0;
            }

            /* IDC_CHATLIST_PASTE → right-click paste from chat listview */
            if (d && ctl_id == IDC_CHATLIST_PASTE) {
                if (d->hInput && IsClipboardFormatAvailable(CF_TEXT) &&
                    OpenClipboard(hwnd)) {
                    HGLOBAL hg = GetClipboardData(CF_TEXT);
                    if (hg) {
                        const char *txt = (const char *)GlobalLock(hg);
                        if (txt) {
                            SendMessageA(d->hInput, EM_REPLACESEL,
                                         TRUE, (LPARAM)txt);
                            GlobalUnlock(hg);
                            SetFocus(d->hInput);
                        }
                    }
                    CloseClipboard();
                }
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
         * lParam: heap-allocated char* text (we must free) */
        if (!d) { free((void *)lParam); break; }
        char *tool_text = (char *)lParam;
        ChatItemType tool_type = (ChatItemType)(WPARAM)wParam;
        chat_msg_append(&d->msg_list, tool_type, tool_text);
        free(tool_text);
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
        if (!d) break;

        int display_dirty = 0;
        int prev_phase = d->stream_phase;  /* before this batch's chunks */

        /* Process this chunk and all queued WM_AI_STREAM messages */
        WPARAM cur_wp = wParam;
        LPARAM cur_lp = lParam;
        for (;;) {
            AiStreamChunk *chunk = (AiStreamChunk *)cur_lp;
            if (!chunk) goto next_coalesce;
            char *delta = chunk->delta;
            AiSessionState *src = chunk->session;
            free(chunk);
            if (!delta) goto next_coalesce;

            if (d->abort_stream) { free(delta); goto next_coalesce; }

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
                /* Thinking disclosure: open while reasoning is still
                 * streaming in; once the reply text starts, collapse back
                 * to the summary unless the user opened it themselves this
                 * session (show_thinking) -- see the design spec's "Thought
                 * process" section. Only decide the collapse once, right
                 * when this batch is the one that crosses into content
                 * (prev_phase < 2), so a later manual expand isn't fought.
                 * If the user has manually opened or closed *this item's*
                 * disclosure (thinking_user_set), that choice sticks for
                 * the rest of the stream -- neither the auto-open nor the
                 * auto-collapse below may touch thinking_collapsed again. */
                if (d->stream_thinking_len > 0 &&
                    !d->stream_ai_item->u.ai.thinking_user_set) {
                    if (d->stream_phase < 2) {
                        d->stream_ai_item->u.ai.thinking_collapsed = 0;
                        d->stream_ai_item->dirty = 1;
                    } else if (prev_phase < 2 && !d->show_thinking) {
                        d->stream_ai_item->u.ai.thinking_collapsed = 1;
                        d->stream_ai_item->dirty = 1;
                    }
                }
                /* Always update thinking text if we have any */
                if (d->stream_thinking_len > 0) {
                    chat_msg_set_thinking(d->stream_ai_item,
                                          d->stream_thinking);
                    if (!d->stream_ai_item->u.ai.thinking_collapsed
                        && d->stream_ai_item->u.ai.thinking_autoscroll) {
                        d->stream_ai_item->u.ai.thinking_scroll_y = 999999;
                    }
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

    case WM_AI_RESPONSE: {
        if (!d) break;
        AiResponseMsg *rmsg = (AiResponseMsg *)lParam;
        if (!rmsg) break;
        AiSessionState *src = rmsg->session;

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
         * chat_rebuild_display() replays every batch in the set the moment
         * the user switches to it, same as an active-session reply. */
        if (src != d->active_state) {
            if (wParam == 2 && src && rmsg->content) {
                char cmds[16][1024];
                int rejected = 0;
                int ncmds = ai_extract_commands_ex(rmsg->content, cmds, 16,
                                                   &rejected);
                if (ncmds > 0) {
                    ApprovalQueue bg_defaults;
                    chat_approval_init(&bg_defaults);
                    bg_defaults.auto_approve = src->auto_approve;
                    bg_defaults.auto_approve_level = src->auto_approve_level;

                    CmdBatch *batch = cmd_batch_add(&src->batches,
                                                    &bg_defaults, NULL);
                    if (batch) {
                        batch->conv_mark = src->conv.msg_count;
                        for (int ci = 0; ci < ncmds; ci++)
                            chat_approval_add(&batch->q, cmds[ci],
                                              (CmdPlatform)src->platform,
                                              d->permit_write);
                    }
                }
                if (rejected > 0) {
                    char note[256];
                    snprintf(note, sizeof(note),
                        "NOTE: %d EXEC block(s) were rejected because they "
                        "contained control characters (newline, tab, "
                        "escape). Put exactly one single-line command in "
                        "each EXEC block.", rejected);
                    EnterCriticalSection(&d->cs);
                    ai_conv_add(&src->conv, AI_ROLE_USER, note);
                    LeaveCriticalSection(&d->cs);
                }
            }
            if (src) src->busy = 0;
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
                EnterCriticalSection(&d->cs);
                ai_conv_add(&d->conv, AI_ROLE_ASSISTANT, text);
                LeaveCriticalSection(&d->cs);
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
            int rejected = 0;
            int ncmds = text ? ai_extract_commands_ex(text, cmds, 16, &rejected) : 0;

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
            if (rejected > 0) {
                char status_text[80];
                snprintf(status_text, sizeof(status_text),
                    "[%d command(s) rejected: control characters inside "
                    "an EXEC block]", rejected);
                chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS, status_text);
                if (d->hChatList) chat_listview_invalidate(d->hChatList);

                char rej_note[256];
                snprintf(rej_note, sizeof(rej_note),
                    "NOTE: %d EXEC block(s) were rejected because they "
                    "contained control characters (newline, tab, escape). "
                    "Put exactly one single-line command in each EXEC "
                    "block.", rejected);
                EnterCriticalSection(&d->cs);
                ai_conv_add(&d->conv, AI_ROLE_USER, rej_note);
                LeaveCriticalSection(&d->cs);
            }

            /* Pending command batches: earlier cards are never touched by
             * a new reply -- each reply that yields commands gets its own
             * batch and its own card (see docs/superpowers/specs/
             * 2026-09-09-pending-command-batches.md). No settle-all here. */

            if (ncmds > 0 && d->active_state) {
                /* New batch for this reply. approval_q's auto_approve/
                 * auto_approve_level are the only fields cmd_batch_add()
                 * reads from `defaults` -- they seed the new batch's queue. */
                int evicted_id = 0;
                CmdBatch *batch = cmd_batch_add(&d->active_state->batches,
                                                &d->approval_q, &evicted_id);

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

                    /* Classify and queue every extracted command in order
                     * -- including write commands when permit_write is
                     * off, which chat_approval_add marks APPROVE_BLOCKED
                     * rather than silently dropping. This keeps item order
                     * == queue order always, so index-based execution
                     * (APPROVE_SEL etc.) can never run the wrong command,
                     * and a write-only batch still shows up as a held card
                     * the user can run after switching to Read + write
                     * (see chat_approval_needs_user below). */
                    for (int ci = 0; ci < ncmds; ci++) {
                        int idx = chat_approval_add(&batch->q, cmds[ci],
                                                    (CmdPlatform)d->active_state->platform,
                                                    d->permit_write);
                        if (idx < 0) continue;  /* queue full or blank -- drop it */

                        ChatMsgItem *cmd_item = chat_msg_append(
                            &d->msg_list, CHAT_ITEM_COMMAND, "");
                        if (cmd_item) {
                            chat_msg_set_command(cmd_item, cmds[ci],
                                batch->q.entries[idx].safety,
                                batch->q.entries[idx].status == APPROVE_BLOCKED);
                            chat_msg_set_batch(cmd_item, batch->id);
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
                            if (batch->q.entries[qi].status == APPROVE_BLOCKED)
                                nblocked++;
                        if (nblocked > 0) {
                            char bmsg[2048];
                            size_t bp = 0;
                            bmsg[0] = '\0';
                            if (str_append_fmt(bmsg, sizeof(bmsg), &bp,
                                    "NOTE: The following commands were BLOCKED by "
                                    "the user's read-only security policy and were "
                                    "NOT executed:\n")) {
                                for (int qi = 0; qi < batch->q.count; qi++) {
                                    if (batch->q.entries[qi].status == APPROVE_BLOCKED) {
                                        if (!str_append_fmt(bmsg, sizeof(bmsg), &bp,
                                                "  - %s\n", batch->q.entries[qi].command))
                                            break;
                                    }
                                }
                                str_append_fmt(bmsg, sizeof(bmsg), &bp,
                                    "Do NOT claim these commands were executed. "
                                    "If the user needs these actions, tell them "
                                    "to enable 'Permit Write' and try again.");
                            }
                            EnterCriticalSection(&d->cs);
                            ai_conv_add(&d->conv, AI_ROLE_USER, bmsg);
                            LeaveCriticalSection(&d->cs);
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
                        /* Let the dispatcher send the approved commands one
                         * at a time, only once the terminal is at a prompt. */
                        dispatch_start(d, batch->id);
                    } else {
                        /* Leave the card pending and reset its container's
                         * scroll -- it stays interactive for the life of
                         * the session (rule 5). */
                        if (d->hChatList)
                            chat_listview_reset_cmd_expand(d->hChatList);
                    }
                    relayout(d);
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
            update_context_bar(d);
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
            maybe_start_next_batch(d);
        return 0;
    }

    case WM_TIMER:
        if (!d) return 0;
        if (wParam == TIMER_CMD_QUEUE) {
            dispatch_tick(d);
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
            input_sync_scroll(d);
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
            input_sync_scroll(d);
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (d && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (wParam == VK_OEM_PLUS || wParam == (WPARAM)'=') {
                chat_apply_zoom(d, 1);
                return 0;
            }
            if (wParam == VK_OEM_MINUS || wParam == (WPARAM)'-') {
                chat_apply_zoom(d, -1);
                return 0;
            }
        }
        break;

    case WM_MOUSEWHEEL:
        if (d) {
            int zdelta = GET_WHEEL_DELTA_WPARAM(wParam);
            /* Ctrl+Scroll zooms the font */
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                chat_apply_zoom(d, zdelta > 0 ? 1 : -1);
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
             * one-word status inside paint_header() is the only piece
             * that's conditional on d->activity.phase. */
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            paint_header(d, hdc);
            paint_status_line(d, hdc);
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
                draw_header_icon_button(dis, d->theme, d->dpi, NS_ICON_SAVE);
            } else if ((int)dis->CtlID == IDC_CHAT_UNDOCK) {
                draw_header_icon_button(dis, d->theme, d->dpi,
                    d->docked ? NS_ICON_UNDOCK : NS_ICON_DOCK);
            } else if ((int)dis->CtlID == IDC_CHAT_NEWCHAT) {
                draw_header_icon_button(dis, d->theme, d->dpi, NS_ICON_NEW_CHAT);
            }
            /* Old IDC_CHAT_ALLOW / IDC_CHAT_DENY draw code removed —
             * approval buttons are now inline in chat_listview. The
             * owner-drawn Permit Write / Auto Approve "tab" buttons are
             * gone too -- both are shown/clicked in the status line
             * instead (WM_PAINT / ai_chat_status_hit()). */
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
            /* Signal any running stream thread to abort before cleanup */
            d->abort_stream = 1;
            if (d->active_state) d->active_state->busy = 0;

            KillTimer(hwnd, TIMER_SCROLL_SYNC);
            KillTimer(hwnd, TIMER_HEARTBEAT);
            KillTimer(hwnd, TIMER_CMD_QUEUE);
            d->dispatch_active = 0;
            /* Save conversation back to session before cleanup */
            if (d->active_state) {
                ai_conv_move(&d->active_state->conv, &d->conv);
                d->active_state->valid = 1;
            }
            thinking_history_clear(d);
            ai_attachment_free(&d->pending_attachment);
            chat_msg_list_clear(&d->msg_list);
            d->stream_ai_item = NULL;
            DeleteCriticalSection(&d->cs);
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
    /* Load RichEdit control library (still needed for input field) */
    LoadLibrary("Riched20.dll");
    LoadLibrary("Msftedit.dll");

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

    InitializeCriticalSection(&d->cs);
    d->indicator_pos = -1;
    d->stream_display_start = -1;
    d->forced_state = -1;
    d->paste_delay_ms = paste_delay_ms;
    d->context_lines = AI_CONTEXT_LINES_DEFAULT;
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
        ai_conv_move(&d->conv, &initial_state->conv);
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
        DeleteCriticalSection(&d->cs);
        free(d);
    }

    return hwnd;
}

void ai_chat_set_session(HWND hwnd, Terminal *term, SSHChannel *channel)
{
    if (!hwnd) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    d->active_term = term;
    d->active_channel = channel;
    update_panel_state(d);
}

void ai_chat_force_state(HWND hwnd, int state_id)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    d->forced_state = state_id;
    update_panel_state(d);
}

void ai_chat_update_key(HWND hwnd, const char *api_key, const char *provider,
                        const char *custom_url, const char *custom_model)
{
    if (!hwnd) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;

    EnterCriticalSection(&d->cs);
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
    LeaveCriticalSection(&d->cs);

    /* Update model name on chat listview */
    if (d->hChatList)
        chat_listview_set_model(d->hChatList, d->conv.model);

    update_panel_state(d);
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
    chat_register_tools(d,
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
    EnterCriticalSection(&d->cs);
    if (session_notes)
        strncpy(d->session_notes, session_notes, sizeof(d->session_notes) - 1);
    if (system_notes)
        strncpy(d->system_notes, system_notes, sizeof(d->system_notes) - 1);
    LeaveCriticalSection(&d->cs);
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

/* Seed state->auto_approve/auto_approve_level from d->auto_approve_default
 * if (and only if) this session's approval state has never been set --
 * a session the user has already toggled (or that was already seeded)
 * keeps its own choice. level0to3: 0 = off, 1..3 = on with level 0..2. */
static void ai_chat_seed_auto_approve(AiChatData *d, AiSessionState *state)
{
    if (!state || state->auto_approve_seeded) return;
    int level0to3 = d->auto_approve_default;
    state->auto_approve = level0to3 > 0 ? 1 : 0;
    state->auto_approve_level = level0to3 > 0 ? (level0to3 - 1) : AUTO_APPROVE_SAFE;
    state->auto_approve_seeded = 1;
}

void ai_chat_set_auto_approve_default(HWND hwnd, int level0to3)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    if (level0to3 < 0) level0to3 = 0;
    if (level0to3 > 3) level0to3 = 3;
    d->auto_approve_default = level0to3;
    /* Apply immediately to the active session if it hasn't been seeded yet
     * (e.g. the panel was just created and this is the first call). A
     * session the user has already touched is left alone. */
    if (d->active_state) {
        ai_chat_seed_auto_approve(d, d->active_state);
        d->approval_q.auto_approve = d->active_state->auto_approve;
        d->approval_q.auto_approve_level = d->active_state->auto_approve_level;
        invalidate_status_line(d);
    }
}

void ai_chat_set_context_lines(HWND hwnd, int lines)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;
    d->context_lines = ai_context_clamp_lines(lines);
}

/* Internal helper: perform the actual session switch (save/load/rebuild).
 * Safe to call even while busy — each thread targets its own session. */
static void do_session_switch(AiChatData *d,
                              AiSessionState *new_state,
                              Terminal *term, SSHChannel *channel,
                              const char *session_notes,
                              const char *system_notes,
                              const char *session_name)
{
    /* Save current conversation to old session state.
     * Skip if the session is busy — the thread will commit to its own conv. */
    if (d->active_state && d->active_state != new_state &&
        !d->active_state->busy) {
        ai_conv_move(&d->active_state->conv, &d->conv);
        d->active_state->valid = 1;
    }

    /* Kill command timers — they belong to the old session. The dispatcher
     * itself is per-panel, not per-session, so switching away just stops
     * it silently (no "[command queue stopped]" -- that's the Stop button's
     * message); dispatch_cancel() denies whatever was left APPROVED in the
     * old session's running batch (so a later dispatch_start() can't
     * resurrect it), settles and removes just that one batch, and restores
     * the Send button. Every other pending batch in the old session stays
     * in its CmdBatchSet untouched -- chat_rebuild_display() re-creates
     * their cards when the user switches back (rule 6). */
    KillTimer(d->hwnd, TIMER_CMD_QUEUE);
    dispatch_cancel(d, NULL);

    /* Save auto-approve, show-thinking and activity phase to old session */
    if (d->active_state && d->active_state != new_state) {
        d->active_state->auto_approve = d->approval_q.auto_approve;
        d->active_state->auto_approve_level = d->approval_q.auto_approve_level;
        d->active_state->auto_approve_seeded = 1;
        d->active_state->show_thinking = d->show_thinking;
        d->active_state->activity_phase = (int)d->activity.phase;
    }

    /* Clear thinking history — it belongs to the old session */
    thinking_history_clear(d);

    /* Load new session's conversation */
    if (new_state && new_state != d->active_state) {
        if (new_state->valid) {
            ai_conv_move(&d->conv, &new_state->conv);
        } else {
            char model[64];
            strncpy(model, d->conv.model, sizeof(model) - 1);
            model[sizeof(model) - 1] = '\0';
            ai_conv_init(&d->conv, model);
        }
    }

    d->active_state = new_state;
    d->active_term = term;
    d->active_channel = channel;

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

    /* Restore auto-approve, show-thinking and activity phase from new session.
     * A session that has never had its approval state set (fresh tab) is
     * seeded from the configured default first. */
    if (new_state) {
        ai_chat_seed_auto_approve(d, new_state);
        d->approval_q.auto_approve = new_state->auto_approve;
        d->approval_q.auto_approve_level = new_state->auto_approve_level;
        d->show_thinking = new_state->show_thinking;
        chat_activity_set_phase(&d->activity,
                                (ActivityPhase)new_state->activity_phase, 0.0f);
    } else {
        d->approval_q.auto_approve = 0;
        d->approval_q.auto_approve_level = AUTO_APPROVE_SAFE;
        d->show_thinking = 0;
        chat_activity_reset(&d->activity);
    }

    relayout(d);

    /* chat_rebuild_display() replays the conversation and re-creates a
     * card for every batch still in new_state->batches (rule 6) --
     * ai_build_confirm_text()'s old single-snapshot summary line is gone
     * along with it. */
    chat_rebuild_display(d);
    update_panel_state(d);

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

    if (d->hChatList) {
        chat_listview_invalidate(d->hChatList);
        chat_listview_scroll_to_bottom(d->hChatList);
    }
}

void ai_chat_switch_session(HWND hwnd,
                            AiSessionState *new_state,
                            Terminal *term, SSHChannel *channel,
                            const char *session_notes,
                            const char *system_notes,
                            const char *session_name)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    AiChatData *d = (AiChatData *)(LONG_PTR)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return;

    /* If the active session is busy, save the current conversation so the
     * thread can commit its result on top of this snapshot. */
    if (d->active_state && d->active_state->busy) {
        ai_conv_move(&d->active_state->conv, &d->conv);
        d->active_state->valid = 1;
    }

    /* Kill any indicator timer — it belongs to the old session's display */
    if (d->indicator_pos >= 0) {
        KillTimer(hwnd, TIMER_THINKING);
        d->indicator_pos = -1;
    }

    /* Switch immediately — any running thread continues in the background
     * targeting its own session directly. */
    do_session_switch(d, new_state, term, channel,
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
        KillTimer(d->hwnd, TIMER_CMD_QUEUE);
        dispatch_cancel(d, NULL);
    }

    /* Free every pending batch (the caller frees the AiSessionState itself
     * right after this call returns). */
    cmd_batch_set_free(&state->batches);

    /* Free per-session stream buffers */
    free(state->stream_content);
    state->stream_content = NULL;
    free(state->stream_thinking);
    state->stream_thinking = NULL;
    state->busy = 0;

    if (d->active_state == state)
        d->active_state = NULL;
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

    /* Pending command batches: build real CmdBatch entries (real, unique
     * ids) in the demo session's own batch set from the canned queues --
     * approval for the state's own batch, approval2 for "batches"'s
     * second one -- instead of touching d->approval_q. chat_rebuild_display()
     * below replays every batch still in the set exactly the way a live
     * session switch does (append_batch_command_items()), so this reuses
     * the same code path a real reply's cards come from. */
    int any_executing = 0;
    if (d->active_state) {
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
    chat_rebuild_display(d);

    /* The demo session has no channel (window.c's create_demo_session
     * passes NULL), so update_panel_state() would otherwise read every
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

    /* Gallery: "chat" shows the Thinking disclosure collapsed (the normal
     * post-reply state); "all" shows it expanded so the review set covers
     * both forms per the design spec's "Thought process" section. Setting
     * the item's own flag (rather than d->show_thinking) is the smallest
     * change -- chat_rebuild_display() always creates it collapsed. */
    if (state && strcmp(state, "all") == 0) {
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

    relayout(d);
    if (d->hChatList) {
        chat_listview_invalidate(d->hChatList);
        /* "chat"/"all": the markdown reply is long enough to push the
         * Thinking disclosure (right above it) off the top of the thread
         * if scrolled to the bottom -- chat_rebuild_display() above always
         * leaves it scrolled to the bottom, so pull it back to the top
         * here for these two states so the gallery review set actually
         * shows the disclosure (collapsed in "chat", expanded in "all").
         * Every other state keeps the bottom scroll, to show its most
         * recent activity. */
        if (state && (strcmp(state, "chat") == 0 || strcmp(state, "all") == 0))
            chat_listview_scroll_to_top(d->hChatList);
    }
}

void ai_chat_close(HWND hwnd)
{
    if (hwnd && IsWindow(hwnd))
        DestroyWindow(hwnd);
}

int ai_chat_has_content(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return 0;
    AiChatData *d = (AiChatData *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!d) return 0;
    /* At least one user or assistant message beyond the system prompt */
    return d->conv.msg_count > 1;
}

#endif /* _WIN32 */
