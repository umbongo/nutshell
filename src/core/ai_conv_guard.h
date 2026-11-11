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

/* The assistant turn whose tool calls are still unanswered at the end of the
 * conversation (the one ai_conv_tool_calls_open() sees, and the one
 * ai_conv_drop_open_tool_turn() drops), or NULL when none is open. It is not
 * necessarily the last message: partial results may follow it. The pointer
 * stays valid until the conversation is changed. The panel reads the calls'
 * ids and inputs from it, never from the response the provider sent. */
const AiMessage *ai_conv_open_tool_turn(const AiConversation *conv);

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

/* The launch's snapshot decision, in one place for the launch and the
 * in-stream rebuild (spec decision 22, R48, R55). `text`/`len` is the
 * terminal text the launch just read (NULL or 0: none). `offers_runcmd` is
 * 1 when the request offers run_command (ai_provider_runcmd).
 *   - last message a USER turn the user typed, or a Nutshell note and the
 *     request does not offer run_command: freeze `text` onto it (a fresh
 *     snapshot, as every launch took before the tool existed);
 *   - last message a Nutshell note and the request offers run_command: no
 *     fresh snapshot; the one the question carries is kept, but a snapshot
 *     that sits on a note (an [EXEC] launch froze it there) is dropped, since
 *     no typed turn has come since and the builder would drop it anyway;
 *   - last message not a USER turn (or no message) and the request does not
 *     offer run_command: none is kept (the builder would put an old one on
 *     the last USER turn, stale).
 * Returns 1 when the request carries a snapshot (ai_conv_has_snapshot), 0
 * when it carries none (also a NULL conv), -1 when an allocation failed (no
 * snapshot kept: launch without one rather than fail). */
int ai_conv_launch_snapshot(AiConversation *conv, const char *text, size_t len,
                            int offers_runcmd);

/* Set frame->snapshot and frame->snapshot_at from the conversation's frozen
 * snapshot: its text and snapshot_msg + 1 when ai_conv_has_snapshot(conv),
 * else NULL and 0 (a fresh snapshot with snapshot_at 0 would land in the
 * question of a request that offers run_command, which spec decision 22
 * forbids). Leaves frame->delim alone. frame NULL: no-op. conv NULL: none. */
void ai_conv_snapshot_frame(const AiConversation *conv, AiBodyFrame *frame);

/* The status row that says why a request body could not be built (the builder
 * returns 0 for pairing, overflow and allocation alike): the pairing text when
 * !ai_conv_tool_pairs_well_formed(conv) (NULL included), else the text for an
 * overflow or an allocation failure, which the builder does not tell apart.
 * Static strings, never freed. */
const char *ai_body_failure_text(const AiConversation *conv);

/* The same for a request to `provider` with `tools_json` (NULL or "" for none):
 * between the pairing cause and the generic one, a DeepSeek request whose tools
 * offer run_command (so the advice to set the option to Off is true) and that
 * holds an assistant turn whose reasoning was cut
 * (ai_body_refuses_cut_reasoning) reads "[request not sent: a reply's
 * reasoning was too long to send back whole; start a New Chat, or set Commands
 * as tool calls to Off]" (spec decision 23). ai_body_failure_text(conv) is this
 * with no provider and no tools. */
const char *ai_body_failure_text_for(const AiConversation *conv,
                                     const char *provider,
                                     const char *tools_json);
#endif /* NUTSHELL_AI_CONV_GUARD_H */
