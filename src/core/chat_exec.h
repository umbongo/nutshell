/* src/core/chat_exec.h */
#ifndef NUTSHELL_CHAT_EXEC_H
#define NUTSHELL_CHAT_EXEC_H

/*
 * The [EXEC] segment split of an AI reply, cut exactly where the chat panel
 * cuts it when it paints (src/ui/chat_listview.c draw_ai_text_with_exec and
 * its measuring twin), so the copier (chat_copy.c) and the painter agree on
 * every byte. In order:
 *
 *   - the prose before an "[EXEC]" marker, its trailing '\n'/'\r' bytes
 *     dropped, and no segment at all when nothing is left;
 *   - the command between "[EXEC]" and "[/EXEC]", markers excluded (an
 *     empty command is a segment with start == end, which paints nothing);
 *   - an "[EXEC]" with no "[/EXEC]" after it: everything from the marker
 *     itself to the end, the marker included, as one command segment;
 *   - the prose after the last "[/EXEC]", untrimmed (it usually starts with
 *     the '\n' that followed the marker, which paints as a blank line).
 *
 * A stray "[/EXEC]" with no "[EXEC]" before it is prose. Portable, no
 * windows.h. Spec: docs/superpowers/specs/
 * 2026-10-18-ai-panel-text-selection-design.md section 2.
 */

typedef enum { CHAT_SEG_PROSE = 0, CHAT_SEG_EXEC = 1 } ChatSegKind;

typedef struct {
    ChatSegKind kind;
    int start;   /* byte offset into the text */
    int end;     /* exclusive */
} ChatSeg;

/* Iterator. Set *pos = 0 and call until it returns 0; every call that
 * returns 1 writes the next segment to *out. `len` bounds the walk (the text
 * need not be NUL-terminated at len). NULL text/pos/out or len <= 0 yield no
 * segment. */
int chat_exec_split(const char *text, int len, int *pos, ChatSeg *out);

#endif /* NUTSHELL_CHAT_EXEC_H */
