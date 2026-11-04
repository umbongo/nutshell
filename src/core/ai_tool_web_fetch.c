#include "ai_tool_web_fetch.h"
#include "html_util.h"
#include "json_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- web_fetch_html_to_text ---------------------------------------------- */

int web_fetch_html_to_text(const char *html, size_t html_len,
                           char **result_buf, size_t *result_len,
                           int *was_truncated)
{
    if (!result_buf || !result_len || !was_truncated)
        return -1;

    *result_buf    = NULL;
    *result_len    = 0;
    *was_truncated = 0;

    if (!html || html_len == 0) {
        /* Return an empty string */
        char *out = malloc(1);
        if (!out) return -1;
        out[0] = '\0';
        *result_buf = out;
        *result_len = 0;
        return 0;
    }

    /* Allocate working buffer — stripping can only make it shorter */
    char *work = malloc(html_len + 1);
    if (!work) return -1;

    /* Step 1: strip HTML tags (removes <script>/<style> blocks entirely) */
    size_t stripped_len = html_strip_tags(html, html_len, work, html_len + 1);
    work[stripped_len] = '\0';

    /* Step 2: decode HTML entities in-place */
    size_t decoded_len = html_decode_entities(work, stripped_len);
    work[decoded_len] = '\0';

    /* Step 3: normalize whitespace in-place.
     * Rules:
     *   - collapse multiple spaces/tabs on the same logical line to one space
     *   - preserve single newlines
     *   - collapse 3+ consecutive newlines to 2 (paragraph break)
     *   - trim leading/trailing whitespace
     */
    {
        const char *src  = work;
        char       *dst  = work;  /* write back in-place (dst <= src always) */
        size_t      len  = decoded_len;

        /* Trim leading whitespace */
        size_t start = 0;
        while (start < len && (src[start] == ' ' || src[start] == '\t' ||
                               src[start] == '\r' || src[start] == '\n'))
            start++;

        /* Process the rest */
        size_t out_len = 0;
        int    nl_run  = 0;   /* consecutive newline count */
        int    sp_run  = 0;   /* are we in a run of spaces/tabs? */

        for (size_t i = start; i < len; i++) {
            char c = src[i];

            if (c == '\r') {
                /* Skip bare \r — treat \r\n as \n handled on next iteration */
                continue;
            }

            if (c == '\n') {
                sp_run = 0;
                nl_run++;
                if (nl_run <= 2) {
                    dst[out_len++] = '\n';
                }
                /* nl_run > 2: skip (collapse 3+ newlines to 2) */
                continue;
            }

            if (c == ' ' || c == '\t') {
                nl_run = 0;
                if (!sp_run) {
                    dst[out_len++] = ' ';
                    sp_run = 1;
                }
                /* else: already emitted a space — skip duplicate */
                continue;
            }

            /* Regular character */
            nl_run = 0;
            sp_run = 0;
            dst[out_len++] = c;
        }

        /* Trim trailing whitespace */
        while (out_len > 0 && (dst[out_len - 1] == ' ' || dst[out_len - 1] == '\t' ||
                               dst[out_len - 1] == '\n' || dst[out_len - 1] == '\r'))
            out_len--;

        dst[out_len] = '\0';
        decoded_len = out_len;
    }

    /* Step 4: truncate to AI_TOOL_RESULT_MAX if needed */
    size_t final_len = decoded_len;
    if (final_len > AI_TOOL_RESULT_MAX) {
        final_len      = AI_TOOL_RESULT_MAX;
        *was_truncated = 1;
    }

    /* Step 5: allocate exact-size result buffer */
    char *out = malloc(final_len + 1);
    if (!out) {
        free(work);
        return -1;
    }
    memcpy(out, work, final_len);
    out[final_len] = '\0';

    free(work);

    *result_buf = out;
    *result_len = final_len;
    return 0;
}

/* ---- helpers --------------------------------------------------------------- */

/* Default per-hop cap when the context sets none. */
#define WEB_FETCH_DEFAULT_HOP_MS 10000

static int fetch_fail(char **result_buf, size_t *result_len, const char *msg)
{
    size_t n = strlen(msg);
    char  *b = malloc(n + 1);
    if (!b) return -1;
    memcpy(b, msg, n + 1);
    *result_buf = b;
    *result_len = n;
    return -1;
}

/* A host as printable ASCII for an error message: the tool result reaches the
 * model and may be shown, so any other byte becomes '?'. */
static void safe_host(const char *host, char *out, size_t cap)
{
    size_t i = 0;
    for (; host[i] != '\0' && i + 1 < cap; i++) {
        unsigned char c = (unsigned char)host[i];
        out[i] = (c >= 0x21 && c < 0x7F) ? (char)c : '?';
    }
    out[i] = '\0';
}

