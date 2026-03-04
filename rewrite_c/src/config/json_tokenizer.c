#include "json_tokenizer.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

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

/* Scan a quoted string into tok.value (without surrounding quotes).
 * Handles: \" \\ \/ \n \r \t — other \uXXXX sequences are copied verbatim. */
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
                case 'u':
                    /* copy \uXXXX verbatim (4 hex digits) */
                    if (buf_append(tok.value, &out, '\\') < 0 ||
                        buf_append(tok.value, &out, 'u')  < 0) {
                        goto overflow;
                    }
                    for (int i = 0; i < 4 && t->pos < t->len; i++) {
                        if (buf_append(tok.value, &out,
                                (unsigned char)t->src[t->pos++]) < 0) {
                            goto overflow;
                        }
                    }
                    continue;
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
