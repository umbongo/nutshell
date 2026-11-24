/* json_scan.c: find one string value in a JSON text without the tokenizer's string cap. */
#include "json_scan.h"

#include <stdlib.h>
#include <string.h>

static const char *skip_ws(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

/* p is at an opening quote; returns the position after the closing quote, or NULL. */
static const char *skip_string(const char *p, const char *end)
{
    p++;
    while (p < end && *p != '"') p += (*p == '\\' && p + 1 < end) ? 2 : 1;
    return p < end ? p + 1 : NULL;
}

/* Skip one value (any type) starting at p; strings are skipped whole so a bracket or key
 * inside one means nothing. Returns the position after it, or NULL when malformed. */
static const char *skip_value(const char *p, const char *end)
{
    p = skip_ws(p, end);
    if (p >= end) return NULL;
    if (*p == '"') return skip_string(p, end);
    if (*p != '{' && *p != '[') {   /* number, true, false, null */
        const char *s = p;
        while (p < end && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\t' &&
               *p != '\r' && *p != '\n') p++;
        return p > s ? p : NULL;
    }
    int depth = 0;
    while (p < end) {
        char c = *p;
        if (c == '"') {
            p = skip_string(p, end);
            if (!p) return NULL;
            continue;
        }
        p++;
        if (c == '{' || c == '[') depth++;
        else if ((c == '}' || c == ']') && --depth == 0) return p;
    }
    return NULL;
}

static int put_utf8(char *out, size_t *n, unsigned long cp)
{
    if (cp < 0x80) { out[(*n)++] = (char)cp; }
    else if (cp < 0x800) {
        out[(*n)++] = (char)(0xC0 | (cp >> 6));
        out[(*n)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out[(*n)++] = (char)(0xE0 | (cp >> 12));
        out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*n)++] = (char)(0x80 | (cp & 0x3F));
    } else {
        out[(*n)++] = (char)(0xF0 | (cp >> 18));
        out[(*n)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*n)++] = (char)(0x80 | (cp & 0x3F));
    }
    return 1;
}

static int hex4(const char *p, const char *end, unsigned long *v)
{
    if (end - p < 4) return 0;
    *v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? 10 + c - 'a'
              : (c >= 'A' && c <= 'F') ? 10 + c - 'A' : -1;
        if (d < 0) return 0;
        *v = (*v << 4) | (unsigned long)d;
    }
    return 1;
}

/* p is at the opening quote of the value. Decodes into a new heap buffer. */
static int decode_string(const char *p, const char *end, size_t max_bytes,
                         char **out, size_t *out_len)
{
    const char *close = skip_string(p, end);
    if (!close) return 0;
    const char *q = p + 1, *stop = close - 1;   /* stop is the closing quote */
    /* decoded bytes never exceed the raw ones: \uXXXX is 6 raw for at most 3, a pair 12 for 4 */
    char *buf = (char *)malloc((size_t)(stop - q) + 1);
    if (!buf) return 0;
    size_t n = 0;
    while (q < stop) {
        if (*q != '\\') { buf[n++] = *q++; continue; }
        if (++q >= stop) goto bad;
        char c = *q++;
        unsigned long cp;
        switch (c) {
        case 'n': cp = '\n'; break;
        case 't': cp = '\t'; break;
        case 'r': cp = '\r'; break;
        case 'b': cp = '\b'; break;
        case 'f': cp = '\f'; break;
        case '"': case '\\': case '/': cp = (unsigned char)c; break;
        case 'u':
            if (!hex4(q, stop, &cp)) goto bad;
            q += 4;
            if (cp >= 0xD800 && cp < 0xDC00) {
                unsigned long lo;
                if (stop - q >= 6 && q[0] == '\\' && q[1] == 'u' && hex4(q + 2, stop, &lo) &&
                    lo >= 0xDC00 && lo < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    q += 6;
                } else {
                    cp = 0xFFFD;
                }
            } else if (cp >= 0xDC00 && cp < 0xE000) {
                cp = 0xFFFD;
            } else if (cp == 0) {
                cp = ' ';
            }
            break;
        default: goto bad;
        }
        put_utf8(buf, &n, cp);
    }
    if (n > max_bytes) goto bad;
    buf[n] = '\0';
    *out = buf;
    *out_len = n;
    return 1;
bad:
    memset(buf, 0, (size_t)(stop - p));   /* decoded text may be a transcript */
    free(buf);
    return 0;
}

/* p is at a value; follow path from it. */
static int follow(const char *p, const char *end, const char *const *path, size_t max_bytes,
                  char **out, size_t *out_len)
{
    p = skip_ws(p, end);
    if (p >= end) return 0;
    if (!path[0]) return *p == '"' ? decode_string(p, end, max_bytes, out, out_len) : 0;
    if (strcmp(path[0], "[0]") == 0) {
        if (*p != '[') return 0;
        p = skip_ws(p + 1, end);
        if (p >= end || *p == ']') return 0;
        return follow(p, end, path + 1, max_bytes, out, out_len);
    }
    if (*p != '{') return 0;
    size_t kl = strlen(path[0]);
    p++;
    for (;;) {
        p = skip_ws(p, end);
        if (p >= end || *p != '"') return 0;   /* also ends at '}' : the key is not here */
        const char *close = skip_string(p, end);
        if (!close) return 0;
        const char *k = p + 1;
        size_t klen = (size_t)(close - 1 - k);
        p = skip_ws(close, end);
        if (p >= end || *p != ':') return 0;
        p++;
        if (klen == kl && memcmp(k, path[0], kl) == 0)
            return follow(p, end, path + 1, max_bytes, out, out_len);
        p = skip_value(p, end);
        if (!p) return 0;
        p = skip_ws(p, end);
        if (p >= end || *p != ',') return 0;
        p++;
    }
}

int json_scan_path_string(const char *json, size_t len, const char *const *path,
                          size_t max_bytes, char **out, size_t *out_len)
{
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (!json || !path || !out || !out_len) return 0;
    return follow(json, json + len, path, max_bytes, out, out_len);
}

int json_scan_string(const char *json, size_t len, int depth, const char *key,
                     size_t max_bytes, char **out, size_t *out_len)
{
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (depth != 1 || !key) return 0;
    const char *const path[] = {key, NULL};
    return json_scan_path_string(json, len, path, max_bytes, out, out_len);
}