static int is_redirect_status(int status)
{
    return status == 301 || status == 302 || status == 303 ||
           status == 307 || status == 308;
}

enum { STOP_NONE = 0, STOP_CANCELLED, STOP_TIMEOUT };

/* One clock read per decision: *now_out (when given) is the time the verdict
 * was reached at, so a caller computing a timeout from it cannot see a later
 * clock than the one it was cleared on. */
static int stop_reason(const WebFetchBackend *be, volatile int *cancel,
                       unsigned long long deadline, unsigned long long *now_out)
{
    unsigned long long now = be->now_ms(be->ctx);
    if (now_out) *now_out = now;
    if (cancel && *cancel) return STOP_CANCELLED;
    if (now >= deadline) return STOP_TIMEOUT;
    return STOP_NONE;
}

static int fail_stopped(int reason, char **result_buf, size_t *result_len)
{
    return fetch_fail(result_buf, result_len,
                      reason == STOP_CANCELLED ? "Fetch cancelled"
                                               : "Fetch timed out after 30 s");
}

/* ---- the hop loop --------------------------------------------------------- */

/* Every request is preceded, in this function, by the address check for the
 * host it goes to: the order below is the guard, so nothing here may fetch
 * first and check afterwards. Every failure refuses; none falls through to a
 * fetch. On cancel or deadline no fetched text is returned. */
static int fetch_loop(const WebFetchBackend *be, const WebFetchContext *ctx,
                      const char *first_url, volatile int *cancel,
                      char **result_buf, size_t *result_len, int *was_truncated)
{
    char        url[NET_URL_MAX];
    char        next[NET_URL_MAX];
    char        location[NET_URL_MAX];
    char        display[NET_URL_DISPLAY_MAX + 8];
    char        msg[NET_HOST_MAX + 96];
    char        host[NET_HOST_MAX];
    NetUrlParts parts;
    NetUrlParts next_parts;
    NetAddr     addrs[WEB_FETCH_MAX_ADDRS];

    int per_hop = (ctx && ctx->timeout_ms > 0) ? ctx->timeout_ms
                                               : WEB_FETCH_DEFAULT_HOP_MS;
    unsigned long long deadline = be->now_ms(be->ctx) + WEB_FETCH_TOTAL_MS;

    snprintf(url, sizeof(url), "%s", first_url);

    for (int hop = 0; hop <= WEB_FETCH_MAX_REDIRECTS; hop++) {
        int stop = stop_reason(be, cancel, deadline, NULL);
        if (stop != STOP_NONE) return fail_stopped(stop, result_buf, result_len);

        if (net_url_parse(url, &parts) != 0)
            return fetch_fail(result_buf, result_len,
                              "Refused: not a valid http or https address");
        net_url_display(url, display, sizeof(display));
        safe_host(parts.host, host, sizeof(host));

        /* Residual risk, accepted: the backend resolves the name again when it
         * connects, so a DNS answer that changes between this lookup and that
         * one (rebinding) is not caught here. A configured proxy also resolves
         * on its own. Pinning the checked address would break the TLS name
         * check, so the check is on the name's addresses, before each hop. */
        int n = be->resolve(be->ctx, parts.host, addrs, WEB_FETCH_MAX_ADDRS);
        if (n <= 0) {
            snprintf(msg, sizeof(msg), "Refused: could not resolve %s", host);
            return fetch_fail(result_buf, result_len, msg);
        }
        if (n > WEB_FETCH_MAX_ADDRS) {
            snprintf(msg, sizeof(msg),
                     "Refused: %s resolved to too many addresses", host);
            return fetch_fail(result_buf, result_len, msg);
        }
        for (int i = 0; i < n; i++) {
            if (net_addr_is_internal(&addrs[i])) {
                if (ctx && ctx->on_hop) ctx->on_hop(ctx->user, display, 1);
                snprintf(msg, sizeof(msg),
                         "Refused: %s is a local or private network address", host);
                return fetch_fail(result_buf, result_len, msg);
            }
        }

        /* The lookup counts against the deadline and may have been cancelled. */
        stop = stop_reason(be, cancel, deadline, NULL);
        if (stop != STOP_NONE) return fail_stopped(stop, result_buf, result_len);

        if (ctx && ctx->on_hop) ctx->on_hop(ctx->user, display, 0);

        /* The row callback can take time too. Decide once, on this reading,
         * whether the hop may start and how long it may run: time left is
         * at least 1 ms here, and a timeout of 0 would mean "forever". */
        unsigned long long now;
        stop = stop_reason(be, cancel, deadline, &now);
        if (stop != STOP_NONE) return fail_stopped(stop, result_buf, result_len);
        unsigned long long left = deadline - now;
        int timeout = per_hop;
        if (left < (unsigned long long)timeout) timeout = (int)left;
        if (timeout < 1) timeout = 1;

        int    status = 0;
        char  *body = NULL;
        size_t body_len = 0;
        location[0] = '\0';
        int got = be->get(be->ctx, &parts, timeout, cancel, &status,
                          location, sizeof(location), &body, &body_len);

        stop = stop_reason(be, cancel, deadline, NULL);
        if (stop != STOP_NONE) {
            free(body);
            return fail_stopped(stop, result_buf, result_len);
        }
        if (got != 0) {
            free(body);
            return fetch_fail(result_buf, result_len, "HTTP request failed");
        }

        if (is_redirect_status(status)) {
            free(body);
            if (hop == WEB_FETCH_MAX_REDIRECTS)
                return fetch_fail(result_buf, result_len,
                                  "Refused: too many redirects (more than 5)");
            if (net_url_resolve(url, location, next, sizeof(next)) != 0 ||
                net_url_parse(next, &next_parts) != 0)
                return fetch_fail(result_buf, result_len,
                                  "Refused: redirect to an unsupported or invalid location");
            if (parts.https && !next_parts.https)
                return fetch_fail(result_buf, result_len,
                                  "Refused: redirect from https to http");
            snprintf(url, sizeof(url), "%s", next);
            continue;
        }

        int rc = web_fetch_html_to_text(body, body_len, result_buf, result_len,
                                        was_truncated);
        free(body);
        return rc;
    }

    /* Unreachable: the last hop either returns or refuses a redirect. */
    return fetch_fail(result_buf, result_len, "HTTP request failed");
}

