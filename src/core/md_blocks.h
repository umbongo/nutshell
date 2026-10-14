#ifndef NUTSHELL_MD_BLOCKS_H
#define NUTSHELL_MD_BLOCKS_H

/*
 * md_blocks -- pure splitter that finds the fenced code blocks in a piece of
 * reply text, using the markdown renderer's own line classifier
 * (md_classify_line in src/ui/markdown.h) so it recognises exactly the fences
 * the renderer draws: a line that starts, at column 0, with three backticks.
 * An indented fence is not a fence. Spec:
 * docs/superpowers/specs/2026-10-13-command-detail-and-copy-design.md
 * section 3.
 */

#include <stddef.h>

/* Byte offsets into the text passed to md_code_blocks(): [start, end) is the
 * block's body, the fence lines excluded. The line terminator (LF or CRLF)
 * that ends the last body line is not part of the body, so a body can be
 * copied to the clipboard as is. An empty body has start == end. */
typedef struct { size_t start, end; } MdCodeSpan;

/* Report each fenced code block's body span in `text` (`len` bytes), in
 * order. The text is split at '\n' like the renderer does; a trailing '\r' is
 * part of its line. An unterminated fence runs to the end of the text.
 * Returns the number of blocks found, which may exceed `max`: only the first
 * `max` are written to `out`. `out` may be NULL (count only). NULL `text`
 * counts as no blocks. */
int md_code_blocks(const char *text, size_t len, MdCodeSpan *out, int max);

#endif /* NUTSHELL_MD_BLOCKS_H */
