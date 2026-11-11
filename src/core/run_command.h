#ifndef NUTSHELL_RUN_COMMAND_H
#define NUTSHELL_RUN_COMMAND_H

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
                                       * terminal reflowed, a scroll region scrolled, or the range
                                       * was empty or inverted. Set on every screen-rows result,
                                       * empty ones included; B8's one note ("the screen changed
                                       * while the command ran; this is the screen as it ended"). */
#define RUNCMD_CAP_SCROLLED_OUT  4u   /* primary range: the start scrolled out of the terminal's history */
#define RUNCMD_CAP_EMPTY         8u   /* the final text, after the echo drop and the blank-row trim,
                                       * is empty (on either path): "no output on the screen" */
#define RUNCMD_CAP_REDRAWN      16u   /* with SCREEN: the cause was an empty or inverted range (clear;
                                       * ls, cls, reset) and the screen has content. Never set for the
                                       * alternate screen, a reflow or a region scroll. */

/* What was read back from the terminal for one call. Reading a capture:
 *   head != NULL                      an excerpt (tail != NULL: cut in the middle,
 *                                     omitted_bytes between head and tail);
 *   head == NULL, EMPTY               nothing to show: "no output on the screen";
 *   head == NULL, no EMPTY, captured  the output could not be read or kept (an
 *                                     allocation failed): say it was dropped;
 *                                     omitted_bytes is its size when known, else 0. */
typedef struct {
    int      started, captured;      /* captured: spec section 10's once-only flag */
    uint64_t start_line;  uint32_t start_reflow_seq, start_region_seq;  int start_alt;
    char    *head; size_t head_len;  /* heap; NULL when no excerpt */
    char    *tail; size_t tail_len;  /* heap; NULL when uncut */
    size_t   omitted_bytes;
    unsigned flags;                  /* RUNCMD_CAP_* */
} RunCmdCapture;

typedef struct {
    char tool_use_id[AI_TOOL_ID_MAX], tool_name[AI_TOOL_NAME_MAX];
    RunCmdKind kind;  RunCmdParse parse;
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
 * the cursor's absolute line, reflow_seq, region_scroll_seq and whether the
 * alternate screen is on, then started = 1. Does nothing for NULL c or t. */
void runcmd_capture_start(RunCmdCapture *c, const Terminal *t);

/* Read the call's output back, once (spec decision 10). Returns 0, touching
 * nothing, unless started && !captured && t. Otherwise sets captured = 1 and
 * stores the excerpt (through runcmd_cut with cap) and the flags:
 *  - alternate screen on at the start or now, reflow_seq or region_scroll_seq
 *    moved: the visible screen rows, SCREEN;
 *  - else the primary range [start_line, cursor line) is empty or inverted:
 *    the screen rows, SCREEN | REDRAWN (just SCREEN when the screen is blank);
 *  - else that range with the echo (command) dropped; SCROLLED_OUT when its
 *    start has gone from the ring.
 * EMPTY whenever the final text is empty. Text never holds a control byte
 * (C0 but the row separator, DEL, C1): term_extract writes those cells as
 * spaces. Returns 1. */
int runcmd_capture_take(RunCmdCapture *c, const Terminal *t, const char *command,
                        size_t cap);

/* Tests only: the allocator the planner and the cut use (NULL restores malloc). */
void runcmd_set_alloc_for_test(void *(*alloc)(size_t));

#endif
