#ifndef NUTSHELL_CONFIG_JSON_TOKENIZER_H
#define NUTSHELL_CONFIG_JSON_TOKENIZER_H

#include <stddef.h>

#define TOK_VALUE_MAX 8192

typedef enum {
    TOK_EOF,
    TOK_ERROR,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COLON,
    TOK_COMMA,
    TOK_STRING,
    TOK_NUMBER,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NULL
} TokenType;

typedef struct {
    TokenType type;
    char value[TOK_VALUE_MAX];
} Token;

typedef struct {
    const char *src;
    size_t pos;
    size_t len;
} Tokenizer;

void tok_init(Tokenizer *t, const char *src);
Token tok_next(Tokenizer *t);

#endif