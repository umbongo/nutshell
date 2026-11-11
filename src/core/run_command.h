#ifndef NUTSHELL_RUN_COMMAND_H
#define NUTSHELL_RUN_COMMAND_H

#include "ai_tools.h"
#include "chat_approval.h"

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

#define RUNCMD_CAP_SCREEN        1u   /* screen rows: alt screen, reflow, region scroll, or empty/inverted range */
#define RUNCMD_CAP_SCROLLED_OUT  4u
#define RUNCMD_CAP_EMPTY         8u   /* nothing readable at all */
#define RUNCMD_CAP_REDRAWN      16u   /* screen rows: empty or inverted range, screen has content */

/* What was read back from the terminal for one call (filled by later tasks). */
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

/* Tests only: the allocator the planner uses (NULL restores malloc). */
void runcmd_set_alloc_for_test(void *(*alloc)(size_t));

#endif
