/* src/core/tool_decision.c -- see tool_decision.h */
#include "tool_decision.h"

ToolGate tool_gate_decide(ToolSafety safety, CmdPolicy p, int attended)
{
    switch (safety) {
    case TOOL_SAFE:
        return TOOL_GATE_RUN;
    case TOOL_UNKNOWN:
        if (!attended &&
            cmd_policy_runs_unattended(p, CMD_MASK_OF(CMD_UNKNOWN)))
            return TOOL_GATE_RUN;
        return TOOL_GATE_ASK;
    case TOOL_WRITE:
    case TOOL_CRITICAL:
    default:
        return TOOL_GATE_ASK;
    }
}
