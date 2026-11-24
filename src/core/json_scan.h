#ifndef NUTSHELL_JSON_SCAN_H
#define NUTSHELL_JSON_SCAN_H

#include <stddef.h>

/* A bounded scanner for one string value in a JSON text, for strings the JSON tokenizer
 * refuses (it caps a string token at 8,191 bytes). Pure: no logging, no I/O. */

/* Find the string value at `path` (member names; "[0]" selects the first array element)
 * starting from the top-level value of json[0..len), e.g. {"choices","[0]","message",
 * "content",NULL}. Other values are skipped without looking inside their strings, so a
 * key text inside a string value never matches; the first member of a given name wins.
 * On success the decoded value (escapes \" \\ \/ \b \f \n \r \t \uXXXX with surrogate pairs;
 * a lone surrogate becomes U+FFFD; \u0000 becomes a space; other bytes pass through) is
 * written NUL-terminated to a heap buffer *out of *out_len bytes and 1 is returned. 0: not
 * found, not a string, malformed, or longer than max_bytes decoded. *out is NULL (and
 * *out_len 0) on 0. The caller secure_zero()s and frees *out. */
int json_scan_path_string(const char *json, size_t len, const char *const *path,
                          size_t max_bytes, char **out, size_t *out_len);

/* The string value of `key` among the members of the object at nesting depth `depth` of
 * the JSON text. Only depth 1 (the top-level object's own members) is supported; any
 * other depth returns 0. Same result rules as json_scan_path_string. */
int json_scan_string(const char *json, size_t len, int depth, const char *key,
                     size_t max_bytes, char **out, size_t *out_len);

#endif
