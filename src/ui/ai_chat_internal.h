/* Shared types, constants and inline helpers of the AI chat panel, used by
   the ai_chat*.c files. Prototypes of cross-file functions go at the end. */
#ifndef NUTSHELL_AI_CHAT_INTERNAL_H
#define NUTSHELL_AI_CHAT_INTERNAL_H

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
#include "run_command.h"
#include "ai_http.h"
#include "ai_stream.h"
#include "ai_data_block.h"
#include "crypto.h"
#include "app_font.h"
#include "ns_font.h"
#include "ns_draw.h"
#include "ns_tokens.h"
#include "ns_scale.h"
#include "ns_type.h"
#include "ns_reduced_motion.h"
#include "ns_hover.h"
#include "ai_panel_layout.h"
#include "text_lines.h"
#include "secure_zero.h"
#include "ai_panel_states.h"
#include "settings_layout.h"
#include "ui_theme.h"
#include "themed_button.h"
#include "custom_scrollbar.h"
#include "edit_scroll.h"
#include "term_extract.h"
#include "profile_kind.h"
#include "session_io.h"
#include "resource.h"
#include "ai_dock.h"
#include "string_utils.h"
#include "paste_filter.h"
#include "chat_msg.h"
#include "chat_thinking.h"
#include "chat_activity.h"
#include "chat_approval.h"
#include "cmd_detail_dlg.h"
#include "cmd_batch.h"
#include "dispatch_line_clear.h"
#include "dlog.h"
#include "dlog_query.h"
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
#define IDC_CHAT_AUTOAPPROVE 4015  /* toggles the Auto-approve button */
/* The level's absolute setter: post 4030 + stop to put the level on a
 * named stop. The integration harness drives the control through these
 * and 4015 (docs/superpowers/specs/2026-10-18-ai-auto-approve-button-
 * design.md section 2). */
#define IDC_CHAT_POLICY_ALLOW_BASE 4030  /* 4030..4033: allowed = stop 0..3 */
/* 4040..4044 set the old unattended marker. Retired 2026-10-18 with the
 * rail: they are swallowed without effect so a stale caller can never
 * fall through to another handler. */
#define IDC_CHAT_POLICY_RETIRED_BASE 4040
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
/* Sent by the integration harness: returns cmd_policy_status_word() of
 * the displayed session's policy -- bits 0-7 the level, bit 8 Auto-approve. */
#define WM_AI_GET_POLICY (WM_USER + 104)
/* Posted by a stream's thread for a tool call that needs the user's say
 * (spec 2026-11-05-trust-boundary-design.md decision 15). lParam: a heap
 * AiToolDecideMsg the UI handler (or the drain) frees. */
#define WM_AI_TOOL_DECIDE (WM_USER + 105)

#define TIMER_CMD_QUEUE   2     /* Command dispatcher poll (prompt-gated) */
#define TIMER_SCROLL_SYNC 4     /* Sync custom scrollbar with RichEdit */
#define TIMER_THINKING    5     /* Animated thinking indicator */
#define TIMER_HEARTBEAT   6     /* Activity monitor heartbeat (1s) */
#define TIMER_STREAM_REAP 7     /* Finds replies whose final message was lost */
#define STREAM_REAP_MS    1000
#define THINKING_ANIM_MS  400   /* Dot animation interval */
#define HEARTBEAT_MS      1000  /* Heartbeat interval */
#define CMD_QUEUE_POLL_MS 250   /* Dispatcher tick interval */
#define PROMPT_QUIET_MS   400   /* Terminal must be quiet this long at a prompt */

