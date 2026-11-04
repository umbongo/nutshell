#ifndef NUTSHELL_AI_TOOL_WEB_FETCH_H
#define NUTSHELL_AI_TOOL_WEB_FETCH_H

#include "ai_tools.h"
#include "net_guard.h"

#define WEB_FETCH_TOTAL_MS      30000  /* deadline for the whole fetch, redirects included */
#define WEB_FETCH_MAX_REDIRECTS 5      /* redirects followed: up to 6 requests */
#define WEB_FETCH_MAX_ADDRS     32     /* addresses one host may resolve to */

/* What the portable hop loop needs from the platform. The loop checks every
 * address a host resolves to before it calls get() for that host, so a
 * backend only has to do the network work. */
typedef struct WebFetchBackend {
    void *ctx;
    /* Every address `host` resolves to: writes up to `max` into out and returns
     * the total found (may exceed max), or -1 when resolution fails. */
    int (*resolve)(void *ctx, const char *host, NetAddr *out, int max);
    /* One GET that never follows redirects. Returns 0 when a response arrived
     * (any status), -1 on failure or cancel. *location is the Location header
     * or "". *body is malloc'd (caller frees), at most AI_TOOL_RESULT_MAX * 4. */
    int (*get)(void *ctx, const NetUrlParts *url, int timeout_ms,
               volatile int *cancel, int *status,
               char *location, size_t location_cap,
               char **body, size_t *body_len);
    unsigned long long (*now_ms)(void *ctx);
    /* Optional (may be NULL). Called exactly once, with ctx, when one fetch
     * ends, on every exit: success, refusal, failure, timeout and cancel.
     * A backend that keeps a connection session for the fetch (all its
     * redirect hops share one) closes it here. */
    void (*end)(void *ctx);
} WebFetchBackend;

/* Web fetch context -- passed as tool_data */
typedef struct {
    int timeout_ms;                    /* per-hop cap (default 10000) */
    /* Called once per hop before it is fetched, or refused. display_url is
     * the net_url_display form, never the raw URL. refused is 1 only for a
     * host refused as local or private. May be NULL. */
    void (*on_hop)(void *user, const char *display_url, int refused);
    void *user;
    const WebFetchBackend *backend;    /* NULL: web_fetch_default_backend() */
} WebFetchContext;

/* The platform's own backend, or NULL where there is none (the tool then
 * answers "not available"). */
const WebFetchBackend *web_fetch_default_backend(void);

/* The "using tool:" suffix: the requested URL in display form. Returns 1, or
 * 0 (out = "") when the input has no usable url. */
int web_fetch_describe(const char *input_json, char *out, size_t cap);

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
