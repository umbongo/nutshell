#include "html_util.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ---- Internal helpers ---------------------------------------------------- */

static int ci_starts_with(const char *s, size_t s_len, const char *prefix)
{
    size_t plen = strlen(prefix);
    if (s_len < plen) return 0;
    for (size_t i = 0; i < plen; i++) {
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i]))
            return 0;
    }
    return 1;
}

/* Case-insensitive search for needle in haystack (length-limited haystack). */
static const char *ci_memmem(const char *hay, size_t hay_len,
                              const char *needle, size_t needle_len)
{
    if (needle_len == 0) return hay;
    if (hay_len < needle_len) return NULL;
    for (size_t i = 0; i <= hay_len - needle_len; i++) {
        size_t j;
        for (j = 0; j < needle_len; j++) {
            if (tolower((unsigned char)hay[i + j]) !=
                tolower((unsigned char)needle[j]))
                break;
        }
        if (j == needle_len) return hay + i;
    }
    return NULL;
}

static int is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

/* Encode a Unicode codepoint as UTF-8. Returns bytes written. */
static int encode_utf8(unsigned long cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)(unsigned char)cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char)(unsigned char)(0xC0 | (cp >> 6));
        out[1] = (char)(unsigned char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(unsigned char)(0xE0 | (cp >> 12));
        out[1] = (char)(unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(unsigned char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp < 0x110000) {
        out[0] = (char)(unsigned char)(0xF0 | (cp >> 18));
        out[1] = (char)(unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(unsigned char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

/* ---- html_strip_tags ----------------------------------------------------- */

typedef enum {
    ST_TEXT,
    ST_TAG,
    ST_SCRIPT,
    ST_STYLE
} StripState;

size_t html_strip_tags(const char *html, size_t html_len,
                       char *out_buf, size_t out_max)
{
    if (!html || !out_buf || out_max == 0) return 0;

    size_t out_pos = 0;
    size_t i = 0;
    StripState state = ST_TEXT;
    int last_was_space = 1; /* suppress leading space */

    while (i < html_len && out_pos + 1 < out_max) {
        char c = html[i];

        switch (state) {
            case ST_TEXT:
                if (c == '<') {
                    /* Peek at tag name to detect script/style */
                    size_t rem = html_len - i;
                    if (ci_starts_with(html + i, rem, "<script") &&
                        (i + 7 >= html_len ||
                         html[i + 7] == '>' || html[i + 7] == ' ' ||
                         html[i + 7] == '\t' || html[i + 7] == '\r' ||
                         html[i + 7] == '\n' || html[i + 7] == '/')) {
                        state = ST_SCRIPT;
                        i++;
                    } else if (ci_starts_with(html + i, rem, "<style") &&
                               (i + 6 >= html_len ||
                                html[i + 6] == '>' || html[i + 6] == ' ' ||
                                html[i + 6] == '\t' || html[i + 6] == '\r' ||
                                html[i + 6] == '\n' || html[i + 6] == '/')) {
                        state = ST_STYLE;
                        i++;
                    } else {
                        state = ST_TAG;
                        i++;
                    }
                } else {
                    /* Emit character, collapsing whitespace */
                    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
                    if (c == ' ' && last_was_space) {
                        i++;
                    } else {
                        out_buf[out_pos++] = c;
                        last_was_space = (c == ' ');
                        i++;
                    }
                }
                break;

            case ST_TAG:
                if (c == '>') {
                    state = ST_TEXT;
                    /* Add a space after tag if not already spaced */
                    if (!last_was_space && out_pos + 1 < out_max) {
                        out_buf[out_pos++] = ' ';
                        last_was_space = 1;
                    }
                }
                i++;
                break;

            case ST_SCRIPT: {
                /* Skip until </script> */
                size_t rem = html_len - i;
                const char *end = ci_memmem(html + i, rem, "</script>", 9);
                if (end) {
                    i = (size_t)(end - html) + 9;
                    state = ST_TEXT;
                    /* Add space after removed block */
                    if (!last_was_space && out_pos + 1 < out_max) {
                        out_buf[out_pos++] = ' ';
                        last_was_space = 1;
                    }
                } else {
                    i = html_len; /* not found — skip rest */
                }
                break;
            }

            case ST_STYLE: {
                /* Skip until </style> */
                size_t rem = html_len - i;
                const char *end = ci_memmem(html + i, rem, "</style>", 8);
                if (end) {
                    i = (size_t)(end - html) + 8;
                    state = ST_TEXT;
                    if (!last_was_space && out_pos + 1 < out_max) {
                        out_buf[out_pos++] = ' ';
                        last_was_space = 1;
                    }
                } else {
                    i = html_len;
                }
                break;
            }
        }
    }

    /* Trim trailing space */
    while (out_pos > 0 && out_buf[out_pos - 1] == ' ') {
        out_pos--;
    }

    out_buf[out_pos] = '\0';
    return out_pos;
}

/* ---- html_decode_entities ------------------------------------------------ */

size_t html_decode_entities(char *buf, size_t len)
{
    if (!buf || len == 0) return 0;

    size_t r = 0; /* read pos */
    size_t w = 0; /* write pos */

    while (r < len) {
        if (buf[r] != '&') {
            buf[w++] = buf[r++];
            continue;
        }

        /* Find the semicolon */
        size_t start = r;
        size_t j = r + 1;
        while (j < len && j - start < 16 && buf[j] != ';' && buf[j] != '&') {
            j++;
        }

        if (j >= len || buf[j] != ';') {
            /* No valid entity — copy as-is */
            buf[w++] = buf[r++];
            continue;
        }

        /* Entity content is buf[r+1 .. j-1] */
        size_t ent_start = r + 1;
        size_t ent_len   = j - ent_start;
        int    matched   = 0;

        if (ent_len == 3 && strncmp(buf + ent_start, "amp", 3) == 0) {
            buf[w++] = '&'; matched = 1;
        } else if (ent_len == 2 && strncmp(buf + ent_start, "lt", 2) == 0) {
            buf[w++] = '<'; matched = 1;
        } else if (ent_len == 2 && strncmp(buf + ent_start, "gt", 2) == 0) {
            buf[w++] = '>'; matched = 1;
        } else if (ent_len == 4 && strncmp(buf + ent_start, "quot", 4) == 0) {
            buf[w++] = '"'; matched = 1;
        } else if (ent_len == 4 && strncmp(buf + ent_start, "apos", 4) == 0) {
            buf[w++] = '\''; matched = 1;
        } else if (ent_len == 4 && strncmp(buf + ent_start, "nbsp", 4) == 0) {
            buf[w++] = ' '; matched = 1;
        } else if (ent_len >= 2 && buf[ent_start] == '#') {
            /* Numeric entity */
            unsigned long cp = 0;
            int valid = 0;
            if (ent_len >= 3 && (buf[ent_start + 1] == 'x' || buf[ent_start + 1] == 'X')) {
                /* Hex */
                size_t k = ent_start + 2;
                valid = (k < j);
                while (k < j && is_hex_digit(buf[k])) {
                    cp = cp * 16 + (unsigned long)hex_val(buf[k]);
                    k++;
                }
                valid = valid && (k == j);
            } else {
                /* Decimal */
                size_t k = ent_start + 1;
                valid = (k < j);
                while (k < j && buf[k] >= '0' && buf[k] <= '9') {
                    cp = cp * 10 + (unsigned long)(buf[k] - '0');
                    k++;
                }
                valid = valid && (k == j);
            }
            if (valid && cp > 0 && cp < 0x110000) {
                char utf8[4];
                int n = encode_utf8(cp, utf8);
                for (int k = 0; k < n; k++) buf[w++] = utf8[k];
                matched = 1;
            }
        }

        if (matched) {
            r = j + 1; /* skip past ';' */
        } else {
            /* Unknown entity — copy as-is */
            buf[w++] = buf[r++];
        }
    }

    buf[w] = '\0';
    return w;
}

/* ---- html_url_decode ----------------------------------------------------- */

static size_t url_decode_pass(char *buf, size_t len)
{
    size_t r = 0, w = 0;
    while (r < len) {
        if (buf[r] == '+') {
            buf[w++] = ' ';
            r++;
        } else if (buf[r] == '%' && r + 2 < len &&
                   is_hex_digit(buf[r + 1]) && is_hex_digit(buf[r + 2])) {
            int val = (hex_val(buf[r + 1]) << 4) | hex_val(buf[r + 2]);
            buf[w++] = (char)(unsigned char)val;
            r += 3;
        } else {
            buf[w++] = buf[r++];
        }
    }
    buf[w] = '\0';
    return w;
}

size_t html_url_decode(char *buf, size_t len)
{
    if (!buf || len == 0) return 0;

    len = url_decode_pass(buf, len);

    /* Second pass: check if double-encoded (e.g. %2520 -> %20 -> space) */
    int needs_second = 0;
    for (size_t i = 0; i + 2 < len; i++) {
        if (buf[i] == '%' && is_hex_digit(buf[i + 1]) && is_hex_digit(buf[i + 2])) {
            needs_second = 1;
            break;
        }
    }
    if (needs_second) {
        len = url_decode_pass(buf, len);
    }

    return len;
}

/* ---- html_strip_tag_by_name ---------------------------------------------- */

size_t html_strip_tag_by_name(char *buf, size_t len, const char *tag_name)
{
    if (!buf || !tag_name || len == 0) return 0;

    size_t tlen = strlen(tag_name);
    size_t w = 0;
    size_t i = 0;

    while (i < len) {
        if (buf[i] != '<') {
            buf[w++] = buf[i++];
            continue;
        }

        /* Check for opening or closing tag matching tag_name */
        size_t tag_start = i;
        int is_close = 0;
        size_t pos = i + 1;

        if (pos < len && buf[pos] == '/') {
            is_close = 1;
            pos++;
        }

        /* Compare tag name case-insensitively */
        if (pos + tlen <= len && ci_starts_with(buf + pos, len - pos, tag_name)) {
            size_t after_name = pos + tlen;
            /* Next char must be '>', ' ', '/' or end */
            if (after_name < len &&
                (buf[after_name] == '>' || buf[after_name] == ' ' ||
                 buf[after_name] == '/' || buf[after_name] == '\t' ||
                 buf[after_name] == '\r' || buf[after_name] == '\n')) {
                /* Skip to closing '>' */
                while (i < len && buf[i] != '>') i++;
                if (i < len) i++; /* skip '>' */
                (void)is_close;
                (void)tag_start;
                continue;
            }
        }

        /* Not a match — copy the '<' and continue */
        (void)is_close;
        (void)tag_start;
        buf[w++] = buf[i++];
    }

    buf[w] = '\0';
    return w;
}

/* ---- html_find_by_class -------------------------------------------------- */

/* Extract the tag name from an opening tag. Writes into out (NUL-terminated).
 * p must point to '<' or the character after '<'. Returns length of name. */
static size_t get_tag_name(const char *p, const char *html_end,
                           char *out, size_t out_max)
{
    /* Skip '<' and optional '/' */
    if (p < html_end && *p == '<') p++;
    if (p < html_end && *p == '/') p++;

    size_t n = 0;
    while (p < html_end && n + 1 < out_max &&
           *p != '>' && *p != ' ' && *p != '/' &&
           *p != '\t' && *p != '\r' && *p != '\n') {
        out[n++] = (char)tolower((unsigned char)*p);
        p++;
    }
    out[n] = '\0';
    return n;
}

const char *html_find_by_class(const char *html, size_t html_len,
                               const char *class_value, size_t *out_len)
{
    if (!html || !class_value || !out_len || html_len == 0) return NULL;

    const char *end = html + html_len;
    const char *p   = html;

    while (p < end) {
        /* Find next tag opening */
        const char *tag_open = p;
        while (tag_open < end && *tag_open != '<') tag_open++;
        if (tag_open >= end) break;

        /* Check that this tag has a class attribute containing class_value */
        /* First, find the end of this opening tag */
        const char *tag_close = tag_open + 1;
        while (tag_close < end && *tag_close != '>') tag_close++;
        if (tag_close >= end) break;

        /* tag text is [tag_open+1 .. tag_close) */
        size_t tag_text_len = (size_t)(tag_close - tag_open - 1);
        const char *tag_text = tag_open + 1;

        /* Skip closing tags */
        if (tag_text_len > 0 && tag_text[0] == '/') {
            p = tag_close + 1;
            continue;
        }

        /* Look for class attribute in this tag */
        /* Search for class=" or class=' */
        const char *class_attr = ci_memmem(tag_text, tag_text_len, "class=", 6);
        int found = 0;
        if (class_attr) {
            const char *cv = class_attr + 6;
            if (cv < tag_close) {
                char quote = *cv;
                if (quote == '"' || quote == '\'') {
                    cv++; /* skip quote */
                    /* Find end of class value */
                    const char *cv_end = cv;
                    while (cv_end < tag_close && *cv_end != quote) cv_end++;
                    /* Check if class_value is a substring */
                    size_t cv_len = (size_t)(cv_end - cv);
                    size_t search_len = strlen(class_value);
                    if (ci_memmem(cv, cv_len, class_value, search_len)) {
                        found = 1;
                    }
                }
            }
        }

        if (!found) {
            p = tag_close + 1;
            continue;
        }

        /* Found the tag. Get its name for matching close tag. */
        char tname[64];
        get_tag_name(tag_open, tag_close, tname, sizeof(tname));

        /* Content starts after '>' */
        const char *content_start = tag_close + 1;

        /* Find matching close tag, tracking nesting depth */
        int depth = 1;
        const char *q = content_start;
        while (q < end && depth > 0) {
            while (q < end && *q != '<') q++;
            if (q >= end) break;

            const char *inner_tag = q;
            const char *inner_end = q + 1;
            while (inner_end < end && *inner_end != '>') inner_end++;
            if (inner_end >= end) break;

            size_t inner_len = (size_t)(inner_end - inner_tag - 1);
            const char *inner_text = inner_tag + 1;

            if (inner_len > 0 && inner_text[0] == '/') {
                /* Closing tag */
                char ctname[64];
                get_tag_name(inner_tag, inner_end, ctname, sizeof(ctname));
                if (strcmp(ctname, tname) == 0) {
                    depth--;
                    if (depth == 0) break;
                }
            } else if (inner_len > 0) {
                /* Opening tag of same name => increase depth */
                char otname[64];
                get_tag_name(inner_tag, inner_end, otname, sizeof(otname));
                if (strcmp(otname, tname) == 0) {
                    /* Check if self-closing */
                    if (inner_end > inner_tag + 1 && *(inner_end - 1) == '/') {
                        /* self-closing, don't increase depth */
                    } else {
                        depth++;
                    }
                }
            }

            q = inner_end + 1;
        }

        /* q now points past the closing tag's '>', or at end */
        /* The content closes at the start of the matching close tag */
        const char *content_end = q;
        /* Walk back to find '<' of the close tag */
        while (content_end > content_start && *content_end != '<') content_end--;

        *out_len = (size_t)(content_end - content_start);
        return content_start;
    }

    return NULL;
}