/* Every AiChatData field is read and written on the UI thread only. The AI
 * stream thread (ai_chat_stream_thread_proc) owns nothing here -- it reads and
 * writes only its own AiStream (src/core/ai_stream.h), including its own
 * copy of the tool registry and search/fetch contexts snapshotted before
 * the thread starts (ai_stream_copy_tools(), called under ai_chat_launch_stream_thread
 * before _beginthreadex) -- so no lock guards this struct. */
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
     * chat_listview and the session policy is shown/clicked in the status
     * line (painted, not child windows; see ai_chat_status_hit()). */
    /* Pending command batches: approval_q.entries/count are unused --
     * decisions live per-batch in d->active_state->batches (CmdBatchSet).
     * approval_q survives only as the panel-wide template of the session's
     * CmdPolicy, copied into every new batch by cmd_batch_add(). It is the
     * one place the live policy lives; the status-line control reads and
     * writes it directly. */
    ApprovalQueue approval_q;
    CmdPolicy policy_default;  /* settings.ai_policy_default: seeds a fresh
                                 * session's policy (see
                                 * ai_chat_set_policy_default()). */
    unsigned session_gen;      /* bumped by do_session_switch(): a policy
                                 * confirmation answered after a switch
                                 * applies nothing (ai_chat_request_policy_change()) */
    int policy_confirm_open;   /* 1 while the Critical confirmation is up:
                                 * every policy gesture is then a no-op */
    HWND hThinkingBtn;
    HWND hTooltip;        /* Win32 tooltip control */
    /* Hover/press tracking for the painted status line (the policy control's
     * four cells and the Auto-approve button) -- see ai_chat_status_hit()/ai_chat_status_rect(). */
    NsHover status_hover;
    int status_hover_tracking;  /* TrackMouseEvent armed for WM_MOUSELEAVE */
    int show_thinking;    /* 1 = user has manually opened a Thinking
                            * disclosure this session -- suppresses
                            * auto-collapse at reply-start (chat_listview.c)
                            * and is also passed to ai_build_save_text() so
                            * a session where the user looked at reasoning
                            * saves it too. Mirrors the policy: persisted
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
    SessionIo  *active_io;   /* transport of the active session, or NULL */
    /* Local sessions only: the shell the resolver picked, as
     * ai_build_system_prompt() names it. Empty means "not a local shell" --
     * for an SSH session, and for a local one before window.c has set it. */
    char        shell_name[32];

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
     * which. See ai_chat_dispatch_start()/ai_chat_dispatch_tick(). */
    int dispatch_active;             /* 1 while the dispatcher is running */
    int dispatch_batch_id;           /* CmdBatch id currently being dispatched, or 0 */
    unsigned long dispatch_seq;      /* last-seen active_term->write_seq */
    int dispatch_await_echo;         /* 1 until the terminal changes after a send */
    DWORD dispatch_last_change_tick; /* GetTickCount() of the last write_seq change */
    int dispatch_last_idx;           /* batch queue index last set EXECUTING, or -1 */
    int dispatch_sent_count;         /* commands sent so far in this dispatch run */
    int dispatch_stall_reported;     /* 1 once the "shell is waiting for more
                                       * input" status line has been posted for
                                       * the current stall; cleared the moment
                                       * the terminal produces output again, so
                                       * the line appears once per stall */
    /* No-prefix safety wait (dispatch_line_clear.h): set the first tick
     * the command about to be sent finds the line ambiguous (or a
     * keystroke too recent) rather than clearly clear; 0 while not
     * currently waiting on it. Mirrors dispatch_stall_reported's
     * report-once behaviour, but unlike a continuation-prompt stall this
     * one is bounded -- see dispatch_ambiguous_prompt_timed_out(). */
    DWORD dispatch_ambiguous_since_tick;
    int   dispatch_ambiguous_reported; /* 1 once this stall's status line
                                         * has been posted */
    /* Diagnostic-only (dispatch_log_stall_if_due(), %TEMP%\nutshell.log):
     * dispatch_start_tick is set once per ai_chat_dispatch_start() and never
     * touched again, for "settled after X ms". dispatch_last_progress_tick
     * is set at ai_chat_dispatch_start() and every actual send (execute_command())
     * -- a batch that hasn't moved either in >3 s is "stalled".
     * dispatch_last_diag_log_tick rate-limits the stall log itself to
     * once per 2 s so a long-stuck batch does not flood the file. */
    DWORD dispatch_start_tick;
    DWORD dispatch_last_progress_tick;
    DWORD dispatch_last_diag_log_tick;
    int stream_phase;  /* 0=not started, 1=in thinking, 2=in content */

    /* AI notes for system prompt context */
    char session_notes[2560];
    char system_notes[2560];

    /* How many terminal lines are sent to the AI as context (1-50000).
     * Always clamped via ai_context_clamp_lines(); never 0. */
    int context_lines;

    /* Seconds a tool approval card waits before it expires, from
     * settings.ai_tool_timeout_sec (15-3600, 0 = never); each launched stream copies it
     * into AiStream.tool_timeout_ms, so a change reaches later streams only. */
    int tool_timeout_sec;

    /* Automatic tool rounds before a call asks again, from
     * settings.ai_tool_auto_rounds (1-20); each launched stream copies it into
     * AiStream.tool_auto_rounds, so a change reaches later streams only. */
    int tool_auto_rounds;

    /* Scratch buffers for the terminal context and the system prompt built
     * from it.  They live as long as the panel and grow with context_lines,
     * so the send paths never allocate and cannot leak on an early return.
     * Freed in WM_DESTROY. */
    char  *ctx_term;
    size_t ctx_term_cap;
    char  *ctx_prompt;
    size_t ctx_prompt_cap;

    /* Bytes of terminal text the last launched request carried in its last
     * user turn (0: none). The context meter adds it to the conversation,
     * because the snapshot is not part of d->conv. */
    size_t last_snapshot_len;

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
     * Set by ai_chat_start_indicator(), cleared by ai_chat_update_context_bar(). */
    char context_label[64];
    int  actual_input_tokens;  /* last known input tokens from API (0 if unavailable) */
    int  actual_output_tokens; /* last known output tokens from API (0 if unavailable) */

    /* Buffer for the status-line tooltips' text (cells, button, meter). Filled on
     * each TTN_GETDISPINFO callback so the tip always reflects the
     * latest token state. */
    char tooltip_buf[CMD_POLICY_TIP_MAX];

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
    /* Composer (message box) height at 96 DPI, set by dragging the sash
     * above it; 0 means AI_INPUT_BASE_H. Scaled and clamped by
     * ai_chat_input_h(). Not persisted. */
    int  input_base_h;
    int  sash_dragging;         /* 1 while the sash is captured */
    int  sash_drag_start_y;     /* client y at button-down */
    int  sash_drag_start_base;  /* input_base_h at button-down */

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

    /* Replies in flight, one per session at most (src/core/ai_stream.h).
     * Each stream has its own abort flag; stream-thread messages carry a
     * stream id that is resolved here before anything is touched. */
    AiStreamTable streams;

    /* GDI+ token for image conversion */
    ULONG_PTR gdip_token;

    /* Pending image attachment for the next send */
    AiAttachment *pending_attachment;

    /* Tool use */
    AiToolRegistry tool_registry;
    WebSearchContext search_ctx;
    WebFetchContext  fetch_ctx;
    unsigned tools_gen;         /* bumped on every reconfigure; each stream
                                  * records the generation it snapshotted */
    int tool_support_notified;  /* 1 after showing "tools unavailable" message */
    int runcmd_mode;            /* "Commands as tool calls": AI_RUNCMD_AUTO (0, the zero value),
                                 * _ON or _OFF; ai_chat_set_runcmd_mode() sets it */

    /* Empty/no-key/no-session state (ai_panel_states.h), pushed to
     * hChatList whenever it might change -- see ai_chat_update_panel_state()
     * below (AI Assist Panel task 4). -1 = no forced override: the state
     * is decided from active_io/api_key. --ui-demo's "empty" state
     * forces AI_STATE_EMPTY via ai_chat_force_state() even though the
     * demo session has no transport (it would otherwise read as
     * AI_STATE_NO_SESSION). */
    int forced_state;
} AiChatData;

