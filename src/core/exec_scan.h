#ifndef NUTSHELL_EXEC_SCAN_H
#define NUTSHELL_EXEC_SCAN_H

/*
 * exec_scan -- the one [EXEC]...[/EXEC] marker scanner of a reply. It serves
 * ai_extract_command(), ai_extract_commands_full() and ai_response_split()
 * (src/core/ai_prompt.c), which decide what runs, and chat_exec_split()
 * (src/core/chat_exec.c), which decides what the chat panel draws as a
 * command, so what is drawn as a command is exactly what can run.
 *
 * A marker is "quoted" when its opening [EXEC] lies inside a fenced code
 * block (the renderer's own fences, from md_code_blocks(), widened to take in
 * the opening and closing fence lines) or inside an inline code span, found
 * with md_parse_inline() exactly as the renderer calls it (a list item or
 * heading from after its marker, a table row cell by cell), so bold, italic
 * and strikethrough spans take backticks first, as they do on screen. What
 * the user sees drawn as code is never a command. Portable, no windows.h.
 */

#include <stddef.h>
#include "md_blocks.h"
#include "markdown.h"

typedef struct { size_t start, end; } ExecFence;   /* fence lines included */

typedef struct {
    const char *text;
    const char *end;      /* text + length: the text need not be NUL-terminated */
    const char *pos;      /* where the next marker search starts; a caller may
                           * set it (within [text, end]) after init to scan
                           * from the middle of the reply */
    ExecFence  *fences;
    int         n_fences;
    int         failed;   /* fence list could not be built: all is quoted */
    /* Inline code spans of the line last asked about, as [start, end) byte
     * offsets into the text (the content between the backticks). Computed
     * once per line, however many markers it holds. */
    size_t      line_off; /* start of that line; SIZE_MAX before the first */
    size_t     *code;     /* pairs: code[2*i], code[2*i+1] */
    int         n_code;
    int         cap_code;
    int         line_failed;
} ExecScan;

typedef struct {
    const char *prev;     /* scan position before this marker: the end of the
                           * previous marker, or the start of the reply */
    const char *marker;   /* the opening "[EXEC]" */
    const char *start;    /* first byte of the payload */
    const char *end;      /* the closing "[/EXEC]" */
    int         quoted;
} ExecMark;

/* Prepare a scan of text[0, len). Fences are found once, here. Pair every
 * init with exec_scan_free(). */
void exec_scan_init(ExecScan *s, const char *text, size_t len);
void exec_scan_free(ExecScan *s);

/* 1 when the "[EXEC]" at `marker` (inside the scanned text) is quoted. */
int exec_scan_quoted(ExecScan *s, const char *marker);

/* Next marker, live or quoted, in reply order; 0 when none is left. An
 * opening marker with no closing one ends the scan. An opener whose closer
 * lies past another opener is not a marker (the text "`[EXEC]` markers" and
 * then a real marker on a later line must not merge into one): it is
 * skipped and the scan resumes at the next opener. */
int exec_scan_next(ExecScan *s, ExecMark *m);

#endif /* NUTSHELL_EXEC_SCAN_H */