/* ---- backend selection and description ------------------------------------ */

const WebFetchBackend *web_fetch_default_backend(void)
{
    return NULL;
}

int web_fetch_describe(const char *input_json, char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!input_json) return 0;

    JsonNode *input = json_parse(input_json);
    if (!input) return 0;
    const char *url = json_obj_str(input, "url");
    int ok = 0;
    if (url && url[0] != '\0') {
        net_url_display(url, out, cap);
        ok = out[0] != '\0';
    }
    json_free(input);
    return ok;
}

/* ---- tool_web_fetch_execute ---------------------------------------------- */

int tool_web_fetch_execute(const char *input_json, void *tool_data,
                           volatile int *cancel_flag,
                           char **result_buf, size_t *result_len,
                           int *was_truncated)
{
    if (!input_json || !result_buf || !result_len || !was_truncated)
        return -1;

    *result_buf    = NULL;
    *result_len    = 0;
    *was_truncated = 0;

    /* Parse URL from input JSON */
    JsonNode *input = json_parse(input_json);
    if (!input) {
        const char *err = "Invalid tool input JSON";
        *result_buf = malloc(strlen(err) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, err);
        *result_len = strlen(err);
        return -1;
    }

    const char *url = json_obj_str(input, "url");
    if (!url || url[0] == '\0') {
        json_free(input);
        const char *err = "Missing required parameter: url";
        *result_buf = malloc(strlen(err) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, err);
        *result_len = strlen(err);
        return -1;
    }

    /* Validate URL scheme */
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        const char *err = "Invalid URL scheme: only http:// and https:// are supported";
        char *buf = malloc(strlen(err) + 1);
        json_free(input);
        if (!buf) return -1;
        strcpy(buf, err);
        *result_buf = buf;
        *result_len = strlen(err);
        return -1;
    }

    /* Copy URL before freeing the JSON tree. A URL that does not fit is
     * refused, never cut: a truncated address is a different address. */
    char url_copy[NET_URL_MAX];
    size_t ulen = strlen(url);
    if (ulen >= sizeof(url_copy)) {
        json_free(input);
        return fetch_fail(result_buf, result_len,
                          "Refused: URL is too long (limit is 2047 bytes)");
    }
    memcpy(url_copy, url, ulen + 1);

    json_free(input);

    const WebFetchContext *ctx = (const WebFetchContext *)tool_data;
    const WebFetchBackend *be  = (ctx && ctx->backend) ? ctx->backend
                                                       : web_fetch_default_backend();
    if (!be || !be->resolve || !be->get || !be->now_ms)
        return fetch_fail(result_buf, result_len,
                          "Web fetch not available (non-Windows build)");

    return fetch_loop(be, ctx, url_copy, cancel_flag,
                      result_buf, result_len, was_truncated);
}