/* Session kind for the system prompt: SSH unless the active transport
 * says otherwise (a panel with no session keeps today's SSH wording). */
static inline SessionKind active_session_kind(const AiChatData *d)
{
    return (d && d->active_io) ? d->active_io->kind : SESSION_SSH;
}

/* The shell name that goes with that kind: NULL unless a local session has
 * actually told us which shell it is, in which case
 * ai_build_system_prompt() names it instead of listing the possibilities. */
static inline const char *active_shell_name(const AiChatData *d)
{
    return (d && d->shell_name[0]) ? d->shell_name : NULL;
}

/* Helper: check if the currently active session has a busy AI stream */
#define ACTIVE_BUSY(d) ((d)->active_state && (d)->active_state->busy)

/* Messages from a stream thread carry its stream id, never a pointer into
 * the panel or a session: the UI resolves the id in d->streams
 * (src/core/ai_stream.h) and drops the message if the stream is gone --
 * stopped, retried, its tab closed, or the panel rebuilt since. */

/* WM_AI_RESPONSE payload: the finished reply (wParam 2) or an error (0). */
typedef struct {
    unsigned stream_id;
    char *content;
    char *thinking;
    int input_tokens;          /* actual input token count from API (0 if unavailable) */
    int output_tokens;         /* actual output token count from API (0 if unavailable) */
    int reply_stored;          /* 1 when the stream thread's own ai_conv_add() of
                                * the reply into its conversation succeeded: the
                                * reply is then that conversation's last message.
                                * 0 on an error, a refused add (a full
                                * conversation) and the tool-only paths. */
    AiToolCall *calls;         /* a run_command stop: the stored turn's calls
                                * (ids as stored), heap-copied by the thread; NULL
                                * otherwise. Freed by ai_response_msg_free. */
    int n_calls;               /* how many; 0 when calls is NULL */
    int exec_inert;            /* the stream's run_command flag: this reply's [EXEC]
                                * text is not acted on (set on every done message) */
    int fetch_registered;      /* the stream's registry held web_fetch (planner input) */
} AiResponseMsg;

