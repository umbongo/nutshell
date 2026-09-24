#include "paste_filter.h"
#include <stdlib.h>
#include <string.h>

/* Classify the character starting at in[i]: returns the number of input
 * bytes it occupies (1 normally, 2 for a C1 control's two-byte UTF-8 form),
 * sets *is_control when it is one paste_filter_controls() must remove, and
 * (only when *is_control is set) *code to the control code -- 0x00-0x1F,
 * 0x7F, or 0x80-0x9F -- so callers building a visible substitute know which
 * glyph to use. Shared by paste_filter_controls() and
 * paste_visualize_controls() so both agree exactly on what gets stripped. */
static size_t classify_char(const unsigned char *in, size_t len, size_t i,
                            int *is_control, unsigned *code)
{
    unsigned char c = in[i];

    /* A C1 control's UTF-8 encoding is always 0xC2 followed by 0x80-0x9F.
     * 0xC2 0xA0-0xBF are ordinary printable characters (NBSP, section sign,
     * etc.) and must not be touched. */
    if (c == 0xC2u && i + 1 < len) {
        unsigned char c2 = in[i + 1];
        if (c2 >= 0x80u && c2 <= 0x9Fu) {
            *is_control = 1;
            *code = c2;
            return 2;
        }
    }

    if (c < 0x20u) {
        if (c == '\t' || c == '\n' || c == '\r') {
            *is_control = 0;
        } else {
            *is_control = 1;
            *code = c;
        }
        return 1;
    }

    if (c == 0x7Fu) {
        *is_control = 1;
        *code = c;
        return 1;
    }

    *is_control = 0;
    return 1;
}

size_t paste_filter_controls(const char *in, size_t in_len, char *out, size_t *removed)
{
    size_t stripped = 0;
    size_t o = 0;

    if (!in) in_len = 0;

    for (size_t i = 0; i < in_len; ) {
        int is_control = 0;
        unsigned code = 0;
        size_t n = classify_char((const unsigned char *)in, in_len, i, &is_control, &code);

        if (is_control) {
            stripped++;
        } else {
            /* o <= i always holds, so [o, o+n) can only ever overlap
             * [i, i+n) from below -- memmove handles that correctly, and
             * is a no-op copy when nothing has been stripped yet (o == i). */
            if (out) memmove(out + o, in + i, n);
            o += n;
        }
        i += n;
    }

    if (removed) *removed = stripped;
    return o;
}

/* Append the 3-byte UTF-8 encoding of a Unicode control-picture codepoint
 * (always in U+2400-U+2421, so always exactly 3 bytes) to *buf, growing it
 * as needed. Returns 1 on success, 0 on allocation failure (in which case
 * *buf has already been freed and set to NULL). */
static int append_control_picture(char **buf, size_t *len, size_t *cap, unsigned code)
{
    unsigned cp;
    if (code == 0x7Fu) {
        cp = 0x2421u;                       /* SYMBOL FOR DELETE */
    } else if (code >= 0x80u) {
        cp = 0x2400u + (code - 0x80u);      /* C1: reuse the C0 picture set */
    } else {
        cp = 0x2400u + code;                /* C0, including ESC -> U+241B */
    }

    if (*len + 3 > *cap) {
        size_t new_cap = (*len + 3) * 2;
        char *nb = (char *)realloc(*buf, new_cap);
        if (!nb) { free(*buf); *buf = NULL; return 0; }
        *buf = nb;
        *cap = new_cap;
    }

    unsigned char *enc = (unsigned char *)(*buf + *len);
    enc[0] = (unsigned char)(0xE0u | (cp >> 12));
    enc[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
    enc[2] = (unsigned char)(0x80u | (cp & 0x3Fu));
    *len += 3;
    return 1;
}

char *paste_visualize_controls(const char *raw, size_t raw_len, size_t *out_removed)
{
    if (!raw) {
        if (out_removed) *out_removed = 0;
        return NULL;
    }

    size_t cap = raw_len + 32;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        if (out_removed) *out_removed = 0;
        return NULL;
    }
    size_t len = 0;
    size_t stripped = 0;

    for (size_t i = 0; i < raw_len; ) {
        int is_control = 0;
        unsigned code = 0;
        size_t n = classify_char((const unsigned char *)raw, raw_len, i, &is_control, &code);

        if (is_control) {
            stripped++;
            if (!append_control_picture(&buf, &len, &cap, code)) {
                if (out_removed) *out_removed = 0;
                return NULL;
            }
        } else {
            if (len + n + 1 > cap) {
                size_t new_cap = (len + n + 1) * 2;
                char *nb = (char *)realloc(buf, new_cap);
                if (!nb) { free(buf); if (out_removed) *out_removed = 0; return NULL; }
                buf = nb;
                cap = new_cap;
            }
            memcpy(buf + len, raw + i, n);
            len += n;
        }
        i += n;
    }

    if (len + 1 > cap) {
        char *nb = (char *)realloc(buf, len + 1);
        if (nb) buf = nb;
        /* If realloc-down somehow fails, buf (still cap >= len+1) is fine. */
    }
    buf[len] = '\0';

    if (out_removed) *out_removed = stripped;
    return buf;
}
