#ifndef NUTSHELL_AI_DATA_BLOCK_H
#define NUTSHELL_AI_DATA_BLOCK_H

#include <stddef.h>

/* Helpers that frame untrusted text (terminal output, web pages, search
 * results, the debug log) as a delimited data block in an AI request.
 * Portable: no windows.h, no OpenSSL include. */

#define AI_DELIM_PREFIX "----nutshell-data-"   /* 18 bytes */
#define AI_DELIM_MAX    40                     /* prefix + 16 hex + NUL fits */

/* Fills out[0..n) with random bytes; returns 1 on success, 0 on failure.
 * crypto_random_bytes() (src/crypto/crypto.h) has this shape. */
typedef int (*AiRandBytesFn)(void *ctx, unsigned char *out, size_t n);

/* Writes AI_DELIM_PREFIX plus 16 lower-case hex digits of 8 bytes from rb to
 * out. If rb is NULL or returns 0 the 16 digits are a per-process counter
 * (atomic increment), so two calls never return the same text. */
void ai_delim_make(AiRandBytesFn rb, void *ctx, char out[AI_DELIM_MAX]);

/* Prepares data for a block. Normalises "\r\n", a lone '\r', U+2028, U+2029
 * and U+0085 to '\n'; then a line that begins with AI_DELIM_PREFIX gains one
 * leading space, and [EXEC] / [/EXEC] in any case become [EXEC ] / [/EXEC ].
 * Nothing else changes (backticks are kept). Returns a malloc'd
 * NUL-terminated string and sets *out_len (may be NULL) to its length, or
 * NULL on allocation failure. `in` NULL is treated as "". */
char *ai_data_escape_dup(const char *in, size_t len, size_t *out_len);

#endif /* NUTSHELL_AI_DATA_BLOCK_H */
