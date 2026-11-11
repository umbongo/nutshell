#ifndef NUTSHELL_AI_CONV_GUARD_H
#define NUTSHELL_AI_CONV_GUARD_H

/* One door for Nutshell's own USER notes, and the tool-pair invariant every
 * request body is checked against (spec 2026-11-05 run-command tool,
 * decisions 19, 24 and 25). */

#include "ai_prompt.h"

/* 1 while some assistant turn with tool calls is followed, up to the end of
 * the conversation, only by AI_ROLE_TOOL turns whose parts answer fewer of
 * its calls than it made (this covers "the last message is such a turn").
 * A parts-less AI_ROLE_TOOL message counts as one part with its tool_call_id.
 * NULL gives 0. */
int ai_conv_tool_calls_open(const AiConversation *conv);

/* Append a USER turn with `text`, the only way Nutshell's own notes enter a
 * session's conversation. `keep_free` is the number of slots the caller needs
 * left free once the text is stored: 1 when the text launches a request (its
 * reply must fit), 0 otherwise; a negative value counts as 0. Returns:
 *   0  appended;
 *   1  queued: `defer` is non-zero (the owner is busy) or
 *      ai_conv_tool_calls_open(conv) is 1, so a copy waits in `q` and
 *      ai_conv_flush_notes() appends it later (nothing else queues a text);
 *   2  the conversation is full: the text is neither stored nor queued. The
 *      queued notes that fit with the text's slot and keep_free more left free
 *      were flushed first (in order, so notes keep their order); the rest stay
 *      queued, in order, and the text is never stored ahead of a queued note.
 *      The caller shows the conversation-full notice (spec decision 24);
 *   -1 a NULL conv or text, an empty text, a full queue or a NULL queue when
 *      one is needed (the caller shows a row), or an allocation failed (the
 *      text is not stored; queued notes that the failure stopped stay queued,
 *      in order). Out of memory is never reported as 2.
 * keep_free is clamped to AI_MAX_MESSAGES (nothing then fits: 2).
 * A text of AI_NOTE_TEXT_MAX bytes or more is cut on a UTF-8 boundary and
 * ends " [\xE2\x80\xA6 cut]" (both ways of storing it). */
int ai_conv_add_user_note(AiConversation *conv, AiNoteQueue *q, int defer,
                          int keep_free, const char *text);

/* TEST ONLY. Replace the function the door and ai_conv_flush_notes() append a
 * turn with (default ai_conv_add; NULL restores it), so a test can fail one
 * append. Not for product code. */
void ai_conv_guard_set_add_for_test(int (*add)(AiConversation *, AiRole, const char *));

/* 1 when a typed message can be stored with its reply:
 * msg_count + 2 (+ 1 when a run_command batch is open, since the typed
 * message finishes it with a results turn) + q->count (the notes the finish
 * flushes into the conversation; NULL counts 0) <= AI_MAX_MESSAGES. NULL conv
 * gives 0. */
int ai_conv_room_for_send(const AiConversation *conv, int open_batch,
                          const AiNoteQueue *q);

/* While no tool call is open, append the queued notes in order and return how
 * many were appended, leaving `keep_free` slots of the conversation free: a
 * flush that precedes a relaunch passes 1 (the reply), one before a typed
 * message and its reply passes 2, any other 0. (A launching [EXEC] continue
 * text does not call this itself: ai_conv_add_user_note(..., keep_free 1, ...)
 * flushes with the text's slot added, C0.) A note the conversation cannot
 * store without taking one of those slots stops the flush: it and every later
 * note stay queued, in order, at the front of `q`, for the next flush; nothing
 * is dropped. While a call is open, return 0 and leave `q` alone. A negative
 * keep_free counts as 0. NULL-safe (0). */
int ai_conv_flush_notes(AiConversation *conv, AiNoteQueue *q, int keep_free);

/* Free a trailing open assistant tool turn and any partial results after it,
 * so the conversation is well-formed again. Returns 1 when it dropped one,
 * else 0. NULL-safe. */
int ai_conv_drop_open_tool_turn(AiConversation *conv);

/* 1 when the conversation pairs its tool calls with their results: every
 * assistant turn with tool calls is followed directly by one AI_ROLE_TOOL
 * turn whose parts answer every call once, ids matching, and no id repeated
 * within the turn (a run of parts-less
 * AI_ROLE_TOOL messages, each one part with its tool_call_id, counts as that
 * one turn), before any other turn; no AI_ROLE_TOOL turn follows anything but
 * such an assistant turn; no open assistant turn ends the conversation.
 * Every request body builder returns 0 when this is 0. NULL gives 0. */
int ai_conv_tool_pairs_well_formed(const AiConversation *conv);

#endif /* NUTSHELL_AI_CONV_GUARD_H */
