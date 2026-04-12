#include "json_validate.h"
#include <string.h>
#include <stdio.h>

/* ---- Consolidated JSON escape function ----------------------------------- */

size_t json_escape_string(const char *s, size_t s_len, char *buf,
                          size_t buf_size, size_t pos, int add_quotes)
{
    if (!s || !buf || pos >= buf_size) return 0;

    if (add_quotes) {
        if (pos + 1 >= buf_size) return 0;
        buf[pos++] = '"';
    }

    for (size_t i = 0; i < s_len; i++) {
        unsigned char c = (unsigned char)s[i];
        const char *esc = NULL;
        char u_esc[7];

        switch (c) {
            case '"':  esc = "\\\""; break;
            case '\\': esc = "\\\\"; break;
            case '\n': esc = "\\n";  break;
            case '\r': esc = "\\r";  break;
            case '\t': esc = "\\t";  break;
            default:
                /* \b (0x08) and \f (0x0C) are intentionally not given named
                 * escapes — they serialise as \u0008 / \u000c, which is
                 * semantically equivalent per RFC 8259. */
                if (c < 0x20) {
                    snprintf(u_esc, sizeof(u_esc), "\\u%04x", (unsigned)c);
                    esc = u_esc;
                }
                break;
        }

        if (esc) {
            size_t len = strlen(esc);
            if (pos + len >= buf_size) return 0;
            memcpy(buf + pos, esc, len);
            pos += len;
        } else {
            if (pos + 1 >= buf_size) return 0;
            buf[pos++] = (char)c;
        }
    }

    if (add_quotes) {
        if (pos + 1 >= buf_size) return 0;
        buf[pos++] = '"';
    }

    return pos;
}

/* ---- JSON validator (single-pass state machine) -------------------------- */

#define VALIDATE_MAX_DEPTH 128

int json_validate(const char *buf, size_t len, char *error, size_t error_size)
{
    if (!buf || len == 0) {
        if (error && error_size > 0)
            snprintf(error, error_size, "Empty or null JSON input");
        return 0;
    }

    /* Nesting stack: '{' or '[' */
    char stack[VALIDATE_MAX_DEPTH];
    int depth = 0;

    int in_string = 0;
    int escape_next = 0;
    int unicode_remaining = 0;  /* counts down hex digits expected after \u */

    /* Context tracking for trailing-comma detection */
    /* after_comma[d] = 1 if the last significant token at depth d was a comma */
    char after_comma[VALIDATE_MAX_DEPTH];
    memset(after_comma, 0, sizeof(after_comma));

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)buf[i];

        /* Inside a \uXXXX escape — expect hex digits */
        if (unicode_remaining > 0) {
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                (c >= 'A' && c <= 'F')) {
                unicode_remaining--;
                continue;
            }
            if (error && error_size > 0)
                snprintf(error, error_size,
                         "Invalid unicode escape at byte %zu", i);
            return 0;
        }

        /* Inside an escape sequence (char after \) */
        if (escape_next) {
            escape_next = 0;
            switch (c) {
                case '"': case '\\': case '/': case 'b':
                case 'f': case 'n': case 'r': case 't':
                    continue;
                case 'u':
                    unicode_remaining = 4;
                    continue;
                default:
                    if (error && error_size > 0)
                        snprintf(error, error_size,
                                 "Invalid escape sequence '\\%c' at byte %zu",
                                 (char)c, i);
                    return 0;
            }
        }

        /* Inside a string literal */
        if (in_string) {
            if (c == '\\') {
                escape_next = 1;
                continue;
            }
            if (c == '"') {
                in_string = 0;
                continue;
            }
            if (c < 0x20) {
                if (error && error_size > 0)
                    snprintf(error, error_size,
                             "Unescaped control character 0x%02x at byte %zu",
                             (unsigned)c, i);
                return 0;
            }
            continue;
        }

        /* Outside strings — skip whitespace */
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;

        /* Structural characters */
        switch (c) {
            case '"':
                in_string = 1;
                if (depth > 0) after_comma[depth] = 0;
                continue;

            case '{': case '[':
                if (depth >= VALIDATE_MAX_DEPTH) {
                    if (error && error_size > 0)
                        snprintf(error, error_size,
                                 "Nesting too deep at byte %zu", i);
                    return 0;
                }
                stack[depth] = (char)c;
                depth++;
                if (depth < VALIDATE_MAX_DEPTH)
                    after_comma[depth] = 0;
                continue;

            case '}':
                if (depth == 0 || stack[depth - 1] != '{') {
                    if (error && error_size > 0)
                        snprintf(error, error_size,
                                 "Unexpected '}' at byte %zu", i);
                    return 0;
                }
                if (after_comma[depth]) {
                    if (error && error_size > 0)
                        snprintf(error, error_size,
                                 "Trailing comma before '}' at byte %zu", i);
                    return 0;
                }
                depth--;
                if (depth > 0) after_comma[depth] = 0;
                continue;

            case ']':
                if (depth == 0 || stack[depth - 1] != '[') {
                    if (error && error_size > 0)
                        snprintf(error, error_size,
                                 "Unexpected ']' at byte %zu", i);
                    return 0;
                }
                if (after_comma[depth]) {
                    if (error && error_size > 0)
                        snprintf(error, error_size,
                                 "Trailing comma before ']' at byte %zu", i);
                    return 0;
                }
                depth--;
                if (depth > 0) after_comma[depth] = 0;
                continue;

            case ',':
                if (depth > 0)
                    after_comma[depth] = 1;
                continue;

            case ':':
                continue;

            default:
                /* Numbers, true, false, null — skip their characters */
                if (c == 't' || c == 'f' || c == 'n') {
                    /* Consume literal: true, false, null */
                    const char *lit = NULL;
                    size_t lit_len = 0;
                    if (c == 't') { lit = "true"; lit_len = 4; }
                    else if (c == 'f') { lit = "false"; lit_len = 5; }
                    else { lit = "null"; lit_len = 4; }

                    if (i + lit_len > len ||
                        memcmp(buf + i, lit, lit_len) != 0) {
                        if (error && error_size > 0)
                            snprintf(error, error_size,
                                     "Invalid literal at byte %zu", i);
                        return 0;
                    }
                    i += lit_len - 1;
                    if (depth > 0) after_comma[depth] = 0;
                    continue;
                }
                if (c == '-' || (c >= '0' && c <= '9')) {
                    /* Consume number */
                    while (i + 1 < len) {
                        unsigned char nc = (unsigned char)buf[i + 1];
                        if ((nc >= '0' && nc <= '9') || nc == '.' ||
                            nc == 'e' || nc == 'E' || nc == '+' || nc == '-')
                            i++;
                        else
                            break;
                    }
                    if (depth > 0) after_comma[depth] = 0;
                    continue;
                }
                if (error && error_size > 0)
                    snprintf(error, error_size,
                             "Unexpected character '%c' at byte %zu",
                             (char)c, i);
                return 0;
        }
    }

    /* Check we're not still inside a string */
    if (in_string) {
        if (error && error_size > 0)
            snprintf(error, error_size, "Unterminated string at end of input");
        return 0;
    }

    /* Check nesting closed */
    if (depth != 0) {
        if (error && error_size > 0)
            snprintf(error, error_size,
                     "Unclosed '%c' — depth %d at end of input",
                     stack[depth - 1], depth);
        return 0;
    }

    return 1;
}
