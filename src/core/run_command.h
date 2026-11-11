#ifndef NUTSHELL_RUN_COMMAND_H
#define NUTSHELL_RUN_COMMAND_H

#include "ai_tools.h"

#include <stddef.h>

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

#endif
