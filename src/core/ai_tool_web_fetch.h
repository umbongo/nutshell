#ifndef NUTSHELL_AI_TOOL_WEB_FETCH_H
#define NUTSHELL_AI_TOOL_WEB_FETCH_H

#include "ai_tools.h"

/* Web fetch context -- passed as tool_data */
typedef struct {
    int timeout_ms;  /* request timeout (default 10000) */
} WebFetchContext;

/* Tool execute callback */
int tool_web_fetch_execute(const char *input_json, void *tool_data,
                           volatile int *cancel_flag,
                           char **result_buf, size_t *result_len,
                           int *was_truncated);

/* Testable HTML-to-text pipeline (exposed for unit tests):
 * Takes raw HTML, strips tags, decodes entities, normalizes whitespace.
 * Allocates result buffer with malloc. Caller frees.
 * Truncates at AI_TOOL_RESULT_MAX with was_truncated flag.
 * Returns 0 on success, -1 on error. */
int web_fetch_html_to_text(const char *html, size_t html_len,
                           char **result_buf, size_t *result_len,
                           int *was_truncated);

#endif /* NUTSHELL_AI_TOOL_WEB_FETCH_H */
