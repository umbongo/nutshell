#include "text_lines.h"

size_t text_lf_to_crlf(const char *in, char *out, size_t out_sz)
{
    size_t need = 0;   /* full length of the converted text */
    size_t used = 0;   /* characters written to out (excluding NUL) */
    int truncated = 0;
    size_t cap = (out && out_sz > 0) ? out_sz - 1 : 0;

    if (!in) {
        if (out && out_sz > 0) out[0] = '\0';
        return 0;
    }

    for (size_t i = 0; in[i]; i++) {
        char ch = in[i];
        size_t unit;           /* characters this step emits */
        int is_pair = 0;

        if (ch == '\r' && in[i + 1] == '\n') {
            unit = 2;
            is_pair = 1;
        } else if (ch == '\n') {
            unit = 2;
            is_pair = 1;
        } else {
            unit = 1;
        }

        if (out && out_sz > 0 && !truncated) {
            if (used + unit <= cap) {
                if (is_pair) {
                    out[used] = '\r';
                    out[used + 1] = '\n';
                } else {
                    out[used] = ch;
                }
                used += unit;
            } else {
                truncated = 1;
            }
        }
        need += unit;
        if (ch == '\r' && is_pair) i++;   /* consumed the LF too */
    }

    if (out && out_sz > 0) out[used] = '\0';
    return need;
}
