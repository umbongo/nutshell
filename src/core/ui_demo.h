/* src/core/ui_demo.h */
#ifndef NUTSHELL_UI_DEMO_H
#define NUTSHELL_UI_DEMO_H

#include <stddef.h>
#include "ai_prompt.h"
#include "chat_approval.h"

/* Canned content for --ui-demo (Design-System Foundation, spec section 5,
 * "Verification harness"). Every state is built from the real
 * AiConversation / AiMessage / ApprovalQueue structures the AI panel
 * already renders, so the gallery cannot drift from production behaviour.
 *
 * Recognised states: "chat", "approval", "executing", "tool", "error",
 * "empty", "nokey", "nosession", "batches", "all" (the union of the other
 * eight -- "nokey" and "nosession" carry no conversation/approval content
 * of their own, same as "empty", so "all" is unaffected by their
 * addition). See docs/superpowers/specs/
 * 2026-09-09-pending-command-batches.md for "batches": two independent
 * pending command cards from two separate assistant replies, with a user
 * turn in between so the transcript shows the interleaving. */

/* Build the canned conversation, approval queue(s) and terminal text for
 * one demo state.
 *
 * conv, approval and approval2 are reset (re-initialised) before filling;
 * any may be NULL to skip it. term_text/term_cap may be NULL/0 to skip
 * building the terminal transcript. conv always gets exactly one
 * AI_ROLE_SYSTEM message at index 0 (mirroring a real conversation), even
 * for "empty".
 *
 * approval2 holds a second, independent batch's entries -- only
 * "batches" and "all" populate it (the union states' pending-batches
 * portion); every other state leaves it at count == 0, same as an empty
 * `approval`.
 *
 * Returns 0 on success, -1 if `state` is not recognised (nothing is
 * written in that case). */
int ui_demo_build(const char *state, AiConversation *conv,
                  ApprovalQueue *approval, ApprovalQueue *approval2,
                  char *term_text, size_t term_cap);

/* 1 if `state` is a recognised demo state name, 0 otherwise (including
 * NULL). */
int ui_demo_state_valid(const char *state);

/* Every recognised state name, in a stable order ending with "all".
 * *count receives the array length. Never returns NULL. */
const char *const *ui_demo_states(int *count);

/* The canned "thinking" block for the chat state's assistant reply.
 * Not part of AiMessage -- ai_chat.c keeps thinking text in a side table
 * keyed by message index, outside AiConversation -- so callers that want
 * to show it must set it there themselves after loading the conversation
 * this module built. */
const char *ui_demo_thinking_text(void);

#endif /* NUTSHELL_UI_DEMO_H */
