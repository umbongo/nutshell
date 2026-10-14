#include "md_blocks.h"
#include "../ui/markdown.h"   /* md_classify_line: header-only, Win32-free */

/* True if the line [p, p + n) is a fence according to the renderer's own
 * classifier. Only the first three bytes can decide that, so the classifier
 * is handed a three-byte copy (it needs a NUL-terminated line, and `text`
 * need not be NUL-terminated at `len`). */
static int line_is_fence(const char *p, size_t n, int in_code_block)
{
    char head[4] = { 0, 0, 0, 0 };
    for (size_t i = 0; i < n && i < 3; i++) head[i] = p[i];
    return md_classify_line(head, in_code_block).type == MD_LINE_CODE_FENCE;
}

int md_code_blocks(const char *text, size_t len, MdCodeSpan *out, int max)
{
    if (!text) return 0;
    int count = 0;
    int in_code = 0;
    size_t body_start = 0;
    size_t p = 0;

    while (p < len) {
        size_t eol = p;
        while (eol < len && text[eol] != '\n') eol++;
        size_t next = (eol < len) ? eol + 1 : eol;

        if (line_is_fence(text + p, eol - p, in_code)) {
            if (!in_code) {
                in_code = 1;
                body_start = next;
            } else {
                in_code = 0;
                /* The body ends before every trailing CR and LF (blank lines
                 * included) that precede this closing fence. */
                size_t end = p;
                while (end > body_start &&
                       (text[end - 1] == '\n' || text[end - 1] == '\r')) end--;
                if (out && count < max) {
                    out[count].start = body_start;
                    out[count].end = end;
                }
                count++;
            }
        }
        p = next;
    }

    if (in_code) {
        /* Unterminated: the body runs to the end (minus every trailing CR and LF). */
        size_t end = len;
        while (end > body_start &&
               (text[end - 1] == '\n' || text[end - 1] == '\r')) end--;
        if (body_start > end) body_start = end;
        if (out && count < max) {
            out[count].start = body_start;
            out[count].end = end;
        }
        count++;
    }
    return count;
}
