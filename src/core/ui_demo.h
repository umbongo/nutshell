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
 * "empty", "all" (the union of the other six). */

/* Build the canned conversation, approval queue and terminal text for one
 * demo state.
 *
 * conv and approval are reset (re-initialised) before filling; either may
 * be NULL to skip it. term_text/term_cap may be NULL/0 to skip building the
 * terminal transcript. conv always gets exactly one AI_ROLE_SYSTEM message
 * at index 0 (mirroring a real conversation), even for "empty".
 *
 * Returns 0 on success, -1 if `state` is not recognised (nothing is
 * written in that case). */
int ui_demo_build(const char *state, AiConversation *conv,
                  ApprovalQueue *approval, char *term_text, size_t term_cap);

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
