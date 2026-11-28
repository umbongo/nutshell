#ifndef NUTSHELL_AI_TOOL_WEB_SEARCH_H
#define NUTSHELL_AI_TOOL_WEB_SEARCH_H

#include <stddef.h>
#include "ai_tools.h"
#include "ai_netcancel.h"

/* Web search context -- passed as tool_data to tool_web_search_execute */
typedef struct {
    char search_provider[64];   /* "duckduckgo-api", "duckduckgo-html", "custom" */
    char search_url[256];       /* custom search endpoint */
    int  max_search_results;    /* max results to return (default 7) */
    AiNetCancel *nc;            /* the stream's cancel token, or NULL */
} WebSearchContext;

/* Tool execute callback */
int tool_web_search_execute(const char *input_json, void *tool_data,
                            volatile int *cancel_flag,
                            char **result_buf, size_t *result_len,
                            int *was_truncated);

/* Build the search URL for `provider` with `query` percent-encoded (every
 * byte but A-Z a-z 0-9 - . _ ~ as %XX, upper-case hex; a space is %20).
 * provider "custom" with a non-empty custom_url uses it, joined with '?' or,
 * when it already has a '?', '&'; "duckduckgo-html" and force_html != 0 use
 * the HTML endpoint; anything else the Instant Answer API.
 * Returns 0; -1 when the result does not fit out_max (out is left empty,
 * nothing is truncated); -2 when custom_url has a '#' fragment (refused: it
 * would swallow the query). */
int ws_build_search_url(const char *provider, const char *custom_url,
                        int force_html, const char *query,
                        char *out, size_t out_max);

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
