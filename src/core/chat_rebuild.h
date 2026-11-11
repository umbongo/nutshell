/* src/core/chat_rebuild.h */
#ifndef NUTSHELL_CHAT_REBUILD_H
#define NUTSHELL_CHAT_REBUILD_H

#include <stddef.h>
#include "ai_prompt.h"
#include "chat_msg.h"
#include "cmd_batch.h"
#include "cmd_run_log.h"

/*
 * chat_rebuild -- the rules of the AI panel's rebuilt display (tab switch and
 * back, undock): which rows a stored conversation yields. Pure and portable
 * (no windows.h), so every rule is a native test; src/ui/ai_chat_view.c only
 * clears the list and calls chat_rebuild_replay().
 *
 * Spec 2026-11-05 run-command tool, "Display": a run_command call is shown
 * once, as its replayed command row when a run-log record or a live batch
 * carries it, else as one "using tool: run_command - <command>" row followed
 * by its result's note, and never with a preview of its output. Other tools
 * keep "using tool", the note and the first 200 bytes of the output. The
 * truncation row appears only for a real cut.
 */

/* The preview of a tool result: at most this many bytes, cut on a UTF-8
 * boundary. */
#define CHAT_REBUILD_PREVIEW_MAX 200

/* The text of a call's "using tool" row:
 *   web_fetch    "using tool: web_fetch - <url>"      (web_fetch_describe)
 *   run_command  "using tool: run_command - <command>" (runcmd_describe)
 *   any other, or an input that gives no description: "using tool: <name>".
 * Cut on a UTF-8 boundary to fit `cap` (the NUL included). Returns the length
 * written; 0 with out[0] = NUL for a NULL call, NULL out or cap 0. */
size_t chat_rebuild_call_text(const AiToolCall *call, char *out, size_t cap);

/* 1 when a result names a real cut: omitted_bytes > 0, or `note`
 * contains AI_TOOL_TRUNC_NOTE. NULL note is "". */
int chat_rebuild_is_cut(const char *note, size_t omitted_bytes);

/* Append the rows of one tool result, in this order:
 *   1. its note, when set, as a CHAT_ITEM_STATUS row (each newline in the
 *      note becomes a space);
 *   2. the truncation row (CHAT_ITEM_STATUS, agentic_truncation_warning with
 *      the part's real omitted_bytes) only when chat_rebuild_is_cut(): never
 *      because the content is longer than the preview;
 *   3. a CHAT_ITEM_TOOL_RESULT row with the first CHAT_REBUILD_PREVIEW_MAX
 *      bytes of `content` (UTF-8 safe), for every tool but run_command, and
 *      only when `content_len` > 0.
 * `covered` is 1 for a run_command result whose command is drawn by a
 * recorded or live row: it adds nothing. NULL list does nothing. */
void chat_rebuild_part_rows(ChatMsgList *list, const char *tool_name,
                            const char *note, const char *content,
                            size_t content_len, size_t omitted_bytes,
                            int covered);

/* Replay the conversation `conv` (message 0, the system prompt, is skipped)
 * into `list`, appending:
 *   user turn       a CHAT_ITEM_USER row;
 *   assistant turn  for each of its tool calls, in order, the call's row
 *                   (chat_rebuild_call_text) and the rows of the result that
 *                   answers it (the next message when that is a tool turn,
 *                   matched by tool_call_id; a parts-less tool turn counts as
 *                   one part), then the reply's text, then the recorded
 *                   command rows, then the recorded status rows;
 *   tool turn       (one no assistant turn above claimed) a part's rows each.
 * A run_command call with a command is covered, and shows no call row and no
 * result rows, when a record of `rlog` that has command rows matches the
 * reply (cmd_run_record_matches_hash at the turn's cmd_run_log_reply_hash) or
 * a live batch of `batches` that is not a tool batch is keyed to it
 * (reply_msg == the turn's index and reply_hash equal): the command is then
 * shown once, by its card. A reply with any such record or live batch shows
 * only its text before the first live [EXEC] marker (ai_response_split), as
 * the live view draws it. Records follow the reply as settled rows with batch
 * id -(record index + 1); status rows of a record follow them.
 * Last, one card per batch of `batches`, oldest first
 * (chat_msg_append_batch_items).
 * `thinking` (AI_MAX_MESSAGES entries or NULL) holds the reasoning text of
 * each assistant turn by message index. Each argument but `list` and `conv`
 * may be NULL. */
void chat_rebuild_replay(ChatMsgList *list, const AiConversation *conv,
                         char *const *thinking, const CmdRunLog *rlog,
                         const CmdBatchSet *batches);

#endif /* NUTSHELL_CHAT_REBUILD_H */
