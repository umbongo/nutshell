#ifndef NUTSHELL_RUN_COMMAND_H
#define NUTSHELL_RUN_COMMAND_H

#include "ai_prompt.h"
#include "ai_tools.h"
#include "chat_approval.h"
#include "term.h"

#include <stddef.h>
#include <stdint.h>

/* The run_command tool: the assistant's way of running one terminal command
 * through an ordinary approval card (spec 2026-11-05-run-command-tool-design.md,
 * sections 1 and 2). This file is the pure part: the tool's definition, the
 * parser for its input and the text of its failures. The panel intercepts the
 * call before the registry runs it; no window code and no I/O live here. */

#define RUNCMD_TOOL_NAME    "run_command"
#define RUNCMD_COMMAND_MAX  1023   /* bytes, terminator excluded */
#define RUNCMD_NOTE_MAX     256    /* buffer size: the note is cut to 255 bytes */

/* Fill out with the tool: name, description and schema as the spec gives
 * them, safety TOOL_CRITICAL. Its execute is a stub that fails with
 * "run_command is handled by the panel", so a call that slips past the
 * panel's intercept gets a card and then that error, never a run. */
void runcmd_tool_def(AiToolDef *out);

/* Add the tool to reg. 0 on success, -1 when reg is NULL or full. */
int  runcmd_register(AiToolRegistry *reg);

typedef enum {
    RUNCMD_PARSE_OK,
    RUNCMD_PARSE_NO_COMMAND,   /* not JSON, not an object, no string "command", or blank */
    RUNCMD_PARSE_TOO_LONG,     /* more than RUNCMD_COMMAND_MAX bytes */
    RUNCMD_PARSE_UNSAFE        /* a control or bidirectional formatting character */
} RunCmdParse;

/* Read the tool's input. On OK, command holds the string exactly as sent
 * (nothing is trimmed) and note the optional note, cut to 255 bytes on a
 * UTF-8 boundary, "" when absent, not a string, or containing an unsafe
 * character (a bad note never stops the command). A command that is blank
 * (only spaces and tabs) is NO_COMMAND. On any failure both outputs are "". */
RunCmdParse runcmd_parse_input(const char *input_json,
                               char command[RUNCMD_COMMAND_MAX + 1],
                               char note[RUNCMD_NOTE_MAX]);

/* The text for a failed parse: the is_error result the model reads. "" for
 * OK. */
const char *runcmd_parse_error(RunCmdParse p);

/* The command for the "using tool" row: 1 and the command, cut to cap - 1
 * bytes on a UTF-8 boundary, or 0 and "" when the input does not parse. A
 * NULL out or cap 0 only returns the verdict. */
int  runcmd_describe(const char *input_json, char *out, size_t cap);

/* 1 when a rejected request deserves the hint row: HTTP 400 or 422 whose
 * error text contains, compared case-insensitively, "tool", "function" or
 * "reasoning_content". Never for another status (401, 402, 429), nor for
 * status 0 (an in-stream error object), nor a 400 without those words (a
 * context-length error). */
int  runcmd_reject_hint_applies(int http_status, const char *error_text);

/* The hint row's text. */
const char *runcmd_reject_hint(void);

/* ---- The planner and the reply's per-call state (spec decisions 4 to 6) ---- */

typedef enum {
    RUNCMD_KIND_SLOT,         /* a valid run_command call that has an approval slot */
    RUNCMD_KIND_OVER_CAP,     /* valid, but past APPROVAL_MAX_CMDS slots */
    RUNCMD_KIND_INVALID,      /* a run_command call whose input does not parse */
    RUNCMD_KIND_OTHER_TOOL    /* any other tool in a reply that calls run_command */
} RunCmdKind;

#define RUNCMD_CAP_SCREEN        1u   /* the excerpt is the screen rows, not the command's own range:
                                       * the alternate screen was on at the start or the take, the
                                       * terminal reflowed, a scroll region scrolled, the display
                                       * was erased (ED 2, or ED 0 straight after a CUP/HVP home: clear; ls),
                                       * or the range was empty or inverted. Set on every screen-rows result,
                                       * empty ones included; B8's one note ("the screen changed
                                       * while the command ran; this is the screen as it ended"). */
