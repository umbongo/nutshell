#ifndef JSON_TOKENIZER_H
#define JSON_TOKENIZER_H

/*
 * JSON tokenizer — converts a JSON string into a stream of tokens.
 *
 * Usage:
 *   Tokenizer t;
 *   tok_init(&t, src);
 *   Token tok = tok_next(&t);
 *   while (tok.type != TOK_EOF && tok.type != TOK_ERROR) { ... }
 */

#include <stddef.h>

/* Maximum length of a token's string value (including null terminator). */
#define TOK_VALUE_MAX ((size_t)1024)

typedef enum {
    TOK_LBRACE,     /* {  */
    TOK_RBRACE,     /* }  */
    TOK_LBRACKET,   /* [  */
    TOK_RBRACKET,   /* ]  */
    TOK_COLON,      /* :  */
    TOK_COMMA,      /* ,  */
    TOK_STRING,     /* "..." — value holds unescaped content */
    TOK_NUMBER,     /* digits — value holds raw text           */
    TOK_TRUE,       /* true  */
    TOK_FALSE,      /* false */
    TOK_NULL,       /* null  */
    TOK_EOF,        /* end of input */
    TOK_ERROR       /* malformed input */
} TokenType;

typedef struct {
    TokenType type;
    char      value[TOK_VALUE_MAX]; /* populated for TOK_STRING / TOK_NUMBER */
} Token;

typedef struct {
    const char *src;
    size_t      pos;
    size_t      len;
} Tokenizer;

/* Initialise a tokenizer over a null-terminated JSON string. */
void  tok_init(Tokenizer *t, const char *src);

/* Return and consume the next token. */
Token tok_next(Tokenizer *t);

#endif /* JSON_TOKENIZER_H */
