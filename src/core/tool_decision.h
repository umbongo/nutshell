/* src/core/tool_decision.h */
#ifndef NUTSHELL_TOOL_DECISION_H
#define NUTSHELL_TOOL_DECISION_H

/*
 * tool_decision -- the tool gate's pure decision rule: does a tool call run
 * at once, or does the user get a card to approve first? No windows.h.
 * Spec docs/superpowers/specs/2026-11-05-trust-boundary-design.md
 * decisions 13 and 14.
 */

#include "ai_tools.h"
#include "cmd_policy.h"

typedef enum {
    TOOL_GATE_RUN,   /* run it now, no card */
    TOOL_GATE_ASK    /* put a card up and wait for the user */
} ToolGate;

/* `p` is the displayed session's live policy; `attended` is non-zero when
 * a person is in the loop for this round (a background session, or the
 * tool-round cap reached), which means nothing runs unattended.
 *
 *   TOOL_SAFE      RUN at every policy, attended or not.
 *   TOOL_UNKNOWN   RUN only when not attended and Auto-approve is on at
 *                  Unknown or above; otherwise ASK. The ceiling never holds
 *                  a tool.
 *   TOOL_WRITE, TOOL_CRITICAL and any out-of-range value: ASK, always. */
ToolGate tool_gate_decide(ToolSafety safety, CmdPolicy p, int attended);

#endif /* NUTSHELL_TOOL_DECISION_H */
