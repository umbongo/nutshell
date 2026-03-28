#ifdef _WIN32
#include <winsock2.h>   /* Must come before windows.h */
#endif

#include "ai_chat.h"

#ifdef _WIN32

#include "ai_prompt.h"
#include "ai_http.h"
#include "app_font.h"
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
#include "chat_listview.h"
#include "dpi_util.h"
#include <windowsx.h>  /* GET_X_LPARAM, GET_Y_LPARAM */
#include <stdio.h>
#include <string.h>
#include <process.h>
#include <richedit.h>
#include <commctrl.h>
#include <commdlg.h>

static const char *AI_CHAT_CLASS = "Nutshell_AIChat";

#define IDC_CHAT_DISPLAY  4001
#define IDC_CHAT_INPUT    4002
#define IDC_CHAT_SEND     4003
#define IDC_CHAT_NEWCHAT  4004
#define IDC_CHAT_PERMIT   4005
#define IDC_CHAT_THINKING 4006
#define IDC_CONTEXT_BAR   4007
#define IDC_CONTEXT_LABEL 4008
#define IDC_SESSION_LABEL 4009
#define IDC_CHAT_SAVE     4010
#define IDC_CHAT_ALLOW    4011
#define IDC_THINKING_BOX  4014
#define IDC_CHAT_DENY     4012
#define IDC_CHAT_UNDOCK   4013
#define IDC_CHAT_AUTOAPPROVE 4015

#define WM_AI_RESPONSE   (WM_USER + 100)
#define WM_AI_CONTINUE   (WM_USER + 101)
#define WM_AI_STREAM     (WM_USER + 102)  /* wParam: 0=thinking, 1=content; lParam: char* */

#define TERM_CONTEXT_ROWS 50
#define CONTINUE_DELAY_MS 2000  /* Wait for terminal output before continuing */
#define TIMER_CONTINUE    1
#define TIMER_CMD_QUEUE   2     /* Delayed command execution (paste delay) */
#define TIMER_SCROLL_SYNC 4     /* Sync custom scrollbar with RichEdit */
#define TIMER_THINKING    5     /* Animated thinking indicator */
#define TIMER_HEARTBEAT   6     /* Activity monitor heartbeat (1s) */
#define THINKING_ANIM_MS  400   /* Dot animation interval */
#define HEARTBEAT_MS      1000  /* Heartbeat interval */

/* Forward declaration for input subclass */
static LRESULT CALLBACK InputSubclassProc(HWND hwnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam,
                                           UINT_PTR uIdSubclass,
                                           DWORD_PTR dwRefData);

typedef struct {
    HWND hwnd;
    HWND hDisplay;
    HWND hThinkingBox;  /* Embedded textbox for thinking/processing content */
    HWND hInput;
    HWND hSendBtn;
    HWND hNewChatBtn;
    HWND hPermitBtn;
    HWND hAllowAllBtn;
    HWND hSaveBtn;
    HWND hUndockBtn;
    /* Old floating Allow/Deny buttons removed — now inline in chat_listview */
    ApprovalQueue approval_q;
    HWND hThinkingBtn;
    HWND hTooltip;        /* Win32 tooltip control */
    int permit_write;     /* 0 = read-only (red), 1 = read/write (green) */
    int show_thinking;    /* 0 = hide reasoning, 1 = show reasoning */
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
    int commands_executed;   /* number of commands just executed */
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
    char queued_cmds[16][1024];
    int queued_count;
    int queued_next;       /* index of next command to execute */
    int pending_approval;  /* 1 = waiting for user to Allow/Deny commands */
    int dpi;
    int stream_phase;  /* 0=not started, 1=in thinking, 2=in content */

    /* AI notes for system prompt context */
    char session_notes[2560];
    char system_notes[2560];

    /* Per-session conversation tracking */
    AiSessionState *active_state;     /* points to current session's ai_state */

    /* Session name label */
    HWND hSessionLabel;
    char session_name[256];

    /* Custom scrollbar for chat display */
    HWND hDisplayScrollbar;
    int  display_line_h;   /* cached line height in px */

    /* Context window usage bar */
    HWND hContextBar;
    HWND hContextLabel;       /* kept for cleanup but hidden — text drawn by subclass */
    int  context_limit;       /* token limit for model, 0=unknown */
    char context_label[64];   /* text drawn on progress bar by subclass */

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

    /* Compact button mode: 1 = icon-only buttons when frame is narrow */
    int compact_buttons;

    /* Fluent UI Icon Font */
    HFONT hIconFont;

    /* New chat list view fields */
    ChatMsgList msg_list;           /* Message item linked list */
    HWND hChatList;                 /* Owner-drawn chat list view */
    ChatMsgItem *stream_ai_item;    /* Current AI item being streamed into */

    /* Owned fonts for ChatListView (caller manages lifetime) */
    HFONT hBoldFont;
    HFONT hMonoFont;

    /* Activity monitor state */
    ActivityState activity;
    int pulse_toggle;          /* 0/1 for pulsing dot animation */

    /* Stream abort: UI thread sets to 1, stream callback checks it */
    volatile int abort_stream;
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

    int rc = ai_parse_stream_chunk(json, content_delta, sizeof(content_delta),
                                   thinking_delta, sizeof(thinking_delta));

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

/* Background thread: streaming AI API call.
 * Receives a heap-allocated AiStreamThreadArg and frees it before returning. */
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

    char auth[300];
    (void)snprintf(auth, sizeof(auth), "Bearer %s", arg->api_key);

    StreamContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.hwnd = arg->hwnd;
    ctx.target = arg->target;
    ctx.abort_flag = arg->abort_flag;

    int status = 0;
    char errbuf[256] = "";
    int rc = ai_http_post_stream(url, auth, arg->body, arg->body_len,
                                 stream_callback, &ctx,
                                 &status, errbuf, sizeof(errbuf));

    /* If stream was aborted (user cancelled), discard partial results */
    if (arg->abort_flag && *arg->abort_flag) {
        free(arg);
        return 0;
    }

    if (rc != 0 || status < 200 || status >= 300) {
        char msg[1024];
        if (rc != 0 && errbuf[0])
            snprintf(msg, sizeof(msg), "HTTP error: %s", errbuf);
        else
            snprintf(msg, sizeof(msg), "HTTP %d: streaming request failed", status);
        post_error_response(arg->hwnd, arg->target, msg);
        free(arg);
        return 0;
    }

    /* Add assistant message to the target session's conversation. */
    EnterCriticalSection(arg->cs);
    ai_conv_add(&arg->target->conv, AI_ROLE_ASSISTANT, ctx.full_content);
    arg->target->valid = 1;
    LeaveCriticalSection(arg->cs);

    /* Signal stream done — wParam=2 means "streaming complete, do command extraction".
     * busy is cleared by the WM_AI_RESPONSE handler on the UI thread to prevent
     * a race where the user sends a new message before cleanup completes. */
    AiResponseMsg *rmsg = (AiResponseMsg *)calloc(1, sizeof(*rmsg));
    if (rmsg) {
        rmsg->session = arg->target;
        rmsg->content = _strdup(ctx.full_content);
        rmsg->thinking = (ctx.full_thinking[0] != '\0') ? _strdup(ctx.full_thinking) : NULL;
        PostMessage(arg->hwnd, WM_AI_RESPONSE, 2, (LPARAM)rmsg);
    } else {
        /* Fallback: can't allocate — clear busy here */
        arg->target->busy = 0;
    }
    free(arg);
    return 0;
}


/* Subclass proc for the context progress bar — draws label text on top of
 * the bar after the default paint.  Avoids the fragile transparent-STATIC
 * overlay that loses its text on parent/sibling repaints. */
#define CONTEXT_BAR_SUBCLASS_ID 99
static LRESULT CALLBACK ContextBarSubclass(HWND hwnd, UINT msg,
    WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (msg == WM_PAINT) {
        LRESULT r = DefSubclassProc(hwnd, msg, wParam, lParam);
        AiChatData *d = (AiChatData *)dwRefData;
        if (d && d->context_label[0] && d->theme) {
            HDC hdc = GetDC(hwnd);
            RECT rc;
            GetClientRect(hwnd, &rc);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, theme_cr(d->theme->text_main));
            HFONT oldFont = (HFONT)SelectObject(hdc, d->hFont);
            DrawTextA(hdc, d->context_label, -1, &rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldFont);
            ReleaseDC(hwnd, hdc);
        }
        return r;
    }
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, ContextBarSubclass, uIdSubclass);
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void update_context_bar(AiChatData *d)
{
    if (!d || !d->hContextBar) return;
    
    LONG style = GetWindowLong(d->hContextBar, GWL_STYLE);
    if (style & PBS_MARQUEE) {
        SendMessage(d->hContextBar, PBM_SETMARQUEE, 0, 0);
        SetWindowLong(d->hContextBar, GWL_STYLE, style & ~PBS_MARQUEE);
    }

    if (d->context_limit <= 0) {
        SendMessage(d->hContextBar, PBM_SETPOS, 0, 0);
        ai_format_context_label(0, 0, d->context_label,
                                sizeof(d->context_label));
        InvalidateRect(d->hContextBar, NULL, TRUE);
        EnableWindow(d->hContextBar, FALSE);
        return;
    }
    int tokens = ai_context_estimate_tokens(&d->conv);
    int pct = (tokens * 100) / d->context_limit;
    if (pct > 100) pct = 100;
    SendMessage(d->hContextBar, PBM_SETPOS, (WPARAM)pct, 0);
    SendMessage(d->hContextBar, PBM_SETBARCOLOR, 0,
                (LPARAM)(pct > 80 ? RGB(220, 50, 50) :
                         pct > 50 ? RGB(220, 180, 50) : RGB(50, 180, 50)));
    ai_format_context_label(tokens, d->context_limit,
                            d->context_label, sizeof(d->context_label));
    InvalidateRect(d->hContextBar, NULL, TRUE);
    EnableWindow(d->hContextBar, TRUE);
}