/* Free the message and what it owns (content, thinking, calls). NULL-safe.
 * Every site that drops an AiResponseMsg uses it. */
void ai_response_msg_free(AiResponseMsg *m);

/* WM_AI_STREAM payload (one delta) and WM_AI_TOOL_MSG payload (one status
 * line; wParam is the ChatItemType). */
typedef struct {
    unsigned stream_id;
    char *delta;
} AiStreamChunk;

/* WM_AI_TOOL_DECIDE payload. ASK: put a card up for the call published in
 * the stream's decision slot under `gen`. EXPIRED: the thread gave up waiting
 * on that card (same stream id and gen), so the row reads "not run" and its
 * batch goes. `row` and `detail` come from ai_tool_card_text(). */
enum { AI_TOOL_DECIDE_ASK = 0, AI_TOOL_DECIDE_EXPIRED = 1 };
typedef struct {
    unsigned   stream_id, gen;
    int        loop_iter, kind;
    ToolSafety safety;
    char       row[256], detail[1024];
} AiToolDecideMsg;

/* Status-line hover/hit ids for ns_hover: 0..3 the four cells, 4 the
 * Auto-approve button; -1 stays "nothing" (spec 2026-10-18 section 3). */
#define STATUS_HIT_CELL(stop) (stop)
#define STATUS_HIT_AUTO       NS_POLICY_STOPS
#define STATUS_HIT_COUNT      (NS_POLICY_STOPS + 1)

/* Tooltip tool ids on the panel: STATUS_TOOL_BASE + hit id (cells 0..3,
 * button 4), then the context meter. */
#define STATUS_TOOL_BASE  100
#define STATUS_TOOL_METER STATUS_HIT_COUNT
#define STATUS_TOOL_COUNT (STATUS_HIT_COUNT + 1)

typedef struct {
    NsRect status;
    AiStatusLayout sl;
    NsPolicyLayout pl;
    char meter_text[32];
    int has_meter;   /* context_limit > 0 -- meter numbers are meaningful */
} AiStatusPaint;

#define BTN_NOERASE_SUBCLASS_ID 43

/* Default message-box height at 96 DPI (about two lines) before the user
 * drags the sash. */
#define AI_INPUT_BASE_H 46

/* Prototypes */

/* ai_chat_stream.c */
unsigned __stdcall ai_chat_stream_thread_proc(void *raw_arg);
void ai_chat_fetch_on_hop(void *user, const char *display_url, int refused);

/* ai_chat_input.c */
LRESULT CALLBACK ai_chat_input_subclass_proc(HWND hwnd, UINT msg,
                                             WPARAM wParam, LPARAM lParam,
                                             UINT_PTR uIdSubclass,
                                             DWORD_PTR dwRefData);
int ai_chat_input_paste_text(HWND hInput, HWND clip_owner);
void ai_chat_input_sync_scroll(AiChatData *d);

/* ai_chat_dispatch.c */
CmdBatch *ai_chat_resolve_batch(AiChatData *d, LPARAM lParam);
void ai_chat_settle_batch_if_done(AiChatData *d, CmdBatch *batch);
/* The conversation `owner` reads and writes: the panel's working copy for
 * the displayed session, the session's own otherwise. */
AiConversation *ai_chat_owner_conv(AiChatData *d, AiSessionState *owner);
/* The one way a Nutshell-written USER turn (a NOTE, the policy notes, the
 * [EXEC] continue text) enters a conversation: ai_conv_add_user_note() on the
 * owner's conversation and note queue, with `defer` = a stream owned by
 * `owner` is still registered (its thread works on a copy of the
 * conversation, so a turn stored now would be lost by the take; inside
 * WM_AI_RESPONSE the stream is already finished and nothing defers).
 * `keep_free` is 1 when the text launches a request (the continue text), 0
 * otherwise. `owner` NULL means the panel's own conversation with no queue.
 * Returns the door's result (0 stored, 1 queued, 2 conversation full, -1
 * failed). On 2 a row is shown when `owner` is the displayed session: the
 * conversation-full notice for a launching text, "a Nutshell note was not
 * stored" otherwise; a background owner's list is not drawn. */
