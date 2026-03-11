#include "json_tokenizer.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>

void tok_init(Tokenizer *t, const char *src)
{
    t->src = src ? src : "";
    t->pos = 0;
    t->len = strlen(t->src);
}

/* Skip ASCII whitespace. */
static void skip_ws(Tokenizer *t)
{
    while (t->pos < t->len && isspace((unsigned char)t->src[t->pos])) {
        t->pos++;
    }
}

/* Append character c to buf[*out], advancing *out.
 * Returns 0 on success, -1 if the buffer is full. */
static int buf_append(char *buf, size_t *out, unsigned char c)
{
    if (*out >= TOK_VALUE_MAX - 1u) {
        return -1;
    }
    buf[(*out)++] = (char)c;
    return 0;
}

/* Parse a single hex digit, returning 0-15 or -1 on error. */
static int hex_digit(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

/* Parse 4 hex digits from t->src[t->pos..] into *cp. Returns 0 on success. */
static int parse_hex4(Tokenizer *t, uint32_t *cp)
{
    if (t->pos + 4 > t->len) return -1;
    uint32_t val = 0;
    for (size_t i = 0; i < 4; i++) {
        int d = hex_digit((unsigned char)t->src[t->pos + i]);
        if (d < 0) return -1;
        val = (val << 4) | (uint32_t)d;
    }
    t->pos += 4;
    *cp = val;
    return 0;
}

/* Encode a Unicode codepoint as UTF-8 into buf at *out. Returns 0 or -1. */
static int encode_utf8(char *buf, size_t *out, uint32_t cp)
{
    if (cp <= 0x7F) {
        return buf_append(buf, out, (unsigned char)cp);
    } else if (cp <= 0x7FF) {
        if (buf_append(buf, out, (unsigned char)(0xC0 | (cp >> 6))) < 0) return -1;
        return buf_append(buf, out, (unsigned char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        if (buf_append(buf, out, (unsigned char)(0xE0 | (cp >> 12))) < 0) return -1;
        if (buf_append(buf, out, (unsigned char)(0x80 | ((cp >> 6) & 0x3F))) < 0) return -1;
        return buf_append(buf, out, (unsigned char)(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        if (buf_append(buf, out, (unsigned char)(0xF0 | (cp >> 18))) < 0) return -1;
        if (buf_append(buf, out, (unsigned char)(0x80 | ((cp >> 12) & 0x3F))) < 0) return -1;
        if (buf_append(buf, out, (unsigned char)(0x80 | ((cp >> 6) & 0x3F))) < 0) return -1;
        return buf_append(buf, out, (unsigned char)(0x80 | (cp & 0x3F)));
    }
    return -1;
}

/* Scan a quoted string into tok.value (without surrounding quotes).
 * Handles: \" \\ \/ \n \r \t \b \f and \uXXXX (with surrogate pairs). */
static Token scan_string(Tokenizer *t)
{
    Token tok;
    memset(&tok, 0, sizeof(tok));
    tok.type = TOK_STRING;

    t->pos++;   /* consume opening '"' */

    size_t out = 0;
    while (t->pos < t->len) {
        unsigned char c = (unsigned char)t->src[t->pos++];

        if (c == '"') {
            /* end of string */
            tok.value[out] = '\0';
            return tok;
        }

        if (c == '\\') {
            if (t->pos >= t->len) {
                break;
            }
            unsigned char esc = (unsigned char)t->src[t->pos++];
            unsigned char out_c;
            switch (esc) {
                case '"':  out_c = '"';  break;
                case '\\': out_c = '\\'; break;
                case '/':  out_c = '/';  break;
                case 'n':  out_c = '\n'; break;
                case 'r':  out_c = '\r'; break;
                case 't':  out_c = '\t'; break;
                case 'b':  out_c = '\b'; break;
                case 'f':  out_c = '\f'; break;
                case 'u': {
                    /* decode \uXXXX (and surrogate pairs) to UTF-8 */
                    uint32_t cp;
                    if (parse_hex4(t, &cp) < 0) goto overflow;
                    /* handle UTF-16 surrogate pairs */
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        /* high surrogate — expect \uDCxx low surrogate */
                        if (t->pos + 6 <= t->len &&
                            t->src[t->pos] == '\\' &&
                            t->src[t->pos + 1] == 'u') {
                            t->pos += 2; /* skip \u */
                            uint32_t lo;
                            if (parse_hex4(t, &lo) < 0) goto overflow;
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 +
                                     ((cp - 0xD800) << 10) +
                                     (lo - 0xDC00);
                            } else {
                                goto overflow;
                            }
                        } else {
                            goto overflow;
                        }
                    }
                    if (encode_utf8(tok.value, &out, cp) < 0)
                        goto overflow;
                    continue;
                }
                default:
                    /* unknown escape — copy both chars */
                    if (buf_append(tok.value, &out, '\\') < 0 ||
                        buf_append(tok.value, &out, esc)  < 0) {
                        goto overflow;
                    }
                    continue;
            }
            if (buf_append(tok.value, &out, out_c) < 0) {
                goto overflow;
            }
        } else {
            if (buf_append(tok.value, &out, c) < 0) {
                goto overflow;
            }
        }
    }

overflow:
    tok.type = TOK_ERROR;
    return tok;
}

/* Scan a number (integer or floating-point, optionally negative). */
static Token scan_number(Tokenizer *t)
{
    Token tok;
    memset(&tok, 0, sizeof(tok));
    tok.type = TOK_NUMBER;

    size_t out = 0;

    if (t->pos < t->len && t->src[t->pos] == '-') {
        if (buf_append(tok.value, &out, '-') < 0) goto overflow;
        t->pos++;
    }

    while (t->pos < t->len) {
        unsigned char c = (unsigned char)t->src[t->pos];
        if (c >= '0' && c <= '9') {
            if (buf_append(tok.value, &out, c) < 0) goto overflow;
            t->pos++;
        } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
            if (buf_append(tok.value, &out, c) < 0) goto overflow;
            t->pos++;
        } else {
            break;
        }
    }

    tok.value[out] = '\0';
    if (out == 0) {
        tok.type = TOK_ERROR;
    }
    return tok;

overflow:
    tok.type = TOK_ERROR;
    return tok;
}

/* Expect and consume the literal string lit (excluding its first char
 * which has already been consumed). Sets tok.type to TOK_ERROR on mismatch. */
static void expect_literal(Tokenizer *t, Token *tok, const char *lit)
{
    for (; *lit; lit++) {
        if (t->pos >= t->len || t->src[t->pos] != *lit) {
            tok->type = TOK_ERROR;
            return;
        }
        t->pos++;
    }
}

Token tok_next(Tokenizer *t)
{
    Token tok;
    memset(&tok, 0, sizeof(tok));

    skip_ws(t);

    if (t->pos >= t->len) {
        tok.type = TOK_EOF;
        return tok;
    }

    unsigned char c = (unsigned char)t->src[t->pos];

    switch (c) {
        case '{':  tok.type = TOK_LBRACE;   t->pos++; return tok;
        case '}':  tok.type = TOK_RBRACE;   t->pos++; return tok;
        case '[':  tok.type = TOK_LBRACKET; t->pos++; return tok;
        case ']':  tok.type = TOK_RBRACKET; t->pos++; return tok;
        case ':':  tok.type = TOK_COLON;    t->pos++; return tok;
        case ',':  tok.type = TOK_COMMA;    t->pos++; return tok;
        case '"':  return scan_string(t);
        case 't':
            tok.type = TOK_TRUE;
            t->pos++;
            expect_literal(t, &tok, "rue");
            return tok;
        case 'f':
            tok.type = TOK_FALSE;
            t->pos++;
            expect_literal(t, &tok, "alse");
            return tok;
        case 'n':
            tok.type = TOK_NULL;
            t->pos++;
            expect_literal(t, &tok, "ull");
            return tok;
        default:
            if (c == '-' || (c >= '0' && c <= '9')) {
                return scan_number(t);
            }
            tok.type = TOK_ERROR;
            t->pos++;
            return tok;
    }
}