/* Start (or replace) the animated indicator with the given base text.
 * Replaces the usual Context progress bar with a Marquee while busy. */
static void start_indicator(AiChatData *d, const char *base)
{
    if (d->hContextBar) {
        LONG style = GetWindowLong(d->hContextBar, GWL_STYLE);
        SetWindowLong(d->hContextBar, GWL_STYLE, style | PBS_MARQUEE);
        SendMessage(d->hContextBar, PBM_SETMARQUEE, 1, 50);
        /* Red/Orange color for Marquee */
        SendMessage(d->hContextBar, PBM_SETBARCOLOR, 0, (LPARAM)RGB(220, 100, 50));
        EnableWindow(d->hContextBar, TRUE);
    }
    if (strcmp(base, "thinking") == 0)
        d->context_label[0] = '\0';   /* inline indicator shows timing */
    else
        snprintf(d->context_label, sizeof(d->context_label), "%c%s",
            base[0] >= 'a' && base[0] <= 'z' ? (char)(base[0]-32) : base[0],
            base + 1);
    InvalidateRect(d->hContextBar, NULL, TRUE);
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

/* Rebuild the chat display from the conversation history.
 * Used when switching sessions to replay the loaded conversation.
 * Populates the ChatMsgList and invalidates the ChatListView. */
static void chat_rebuild_display(AiChatData *d)
{
    if (!d) return;

    chat_msg_list_clear(&d->msg_list);
    d->stream_ai_item = NULL;

    /* Add welcome status message */
    chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
        "AI Assist - Type a message and press Enter or click Send.\n"
        "The AI can see your terminal and execute commands.");

    /* Replay messages, skipping the system prompt at index 0 */
    for (int i = 1; i < d->conv.msg_count; i++) {
        const AiMessage *msg = &d->conv.messages[i];

        if (msg->role == AI_ROLE_USER) {
            chat_msg_append(&d->msg_list, CHAT_ITEM_USER, msg->content);
        } else if (msg->role == AI_ROLE_ASSISTANT) {
            ChatMsgItem *item = chat_msg_append(&d->msg_list,
                                                 CHAT_ITEM_AI_TEXT,
                                                 msg->content);
            if (item && d->thinking_history[i] &&
                d->thinking_history[i][0]) {
                chat_msg_set_thinking(item, d->thinking_history[i]);
                item->u.ai.thinking_complete = 1;
            }
        }
        /* Skip system messages injected mid-conversation */
    }

    if (d->hChatList) {
        chat_listview_invalidate(d->hChatList);
        chat_listview_scroll_to_bottom(d->hChatList);
    }
    update_context_bar(d);
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
    arg->body_len = ai_build_request_body_ex(&d->conv, arg->body,
                                              sizeof(arg->body), 1);
    LeaveCriticalSection(&d->cs);

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

/* Mark all current command items as settled so they render inline
 * and are excluded from the active command container. */
static void settle_all_commands(ChatMsgList *list)
{
    ChatMsgItem *it = list->head;
    while (it) {
        if (it->type == CHAT_ITEM_COMMAND && !it->u.cmd.settled) {
            it->u.cmd.settled = 1;
            it->dirty = 1;
        }
        it = it->next;
    }
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
    KillTimer(d->hwnd, TIMER_CONTINUE);
    KillTimer(d->hwnd, TIMER_CMD_QUEUE);
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
    d->commands_executed = 0;
    d->pending_approval = 0;
    d->queued_count = 0;
    d->queued_next = 0;

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

static void send_user_message(AiChatData *d)
{
    if (!d || !d->active_state || d->active_state->busy || d->pending_approval)
        return;

    char input[2048];
    GetWindowText(d->hInput, input, (int)sizeof(input));
    if (input[0] == '\0') return;

    SetWindowText(d->hInput, "");

    /* Display user message in ChatListView */
    chat_msg_append(&d->msg_list, CHAT_ITEM_USER, input);
    if (d->hChatList) {
        chat_listview_invalidate(d->hChatList);
        chat_listview_scroll_to_bottom(d->hChatList);
    }

    /* Extract terminal context */
    char term_text[8192] = "";
    if (d->active_term) {
        term_extract_last_n(d->active_term, TERM_CONTEXT_ROWS,
                           term_text, sizeof(term_text));
    }

    EnterCriticalSection(&d->cs);

    /* On first message, add system prompt */
    if (d->conv.msg_count == 0) {
        char sys_prompt[AI_MSG_MAX];
        ai_build_system_prompt(sys_prompt, sizeof(sys_prompt), term_text,
                               d->session_notes, d->system_notes);
        ai_conv_add(&d->conv, AI_ROLE_SYSTEM, sys_prompt);
    } else if (d->active_term) {
        /* Update system prompt with fresh terminal context */
        char sys_prompt[AI_MSG_MAX];
        ai_build_system_prompt(sys_prompt, sizeof(sys_prompt), term_text,
                               d->session_notes, d->system_notes);
        /* Replace the first (system) message */
        snprintf(d->conv.messages[0].content,
                 sizeof(d->conv.messages[0].content), "%s", sys_prompt);
    }

    ai_conv_add(&d->conv, AI_ROLE_USER, input);
    LeaveCriticalSection(&d->cs);

    update_context_bar(d);

    d->stream_display_start = -1;
    start_indicator(d, "thinking");

    launch_stream_thread(d);
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

    /* Clear any existing text on the line before pasting:
       Ctrl+E (end of line) + Ctrl+U (kill to start of line) */
    ssh_channel_write(d->active_channel, "\x05\x15", 2);

    /* Send command + CR to SSH channel (CR = Enter key, same as WM_CHAR) */
    ssh_channel_write(d->active_channel, cmd, (size_t)strlen(cmd));
    ssh_channel_write(d->active_channel, "\r", 1);

}

static void send_continue_message(AiChatData *d)
{
    if (!d || !d->active_state || d->active_state->busy) return;

    /* Extract fresh terminal context after command execution */
    char term_text[8192] = "";
    if (d->active_term) {
        term_extract_last_n(d->active_term, TERM_CONTEXT_ROWS,
                           term_text, sizeof(term_text));
    }

    EnterCriticalSection(&d->cs);

    /* Update system prompt with fresh terminal context */
    if (d->conv.msg_count > 0 && d->active_term) {
        char sys_prompt[AI_MSG_MAX];
        ai_build_system_prompt(sys_prompt, sizeof(sys_prompt), term_text,
                               d->session_notes, d->system_notes);
        snprintf(d->conv.messages[0].content,
                 sizeof(d->conv.messages[0].content), "%s", sys_prompt);
    }

    ai_conv_add(&d->conv, AI_ROLE_USER,
        "The commands above have been executed. Look at the updated terminal "
        "output and continue with any remaining tasks from my original request. "
        "If there are more commands to run, include ALL of them now. "
        "If everything is done, just summarize what was accomplished.");

    LeaveCriticalSection(&d->cs);

    start_indicator(d, "continuing");

    launch_stream_thread(d);
}

static void input_sync_scroll(AiChatData *d);

/* Subclass proc for the multiline input: Enter sends, Shift+Enter inserts newline */
static LRESULT CALLBACK InputSubclassProc(HWND hwnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam,
                                           UINT_PTR uIdSubclass,
                                           DWORD_PTR dwRefData)
{
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
    if (msg == WM_MOUSEWHEEL) {
        int zdelta = GET_WHEEL_DELTA_WPARAM(wParam);
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

/* Match the tab strip layout constants for consistent appearance (base values at 96 DPI) */
#define AI_INDICATOR_W_BASE   12  /* same as INDICATOR_W in tabs.c */
#define AI_INDICATOR_GAP_BASE  3  /* same as INDICATOR_GAP in tabs.c */

/* Draw a tab-style button: same shape, border, font and indicator style
 * as the session tabs in the main window's tab strip. */
static void draw_tab_button(LPDRAWITEMSTRUCT dis, const ThemeColors *theme,
                             AiChatData *d)
{
    if (!dis || !theme) return;
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    int btnH = rc.bottom - rc.top;
    int pressed = (dis->itemState & ODS_SELECTED) != 0;

    COLORREF bg = theme_cr(pressed ? theme->bg_primary : theme->bg_secondary);
    COLORREF fg = theme_cr(theme->text_main);
    COLORREF border_cr = theme_cr(theme->border);

    /* Clear with parent bg so rounded corners are clean */
    HBRUSH hParentBr = CreateSolidBrush(theme_cr(theme->bg_primary));
    FillRect(hdc, &rc, hParentBr);
    DeleteObject(hParentBr);

    /* Rounded rect background + border — radius 6 matches tabs */
    HBRUSH hBr = CreateSolidBrush(bg);
    HPEN hPen = CreatePen(PS_SOLID, 1, border_cr);
    HGDIOBJ oldBr = SelectObject(hdc, hBr);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    DeleteObject(hPen);
    DeleteObject(hBr);

    /* Indicator height: same formula as tabs — button height minus 10px */
    int indicH = btnH - 10;
    if (indicH < 4) indicH = 4;
    int indY = rc.top + (btnH - indicH) / 2;

    /* For Permit Write / Allow All indicator: draw status indicator */
    int text_left = rc.left;
    if (((int)dis->CtlID == IDC_CHAT_AUTOAPPROVE) && d) {
        int is_active = d->approval_q.auto_approve;
        int indW = MulDiv(AI_INDICATOR_W_BASE, d->dpi, 96);
        int indGap = MulDiv(AI_INDICATOR_GAP_BASE, d->dpi, 96);
        int indX = rc.left + indGap;
        COLORREF dot_col = is_active ? RGB(0, 160, 80) : RGB(128, 128, 128);
        HBRUSH hDot = CreateSolidBrush(dot_col);
        HPEN hDotPen = CreatePen(PS_SOLID, 1, dot_col);
        HGDIOBJ oBr = SelectObject(hdc, hDot);
        HGDIOBJ oPen = SelectObject(hdc, hDotPen);
        RoundRect(hdc, indX, indY,
                  indX + indW, indY + indicH, 3, 3);
        SelectObject(hdc, oPen);
        SelectObject(hdc, oBr);
        DeleteObject(hDotPen);
        DeleteObject(hDot);

        /* Draw letter on indicator: A for Allow All */
        {
            const char *letter = "A";
            HFONT hSmall = CreateFont(
                -MulDiv(8, d->dpi, 72), 0, 0, 0, FW_BOLD,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                "Segoe UI");
            HFONT hOldF = (HFONT)SelectObject(hdc, hSmall);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            RECT indRect = { indX, indY, indX + indW, indY + indicH };
            DrawText(hdc, letter, 1, &indRect,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, hOldF);
            DeleteObject(hSmall);
        }

        text_left = indX + indW + indGap;
    }
    if (((int)dis->CtlID == IDC_CHAT_PERMIT) && d) {
        int is_active = d->permit_write;
        int indW = MulDiv(AI_INDICATOR_W_BASE, d->dpi, 96);
        int indGap = MulDiv(AI_INDICATOR_GAP_BASE, d->dpi, 96);
        int indX = rc.left + indGap;
        COLORREF dot_col = is_active ? RGB(0, 160, 80) : RGB(128, 128, 128);
        HBRUSH hDot = CreateSolidBrush(dot_col);
        HPEN hDotPen = CreatePen(PS_SOLID, 1, dot_col);
        HGDIOBJ oBr = SelectObject(hdc, hDot);
        HGDIOBJ oPen = SelectObject(hdc, hDotPen);
        RoundRect(hdc, indX, indY,
                  indX + indW, indY + indicH, 3, 3);
        SelectObject(hdc, oPen);
        SelectObject(hdc, oBr);
        DeleteObject(hDotPen);
        DeleteObject(hDot);

        /* Draw letter on indicator: W for write */
        {
            const char *letter = "W";
            HFONT hSmall = CreateFont(
                -MulDiv(8, d->dpi, 72), 0, 0, 0, FW_BOLD,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                "Segoe UI");
            HFONT hOldF = (HFONT)SelectObject(hdc, hSmall);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            RECT indRect = { indX, indY, indX + indW, indY + indicH };
            DrawText(hdc, letter, 1, &indRect,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, hOldF);
            DeleteObject(hSmall);
        }

        text_left = indX + indW + indGap;
    }

    /* Text — skip for indicator buttons when in compact mode (icon-only) */
    int is_indicator_btn = ((int)dis->CtlID == IDC_CHAT_PERMIT ||
                            (int)dis->CtlID == IDC_CHAT_AUTOAPPROVE ||
                            (int)dis->CtlID == IDC_CHAT_NEWCHAT);
    if (!d || !d->compact_buttons || !is_indicator_btn) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, fg);

        HFONT hOldFont = NULL;
        if (d && d->hFont)
            hOldFont = (HFONT)SelectObject(hdc, d->hFont);

        wchar_t text[64];
        GetWindowTextW(dis->hwndItem, text, (int)(sizeof(text)/sizeof(text[0])));
        RECT rcText = rc;
        rcText.left = text_left;
        rcText.right -= MulDiv(AI_INDICATOR_GAP_BASE, d ? d->dpi : 96, 96);
        DrawTextW(hdc, text, -1, &rcText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        if (hOldFont) SelectObject(hdc, hOldFont);
    } else if (d && d->compact_buttons &&
               (int)dis->CtlID == IDC_CHAT_NEWCHAT) {
        /* Compact mode: draw a "+" icon for New Chat */
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, fg);
        HFONT hBold = CreateFont(
            -MulDiv(10, d->dpi, 72), 0, 0, 0, FW_BOLD,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
            APP_FONT_UI_FACE);
        HFONT hOldF = (HFONT)SelectObject(hdc, hBold);
        DrawText(hdc, "+", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, hOldF);
        DeleteObject(hBold);
    }
}

/* Reposition all child controls.  Called from WM_SIZE and when
 * the approval bar is shown / hidden so the display shrinks to
 * make room for the Allow / Deny buttons. */
static void relayout(AiChatData *d)
{
    if (!d || !d->hwnd) return;
    RECT rc;
    GetClientRect(d->hwnd, &rc);
    int cw = rc.right;
    int ch = rc.bottom;
    #define S(px) MulDiv((px), d->dpi, 96)
    int btn_h = S(24);
    int pad = S(4);
    int top_y = pad + btn_h + pad;
    int input_h = S(46);
    int margin = S(5);
    int send_w = S(40);
    int approve_h = d->pending_approval ? (btn_h + pad) : 0;

    /* Right-side icon buttons (always shown) */
    int right_w = pad + btn_h + pad + btn_h + pad;
    if (d->hSaveBtn)
        MoveWindow(d->hSaveBtn, cw - pad - btn_h, pad, btn_h, btn_h, TRUE);
    if (d->hUndockBtn)
        MoveWindow(d->hUndockBtn, cw - pad - btn_h - pad - btn_h, pad,
                   btn_h, btn_h, TRUE);

    /* Decide if left-side buttons fit with full text or need compact mode.
     * Full: New Chat (78) + Permit Write (115) + Auto Approve (115)
     * Compact: New Chat (78) + indicator-only (btn_h) + indicator-only (btn_h) */
    int full_w = pad + S(78) + pad + S(115) + pad + S(115);
    int avail = cw - right_w;
    d->compact_buttons = (full_w > avail);
    int pw = d->compact_buttons ? btn_h : S(115);
    int nw = d->compact_buttons ? btn_h : S(78);
    int aw = d->compact_buttons ? btn_h : S(115);

    if (d->hNewChatBtn)
        MoveWindow(d->hNewChatBtn, pad, pad, nw, btn_h, TRUE);
    if (d->hPermitBtn)
        MoveWindow(d->hPermitBtn, pad + nw + pad, pad, pw, btn_h, TRUE);
    if (d->hAllowAllBtn)
        MoveWindow(d->hAllowAllBtn, pad + nw + pad + pw + pad, pad, aw, btn_h, TRUE);
    {
        int bar_h = S(16);
        int ctx_w = S(180);
        int label_w = cw - ctx_w - pad * 3;
        if (d->hSessionLabel)
            MoveWindow(d->hSessionLabel, pad, top_y, label_w, bar_h, TRUE);
        if (d->hContextBar)
            MoveWindow(d->hContextBar, cw - ctx_w - pad, top_y, ctx_w, bar_h, TRUE);
        top_y += bar_h + pad;
    }
    {
        int disp_w, disp_h;
        ai_dock_chat_layout(cw, ch, top_y, input_h, approve_h, margin,
                            CSB_WIDTH, &disp_w, &disp_h);

        /* Display takes full calculated height */
        if (d->hDisplay)
            MoveWindow(d->hDisplay, margin, top_y, disp_w, disp_h, TRUE);
        if (d->hDisplayScrollbar)
            MoveWindow(d->hDisplayScrollbar, margin + disp_w, top_y,
                       CSB_WIDTH, disp_h, TRUE);

        /* Notify ChatListView of size change */
        if (d->hChatList)
            chat_listview_relayout(d->hChatList);
    }
    /* Old floating approval buttons removed — now inline in chat_listview */
    {
        int input_y = ch - input_h - margin;
        if (input_y < top_y) input_y = top_y;
        int input_w = cw - send_w - margin * 3 - CSB_WIDTH;
        if (input_w < 1) input_w = 1;
        if (d->hInput)
            MoveWindow(d->hInput, margin, input_y, input_w, input_h, TRUE);
        if (d->hInputScrollbar)
            MoveWindow(d->hInputScrollbar, margin + input_w, input_y,
                       CSB_WIDTH, input_h, TRUE);
        if (d->hSendBtn)
            MoveWindow(d->hSendBtn, cw - send_w - margin, input_y, send_w, input_h, TRUE);
    }
    #undef S
}

/* Sync the input scrollbar with the EDIT control's scroll state */
static void input_sync_scroll(AiChatData *d)
{
    if (!d || !d->hInputScrollbar) return;
    int lh = d->input_line_h > 0 ? d->input_line_h : 1;
    csb_sync_edit(d->hInput, d->hInputScrollbar, lh);
}

/* Zoom the AI chat font: recreate hFont at the new size, apply to controls,
 * and re-measure line height for scroll sync. */
static void chat_apply_zoom(AiChatData *d, int delta)
{
    if (!d) return;
    int new_size = app_font_zoom(d->ui_font_size, delta);
    if (new_size == d->ui_font_size)
        return;
    d->ui_font_size = new_size;

    /* Recreate main font at new size */
    if (d->hFont) DeleteObject(d->hFont);
    if (d->hIconFont) DeleteObject(d->hIconFont);
    int h = -MulDiv(new_size, d->dpi, 72);
    d->hFont = CreateFont(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_TT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_SWISS, APP_FONT_UI_FACE);
                          
    int ih = -MulDiv(new_size, d->dpi, 72);
    d->hIconFont = CreateFont(ih, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, "Segoe Fluent Icons");
    if (!d->hIconFont) {
        d->hIconFont = CreateFont(ih, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_TT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, "Segoe MDL2 Assets");
    }

    if (d->hFont) {
        SendMessage(d->hInput, WM_SETFONT, (WPARAM)d->hFont, TRUE);
        /* Update ChatListView fonts (free old first to prevent leaks) */
        if (d->hChatList) {
            if (d->hBoldFont) DeleteObject(d->hBoldFont);
            if (d->hMonoFont) DeleteObject(d->hMonoFont);
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
            chat_listview_set_fonts(d->hChatList, d->hFont,
                                    d->hMonoFont, d->hBoldFont,
                                    d->hSmallFont, d->hIconFont);
            chat_listview_relayout(d->hChatList);
        }
        /* Re-measure line height */
        HDC hdc = GetDC(d->hInput);
        HGDIOBJ old = SelectObject(hdc, (HGDIOBJ)d->hFont);
        TEXTMETRIC tm;
        GetTextMetrics(hdc, &tm);
        d->display_line_h = tm.tmHeight + tm.tmExternalLeading;
        d->input_line_h = d->display_line_h;
        SelectObject(hdc, old);
        ReleaseDC(d->hInput, hdc);
    }
    input_sync_scroll(d);
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

        /* Get per-monitor DPI for layout scaling */
        nd->dpi = get_window_dpi(hwnd);
        #define S(px) MulDiv((px), nd->dpi, 96)

        RECT rc;
        GetClientRect(hwnd, &rc);
        int cw = rc.right;
        int ch = rc.bottom;
        int btn_h = S(24); /* compact — matches BTN_SIZE in tab strip */
        int pad = S(4);
        int top_y = pad + btn_h + pad; /* display starts below buttons */

        /* New Chat button (owner-drawn for theme) */
        nd->hNewChatBtn = CreateWindow("BUTTON", "New Chat",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            pad, pad, S(78), btn_h,
            hwnd, (HMENU)IDC_CHAT_NEWCHAT, NULL, NULL);

        /* Permit Write button */
        nd->permit_write = 0; /* default: read-only */
        nd->hPermitBtn = CreateWindow("BUTTON", "Permit Write",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            pad + S(78) + pad, pad, S(115), btn_h,
            hwnd, (HMENU)IDC_CHAT_PERMIT, NULL, NULL);

        /* Auto Approve button */
        nd->hAllowAllBtn = CreateWindow("BUTTON", "Auto Approve",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            pad + S(78) + pad + S(115) + pad, pad, S(115), btn_h,
            hwnd, (HMENU)IDC_CHAT_AUTOAPPROVE, NULL, NULL);

        nd->show_thinking = 0; /* default: collapsed (user must click '>' to expand) */
        nd->hThinkingBtn = NULL; /* Thinking button removed - now inline in chat */

        /* Save button — square, right-aligned in button row */
        nd->hSaveBtn = CreateWindow("BUTTON", "",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            cw - pad - btn_h, pad, btn_h, btn_h,
            hwnd, (HMENU)IDC_CHAT_SAVE, NULL, NULL);

        /* Undock/Dock button — square, left of save button */
        nd->hUndockBtn = CreateWindow("BUTTON", "",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            cw - pad - btn_h - pad - btn_h, pad, btn_h, btn_h,
            hwnd, (HMENU)IDC_CHAT_UNDOCK, NULL, NULL);

        /* Old floating Allow/Deny buttons removed — approval is now
         * handled inline via chat_listview command block buttons.
         * Initialize the approval queue. */
        chat_approval_init(&nd->approval_q);
        chat_activity_init(&nd->activity);

        /* Session name (left) + context bar (right) row */
        {
            int bar_h = S(16);
            int ctx_w = S(180);  /* fixed width for context bar */
            int label_w = cw - ctx_w - pad * 3;

            /* Session name label on the left */
            nd->hSessionLabel = CreateWindow("STATIC",
                nd->session_name[0] ? nd->session_name : "",
                WS_VISIBLE | WS_CHILD | SS_LEFT | SS_ENDELLIPSIS,
                pad, top_y, label_w, bar_h,
                hwnd, (HMENU)IDC_SESSION_LABEL, NULL, NULL);

            /* Context bar and label on the right */
            nd->hContextBar = CreateWindow(PROGRESS_CLASS, "",
                WS_VISIBLE | WS_CHILD,
                cw - ctx_w - pad, top_y, ctx_w, bar_h,
                hwnd, (HMENU)IDC_CONTEXT_BAR, NULL, NULL);
            SendMessage(nd->hContextBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessage(nd->hContextBar, PBM_SETPOS, 0, 0);
            SendMessage(nd->hContextBar, PBM_SETBKCOLOR, 0,
                        (LPARAM)theme_cr(nd->theme->bg_secondary));

            /* Label overlay hidden — text is now drawn by the progress
             * bar subclass (ContextBarSubclass) for reliable rendering. */
            nd->hContextLabel = CreateWindow("STATIC", "",
                WS_CHILD, /* NOT visible */
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_CONTEXT_LABEL, NULL, NULL);

            /* Subclass the progress bar to draw label text on itself */
            SetWindowSubclass(nd->hContextBar, ContextBarSubclass,
                              CONTEXT_BAR_SUBCLASS_ID, (DWORD_PTR)nd);

            top_y += bar_h + pad;
        }

        /* Chat display: owner-drawn ChatListView replaces RichEdit.
         * In docked mode the initial window may be 1x1, so clamp
         * all dimensions to >=1 — relayout() fixes them on first WM_SIZE. */
        int input_h = S(46); /* ~2 lines for multiline input */
        int margin = S(5);
        int disp_w = cw - margin * 2 - CSB_WIDTH;
        if (disp_w < 1) disp_w = 1;
        int disp_h = ch - input_h - top_y - margin * 2;
        if (disp_h < 1) disp_h = 1;

        /* Initialize the message list */
        chat_msg_list_init(&nd->msg_list);

        /* Create ChatListView — hDisplay points to same HWND for layout compat */
        nd->hChatList = chat_listview_create(hwnd, margin, top_y,
                                              disp_w, disp_h,
                                              &nd->msg_list, nd->theme);
        nd->hDisplay = nd->hChatList;  /* layout code uses hDisplay */
        if (nd->hChatList)
            chat_listview_set_activity(nd->hChatList, &nd->activity);

        /* Custom themed scrollbar for chat display (kept for visual consistency) */
        csb_register(GetModuleHandle(NULL));
        nd->hDisplayScrollbar = csb_create(hwnd,
            margin + disp_w, top_y, CSB_WIDTH, disp_h,
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
        int send_w = S(40);
        int input_y = ch - input_h - margin;
        if (input_y < 0) input_y = 0;
        int input_w = cw - send_w - margin * 3 - CSB_WIDTH;
        if (input_w < 1) input_w = 1;
        nd->hInput = CreateWindow("EDIT", "",
            WS_VISIBLE | WS_CHILD | WS_BORDER |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            margin, input_y, input_w, input_h,
            hwnd, (HMENU)IDC_CHAT_INPUT, NULL, NULL);
        if (nd->hInput)
            SetWindowSubclass(nd->hInput, InputSubclassProc, 0, 0);

        /* Custom themed scrollbar for input */
        nd->hInputScrollbar = csb_create(hwnd,
            margin + input_w, input_y, CSB_WIDTH, input_h,
            nd->theme, GetModuleHandle(NULL));

        /* Send button (owner-drawn for theme) */
        nd->hSendBtn = CreateWindow("BUTTON", ">",
            WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            cw - send_w - margin, input_y, send_w, input_h,
            hwnd, (HMENU)IDC_CHAT_SEND, NULL, NULL);

        /* Font — use Inter UI font */
        nd->ui_font_size = APP_FONT_UI_SIZE;
        int h = -MulDiv(APP_FONT_UI_SIZE, nd->dpi, 72);
        nd->hFont = CreateFont(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_SWISS, APP_FONT_UI_FACE);
        /* Small bold font — Segoe UI renders reliably at small sizes on all
         * Windows versions (hand-tuned hinting), unlike bundled Inter. */
        int sh = -MulDiv(8, nd->dpi, 72);
        nd->hSmallFont = CreateFont(sh, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_TT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_SWISS, "Segoe UI");

        /* Icon Font for Fluent UI */
        int ih = -MulDiv(APP_FONT_UI_SIZE, nd->dpi, 72);
        nd->hIconFont = CreateFont(ih, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_TT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, "Segoe Fluent Icons");
        if (!nd->hIconFont) {
            nd->hIconFont = CreateFont(ih, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_TT_PRECIS,
                                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       DEFAULT_PITCH | FF_DONTCARE, "Segoe MDL2 Assets");
        }
        #undef S
        if (nd->hFont) {
            SendMessage(nd->hInput, WM_SETFONT, (WPARAM)nd->hFont, TRUE);
            /* Set fonts on the ChatListView */
            if (nd->hChatList) {
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
                chat_listview_set_fonts(nd->hChatList, nd->hFont,
                                        nd->hMonoFont, nd->hBoldFont,
                                        nd->hSmallFont, nd->hIconFont);
                chat_listview_set_model(nd->hChatList, nd->conv.model);
            }
            /* Measure line height for scrollbar sync */
            HDC hdc_m = GetDC(nd->hInput);
            HGDIOBJ old_m = SelectObject(hdc_m, (HGDIOBJ)nd->hFont);
            TEXTMETRIC tm_m;
            GetTextMetrics(hdc_m, &tm_m);
            nd->display_line_h = tm_m.tmHeight + tm_m.tmExternalLeading;
            nd->input_line_h = nd->display_line_h;
            SelectObject(hdc_m, old_m);
            ReleaseDC(nd->hInput, hdc_m);
        }
        if (nd->hSmallFont) {
            SendMessage(nd->hSessionLabel, WM_SETFONT, (WPARAM)nd->hSmallFont, TRUE);
            SendMessage(nd->hContextLabel, WM_SETFONT, (WPARAM)nd->hSmallFont, TRUE);
        }

        /* Apply theme title bar + borders */
        if (nd->theme) {
            themed_apply_title_bar(hwnd, nd->theme);
            themed_apply_borders(hwnd, nd->theme);
        }

        /* Create tooltip control and add tips for all buttons */
        nd->hTooltip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
            WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
            0, 0, 0, 0, hwnd, NULL, NULL, NULL);
        if (nd->hTooltip) {
            SendMessage(nd->hTooltip, TTM_SETMAXTIPWIDTH, 0, 300);
            add_tooltip(nd->hTooltip, nd->hNewChatBtn,
                "New Chat\nClear the conversation and start fresh.");
            add_tooltip(nd->hTooltip, nd->hPermitBtn,
                "Permit Write\n"
                "Toggle read/write mode for AI commands.\n"
                "Green = AI can execute any command.\n"
                "Grey = AI can only run read-only commands\n"
                "(ls, cat, pwd, etc).");
            add_tooltip(nd->hTooltip, nd->hAllowAllBtn,
                "Allow all commands this session\n"
                "When active, AI commands are executed\n"
                "without individual approval prompts.");
            /* Thinking tooltip removed - click '>' in chat to expand/collapse */
            add_tooltip(nd->hTooltip, nd->hSaveBtn,
                "Save\nSave the conversation as a text file.");
            if (nd->hUndockBtn)
                add_tooltip(nd->hTooltip, nd->hUndockBtn,
                    nd->docked ? "Undock\nOpen AI Assist in a separate window."
                               : "Dock\nDock AI Assist inside the main window.");
            add_tooltip(nd->hTooltip, nd->hSendBtn,
                "Send\nSend your message to the AI.\n"
                "Shortcut: press Enter in the input box.");
        }

        /* Show loaded conversation or fresh welcome message */
        if (nd->conv.msg_count > 0) {
            chat_rebuild_display(nd);
        } else {
            chat_msg_append(&nd->msg_list, CHAT_ITEM_STATUS,
                "AI Assist - Type a message and press Enter or click Send.\n"
                "The AI can see your terminal and execute commands.");
            if (nd->hChatList)
                chat_listview_invalidate(nd->hChatList);
        }

        update_context_bar(nd);
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
         * so the parent window can handle splitter dragging. */
        if (d && d->docked) {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            if (pt.x < AI_DOCK_SPLITTER_HIT / 2)
                return HTTRANSPARENT;
        }
        break;
    }

    case WM_NOTIFY: {
        /* EN_LINK handling removed — ChatListView handles thinking toggle
         * inline via its own click handling in the list view WndProc. */
        (void)lParam;
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CHAT_SEND:
            if (d && ACTIVE_BUSY(d)) {
                /* While streaming, Send button acts as Stop */
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
        case IDC_CHAT_NEWCHAT:
            if (d) {
                /* Cancel any active stream first */
                if (ACTIVE_BUSY(d))
                    cancel_active_stream(d);

                /* Reset only the ACTIVE session's conversation.
                 * Other sessions' AiSessionState objects are untouched. */
                ai_conv_reset(&d->conv);
                if (d->active_state) {
                    ai_conv_reset(&d->active_state->conv);
                    d->active_state->valid = 1;
                }
                d->indicator_pos = -1;
                d->commands_executed = 0;
                d->pending_approval = 0;
                d->pending_request[0] = '\0';
                d->queued_count = 0;
                d->queued_next = 0;
                d->stream_thinking[0] = '\0';
                d->stream_thinking_len = 0;
                d->stream_content[0] = '\0';
                d->stream_content_len = 0;
                d->stream_phase = 0;
                KillTimer(hwnd, TIMER_HEARTBEAT);
                chat_activity_reset(&d->activity);
                thinking_history_clear(d);
                /* Clear display and show welcome message */
                chat_msg_list_clear(&d->msg_list);
                d->stream_ai_item = NULL;
                chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                    "AI Assist - Type a message and press Enter or click Send.\n"
                    "The AI can see your terminal and execute commands.");
                if (d->hChatList)
                    chat_listview_invalidate(d->hChatList);
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
                InvalidateRect(d->hPermitBtn, NULL, TRUE);
                if (d->permit_write) {
                    /* Enabling: unblock all blocked commands */
                    chat_approval_unblock_all(&d->approval_q);
                    ChatMsgItem *it = d->msg_list.head;
                    while (it) {
                        if (it->type == CHAT_ITEM_COMMAND &&
                            it->u.cmd.blocked) {
                            it->u.cmd.blocked = 0;
                            it->u.cmd.approved = -1;
                        }
                        it = it->next;
                    }
                } else {
                    /* Disabling: re-block pending write/critical commands */
                    chat_approval_block_pending_writes(&d->approval_q);
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
            if (d) {
                d->approval_q.auto_approve = !d->approval_q.auto_approve;
                InvalidateRect(d->hAllowAllBtn, NULL, TRUE);
                if (d->hChatList) chat_listview_invalidate(d->hChatList);
            }
            return 0;

        /* IDC_CHAT_THINKING removed - thinking toggle now inline in chat */

        /* ── Inline command approval from chat_listview ───────────── */
        default: {
            int ctl_id = LOWORD(wParam);

            /* IDC_CMD_APPROVE_BASE + index → approve single command */
            if (d && ctl_id >= IDC_CMD_APPROVE_BASE &&
                ctl_id < IDC_CMD_APPROVE_BASE + APPROVAL_MAX_CMDS) {
                int idx = ctl_id - IDC_CMD_APPROVE_BASE;
                chat_approval_approve(&d->approval_q, idx);

                /* Sync approval state back to ChatMsgItem list */
                int ci = 0;
                ChatMsgItem *it = d->msg_list.head;
                while (it) {
                    if (it->type == CHAT_ITEM_COMMAND && !it->u.cmd.settled) {
                        if (ci == idx) { it->u.cmd.approved = 1; break; }
                        ci++;
                    }
                    it = it->next;
                }

                if (d->hChatList) chat_listview_invalidate(d->hChatList);

                /* Execute approved command */
                if (idx < d->queued_count) {
                    execute_command(d, d->queued_cmds[idx]);
                    float now_a = (float)GetTickCount() / 1000.0f;
                    chat_activity_set_phase(&d->activity, ACTIVITY_EXECUTING, now_a);
                    chat_activity_set_exec(&d->activity, idx + 1, d->queued_count);
                }

                /* Check if all decided — if so, wrap up */
                if (chat_approval_all_decided(&d->approval_q)) {
                    d->pending_approval = 0;
                    settle_all_commands(&d->msg_list);
                    d->commands_executed = d->queued_count;
                    start_indicator(d, "waiting for output");
                    {
                        float now_w = (float)GetTickCount() / 1000.0f;
                        chat_activity_set_phase(&d->activity, ACTIVITY_WAITING, now_w);
                    }
                    SetTimer(hwnd, TIMER_CONTINUE,
                             CONTINUE_DELAY_MS, NULL);
                }
                SetFocus(d->hInput);
                return 0;
            }

            /* IDC_CMD_DENY_BASE + index → deny single command */
            if (d && ctl_id >= IDC_CMD_DENY_BASE &&
                ctl_id < IDC_CMD_DENY_BASE + APPROVAL_MAX_CMDS) {
                int idx = ctl_id - IDC_CMD_DENY_BASE;
                chat_approval_deny(&d->approval_q, idx);

                /* Sync denial state back to ChatMsgItem list */
                int ci = 0;
                ChatMsgItem *it = d->msg_list.head;
                while (it) {
                    if (it->type == CHAT_ITEM_COMMAND && !it->u.cmd.settled) {
                        if (ci == idx) { it->u.cmd.approved = 0; break; }
                        ci++;
                    }
                    it = it->next;
                }

                if (d->hChatList) chat_listview_invalidate(d->hChatList);

                /* Check if all decided */
                if (chat_approval_all_decided(&d->approval_q)) {
                    d->pending_approval = 0;
                    settle_all_commands(&d->msg_list);
                    chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                                    "[some commands denied]");
                    if (d->hChatList)
                        chat_listview_invalidate(d->hChatList);
                }
                SetFocus(d->hInput);
                return 0;
            }

            /* IDC_CMD_APPROVE_ALL → approve all pending commands */
            if (d && ctl_id == IDC_CMD_APPROVE_ALL) {
                chat_approval_approve_all(&d->approval_q);

                /* Sync all to approved in ChatMsgItem list */
                ChatMsgItem *it = d->msg_list.head;
                while (it) {
                    if (it->type == CHAT_ITEM_COMMAND && !it->u.cmd.settled &&
                        it->u.cmd.approved == -1 && !it->u.cmd.blocked)
                        it->u.cmd.approved = 1;
                    it = it->next;
                }

                d->pending_approval = 0;
                settle_all_commands(&d->msg_list);
                if (d->hChatList) chat_listview_invalidate(d->hChatList);

                /* Execute all approved commands */
                {
                    float now_e = (float)GetTickCount() / 1000.0f;
                    chat_activity_set_phase(&d->activity, ACTIVITY_EXECUTING, now_e);
                }
                for (int ci = 0; ci < d->queued_count; ci++) {
                    execute_command(d, d->queued_cmds[ci]);
                    chat_activity_set_exec(&d->activity, ci + 1, d->queued_count);
                }
                d->queued_next = d->queued_count;
                d->commands_executed = d->queued_count;
                start_indicator(d, "waiting for output");
                {
                    float now_w2 = (float)GetTickCount() / 1000.0f;
                    chat_activity_set_phase(&d->activity, ACTIVITY_WAITING, now_w2);
                }
                SetTimer(hwnd, TIMER_CONTINUE,
                         CONTINUE_DELAY_MS, NULL);
                SetFocus(d->hInput);
                return 0;
            }

            /* IDC_CMD_APPROVE_SEL → approve only selected (ticked) commands */
            if (d && ctl_id == IDC_CMD_APPROVE_SEL) {
                int ci = 0;
                int any_approved = 0;
                ChatMsgItem *it = d->msg_list.head;
                while (it) {
                    if (it->type == CHAT_ITEM_COMMAND && !it->u.cmd.settled) {
                        if (it->u.cmd.selected && it->u.cmd.approved == -1
                            && !it->u.cmd.blocked) {
                            it->u.cmd.approved = 1;
                            chat_approval_approve(&d->approval_q, ci);
                            any_approved = 1;
                        }
                        ci++;
                    }
                    it = it->next;
                }

                /* Deny unselected pending commands */
                ci = 0;
                it = d->msg_list.head;
                while (it) {
                    if (it->type == CHAT_ITEM_COMMAND && !it->u.cmd.settled) {
                        if (it->u.cmd.approved == -1 && !it->u.cmd.blocked) {
                            it->u.cmd.approved = 0;
                            chat_approval_deny(&d->approval_q, ci);
                        }
                        ci++;
                    }
                    it = it->next;
                }

                d->pending_approval = 0;
                if (d->hChatList) chat_listview_invalidate(d->hChatList);

                if (any_approved) {
                    float now_e = (float)GetTickCount() / 1000.0f;
                    chat_activity_set_phase(&d->activity, ACTIVITY_EXECUTING, now_e);
                    ci = 0;
                    it = d->msg_list.head;
                    while (it) {
                        if (it->type == CHAT_ITEM_COMMAND && !it->u.cmd.settled &&
                            it->u.cmd.approved == 1 && ci < d->queued_count) {
                            execute_command(d, d->queued_cmds[ci]);
                            chat_activity_set_exec(&d->activity, ci + 1,
                                                   d->queued_count);
                        }
                        if (it->type == CHAT_ITEM_COMMAND && !it->u.cmd.settled) ci++;
                        it = it->next;
                    }
                    d->commands_executed = d->queued_count;
                    start_indicator(d, "waiting for output");
                    float now_w = (float)GetTickCount() / 1000.0f;
                    chat_activity_set_phase(&d->activity, ACTIVITY_WAITING, now_w);
                    SetTimer(hwnd, TIMER_CONTINUE,
                             CONTINUE_DELAY_MS, NULL);
                } else {
                    chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                                    "[no commands selected]");
                    if (d->hChatList)
                        chat_listview_invalidate(d->hChatList);
                }
                settle_all_commands(&d->msg_list);
                SetFocus(d->hInput);
                return 0;
            }

            /* IDC_CMD_CANCEL_ALL → deny all pending commands */
            if (d && ctl_id == IDC_CMD_CANCEL_ALL) {
                int ci = 0;
                ChatMsgItem *it = d->msg_list.head;
                while (it) {
                    if (it->type == CHAT_ITEM_COMMAND && !it->u.cmd.settled &&
                        it->u.cmd.approved == -1 && !it->u.cmd.blocked) {
                        it->u.cmd.approved = 0;
                        chat_approval_deny(&d->approval_q, ci);
                    }
                    if (it->type == CHAT_ITEM_COMMAND && !it->u.cmd.settled) ci++;
                    it = it->next;
                }
                d->pending_approval = 0;
                settle_all_commands(&d->msg_list);
                if (d->hChatList) chat_listview_invalidate(d->hChatList);
                chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                                "[all commands cancelled]");
                if (d->hChatList)
                    chat_listview_invalidate(d->hChatList);
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
        case IDC_CONTEXT_LABEL:
            if (d && HIWORD(wParam) == STN_CLICKED) {
                /* Context label click - removed thinking toggle (now inline in chat) */
                if (d->context_limit <= 0) {
                    MessageBox(hwnd,
                        "Context usage tracking is not available\n"
                        "for this model (unknown context limit).",
                        "Compact Context", MB_OK | MB_ICONINFORMATION);
                } else {
                    int r = MessageBox(hwnd,
                        "Trim older messages to free context space?\n"
                        "The 3 most recent exchanges will be kept.",
                        "Compact Context", MB_YESNO | MB_ICONQUESTION);
                    if (r == IDYES) {
                        EnterCriticalSection(&d->cs);
                        int removed = ai_conv_compact(&d->conv, 3);
                        LeaveCriticalSection(&d->cs);
                        if (removed > 0) {
                            char cbuf[128];
                            snprintf(cbuf, sizeof(cbuf),
                                     "[compacted: removed %d older messages]",
                                     removed);
                            chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                                            cbuf);
                            if (d->hChatList)
                                chat_listview_invalidate(d->hChatList);
                            update_context_bar(d);
                        } else {
                            chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS,
                                            "[nothing to compact]");
                            if (d->hChatList)
                                chat_listview_invalidate(d->hChatList);
                        }
                    }
                }
            }
            return 0;
        case IDC_CHAT_INPUT:
            /* Handle Enter key in input (via EN_CHANGE notification is wrong;
             * we handle it via WM_KEYDOWN subclass or default button) */
            break;
        }
        break;

    case WM_AI_STREAM: {
        /* Realtime streaming chunk: wParam 0=thinking, 1=content */
        if (!d) break;
        AiStreamChunk *chunk = (AiStreamChunk *)lParam;
        if (!chunk) break;
        char *delta = chunk->delta;
        AiSessionState *src = chunk->session;
        free(chunk);  /* free wrapper; delta freed below */
        if (!delta) return 0;

        /* Discard stale chunks that arrive after cancel */
        if (d->abort_stream) {
            free(delta);
            return 0;
        }

        /* Always accumulate to the source session's stream buffers */
        if (src) {
            size_t dlen = strlen(delta);
            if (wParam == 0) {
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

        /* If this chunk is for a different session, don't touch the display */
        if (src != d->active_state) {
            free(delta);
            return 0;
        }

        /* Also accumulate to display-side buffers */
        {
            size_t dlen = strlen(delta);
            if (wParam == 0) {
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
        }

        /* Update activity state on stream chunks */
        {
            float now = (float)GetTickCount() / 1000.0f;
            if (wParam == 0) {
                if (d->activity.phase != ACTIVITY_THINKING)
                    chat_activity_set_phase(&d->activity, ACTIVITY_THINKING, now);
                chat_activity_token(&d->activity, now);
            } else {
                if (d->activity.phase != ACTIVITY_RESPONDING)
                    chat_activity_set_phase(&d->activity, ACTIVITY_RESPONDING, now);
                chat_activity_token(&d->activity, now);
            }
        }

        /* Update the streaming AI item in the ChatMsgList */
        if (d->stream_ai_item) {
            if (wParam == 0) {
                /* Thinking delta — update thinking text on AI item */
                if (d->stream_phase < 1)
                    d->stream_phase = 1;
                chat_msg_set_thinking(d->stream_ai_item,
                                      d->stream_thinking);
                /* Auto-scroll expanded thinking to bottom */
                if (!d->stream_ai_item->u.ai.thinking_collapsed
                    && d->stream_ai_item->u.ai.thinking_autoscroll) {
                    d->stream_ai_item->u.ai.thinking_scroll_y = 999999;
                    /* Will be clamped to max_scroll by paint/mousewheel */
                }
            } else {
                /* Content delta — update AI item text */
                if (d->stream_phase < 2)
                    d->stream_phase = 2;
                chat_msg_set_text(d->stream_ai_item,
                                  d->stream_content);
            }
            if (d->hChatList) {
                chat_listview_invalidate(d->hChatList);
                chat_listview_scroll_to_bottom(d->hChatList);
            }
        }

        free(delta);
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
         * the thread already committed its result to src->conv.
         * Extract commands for deferred approval, then clean up. */
        if (src != d->active_state) {
            if (wParam == 2 && src && rmsg->content) {
                char cmds[16][1024];
                int ncmds = ai_extract_commands(rmsg->content, cmds, 16);
                if (ncmds > 0) {
                    free(src->pending_cmds);
                    size_t sz = (size_t)ncmds * sizeof(cmds[0]);
                    src->pending_cmds = malloc(sz);
                    if (src->pending_cmds) {
                        memcpy(src->pending_cmds, cmds, sz);
                        src->pending_cmd_count = ncmds;
                        src->pending_approval = 1;
                    }
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
            int ncmds = text ? ai_extract_commands(text, cmds, 16) : 0;

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

            /* Settle old command items so they render inline */
            settle_all_commands(&d->msg_list);

            /* Filter out write commands when permit_write is off */
            if (ncmds > 0 && !d->permit_write) {
                int ncmds_before = ncmds;
                char filtered[16][1024];
                int nfiltered = 0;
                for (int ci = 0; ci < ncmds; ci++) {
                    if (ai_command_is_readonly(cmds[ci])) {
                        memcpy(filtered[nfiltered], cmds[ci],
                               sizeof(filtered[0]));
                        nfiltered++;
                    } else {
                        /* Show blocked command as CHAT_ITEM_COMMAND */
                        ChatMsgItem *blk = chat_msg_append(
                            &d->msg_list, CHAT_ITEM_COMMAND, "");
                        if (blk)
                            chat_msg_set_command(blk, cmds[ci],
                                CMD_WRITE, 1);
                    }
                }
                if (nfiltered < ncmds_before) {
                    char bmsg[2048];
                    int bp = snprintf(bmsg, sizeof(bmsg),
                        "NOTE: The following commands were BLOCKED by "
                        "the user's read-only security policy and were "
                        "NOT executed:\n");
                    for (int ci = 0; ci < ncmds; ci++) {
                        if (!ai_command_is_readonly(cmds[ci]))
                            bp += snprintf(bmsg + bp,
                                sizeof(bmsg) - (size_t)bp,
                                "  - %s\n", cmds[ci]);
                    }
                    snprintf(bmsg + bp, sizeof(bmsg) - (size_t)bp,
                        "Do NOT claim these commands were executed. "
                        "If the user needs these actions, tell them "
                        "to enable 'Permit Write' and try again.");
                    EnterCriticalSection(&d->cs);
                    ai_conv_add(&d->conv, AI_ROLE_USER, bmsg);
                    LeaveCriticalSection(&d->cs);
                }
                memcpy(cmds, filtered,
                       (size_t)nfiltered * sizeof(cmds[0]));
                ncmds = nfiltered;
            }

            if (ncmds > 0) {
                /* Reset approval queue for this batch */
                chat_approval_reset(&d->approval_q);

                /* Create CHAT_ITEM_COMMAND items and populate approval queue */
                for (int ci = 0; ci < ncmds; ci++) {
                    ChatMsgItem *cmd_item = chat_msg_append(
                        &d->msg_list, CHAT_ITEM_COMMAND, "");
                    if (cmd_item)
                        chat_msg_set_command(cmd_item, cmds[ci],
                            CMD_SAFE, 0);
                    chat_approval_add(&d->approval_q, cmds[ci],
                                      CMD_PLATFORM_LINUX, d->permit_write);
                }

                /* Stash commands */
                memcpy(d->queued_cmds, cmds,
                       (size_t)ncmds * sizeof(cmds[0]));
                d->queued_count = ncmds;
                d->queued_next = 0;

                if (chat_approval_all_decided(&d->approval_q)) {
                    /* Auto-approve already decided all commands —
                     * skip approval UI and execute immediately */
                    d->pending_approval = 0;
                    /* Sync approved state to ChatMsgItems */
                    {
                        int ci2 = 0;
                        ChatMsgItem *it = d->msg_list.head;
                        while (it) {
                            if (it->type == CHAT_ITEM_COMMAND &&
                                !it->u.cmd.settled && ci2 < d->approval_q.count) {
                                if (d->approval_q.entries[ci2].status == APPROVE_APPROVED)
                                    it->u.cmd.approved = 1;
                                ci2++;
                            }
                            it = it->next;
                        }
                    }
                    settle_all_commands(&d->msg_list);
                    {
                        float now_e = (float)GetTickCount() / 1000.0f;
                        chat_activity_set_phase(&d->activity, ACTIVITY_EXECUTING, now_e);
                    }
                    for (int ci2 = 0; ci2 < d->queued_count; ci2++) {
                        if (d->approval_q.entries[ci2].status == APPROVE_APPROVED) {
                            execute_command(d, d->queued_cmds[ci2]);
                            chat_activity_set_exec(&d->activity, ci2 + 1,
                                                   d->queued_count);
                        }
                    }
                    d->queued_next = d->queued_count;
                    d->commands_executed = d->queued_count;
                    start_indicator(d, "waiting for output");
                    {
                        float now_w = (float)GetTickCount() / 1000.0f;
                        chat_activity_set_phase(&d->activity, ACTIVITY_WAITING, now_w);
                    }
                    SetTimer(hwnd, TIMER_CONTINUE,
                             CONTINUE_DELAY_MS, NULL);
                } else {
                    /* Show approval buttons and wait for user */
                    d->pending_approval = 1;
                    if (d->hChatList)
                        chat_listview_reset_cmd_expand(d->hChatList);
                }
                relayout(d);
                SetFocus(d->hInput);
            }

            if (d->hChatList) {
                chat_listview_invalidate(d->hChatList);
                chat_listview_scroll_to_bottom(d->hChatList);
            }

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
                if (d->hChatList)
                    chat_listview_invalidate(d->hChatList);
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
        return 0;
    }

    case WM_TIMER:
        if (!d) return 0;
        if (wParam == TIMER_CMD_QUEUE) {
            /* Execute next queued command with paste delay */
            if (d->queued_next < d->queued_count) {
                execute_command(d, d->queued_cmds[d->queued_next]);
                d->queued_next++;
                /* Update activity: executing N/M */
                {
                    float now = (float)GetTickCount() / 1000.0f;
                    if (d->activity.phase != ACTIVITY_EXECUTING)
                        chat_activity_set_phase(&d->activity, ACTIVITY_EXECUTING, now);
                    chat_activity_set_exec(&d->activity, d->queued_next, d->queued_count);
                }
            }
            if (d->queued_next >= d->queued_count) {
                /* All commands executed — stop timer, show waiting */
                KillTimer(hwnd, TIMER_CMD_QUEUE);
                d->commands_executed = d->queued_count;
                start_indicator(d, "waiting for output");
                {
                    float now = (float)GetTickCount() / 1000.0f;
                    chat_activity_set_phase(&d->activity, ACTIVITY_WAITING, now);
                }
                SetTimer(hwnd, TIMER_CONTINUE, CONTINUE_DELAY_MS, NULL);
            } else {
                /* More commands pending — show progress after cmd text */
                char prog[80];
                snprintf(prog, sizeof(prog), "executing %d/%d",
                         d->queued_next + 1, d->queued_count);
                start_indicator(d, prog);
            }
        } else if (wParam == TIMER_CONTINUE) {
            KillTimer(hwnd, TIMER_CONTINUE);
            if (d->commands_executed > 0 && !ACTIVE_BUSY(d)) {
                d->commands_executed = 0;
                send_continue_message(d);
            }
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
                #define HS(px) MulDiv((px), d->dpi, 96)
                hdr_rc.bottom = HS(4) + HS(24) + HS(4) + HS(16) + HS(4);
                #undef HS
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
        if (d && d->theme && d->activity.phase != ACTIVITY_IDLE) {
            /* Paint a small activity dot + one-word status in the header bar,
             * next to the session label.  We paint only in the header strip
             * so child controls are not affected. */
            PAINTSTRUCT ps_hdr;
            HDC hdc_hdr = BeginPaint(hwnd, &ps_hdr);
            #define HP(px) MulDiv((px), d->dpi, 96)
            {
                int pad_h   = HP(4);
                int btn_h_h = HP(24);
                int bar_h_h = HP(16);
                int top_y_h = pad_h + btn_h_h + pad_h; /* session label row y */

                /* Dot position: right end of session label area */
                RECT rc_lbl;
                if (d->hSessionLabel) {
                    GetWindowRect(d->hSessionLabel, &rc_lbl);
                    MapWindowPoints(NULL, hwnd, (POINT *)&rc_lbl, 2);
                } else {
                    SetRect(&rc_lbl, pad_h, top_y_h, HP(200), top_y_h + bar_h_h);
                }

                /* Get session label text width */
                SIZE sz_lbl;
                HGDIOBJ old_f = SelectObject(hdc_hdr,
                    d->hSmallFont ? d->hSmallFont
                                  : GetStockObject(DEFAULT_GUI_FONT));
                char lbl_text[256];
                GetWindowTextA(d->hSessionLabel, lbl_text, (int)sizeof(lbl_text));
                GetTextExtentPoint32A(hdc_hdr, lbl_text, (int)strlen(lbl_text), &sz_lbl);

                int dot_sz  = HP(6);
                int dot_x   = rc_lbl.left + sz_lbl.cx + HP(6);
                int dot_y   = top_y_h + (bar_h_h - dot_sz) / 2;

                /* Choose colour from health */
                COLORREF dot_clr;
                switch (d->activity.health) {
                case HEALTH_YELLOW:
                    dot_clr = RGB(((d->theme->chat.indicator_yellow)>>16)&0xFF,
                                  ((d->theme->chat.indicator_yellow)>>8)&0xFF,
                                  (d->theme->chat.indicator_yellow)&0xFF);
                    break;
                case HEALTH_RED:
                    dot_clr = RGB(((d->theme->chat.indicator_red)>>16)&0xFF,
                                  ((d->theme->chat.indicator_red)>>8)&0xFF,
                                  (d->theme->chat.indicator_red)&0xFF);
                    break;
                default:
                    dot_clr = RGB(((d->theme->chat.indicator_green)>>16)&0xFF,
                                  ((d->theme->chat.indicator_green)>>8)&0xFF,
                                  (d->theme->chat.indicator_green)&0xFF);
                    break;
                }

                /* Apply pulse: blend with bg on toggle */
                if (d->pulse_toggle) {
                    COLORREF bg_c = RGB(((d->theme->bg_primary)>>16)&0xFF,
                                        ((d->theme->bg_primary)>>8)&0xFF,
                                        (d->theme->bg_primary)&0xFF);
                    dot_clr = RGB(
                        (GetRValue(dot_clr) + GetRValue(bg_c)) / 2,
                        (GetGValue(dot_clr) + GetGValue(bg_c)) / 2,
                        (GetBValue(dot_clr) + GetBValue(bg_c)) / 2);
                }

                /* Draw dot */
                HBRUSH hDotBr = CreateSolidBrush(dot_clr);
                HPEN   hDotPn = CreatePen(PS_SOLID, 1, dot_clr);
                HGDIOBJ ob = SelectObject(hdc_hdr, hDotBr);
                HGDIOBJ op = SelectObject(hdc_hdr, hDotPn);
                Ellipse(hdc_hdr, dot_x, dot_y,
                        dot_x + dot_sz, dot_y + dot_sz);
                SelectObject(hdc_hdr, op);
                SelectObject(hdc_hdr, ob);
                DeleteObject(hDotPn);
                DeleteObject(hDotBr);

                /* Draw one-word status */
                const char *word;
                switch (d->activity.phase) {
                case ACTIVITY_PROCESSING: word = "Processing"; break;
                case ACTIVITY_THINKING:   word = "Thinking";   break;
                case ACTIVITY_RESPONDING:  word = "Responding"; break;
                case ACTIVITY_EXECUTING:   word = "Executing";  break;
                case ACTIVITY_WAITING:     word = "Waiting";    break;
                default:                   word = "";           break;
                }
                if (word[0]) {
                    SetBkMode(hdc_hdr, TRANSPARENT);
                    SetTextColor(hdc_hdr, dot_clr);
                    RECT wrc;
                    wrc.left   = dot_x + dot_sz + HP(4);
                    wrc.top    = top_y_h;
                    wrc.right  = rc_lbl.right;
                    wrc.bottom = top_y_h + bar_h_h;
                    DrawTextA(hdc_hdr, word, -1, &wrc,
                              DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
                }

                SelectObject(hdc_hdr, old_f);
            }
            #undef HP
            EndPaint(hwnd, &ps_hdr);
            return 0;
        }
        break;  /* Let DefWindowProc handle WM_PAINT when idle */

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
                    HBRUSH hBr = CreateSolidBrush(theme_cr(bg_rgb));
                    HPEN hPen = CreatePen(PS_SOLID, 1, theme_cr(d->theme->border));
                    HGDIOBJ oBr = SelectObject(hdc, hBr);
                    HGDIOBJ oPen = SelectObject(hdc, hPen);
                    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
                    SelectObject(hdc, oPen);
                    SelectObject(hdc, oBr);
                    DeleteObject(hPen);
                    DeleteObject(hBr);

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

                    /* Icon — same font as other panel icons */
                    HFONT prev = (HFONT)SelectObject(hdc,
                        d->hIconFont ? d->hIconFont : d->hFont);
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, theme_cr(0xFFFFFF));
                    DrawTextW(hdc, btn_text, -1, &rc,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hdc, prev);
                }
            } else if ((int)dis->CtlID == IDC_CHAT_SAVE) {
                /* Square save button with floppy disk icon */
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                int pressed = (dis->itemState & ODS_SELECTED) != 0;
                COLORREF bg = theme_cr(pressed ? d->theme->bg_primary
                                               : d->theme->bg_secondary);
                COLORREF fg = theme_cr(d->theme->text_main);
                COLORREF bdr = theme_cr(d->theme->border);

                HBRUSH hBgBr = CreateSolidBrush(theme_cr(d->theme->bg_primary));
                FillRect(hdc, &rc, hBgBr);
                DeleteObject(hBgBr);

                HBRUSH hBr = CreateSolidBrush(bg);
                HPEN hPen = CreatePen(PS_SOLID, 1, bdr);
                HGDIOBJ oBr = SelectObject(hdc, hBr);
                HGDIOBJ oPen = SelectObject(hdc, hPen);
                RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
                SelectObject(hdc, oPen);
                SelectObject(hdc, oBr);
                DeleteObject(hPen);
                DeleteObject(hBr);

                /* Draw Fluent UI floppy disk icon (\xE74E Save) */
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, fg);
                HFONT prevF = (HFONT)SelectObject(hdc, d->hIconFont ? d->hIconFont : d->hFont);
                RECT txtRc = rc;
                DrawTextW(hdc, L"\xE74E", -1, &txtRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, prevF);
            } else if ((int)dis->CtlID == IDC_CHAT_UNDOCK) {
                /* Square undock button with 3D pop-out/dock-in icon */
                HDC hdc = dis->hDC;
                RECT brc = dis->rcItem;
                int pressed = (dis->itemState & ODS_SELECTED) != 0;
                COLORREF bg = theme_cr(pressed ? d->theme->bg_primary
                                               : d->theme->bg_secondary);
                COLORREF fg = theme_cr(d->theme->text_main);
                COLORREF bdr = theme_cr(d->theme->border);

                HBRUSH hBgBr = CreateSolidBrush(theme_cr(d->theme->bg_primary));
                FillRect(hdc, &brc, hBgBr);
                DeleteObject(hBgBr);

                HBRUSH hBr = CreateSolidBrush(bg);
                HPEN hPen = CreatePen(PS_SOLID, 1, bdr);
                HGDIOBJ oBr = SelectObject(hdc, hBr);
                HGDIOBJ oPen = SelectObject(hdc, hPen);
                RoundRect(hdc, brc.left, brc.top, brc.right, brc.bottom, 6, 6);
                SelectObject(hdc, oPen);
                SelectObject(hdc, oBr);
                DeleteObject(hPen);
                DeleteObject(hBr);

                /* Draw Fluent UI Pop-out/Dock icon */
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, fg);
                HFONT prevF = (HFONT)SelectObject(hdc, d->hIconFont ? d->hIconFont : d->hFont);
                /* \xE8A7 = FullScreen (outer arrows), \xE923 = BackToWindow */
                DrawTextW(hdc, d->docked ? L"\xE8A7" : L"\xE923", -1, &brc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(hdc, prevF);
            /* Old IDC_CHAT_ALLOW / IDC_CHAT_DENY draw code removed —
             * approval buttons are now inline in chat_listview */
            } else {
                draw_tab_button(dis, d->theme, d);
            }
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
            /* Save conversation back to session before cleanup */
            if (d->active_state) {
                memcpy(&d->active_state->conv, &d->conv, sizeof(AiConversation));
                d->active_state->valid = 1;
            }
            thinking_history_clear(d);
            chat_msg_list_clear(&d->msg_list);
            d->stream_ai_item = NULL;
            DeleteCriticalSection(&d->cs);
            if (d->hFont) DeleteObject(d->hFont);
            if (d->hSmallFont) DeleteObject(d->hSmallFont);
            if (d->hIconFont) DeleteObject(d->hIconFont);
            if (d->hBoldFont) DeleteObject(d->hBoldFont);
            if (d->hMonoFont) DeleteObject(d->hMonoFont);
            if (d->hTooltip) DestroyWindow(d->hTooltip);
            if (d->hBrBgPrimary)   DeleteObject(d->hBrBgPrimary);
            if (d->hBrBgSecondary) DeleteObject(d->hBrBgSecondary);
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
    d->paste_delay_ms = paste_delay_ms;
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
        memcpy(&d->conv, &initial_state->conv, sizeof(AiConversation));
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
        docked ? 1 : MulDiv(500, pdpi, 96),
        docked ? 1 : MulDiv(600, pdpi, 96),
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

    /* Update context bar background to match theme */
    if (d->hContextBar)
        SendMessage(d->hContextBar, PBM_SETBKCOLOR, 0,
                    (LPARAM)theme_cr(d->theme->bg_primary));

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
        memcpy(&d->active_state->conv, &d->conv, sizeof(AiConversation));
        d->active_state->valid = 1;
    }

    /* Kill command timers — they belong to the old session */
    KillTimer(d->hwnd, TIMER_CMD_QUEUE);
    KillTimer(d->hwnd, TIMER_CONTINUE);

    /* Save pending approval state to old session (heap-allocated) */
    if (d->active_state && d->active_state != new_state) {
        /* Free any previous pending commands */
        free(d->active_state->pending_cmds);
        d->active_state->pending_cmds = NULL;
        d->active_state->pending_approval = d->pending_approval;
        d->active_state->pending_cmd_count = 0;
        if (d->pending_approval && d->queued_count > 0) {
            size_t sz = (size_t)d->queued_count * sizeof(d->queued_cmds[0]);
            d->active_state->pending_cmds = malloc(sz);
            if (d->active_state->pending_cmds) {
                memcpy(d->active_state->pending_cmds,
                       d->queued_cmds, sz);
                d->active_state->pending_cmd_count = d->queued_count;
            }
        }
        /* Save auto-approve and activity phase to old session */
        d->active_state->auto_approve = d->approval_q.auto_approve;
        d->active_state->activity_phase = (int)d->activity.phase;
    }

    /* Clear thinking history — it belongs to the old session */
    thinking_history_clear(d);

    /* Load new session's conversation */
    if (new_state && new_state != d->active_state) {
        if (new_state->valid) {
            memcpy(&d->conv, &new_state->conv, sizeof(AiConversation));
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
    d->commands_executed = 0;
    d->pending_request[0] = '\0';
    d->stream_phase = 0;

    /* Restore pending approval state from new session */
    if (new_state && new_state->pending_approval &&
        new_state->pending_cmds && new_state->pending_cmd_count > 0) {
        int nc = new_state->pending_cmd_count;
        if (nc > 16) nc = 16;
        memcpy(d->queued_cmds, new_state->pending_cmds,
               (size_t)nc * sizeof(d->queued_cmds[0]));
        d->queued_count = nc;
        d->queued_next = 0;
        d->pending_approval = 1;
    } else {
        d->pending_approval = 0;
        d->queued_count = 0;
        d->queued_next = 0;
    }

    /* Restore auto-approve and activity phase from new session */
    if (new_state) {
        d->approval_q.auto_approve = new_state->auto_approve;
        chat_activity_set_phase(&d->activity,
                                (ActivityPhase)new_state->activity_phase, 0.0f);
    } else {
        d->approval_q.auto_approve = 0;
        chat_activity_reset(&d->activity);
    }

    relayout(d);

    chat_rebuild_display(d);

    /* Re-show the command approval prompt if switching back to a
     * session with pending approval */
    if (d->pending_approval && d->queued_count > 0) {
        char confirm[4096];
        size_t clen = ai_build_confirm_text(d->queued_cmds,
                         d->queued_count, confirm, sizeof(confirm));
        if (clen == 0)
            snprintf(confirm, sizeof(confirm),
                     "Execute %d command(s)?", d->queued_count);
        chat_msg_append(&d->msg_list, CHAT_ITEM_STATUS, confirm);
    }

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
        memcpy(&d->active_state->conv, &d->conv, sizeof(AiConversation));
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

    /* Free heap-allocated pending commands */
    free(state->pending_cmds);
    state->pending_cmds = NULL;

    /* Free per-session stream buffers */
    free(state->stream_content);
    state->stream_content = NULL;
    free(state->stream_thinking);
    state->stream_thinking = NULL;
    state->busy = 0;

    if (d->active_state == state)
        d->active_state = NULL;
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