int ai_chat_owner_add_note(AiChatData *d, AiSessionState *owner,
                           const char *text, int keep_free);
/* Record a command batch in `owner`'s run log (cmd_run_log.h), then remove it
 * from the owner's set and free it: the one way a command batch leaves the
 * set, so a rebuilt display can show what ran (spec 2026-11-05 run-command
 * tool, decision 21). `batch_ending` is 1 for every removal: a removed batch
 * never runs again, so its replay must not read "queued" or "running". A tool
 * batch is removed without a record. `batch` is freed. */
void ai_chat_batch_remove_recorded(AiChatData *d, AiSessionState *owner,
                                   CmdBatch *batch, int batch_ending);
/* Call before cmd_batch_add() on `owner`'s set: when the set is full the add
 * evicts the oldest batch, which is recorded here first (batch_ending 1) so
 * its commands are not lost. Does nothing when there is room. */
void ai_chat_batch_record_oldest_if_full(AiChatData *d, AiSessionState *owner);
/* Answer a run_command reply that has no batch (spec decisions 5 and 7): one
 * result per call of `r` in call order (runcmd_build_results with no queue
 * under `cause`: NO_SLOT, SESSION_GONE or SET_FULL), fitted to the budget and
 * stored in `owner`'s conversation against the open assistant tool turn, its
 * calls passed in the stored order so each part is recognised as run_command;
 * then runcmd_age_results and a flush of the owner's queued notes (keep_free
 * 0). The return of the store is handled as spec decision 25: 1, -1 and -2
 * each show a status row (the turn is already dropped on a negative one).
 * Appends the status row of spec decision 5 for `cause` when `owner` is
 * displayed (a background owner's list is not drawn; the rebuild shows the
 * stored results). It never relaunches: no model request starts here. Frees
 * `r`. When even the results array cannot be had, the open turn is dropped
 * (ai_conv_drop_open_tool_turn) and a row says so. */
void ai_chat_runcmd_answer_reply(AiChatData *d, AiSessionState *owner,
                                 RunCmdReply *r, RunCmdCause cause);
/* The planner-failure answer (runcmd_plan or cmd_batch_add ran out of memory,
 * so there is no RunCmdReply): every call of the open stored tool turn is
 * answered "not run: Nutshell ran out of memory" (runcmd_oom_results, R41),
 * stored as above; the turn is dropped when even that array cannot be had.
 * Never relaunches. */
void ai_chat_runcmd_answer_oom(AiChatData *d, AiSessionState *owner);
void ai_chat_dispatch_start(AiChatData *d, int batch_id);
void ai_chat_maybe_start_next_batch(AiChatData *d);
/* Stop the dispatcher (see ai_chat_dispatch.c). `cause` is the RunCmdCause the
 * running batch is answered with when it is a run_command batch (read from
 * runcmd_path_plan(<the path>).cause at each caller); an [EXEC] batch ignores it. */
void ai_chat_dispatch_cancel(AiChatData *d, const char *status_msg, int notify,
                             RunCmdCause cause);
/* The one end of a run_command batch (spec decisions 8 and 9): capture the
 * EXECUTING entry's output where the cause says the terminal may exist, build
 * the results, store them in `owner`'s conversation when the cause appends,
 * settle the rows when `owner` is displayed, record and remove the batch (freed),
 * and relaunch only where runcmd_finish_relaunches says so. Everything runs on
 * the UI thread. */
void ai_chat_runcmd_finish(AiChatData *d, AiSessionState *owner, CmdBatch *batch,
                           RunCmdCause cause);
void ai_chat_dispatch_tick(AiChatData *d);
/* Tool cards (trust boundary spec decisions 14, 15, 20). The slot answer is
 * found by the batch's stream id and generation; no stream: NONE. Returns the
 * state that holds afterwards (the CAS winner). The card answer is the Run
 * (APPROVED) / Deny (NOT_APPROVED) path: answer the slot first, then label
 * the rows from the result, then settle. The handler never waits and frees
 * `m` on every path. */