#define RUNCMD_CAP_SCROLLED_OUT  4u   /* primary range: the start scrolled out of the terminal's history */
#define RUNCMD_CAP_EMPTY         8u   /* the final text, after the echo drop and the blank-row trim,
                                       * is empty (on either path): "no output on the screen" */
#define RUNCMD_CAP_REDRAWN      16u   /* with SCREEN: the cause was a clear (ED 2, ED 0 straight after a CUP/HVP
                                       * home, or an empty or inverted range: clear; ls, cls) and
                                       * the screen has content. Never set for the alternate screen,
                                       * a reflow or a region scroll. */
                                      /* (32 was RUNCMD_CAP_PROMPT, withdrawn in C0: no flag for a changed prompt) */

#define RUNCMD_PROMPT_MAX 256   /* buffer size: a prompt is recorded up to 255 bytes */

/* What was read back from the terminal for one call. Reading a capture:
 *   head != NULL                      an excerpt (tail != NULL: cut in the middle,
 *                                     omitted_bytes between head and tail);
 *   head == NULL, EMPTY               nothing to show: "no output on the screen";
 *   head == NULL, neither flag, captured
 *                                     the output could not be read or kept (an
 *                                     allocation failed): say it was dropped, never
 *                                     "no output". omitted_bytes is its size when the
 *                                     cut failed, 0 when the read failed; B8 need not
 *                                     tell the two apart (one note either way). The
 *                                     other flags (SCREEN, REDRAWN) are still set.
 * A RunCmdCapture must be zero-initialised (memset, calloc, = {0}) before its
 * FIRST runcmd_capture_start: start frees whatever excerpt the capture holds,
 * so an uninitialised one frees garbage. runcmd_plan zeroes every capture it
 * makes; a capture on the stack or in a struct is zeroed by its owner. After
 * the first start it can be started again (the excerpt is freed then). The
 * excerpt is freed by runcmd_reply_free, or by the owner of a capture it made. */
typedef struct {
    int      started, captured;      /* captured: spec section 10's once-only flag */
    uint64_t start_line;  uint32_t start_reflow_seq, start_region_seq, start_erase_seq;  int start_alt;
    char    *head; size_t head_len;  /* heap; NULL when no excerpt */
    char    *tail; size_t tail_len;  /* heap; NULL when uncut */
    size_t   omitted_bytes;
    unsigned flags;                  /* RUNCMD_CAP_* */
    char     prompt[RUNCMD_PROMPT_MAX];   /* the start row's text left of the cursor */
    size_t   prompt_len;             /* bytes in prompt; 0 with prompt_long */
    int      prompt_long;            /* the start row's text did not fit: not recorded */
} RunCmdCapture;

/* Why the dispatcher never wrote a SLOT call's command although its entry went
 * on through EXECUTING and COMPLETED: set by the UI where the write is refused.
 * NONE: it was written. */
typedef enum {
    RUNCMD_UNSENT_NONE,
    RUNCMD_UNSENT_NO_SESSION,   /* no transport to write to: "not run (session ended)" */
    RUNCMD_UNSENT_UNSAFE        /* a control or bidirectional character: refused, is_error 1 */
} RunCmdUnsent;

typedef struct {
    char tool_use_id[AI_TOOL_ID_MAX], tool_name[AI_TOOL_NAME_MAX];
    RunCmdKind kind;  RunCmdParse parse;
    RunCmdUnsent unsent;             /* NONE from runcmd_plan; the UI sets it when the write is refused */
    char command[RUNCMD_COMMAND_MAX + 1], note[RUNCMD_NOTE_MAX];
    int  entry;                      /* ApprovalEntry index; -1 unless SLOT and queued */
    int  fetch_off;                  /* OTHER_TOOL web_fetch the stream's registry lacked (spec section 4) */
    RunCmdCapture cap;
} RunCmdCall;

typedef struct RunCmdReply { RunCmdCall *calls; int n_calls; int n_slot; } RunCmdReply;

#define RUNCMD_SLOT_MAX APPROVAL_MAX_CMDS

/* Sort every call of a reply, in call order: OTHER_TOOL (any name but
 * run_command), INVALID (the input does not parse), OVER_CAP (valid, but
 * RUNCMD_SLOT_MAX slots are taken) or SLOT. INVALID and OTHER_TOOL calls take
 * no slot. n_calls may exceed RUNCMD_SLOT_MAX (the stream keeps up to
 * AI_TOOL_STREAM_CALLS_MAX). When !fetch_registered, a web_fetch call has
 * fetch_off set (tool_dispatch_rule's TOOL_DISPATCH_FETCH_OFF). command and
 * note are filled for SLOT and OVER_CAP; every entry is -1 and every capture
 * zero. NULL when calls is NULL, n_calls <= 0 or an allocation fails. */
