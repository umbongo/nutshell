/* src/core/chat_copy.h */
#ifndef NUTSHELL_CHAT_COPY_H
#define NUTSHELL_CHAT_COPY_H

/*
 * Visible-text extraction for the AI Assist panel's character selection
 * (spec docs/superpowers/specs/2026-10-18-ai-panel-text-selection-design.md
 * section 2). No geometry: every newline in the source is a hard break the
 * renderer never joins, so the visible text of a byte range is a pure
 * function of the source. This walks the text exactly as the painter does --
 * the same [EXEC] split (chat_exec_split), the same line classifier and
 * inline parser (markdown.h), the same list numbering and table columns
 * (md_table.h) -- and writes what the painter shows. Portable, no windows.h.
 */

#include <stddef.h>
#include "chat_msg.h"

/* Flags for chat_copy_visible(). */
#define CHAT_COPY_MD    1   /* prose is markdown (the render setting is on) */
#define CHAT_COPY_EXEC  2   /* the text is an AI reply: split at [EXEC] */

/* The visible text of text[start, end) (len bytes of text). A range edge
 * inside a multi-byte character is widened to take the whole character.
 *  - CHAT_COPY_EXEC: walk the segments of chat_exec_split(); a command
 *    segment copies its bytes; consecutive segments the range touches are
 *    joined with '\n' (they paint on separate lines). Without the flag the
 *    whole text is one prose segment.
 *  - CHAT_COPY_MD: per segment, the markdown state (fence, list number,
 *    table block) starts afresh, as md_render runs once per segment; per
 *    source line: prose, heading, list and quote content goes through
 *    md_parse_inline, markers never copied, whitespace at the start of the
 *    content dropped as the painter drops it; a list item gets "- " or its
 *    displayed number when the range covers a byte of its marker; fenced
 *    code lines copy verbatim, fence lines, rules and the table alignment
 *    row copy nothing; a table row copies its cells (md_table_split_row, as
 *    many columns as the painter draws) joined with '\t'.
 *    Without the flag a prose segment copies its bytes as they are.
 *  - A '\n' byte inside the range is copied as '\n' (between lines, and for
 *    the blank lines a range spans); nothing else is inserted.
 * Writes a heap, NUL-terminated string to *out (the caller frees it) and
 * returns its length; returns -1 with *out = NULL on a NULL argument or
 * when memory runs out. An empty range gives "" and 0. */
int chat_copy_visible(const char *text, int len, int start, int end,
                      int flags, char **out);

/* One end of a selection: a message (by its id), a part of it (TR_PART_*
 * in text_runs.h) and a byte offset into that part's source text. */
typedef struct { int msg_id; int part; int off; } ChatSelPos;

/* Reading order: message id (ids only grow along the list), then part
 * (the Thinking body before the reply text), then offset. <0, 0, >0. */
int chat_sel_cmp(ChatSelPos a, ChatSelPos b);

/* The part of the ordered selection [a, b) that lies in part `part` of
 * message `msg_id`, whose text is part_len bytes: [*s, *e). Offsets past
 * part_len are clamped. Returns 1 when non-empty, 0 otherwise. */
int chat_sel_part_range(ChatSelPos a, ChatSelPos b, int msg_id, int part,
                        int part_len, int *s, int *e);

/* The visible text of the selection between a and b (either order) across
 * the list: command items skipped, the Thinking body only while expanded,
 * a user bubble, status and tool line as their bytes, an AI reply through
 * chat_copy_visible(CHAT_COPY_EXEC | (markdown ? CHAT_COPY_MD : 0)); the
 * non-empty pieces joined with "\n\n". Writes a heap string to *out (caller
 * frees) and returns its length; returns 0 with *out = NULL when a or b no
 * longer names a message in the list, the selection is empty or memory runs
 * out. */
size_t chat_copy_selection(const ChatMsgList *list, ChatSelPos a,
                           ChatSelPos b, int markdown, char **out);

/* Select all: from offset 0 of the first item with text (its Thinking body
 * when that is expanded) to the end of the last item with text, command
 * items skipped. Returns 1 and fills *a and *b, or 0 when there is none. */
int chat_sel_all(const ChatMsgList *list, ChatSelPos *a, ChatSelPos *b);

#endif /* NUTSHELL_CHAT_COPY_H */
