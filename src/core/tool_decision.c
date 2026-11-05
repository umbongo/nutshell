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

/* ---- the decision slot ---- */

#define TD_STATE_BITS 3u
#define TD_STATE_MASK ((1u << TD_STATE_BITS) - 1u)
#define TD_GEN_MASK   (0xFFFFFFFFu >> TD_STATE_BITS)   /* 29 bits */

static unsigned td_pack(unsigned gen, ToolDecision st)
{
    return (gen << TD_STATE_BITS) | (unsigned)st;
}

void tool_decision_init(ToolDecisionSlot *s)
{
    if (!s) return;
    __atomic_store_n(&s->word, td_pack(0u, TOOL_DECISION_NONE), __ATOMIC_SEQ_CST);
}

unsigned tool_decision_publish(ToolDecisionSlot *s)
{
    if (!s) return 0;
    unsigned cur = __atomic_load_n(&s->word, __ATOMIC_SEQ_CST);
    unsigned gen;
    do {
        gen = ((cur >> TD_STATE_BITS) + 1u) & TD_GEN_MASK;
        if (gen == 0) gen = 1;   /* 0 is the fresh slot's generation: skip it on wrap */
    } while (!__atomic_compare_exchange_n(&s->word, &cur,
                                          td_pack(gen, TOOL_DECISION_PENDING), 0,
                                          __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
    return gen;
}

/* The state that holds for `gen` in the word `w`: NONE for a stale gen. */
static ToolDecision td_state_for(unsigned w, unsigned gen)
{
    if ((w >> TD_STATE_BITS) != gen) return TOOL_DECISION_NONE;
    return (ToolDecision)(w & TD_STATE_MASK);
}

/* CAS PENDING -> `to` for `gen`; returns the state that holds for `gen`
 * afterwards (`to` when this call won). */
static ToolDecision td_settle(ToolDecisionSlot *s, unsigned gen, ToolDecision to)
{
    unsigned cur = __atomic_load_n(&s->word, __ATOMIC_SEQ_CST);
    for (;;) {
        ToolDecision st = td_state_for(cur, gen);
        if (st != TOOL_DECISION_PENDING) return st;
        if (__atomic_compare_exchange_n(&s->word, &cur, td_pack(gen, to), 0,
                                        __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
            return to;
        /* cur was reloaded by the failed CAS: look again */
    }
}

ToolDecision tool_decision_answer(ToolDecisionSlot *s, unsigned gen, ToolDecision a)
{
    if (!s) return TOOL_DECISION_NONE;
    switch (a) {
    case TOOL_DECISION_RUN:
    case TOOL_DECISION_APPROVED:
    case TOOL_DECISION_NOT_APPROVED:
    case TOOL_DECISION_TOO_MANY:
    case TOOL_DECISION_CANCELLED:
        break;
    default:
        return TOOL_DECISION_NONE;
    }
    return td_settle(s, gen, a);
}

unsigned long long tool_decision_timeout_ms(int sec)
{
    if (sec < AI_TOOL_TIMEOUT_SEC_MIN) sec = AI_TOOL_TIMEOUT_SEC_MIN;
    if (sec > AI_TOOL_TIMEOUT_SEC_MAX) sec = AI_TOOL_TIMEOUT_SEC_MAX;
    return (unsigned long long)sec * 1000ULL;
}

ToolDecision tool_decision_poll(ToolDecisionSlot *s, unsigned gen, int aborted,
                                unsigned long long waited_ms,
                                unsigned long long timeout_ms)
{
    if (!s) return TOOL_DECISION_NONE;
    ToolDecision st = td_state_for(__atomic_load_n(&s->word, __ATOMIC_SEQ_CST), gen);
    if (st != TOOL_DECISION_PENDING) return st;
    if (aborted) return td_settle(s, gen, TOOL_DECISION_CANCELLED);
    const unsigned long long lo = (unsigned long long)AI_TOOL_TIMEOUT_SEC_MIN * 1000ULL;
    const unsigned long long hi = (unsigned long long)AI_TOOL_TIMEOUT_SEC_MAX * 1000ULL;
    if (timeout_ms < lo) timeout_ms = lo;
    if (timeout_ms > hi) timeout_ms = hi;
    if (waited_ms >= timeout_ms) return td_settle(s, gen, TOOL_DECISION_EXPIRED);
    return TOOL_DECISION_PENDING;
}

const char *tool_decision_note(ToolDecision d)
{
    switch (d) {
    case TOOL_DECISION_RUN:
    case TOOL_DECISION_APPROVED:
        return NULL;
    case TOOL_DECISION_TOO_MANY:
        return "The user did not approve this tool call (too many pending cards).";
    default:
        return "The user did not approve this tool call.";
    }
}

const char *tool_decision_row_label(ToolDecision d)
{
    switch (d) {
    case TOOL_DECISION_PENDING:      return "waiting";
    case TOOL_DECISION_APPROVED:     return "approved";
    case TOOL_DECISION_NOT_APPROVED:
    case TOOL_DECISION_TOO_MANY:     return "not approved";
    case TOOL_DECISION_EXPIRED:      return "expired, not run";
    case TOOL_DECISION_CANCELLED:    return "cancelled, not run";
    case TOOL_DECISION_NONE:
    case TOOL_DECISION_RUN:
    default:                         return "not run";
    }
}