RunCmdReply *runcmd_plan(const AiToolCall *calls, int n_calls,
                         int fetch_registered);

/* Free every capture's text, the calls and the reply. NULL-safe. */
void runcmd_reply_free(RunCmdReply *r);

/* The call whose entry is `entry`, NULL when none (entry -1 never matches). */
RunCmdCall *runcmd_call_for_entry(RunCmdReply *r, int entry);

/* 1 when any of the calls is named run_command. */
int runcmd_reply_has_call(const AiToolCall *calls, int n_calls);

/* What the stream thread does with a reply that made tool calls (Task B13),
 * decided in this order: no room for a round anywhere (`room` 0, from
 * agentic_room_for_tool_round) is FULL for every tool, run_command or not;
 * then a stream that registered run_command (`run_command` 1) whose reply
 * holds a run_command call is STOP (the turn is stored and the calls go to the
 * panel); anything else is today's LOOP. */
typedef enum {
    RUNCMD_ROUND_LOOP,   /* today's tool loop */
    RUNCMD_ROUND_FULL,   /* the text becomes a plain turn; "conversation full" row */
    RUNCMD_ROUND_STOP    /* store the tool turn, hand the calls to the panel */
} RunCmdRound;

RunCmdRound runcmd_round_plan(int room, int run_command,
                              const AiToolCall *calls, int n_calls);

/* ---- Capture: where a call's output starts and what it reads back ---- */

/* The most bytes of one command's output kept: min(16384, 65536 / n_slot), n
 * the batch's SLOT calls; n <= 0 gives 16384. */
size_t runcmd_cap_bytes(int n_slot);

/* Store text[0, len) in c as the excerpt, replacing (and freeing) any
 * already there. len <= cap: all of it as head, tail NULL, omitted_bytes 0.
 * Longer: head the first 3/4 of cap bytes and tail the last 1/4, each cut on
 * a UTF-8 boundary (head backs up, tail moves forward), omitted_bytes the
 * rest, so head_len + tail_len + omitted_bytes == len. The marker is never
 * stored. A piece of 0 bytes is NULL; an empty text leaves no excerpt. When
 * an allocation fails the capture is left well-formed: head and tail NULL,
 * omitted_bytes == len (the output was dropped). Flags are not touched.
 * NULL c does nothing; NULL text is empty. */
void runcmd_cut(const char *text, size_t len, size_t cap, RunCmdCapture *c);

/* Record where a call's output starts, before the command's first write:
 * the cursor's absolute line, reflow_seq, region_scroll_seq, erase_seq, whether
 * the alternate screen is on, and the prompt (below), then started = 1. It
 * writes nothing to the terminal (a const Terminal), so captures of several
 * calls can be armed one after another. c must be zero-initialised before its
 * first start (see RunCmdCapture). A capture armed again is reset: captured 0,
 * flags 0, any excerpt freed. Does nothing for NULL c or t.
 *
 * The prompt is the start row's text left of the cursor, as term_extract writes
 * a row but NOT trimmed (a prompt ends with a space): up to 255 bytes in
 * prompt, whole characters only, prompt_len its length. A longer one is not
 * recorded: prompt_long = 1, prompt_len 0. */
void runcmd_capture_start(RunCmdCapture *c, const Terminal *t);

