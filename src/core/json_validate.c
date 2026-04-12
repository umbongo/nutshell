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
