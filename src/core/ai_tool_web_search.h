#ifndef NUTSHELL_AI_TOOL_WEB_SEARCH_H
#define NUTSHELL_AI_TOOL_WEB_SEARCH_H

#include "ai_tools.h"

/* Web search context -- passed as tool_data to tool_web_search_execute */
typedef struct {
    char search_provider[64];   /* "duckduckgo-api", "duckduckgo-html", "custom" */
    char search_url[256];       /* custom search endpoint */
    int  max_search_results;    /* max results to return (default 7) */
} WebSearchContext;

/* Tool execute callback */
int tool_web_search_execute(const char *input_json, void *tool_data,
                            volatile int *cancel_flag,
                            char **result_buf, size_t *result_len,
                            int *was_truncated);

/* --- Testable parsing functions (exposed for unit tests) --- */

/* Parse DuckDuckGo Instant Answer API JSON response.
 * Returns number of results extracted, 0 if Type is "" (trigger fallback),
 * -1 on parse error. Results written to out_buf. */
int ddg_parse_api_json(const char *json_response, size_t json_len,
                       int max_results, char *out_buf, size_t out_max);

/* Parse DuckDuckGo HTML lite response.
 * Returns number of results extracted, 0 if no results or CAPTCHA,
 * -1 on parse error. Results written to out_buf. */
int ddg_parse_html(const char *html, size_t html_len,
                   int max_results, char *out_buf, size_t out_max);

#endif /* NUTSHELL_AI_TOOL_WEB_SEARCH_H */