/* Read the call's output back, once (spec decision 10). Returns 0, touching
 * nothing, unless started && !captured && t. Otherwise sets captured = 1 and
 * stores the excerpt (through runcmd_cut with cap) and the flags:
 *  - alternate screen on at the start or now, reflow_seq or region_scroll_seq
 *    (primary-screen scrolls only) moved: the visible screen rows, SCREEN;
 *  - else erase_seq moved (an ED 2 on the primary screen, or an ED 0 straight
 *    after a CUP/HVP to row 0 column 0, which is how `clear` erases; ED 3 and RIS are
 *    not handled by the parser), or the primary range [start_line, cursor line)
 *    is empty or inverted: the screen rows, SCREEN | REDRAWN (just SCREEN when
 *    the screen is blank). A cursor move alone (a home, an up-and-erase of a
 *    two-line prompt, a save-and-restore clock) is not a trigger: a program
 *    that homes and repaints without erasing is read from start_line down;
 *  - else that range with the echo (command) dropped; SCROLLED_OUT when its
 *    start has gone from the ring; then the cursor row's own text, which the
 *    range excludes: the text left of the cursor, compared with the recorded
 *    prompt as raw text (no trimming, so the prompt's trailing space counts):
 *    in this order (C0, replacing R49):
 *      ending with the prompt        the part before it is added (equal: nothing);
 *      anything else                 all of it is added, a prompt that changed
 *                                    since the start (a counter, a cd, another
 *                                    host, a REPL's) included, as the last line;
 *    trailing blanks of what is added are trimmed, and it follows the range
 *    text after a '\n' (alone when the range is empty). With prompt_long the
 *    cursor row is left out. Known limits: a prompt wrapped over two rows
 *    leaves its first row's fragment; a progress line that returns to column
 *    0 shows as the last line; a take before the echo's CR (the command line
 *    still being written) adds that line.
 * EMPTY whenever the final text is empty; a failed read or copy sets neither
 * (see RunCmdCapture). Text never holds a control byte
 * (C0 but the row separator, DEL, C1): term_extract writes those cells as
 * spaces. Returns 1. */
int runcmd_capture_take(RunCmdCapture *c, const Terminal *t, const char *command,
                        size_t cap);

/* ---- Results: one per call, their notes, the fit and the framing (spec decisions 4, 11 to 13, 15) ---- */

/* Why the batch is being answered. The first group is a batch that existed;
 * the second answers a whole reply with no batch (built with q == NULL). */
typedef enum {
    RUNCMD_CAUSE_SETTLED, RUNCMD_CAUSE_DECIDED, RUNCMD_CAUSE_TIMEOUT, RUNCMD_CAUSE_RETRY, RUNCMD_CAUSE_STOP,
    RUNCMD_CAUSE_TAB_SWITCH, RUNCMD_CAUSE_SESSION_ENDED, RUNCMD_CAUSE_TYPED,
    RUNCMD_CAUSE_NEW_CHAT, RUNCMD_CAUSE_SESSION_CLOSED, RUNCMD_CAUSE_PANEL_CLOSED,
    RUNCMD_CAUSE_EVICTED,
    /* whole-reply answers, built with q == NULL */
    RUNCMD_CAUSE_NO_SLOT, RUNCMD_CAUSE_SESSION_GONE, RUNCMD_CAUSE_SET_FULL
} RunCmdCause;

/* 1 when the batch's removal continues the conversation with a request:
 * SETTLED (the tick's end), TIMEOUT, RETRY. 0 for every other cause: DECIDED
 * (a Deny or Deny all that left nothing runnable), NO_SLOT, STOP, the tab
 * switch, session and panel causes, TYPED (the typed message continues), NEW_CHAT
 * and EVICTED (spec decision 8's table and the governing principle: no
 * request without a user action that would start one today). */
int runcmd_cause_relaunches(RunCmdCause c);

/* 0 for NEW_CHAT and SESSION_CLOSED (the conversation is cleared or its
 * session gone: the results are not stored); 1 for every other cause. */
int runcmd_cause_appends(RunCmdCause c);

/* 1 when the session's terminal may still exist at the batch's end, so an
 * EXECUTING entry's rows are captured first: 0 for NEW_CHAT and SESSION_CLOSED,
 * 1 for every other cause. */
int runcmd_cause_captures(RunCmdCause c);

/* The slots the flush of the owner's queued notes leaves free (R47): 1 for a
 * cause that relaunches (the reply's), 2 for TYPED (the typed message's and
 * its reply's), 0 for every other. */
int runcmd_cause_keep_free(RunCmdCause c);

/* 1 when the finish of a batch starts the next model request: the cause
 * relaunches (runcmd_cause_relaunches), the owner is the displayed session
 * (`displayed`) and has no stream running (`busy` 0), the results were stored
 * (`stored`: the store returned 0 or 1) and the conversation has room for the
 * round (`room`: agentic_room_for_tool_round with the owner's queue). Any
 * other combination starts nothing (spec decision 8 and the governing
 * principle). */