ToolDecision ai_chat_tool_slot_answer(AiChatData *d, const CmdBatch *batch,
                                      ToolDecision answer);
void ai_chat_tool_card_answer(AiChatData *d, CmdBatch *batch, ToolDecision answer);
void ai_chat_on_tool_decide(AiChatData *d, AiToolDecideMsg *m);
/* The sweep: answer every tool card of `state` CANCELLED (slot first), settle
 * the displayed rows and remove the batches. Command batches are untouched.
 * Called before anything that frees or abandons the stream or the set. */
void ai_chat_cancel_tool_batches(AiChatData *d, AiSessionState *state);

/* ai_chat_send.c */
void ai_chat_register_tools(AiChatData *d, const char *search_provider,
                            const char *search_url, int max_search_results,
                            int web_fetch_enabled);
void ai_chat_launch_stream_thread(AiChatData *d, const char *src);
/* `keep_free`: the slots the flush of the owner's queued notes leaves free: 1
 * where a launch follows the abort (Retry: runcmd_cause_keep_free of RETRY),
 * 0 otherwise (Stop, New Chat, a closed session, the reaper, WM_DESTROY). */
void ai_chat_abort_session_stream(AiChatData *d, AiSessionState *state,
                                  int keep_free);
void ai_chat_drain_stream_messages(HWND hwnd);
void ai_chat_cancel_active_stream(AiChatData *d);
void ai_chat_send_user_message(AiChatData *d);
void ai_chat_send_continue_message(AiChatData *d, const char *msg_text,
                                   int unattended);
/* Relaunch from a conversation that now ends on a run_command results turn: no
 * USER turn. Same guard as the continue message; counts as a round
 * (ai_rounds_continue), flushes the displayed session's queued notes leaving
 * the reply's slot free, and launches the stream. Returns 1 when a stream is
 * in flight, 0 when nothing launched (the guard, or a launch that failed and
 * said so in a status row): the run_command finish then takes its no-relaunch
 * tail. */
int ai_chat_send_tool_results(AiChatData *d, int unattended);

/* ai_chat_status.c */
void ai_chat_invalidate_status_line(AiChatData *d);
void ai_chat_request_policy_change(HWND hwnd, AiChatData *d, CmdPolicy next);
void ai_chat_update_context_bar(AiChatData *d);
int ai_chat_sash_hit(AiChatData *d, int x, int y);
void ai_chat_paint_sash(AiChatData *d, HDC hdc);
void ai_chat_get_status_paint(AiChatData *d, HDC hdc, AiStatusPaint *out);
int ai_chat_status_hit(AiChatData *d, int x, int y);
int ai_chat_status_rect(AiChatData *d, int id, RECT *out_rc);
void ai_chat_status_tools_update(AiChatData *d);
void ai_chat_paint_status_line(AiChatData *d, HDC hdc);

/* ai_chat_view.c */
void ai_chat_start_indicator(AiChatData *d, const char *base);
void ai_chat_compute_layout(AiChatData *d, AiPanelLayout *out);
RECT ai_chat_to_RECT(NsRect r);
LRESULT CALLBACK ai_chat_btn_noerase_subclass(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
    UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
int ai_chat_unscale(int px, int dpi);
int ai_chat_input_h(const AiChatData *d);
void ai_chat_update_panel_state(AiChatData *d);
void ai_chat_thinking_history_clear(AiChatData *d);

/* Settle `batch`'s rows in the displayed list (chat_msg_batch_settle) and
 * remember it on the batch (CmdBatch.settled), which a rebuild reads. The one
 * place a live batch's rows are settled while the batch can outlive the call. */
void ai_chat_batch_settle_rows(AiChatData *d, CmdBatch *batch);
void ai_chat_rebuild_display(AiChatData *d);
void ai_chat_add_tooltip(HWND hTooltip, HWND hCtrl, const char *text);
void ai_chat_draw_header_icon_button(LPDRAWITEMSTRUCT dis,
                                     const ThemeColors *theme, int dpi,
                                     NsIconId icon);
void ai_chat_relayout(AiChatData *d);
void ai_chat_paint_header(AiChatData *d, HDC hdc);
void ai_chat_header_sync(AiChatData *d);
void ai_chat_header_invalidate(AiChatData *d);
void ai_chat_apply_zoom(AiChatData *d, int delta);

#endif /* _WIN32 */

#endif /* NUTSHELL_AI_CHAT_INTERNAL_H */
