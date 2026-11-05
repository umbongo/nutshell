/* src/core/tool_decision.h */
#ifndef NUTSHELL_TOOL_DECISION_H
#define NUTSHELL_TOOL_DECISION_H

/*
 * tool_decision -- the tool gate's pure decision rule (does a tool call run
 * at once, or does the user get a card to approve first?) and the decision
 * slot that carries the user's answer back to the streaming thread. No
 * windows.h. Spec docs/superpowers/specs/2026-11-05-trust-boundary-design.md
 * decisions 13-16 and 20.
 *
 * The slot. One ToolDecisionSlot lives on each AiStream. For a tool call
 * that needs approval the stream's thread publishes the call
 * (tool_decision_publish(): state PENDING under a fresh generation), posts
 * the card request carrying (stream_id, gen) to the UI thread, then polls
 * the slot every AI_TOOL_DECIDE_POLL_MS. The UI thread answers once, by
 * (stream_id, gen), with tool_decision_answer(): a single compare-and-swap
 * from PENDING to the outcome. The thread's own poll can also settle the
 * slot: an aborted stream CASes CANCELLED, a wait past
 * AI_TOOL_DECIDE_TIMEOUT_MS CASes EXPIRED. Whoever wins the CAS decides;
 * every later answer for that generation just reads the winner back (so a
 * late "Run" on a card that expired cannot turn into a run, and the caller
 * labels the row from what answer() returns, not from what it asked for).
 *
 * The word packs `gen << 3 | state` so the generation and the state change
 * in one atomic step: an answer for an earlier call (an older generation)
 * can never win against a later publish, whatever the interleaving. The
 * generation is never 0 after a publish (a wrap of its 29 bits skips 0), so
 * a freshly initialised slot (gen 0, NONE) matches no published request.
 *
 * Publish-before-check, and no sticky cancelled flag. Unlike conn_prompt,
 * this slot has no "cancelled" flag of its own. The stream's abort flag
 * (ai_stream_aborted(), the WorkerLife cancel) is already sticky, and the
 * thread reads it on every poll. The thread publishes first, then polls:
 * a cancel that landed before the publish is still seen by that first
 * poll (aborted, so CAS CANCELLED), and a cancel that lands after it
 * either answers the slot CANCELLED itself or is seen by the next poll.
 * Either way nobody waits on a card that nobody can answer, and no
 * callback is needed to drop a request.
 *
 * All transitions use GCC __atomic builtins, sequentially consistent, and
 * compare-exchange for every state change.
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

/* How long the thread waits for a card, and how often it polls the slot. */
#define AI_TOOL_DECIDE_TIMEOUT_MS 600000u
#define AI_TOOL_DECIDE_POLL_MS    250u

typedef enum {
    TOOL_DECISION_NONE = 0,      /* nothing published, or a stale generation */
    TOOL_DECISION_PENDING,
    TOOL_DECISION_RUN,           /* the gate said RUN: no card */
    TOOL_DECISION_APPROVED,      /* Run on the card: redirects pinned (spec section 23) */
    TOOL_DECISION_NOT_APPROVED,  /* Deny, refused card, failed post */
    TOOL_DECISION_TOO_MANY,      /* the owner's set was full (spec decision 14) */
    TOOL_DECISION_EXPIRED,
    TOOL_DECISION_CANCELLED
} ToolDecision;

typedef struct { unsigned volatile word; } ToolDecisionSlot;   /* gen << 3 | state */

/* NULL-safe. A fresh slot reads as generation 0, NONE. */
void tool_decision_init(ToolDecisionSlot *s);

/* Thread: publish a new call. State PENDING under a new generation, which
 * is returned and is never 0 (the 29-bit space skips 0 on wrap). NULL
 * returns 0. */
unsigned tool_decision_publish(ToolDecisionSlot *s);

/* CAS PENDING -> `a` for generation `gen`. `a` must be RUN, APPROVED,
 * NOT_APPROVED, TOO_MANY or CANCELLED. Returns the state that holds for
 * `gen` afterwards: `a` when this call won, the earlier outcome when the
 * call was already settled, NONE for a stale `gen` or an invalid `a`. */
ToolDecision tool_decision_answer(ToolDecisionSlot *s, unsigned gen, ToolDecision a);

/* Thread: `aborted` -> CAS CANCELLED; else waited_ms >= AI_TOOL_DECIDE_TIMEOUT_MS
 * -> CAS EXPIRED. Returns the current state for `gen` (PENDING while
 * undecided, NONE for a stale `gen`). */
ToolDecision tool_decision_poll(ToolDecisionSlot *s, unsigned gen, int aborted,
                                unsigned long long waited_ms);

/* The note for the tool_result of an outcome (spec decision 16): NULL for
 * RUN and APPROVED, else "The user did not approve this tool call." (TOO_MANY
 * names the full set). */
const char *tool_decision_note(ToolDecision d);

/* The card row's label for an outcome. */
const char *tool_decision_row_label(ToolDecision d);

#endif /* NUTSHELL_TOOL_DECISION_H */