int runcmd_finish_relaunches(RunCmdCause c, int displayed, int busy, int stored,
                             int room);

/* ---- Retry and the removal paths (spec decisions 8, 9 and 14) ---- */

/* What the Retry button does (IDC_ACTIVITY_RETRY). */
typedef enum {
    RUNCMD_RETRY_CANCEL_BATCH,    /* a run_command batch exists: cancel it with its own cause,
                                   * append the results, relaunch from them */
    RUNCMD_RETRY_POP_ASSISTANT,   /* pop the last (plain assistant) message, relaunch */
    RUNCMD_RETRY_RELAUNCH         /* pop nothing, relaunch from the conversation as it stands */
} RunCmdRetry;

/* has_runcmd_batch: CANCEL_BATCH. Else POP_ASSISTANT only when the last
 * message is an assistant turn with n_tool_calls == 0 (its content may be
 * empty); an assistant turn that holds tool calls is never popped, and a
 * conversation ending on results, on a user turn, an empty one or a NULL one
 * relaunches. Reads the conversation, changes nothing. */
RunCmdRetry runcmd_retry_plan(const AiConversation *conv, int has_runcmd_batch);

/* The ways a run_command batch leaves (spec decision 8's table). */
typedef enum {
    RUNCMD_PATH_TICK_END,         /* the tick's settle branch: every entry done */
    RUNCMD_PATH_DECIDED,          /* Run, Deny or Deny all that left nothing runnable */
    RUNCMD_PATH_TIMEOUT,          /* the ambiguous-prompt timeout */
    RUNCMD_PATH_STOP,             /* Send while dispatching */
    RUNCMD_PATH_SESSION_ENDED,    /* the tick finds the session ended */
    RUNCMD_PATH_TAB_SWITCH,       /* do_session_switch */
    RUNCMD_PATH_RETRY,            /* IDC_ACTIVITY_RETRY with a batch */
    RUNCMD_PATH_TYPED,            /* a typed message while the batch is unsettled */
    RUNCMD_PATH_NEW_CHAT,
    RUNCMD_PATH_SESSION_CLOSED,   /* ai_chat_notify_session_closed, session free */
    RUNCMD_PATH_PANEL_CLOSED,     /* WM_DESTROY while dispatching */
    RUNCMD_PATH_COUNT             /* not a path */
} RunCmdPath;

/* What src/ui does on a path: it asks and writes no such condition of its own.
 * cause: the RunCmdCause the results are built with. capture: 1 where the
 * session's terminal may still exist, so an EXECUTING entry's rows are
 * captured first. append and relaunch are exactly runcmd_cause_appends and
 * runcmd_cause_relaunches of the cause (one table). reset_rounds: 1 where
 * today's [EXEC] path calls ai_rounds_user_message (Stop, a typed message);
 * New Chat does not reset auto_rounds today, so 0 there too. An unknown path
 * answers the safest plan: nothing captured, appended, relaunched or reset
 * (cause SETTLED). */
typedef struct {
    RunCmdCause cause;
    int capture, append, relaunch, reset_rounds;
} RunCmdPathPlan;

RunCmdPathPlan runcmd_path_plan(RunCmdPath p);

/* The text between the head and the tail of a cut result. */
#define RUNCMD_OMIT_MARKER_FMT "[\xE2\x80\xA6 %zu bytes omitted \xE2\x80\xA6]"

