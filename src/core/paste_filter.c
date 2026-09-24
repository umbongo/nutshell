#include "paste_filter.h"
#include <stdlib.h>
#include <string.h>

/* Classify the character starting at in[i]: returns the number of input
 * bytes it occupies (1 normally, 2 for a C1 control's two-byte UTF-8 form,
 * 3 for a bidi override/isolate), sets *is_control when it is one
 * paste_filter_controls() must remove, and (only when *is_control is set)
 * *code to the control code -- 0x00-0x1F, 0x7F, 0x80-0x9F for a C0/DEL/C1
 * control, or the bidi character's actual codepoint (always >= 0x2000,
 * so never confusable with a C0/C1/DEL code) -- so callers building a
 * visible substitute know which glyph or tag to use. Shared by
 * paste_filter_controls() and paste_visualize_controls() so both agree
 * exactly on what gets stripped. */
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

    /* Bidi override/isolate controls: U+202A-U+202E (LRE/RLE/PDF/LRO/RLO,
     * UTF-8 E2 80 AA..AE) and U+2066-U+2069 (LRI/RLI/FSI/PDI, UTF-8
     * E2 81 A6..A9). Hardening against "Trojan Source" (CVE-2021-42574):
     * these can make text render in an order different from the bytes
     * that are actually stored and sent, so they are stripped from every
     * paste the same as a raw control byte. */
    if (c == 0xE2u && i + 2 < len) {
        unsigned char c2 = in[i + 1], c3 = in[i + 2];
        if (c2 == 0x80u && c3 >= 0xAAu && c3 <= 0xAEu) {
            *is_control = 1;
            *code = 0x202Au + (unsigned)(c3 - 0xAAu);
            return 3;
        }
        if (c2 == 0x81u && c3 >= 0xA6u && c3 <= 0xA9u) {
            *is_control = 1;
            *code = 0x2066u + (unsigned)(c3 - 0xA6u);
            return 3;
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

/* Short bracketed tag naming a bidi override/isolate codepoint, for the
 * paste-confirm preview -- Unicode has no single-codepoint "control
 * picture" for these the way it does for C0/C1/DEL, and inserting the raw
 * character would just make the preview text reorder too, defeating the
 * point of showing it. `code` is one of the codepoints classify_char()
 * sets for a bidi match (0x202A-0x202E or 0x2066-0x2069). */
static const char *bidi_tag(unsigned code)
{
    switch (code) {
    case 0x202Au: return "[LRE]";
    case 0x202Bu: return "[RLE]";
    case 0x202Cu: return "[PDF]";
    case 0x202Du: return "[LRO]";
    case 0x202Eu: return "[RLO]";
    case 0x2066u: return "[LRI]";
    case 0x2067u: return "[RLI]";
    case 0x2068u: return "[FSI]";
    case 0x2069u: return "[PDI]";
    default:       return "[BIDI]";
    }
}

/* Ensure *buf has room for `extra` more bytes plus the NUL terminator
 * paste_visualize_controls() writes after its loop, growing (and updating
 * *cap) if not. Returns 1 on success, 0 on allocation failure (in which
 * case *buf has already been freed and set to NULL). Centralising the
 * "+1 for the terminator" in the growth check itself, rather than relying
 * on a final realloc after the loop to make room for it, is what fixes a
 * one-byte heap overflow: if that final realloc had ever failed, the
 * unconditional `buf[len] = '\0'` after it would have written one byte
 * past a buffer sized to exactly `len`. */
static int ensure_cap(char **buf, size_t *len, size_t *cap, size_t extra)
{
    if (*len + extra + 1 <= *cap) return 1;
    size_t new_cap = (*len + extra + 1) * 2;
    char *nb = (char *)realloc(*buf, new_cap);
    if (!nb) { free(*buf); *buf = NULL; return 0; }
    *buf = nb;
    *cap = new_cap;
    return 1;
}

/* Append the visible substitute for a stripped character to *buf, growing
 * it as needed: a bidi override/isolate becomes its bracketed tag (see
 * bidi_tag()); anything else is a C0/C1/DEL control and becomes the
 * 3-byte UTF-8 encoding of its Unicode control-picture codepoint (always
 * in U+2400-U+2421). Returns 1 on success, 0 on allocation failure (in
 * which case *buf has already been freed and set to NULL). */
static int append_control_picture(char **buf, size_t *len, size_t *cap, unsigned code)
{
    if (code >= 0x2000u) {
        const char *tag = bidi_tag(code);
        size_t tag_len = strlen(tag);
        if (!ensure_cap(buf, len, cap, tag_len)) return 0;
        memcpy(*buf + *len, tag, tag_len);
        *len += tag_len;
        return 1;
    }

    unsigned cp;
    if (code == 0x7Fu) {
        cp = 0x2421u;                       /* SYMBOL FOR DELETE */
    } else if (code >= 0x80u) {
        cp = 0x2400u + (code - 0x80u);      /* C1: reuse the C0 picture set */
    } else {
        cp = 0x2400u + code;                /* C0, including ESC -> U+241B */
    }

    if (!ensure_cap(buf, len, cap, 3)) return 0;

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
            if (!ensure_cap(&buf, &len, &cap, n)) {
                if (out_removed) *out_removed = 0;
                return NULL;
            }
            memcpy(buf + len, raw + i, n);
            len += n;
        }
        i += n;
    }

    /* ensure_cap() always reserves room for this terminator alongside
     * whatever it just appended, so cap >= len + 1 here unconditionally --
     * no realloc (and so no failure path) needed to write it. */
    buf[len] = '\0';

    if (out_removed) *out_removed = stripped;
    return buf;
}

int text_has_unsafe_command_char(const char *s)
{
    if (!s) return 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned char c = p[0];
        if (c < 0x20u || c == 0x7Fu) return 1;
        /* C1 control, UTF-8-encoded (0xC2 0x80-0x9F). p[1] is always safe
         * to read here: the loop only continues while *p != '\0', so p[1]
         * is either the next real byte or the string's own terminator. */
        if (c == 0xC2u && p[1] >= 0x80u && p[1] <= 0x9Fu) return 1;
        /* Bidi override/isolate, UTF-8-encoded (E2 80 AA-AE / E2 81 A6-A9).
         * p[2] is only read once p[1] has been confirmed non-zero (the
         * lead byte of a match), so it too is always within the string. */
        if (c == 0xE2u && p[1] == 0x80u && p[2] >= 0xAAu && p[2] <= 0xAEu) return 1;
        if (c == 0xE2u && p[1] == 0x81u && p[2] >= 0xA6u && p[2] <= 0xA9u) return 1;
        p++;
    }
    return 0;
}
