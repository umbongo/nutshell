#include "string_utils.h"
#include "xmalloc.h"
#include <string.h>
#include <ctype.h>
#include <stdint.h>

char *str_dup(const char *s)
{
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s) + 1u;
    char *copy = xmalloc(len);
    memcpy(copy, s, len);
    return copy;
}

void str_cat(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0u || !src) {
        return;
    }
    size_t dst_len = strlen(dst);
    if (dst_len >= dst_size - 1u) {
        return;  /* buffer already full */
    }
    size_t remaining = dst_size - dst_len - 1u;
    strncat(dst, src, remaining);
}

void str_trim(char *s)
{
    if (!s) {
        return;
    }

    /* Trim trailing whitespace */
    size_t len = strlen(s);
    while (len > 0u && isspace((unsigned char)s[len - 1u])) {
        s[--len] = '\0';
    }

    /* Trim leading whitespace */
    size_t start = 0u;
    while (s[start] != '\0' && isspace((unsigned char)s[start])) {
        start++;
    }
    if (start > 0u) {
        memmove(s, s + start, len - start + 1u);
    }
}

int str_starts_with(const char *s, const char *prefix)
{
    if (!s || !prefix) {
        return 0;
    }
    size_t plen = strlen(prefix);
    return strncmp(s, prefix, plen) == 0;
}

int str_ends_with(const char *s, const char *suffix)
{
    if (!s || !suffix) {
        return 0;
    }
    size_t slen   = strlen(s);
    size_t suflen = strlen(suffix);
    if (suflen > slen) {
        return 0;
    }
    return strcmp(s + (slen - suflen), suffix) == 0;
}

/* ---- UTF-8 encoder ---------------------------------------------------- */

int utf8_encode(uint32_t cp, char *buf)
{
    if (cp < 0x80) {
        buf[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp < 0x110000) {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

/* ---- ANSI escape sequence stripper ------------------------------------- */

typedef enum {
    AS_NORMAL = 0,  /* plain text */
    AS_ESC,         /* saw ESC (0x1B) */
    AS_CSI,         /* inside ESC [ ... sequence */
    AS_OSC,         /* inside ESC ] ... sequence (OSC) */
    AS_OSC_ESC      /* saw ESC inside an OSC (potential ST: ESC \) */
} AnsiState;

size_t ansi_strip(char *dst, size_t dst_size,
                  const char *src, size_t src_len)
{
    if (!dst || dst_size == 0u) return 0u;
    if (!src) { dst[0] = '\0'; return 0u; }

    AnsiState state = AS_NORMAL;
    size_t out = 0u;

    for (size_t i = 0u; i < src_len; i++) {
        unsigned char c = (unsigned char)src[i];

        switch (state) {
        case AS_NORMAL:
            if (c == 0x1Bu) {             /* ESC */
                state = AS_ESC;
            } else if (c == '\r') {
                /* suppress bare CR — the log is line-based plain text */
            } else {
                if (out < dst_size - 1u) dst[out++] = (char)c;
            }
            break;

        case AS_ESC:
            if (c == '[') {
                state = AS_CSI;
            } else if (c == ']') {
                state = AS_OSC;
            } else {
                /* Two-char escape sequence (e.g., ESC M reverse-index):
                 * skip just the introducer byte and return to normal. */
                state = AS_NORMAL;
            }
            break;

        case AS_CSI:
            /* Accumulate until a letter (final byte, 0x40–0x7E) */
            if (c >= 0x40u && c <= 0x7Eu) {
                state = AS_NORMAL;
            }
            /* else: still in sequence parameters/intermediates — skip */
            break;

        case AS_OSC:
            if (c == 0x07u) {             /* BEL terminates OSC */
                state = AS_NORMAL;
            } else if (c == 0x1Bu) {       /* ESC inside OSC → maybe ST */
                state = AS_OSC_ESC;
            }
            /* else: inside OSC payload — skip */
            break;

        case AS_OSC_ESC:
            /* ESC \ = String Terminator; anything else is odd but end OSC */
            state = AS_NORMAL;
            break;
        }
    }

    dst[out] = '\0';
    return out;
}