/* Build one result per call of r, in call order, into out[r->n_calls] (the
 * caller's array; every element is zeroed first, then filled): tool_use_id,
 * is_error (1 only for an OTHER_TOOL call without fetch_off and an INVALID
 * call), tool_note (decision 13's table and decision 10's capture notes,
 * parts joined with "; ", always under AI_TOOL_PART_NOTE_MAX) and, for a call
 * that ran (COMPLETED, or EXECUTING with rows read), content, the malloc'd
 * head then tail of its capture, omit_at = head length and omitted_bytes the
 * capture's when cut (both 0 when uncut). Content is NULL when there is no
 * excerpt. q is the batch's approval queue; the entry of a SLOT call indexes
 * it. A SLOT call with no entry in q, or q NULL under a cause that is not a
 * whole-reply one, is "not run (refused by the approval queue)"; under
 * SET_FULL and SESSION_GONE every SLOT call gets that cause's note. OVER_CAP,
 * INVALID and OTHER_TOOL calls keep their own rows under every cause. A
 * capture whose excerpt was lost to an allocation failure (captured, no
 * head or tail, no EMPTY) says "output dropped: Nutshell ran out of memory".
 * A COMPLETED or EXECUTING call whose capture never ran (captured 0) ends its
 * note with "output not captured" ("ran; output not captured"), never a bare
 * "ran". The Stop reason reads "stopped by the user; do not run it again
 * unless the user asks" in every note that carries it.
 * A SLOT call with `unsent` set and an entry that is COMPLETED or EXECUTING
 * (the dispatcher walked it through its states but never wrote the command)
 * is "not run (session ended)" (NO_SESSION) or "not run (command rejected:
 * control characters)" with is_error 1 (UNSAFE), no excerpt, whatever its
 * capture holds; any other entry status keeps its own row.
 * An excerpt that cannot be copied (the allocation fails) is dropped with the
 * same note and the call is still answered, so no allocation failure leaves a
 * call without a result (spec decision 25). Returns r->n_calls, or -1 for a
 * NULL r or out, or no calls. Free each out[i].content. */
int runcmd_build_results(const RunCmdReply *r, const ApprovalQueue *q, RunCmdCause cause,
                         AiToolResult *out);

/* The answer for a reply whose planner ran out of memory (runcmd_plan or
 * cmd_batch_add failed, so no call was planned; spec decision 13's last row,
 * R41): one result per call of `calls` (the stored turn's, ids as stored), in
 * order, into out[n] (every element zeroed first): tool_use_id the call's id,
 * is_error 0, no content, tool_note "not run: Nutshell ran out of memory".
 * Allocates nothing, so it works where the planner could not. Returns n, or -1
 * for NULL calls or out, or n <= 0. */
int runcmd_oom_results(const AiToolCall *calls, int n, AiToolResult *out);

/* The status rows of a reply answered without a card, and of the store of its
 * results (spec decisions 5 and 25): one text for the live row and the
 * rebuild's. NONE writes nothing. */
typedef enum {
    RUNCMD_ROW_NONE,
    RUNCMD_ROW_SESSION_GONE,   /* "... the session ended ..." */
    RUNCMD_ROW_SET_FULL,       /* "... too many pending cards ..." */
    RUNCMD_ROW_NO_SLOT,        /* "... N refused ...": n the reply's calls, none could run */
    RUNCMD_ROW_PLANNER_OOM,    /* "... Nutshell ran out of memory ..." */
    RUNCMD_ROW_REDUCED,        /* the results were stored as notes only (store returned 1) */
    RUNCMD_ROW_FULL,           /* the conversation is full: n tool calls not run (-1) */
    RUNCMD_ROW_STORE_FAILED    /* not even the notes-only answer could be stored (-2) */
} RunCmdStatusRow;

/* Write the row's text into buf and return its length; 0 and "" (when cap > 0)
 * for NONE, an unknown kind, a NULL buf or a cap too small. `n` is the reply's
 * number of calls: for NO_SLOT every call of the reply (none of them could
 * run: refused, invalid, another tool, or over the cap), for FULL the calls
 * not run. */
int runcmd_status_text(RunCmdStatusRow kind, int n, char *buf, size_t cap);

/* An estimate of the request body in bytes, before the results are added:
 * the summed JSON-escaped length of every stored message's content and of
 * every results part's content and note, plus what the tool turns add around
 * them (ai_tool_wire_bytes(conv, 1, RUNCMD_AGE_ROUNDS - 1): every call's id,
 * name and input, every answer's id and framing, the JSON around them), plus
 * tools_json_len plus 2 * snapshot_len (the snapshot's stored length, counted
 * twice for its escaping and its block), plus, for "moonshot", every stored
 * assistant turn's escaped reasoning (the only provider sending it back so
 * far; DeepSeek's is added with its request path). A run_command part that
 * the append about to happen will age (a command round with at least
 * RUNCMD_AGE_ROUNDS - 1 command rounds after it) is counted as the stub, which
 * is what the next request carries (the append being a command round). NULL conv: tools and snapshot only.
 * provider may be NULL. */
size_t runcmd_request_estimate(const AiConversation *conv, const char *provider,
                               size_t tools_json_len, size_t snapshot_len);

#define RUNCMD_BODY_HEADROOM  32768
#define RUNCMD_TOOLS_ESTIMATE 8192

/* The room for results: min(body_max, 3 bytes per token of the model's
 * context when context_limit_tokens > 0) less RUNCMD_BODY_HEADROOM, and 0
 * when that is not above the headroom (a small context gets notes only). */
size_t runcmd_budget(size_t body_max, int context_limit_tokens);

/* Make the results fit: while estimate plus the summed escaped content and
 * note of res[0, n) exceeds budget, halve every excerpt, head keeping its
 * start and tail its end, both cut on UTF-8 boundaries (an uncut excerpt is
 * first split like runcmd_cut does, 3/4 and 1/4), omitted_bytes growing and
 * the "output cut: N bytes omitted from the middle" note kept in step. A
 * result with nothing left has content NULL, omit_at and omitted_bytes 0 and
 * "output dropped: no room left in this conversation" appended to its note.
 * Shrinks in place: it never allocates and never fails. Returns the number
 * of halving rounds done (0: it already fit, or nothing to cut). */
int runcmd_fit(AiToolResult *res, int n, size_t estimate, size_t budget);

/* What one result adds to the body, the sum runcmd_fit tests: its JSON-escaped
 * content and note plus the framing runcmd_frame_result() puts around them
 * (the lead sentence, the delimiters, the marker of a cut one, the newlines).
 * NULL: 0. */
size_t runcmd_result_bytes(const AiToolResult *r);

/* The text one run_command part is sent as, malloc'd (free it), *out_len its
 * length; NULL on allocation failure or a NULL part.
 *   uncut: trust's one block, ai_data_lead_tool("run_command"), the block, the
 *          note after the closing delimiter (as ai_data_block_dup);
 *   cut:   spec decision 12's layout: the lead, the head block, the marker
 *          (RUNCMD_OMIT_MARKER_FMT) as Nutshell text between delimiters, the
 *          tail block, the note, all joined by "\n". A head or tail that is
 *          empty (omit_at 0, or the head runs to the end) has no block and
 *          no delimiters, so the marker never assumes either side;
 *   no excerpt (content NULL or empty): the note alone, no lead, no block.
 * delim NULL: no lead and no delimiters, the pieces joined the same way
 * (a conversation built without a frame). The result and the cut layout's
 * assembly are allocated through the test allocator; the escaped pieces and
 * trust's block (ai_data_escape_dup, ai_data_block_dup) come from malloc, so
 * only the allocations made here can be failed by a test. */
char *runcmd_frame_result(const AiToolPart *part, const char *delim, size_t *out_len);

/* Aging (spec decision 11): the output of a run_command part in a results
 * turn older than the newest RUNCMD_AGE_ROUNDS command rounds is replaced by
 * the stub. A command round is a results turn that holds a run_command part;
 * web results turns between them are not rounds. */
#define RUNCMD_AGE_ROUNDS 2
#define RUNCMD_AGED_STUB "[output dropped to save room; run the command again if you need it]"

/* Write the stub over such a part's content in place (no allocation, the
 * part keeps owning its heap block; content_len becomes the stub's length;
 * omit_at and omitted_bytes 0). The part's note loses its "output cut: N bytes
 * omitted from the middle" phrase (the rest of the note, joins tidied, stays).
 * A part whose content is shorter than the stub or NULL, or already the stub,
 * is left as it is; its note and its call stay.
 * Other tools' parts are untouched. Returns the number of parts changed. */
int runcmd_age_results(AiConversation *conv);

/* Remove the note's "output cut: N bytes omitted from the middle" phrase and
 * one "; " join beside it (the one before it, else the one after); the rest of
 * the note stays. A note without the phrase is left as it is. cap is the
 * note's buffer size. */
void runcmd_note_drop_cut(char *note, size_t cap);

/* 1 when part p, which sits in the results turn at message index m of conv, is
 * a run_command part whose output is at least the stub's size and that has at
 * least `keep` command rounds (results turns holding a run_command part) after
 * its own: the parts
 * runcmd_age_results() (keep = RUNCMD_AGE_ROUNDS) replaces by the stub.
 * keep <= 0: 0. */
int runcmd_part_ages(const AiConversation *conv, int m, const AiToolPart *p, int keep);

/* Tests only: the allocator the planner and the cut use (NULL restores malloc). */
void runcmd_set_alloc_for_test(void *(*alloc)(size_t));

#endif
