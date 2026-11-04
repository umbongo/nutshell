#include "ai_tool_web_fetch.h"
#include "html_util.h"
#include "json_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include "winhttp_open.h"
#endif

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
static int fetch_hops(const WebFetchBackend *be, const WebFetchContext *ctx,
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

/* One fetch: the hops, then the backend's end hook exactly once whatever way
 * the hops ended. */
static int fetch_loop(const WebFetchBackend *be, const WebFetchContext *ctx,
                      const char *first_url, volatile int *cancel,
                      char **result_buf, size_t *result_len, int *was_truncated)
{
    int rc = fetch_hops(be, ctx, first_url, cancel,
                        result_buf, result_len, was_truncated);
    if (be->end) be->end(be->ctx);
    return rc;
}

/* ---- backend selection and description ------------------------------------ */

#ifdef _WIN32

/* The WinHTTP backend: thin on purpose. It does the network work for one hop
 * and nothing else; the address check, redirect policy, deadline and cancel
 * decisions are all in fetch_loop() above. It never parses a URL string, it
 * uses the parts fetch_loop() has already checked. */

/* Older MinGW headers may lack this; the value is the documented one. */
#ifndef WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT
#define WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT 7
#endif

/* Per-fetch state: one WinHTTP session (a proxy-detecting one costs a WPAD
 * lookup to open) shared by every hop of one fetch, opened by the first hop
 * and closed by wf_end(). It lives on the stack of one tool_web_fetch_execute
 * call, so two fetches running at once never share it. With a NULL ctx
 * (web_fetch_default_backend() used directly) each hop opens and closes its
 * own session instead. */
typedef struct {
    HINTERNET session;
} WfFetch;

#define WIDEN2(s) L##s
#define WIDEN(s)  WIDEN2(s)

static int wf_to_wide(const char *s, wchar_t *out, int cap)
{
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out, cap) > 0
               ? 0 : -1;
}

/* Name resolution needs WSAStartup, which src/main.c runs once for the
 * process before the UI starts; the backend relies on that. */
static int wf_resolve(void *ctx, const char *host, NetAddr *out, int max)
{
    (void)ctx;
    ADDRINFOW hints;
    ADDRINFOW *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    /* The host is decoded from UTF-8 to UTF-16 exactly as wf_get() decodes it
     * for WinHttpConnect (same function, flags and buffer size), and the wide
     * string is what GetAddrInfoW resolves, so the checked name and the name
     * WinHTTP connects to are the same string. Both APIs then apply the
     * system's own IDN handling to it; a narrow getaddrinfo would read the
     * bytes through the ANSI code page instead and could check a different
     * name. Invalid UTF-8 is refused. */
    wchar_t w_host[NET_HOST_MAX];
    if (wf_to_wide(host, w_host, NET_HOST_MAX) != 0)
        return -1;
    if (GetAddrInfoW(w_host, NULL, &hints, &res) != 0 || !res)
        return -1;

    int total = 0;
    for (const ADDRINFOW *ai = res; ai; ai = ai->ai_next) {
        NetAddr a;
        memset(&a, 0, sizeof(a));
        if (ai->ai_family == AF_INET && ai->ai_addr) {
            const struct sockaddr_in *sa = (const struct sockaddr_in *)ai->ai_addr;
            a.v6 = 0;
            memcpy(a.b, &sa->sin_addr, 4);
        } else if (ai->ai_family == AF_INET6 && ai->ai_addr) {
            const struct sockaddr_in6 *sa = (const struct sockaddr_in6 *)ai->ai_addr;
            a.v6 = 1;
            memcpy(a.b, &sa->sin6_addr, 16);
        } else {
            continue;
        }
        if (out && total < max) out[total] = a;
        total++;
    }
    FreeAddrInfoW(res);
    return total > 0 ? total : -1;
}

static unsigned long long wf_now_ms(void *ctx)
{
    (void)ctx;
    return (unsigned long long)GetTickCount64();
}

static int wf_stopped(volatile int *cancel)
{
    return cancel && *cancel;
}

static void wf_end(void *ctx)
{
    WfFetch *fetch = (WfFetch *)ctx;
    if (fetch && fetch->session) {
        WinHttpCloseHandle(fetch->session);
        fetch->session = NULL;
    }
}

static int wf_get(void *ctx, const NetUrlParts *url, int timeout_ms,
                  volatile int *cancel, int *status,
                  char *location, size_t location_cap,
                  char **body, size_t *body_len)
{
    WfFetch *fetch = (WfFetch *)ctx;
    if (!url || !status || !location || location_cap == 0 || !body || !body_len)
        return -1;
    *status = 0;
    location[0] = '\0';
    *body = NULL;
    *body_len = 0;

    int t = timeout_ms >= 1 ? timeout_ms : 1;
    unsigned long long hop_end = (unsigned long long)GetTickCount64() + (unsigned long long)t;

    wchar_t w_host[NET_HOST_MAX];
    wchar_t w_path[NET_URL_MAX];
    if (wf_to_wide(url->host, w_host, NET_HOST_MAX) != 0 ||
        wf_to_wide(url->path, w_path, NET_URL_MAX) != 0)
        return -1;

    HINTERNET session = NULL, connect = NULL, req = NULL;
    int own_session = 0;   /* opened here, so closed here */
    char *buf = NULL;
    size_t len = 0, cap = 0;
    const size_t max_body = (size_t)AI_TOOL_RESULT_MAX * 4;

    if (fetch && fetch->session) {
        session = fetch->session;
    } else {
        session = ns_winhttp_open(WIDEN(AI_TOOL_USER_AGENT));
        if (!session) goto fail;
        if (fetch) fetch->session = session;   /* kept for the next hop */
        else own_session = 1;
    }

    /* url->host has no brackets, also for an IPv6 literal: WinHttpConnect
     * takes a bare literal as the server name. */
    connect = WinHttpConnect(session, w_host, (INTERNET_PORT)url->port, 0);
    if (!connect) goto fail;

    req = WinHttpOpenRequest(connect, L"GET", w_path, NULL, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                             url->https ? WINHTTP_FLAG_SECURE : 0);
    if (!req) goto fail;

    DWORD no_redirects = WINHTTP_DISABLE_REDIRECTS;
    if (!WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE,
                          &no_redirects, sizeof(no_redirects)))
        goto fail;
    if (!WinHttpSetTimeouts(req, t, t, t, t)) goto fail;
    /* WinHttpSetTimeouts leaves the wait for the response headers at the
     * system default (90 s): bound it by the hop's time too, or a server that
     * accepts the request and then trickles holds WinHttpReceiveResponse. */
    DWORD resp_ms = (DWORD)t;
    if (!WinHttpSetOption(req, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT,
                          &resp_ms, sizeof(resp_ms)))
        goto fail;

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        goto fail;
    if (wf_stopped(cancel)) goto fail;
    if (!WinHttpReceiveResponse(req, NULL)) goto fail;
    if (wf_stopped(cancel)) goto fail;

    DWORD code = 0, code_sz = sizeof(code);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &code, &code_sz,
                             WINHTTP_NO_HEADER_INDEX))
        goto fail;

    wchar_t w_loc[NET_URL_MAX];
    DWORD loc_sz = (DWORD)sizeof(w_loc);
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                            w_loc, &loc_sz, WINHTTP_NO_HEADER_INDEX)) {
        /* A Location that does not fit or is not valid UTF-8 is reported as
         * "", never cut: the loop refuses a redirect with no usable
         * Location, and a non-redirect page keeps its body. */
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w_loc, -1,
                                location, (int)(location_cap > 0x7FFFFFFF ? 0x7FFFFFFF : location_cap),
                                NULL, NULL) <= 0)
            location[0] = '\0';
    } else {
        /* Absent, too long for the buffer (ERROR_INSUFFICIENT_BUFFER) or
         * unreadable: all mean no usable Location. */
        location[0] = '\0';
    }

    /* Body, capped; cancel between reads, and the hop's own time limit so a
     * server that trickles bytes cannot outlast it. */
    for (;;) {
        if (wf_stopped(cancel)) goto fail;
        unsigned long long now = (unsigned long long)GetTickCount64();
        if (now >= hop_end) goto fail;

        /* A read can block for a full timeout, so shrink the timeouts to the
         * time the hop has left before each one. */
        unsigned long long left = hop_end - now;
        int r = left > 0x7FFFFFFFULL ? 0x7FFFFFFF : (int)left;
        if (r < 1) r = 1;
        if (!WinHttpSetTimeouts(req, r, r, r, r)) goto fail;

        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) goto fail;
        if (avail == 0) break;

        size_t room = max_body - len;
        if ((size_t)avail > room) avail = (DWORD)room;
        if (avail == 0) break;

        if (len + (size_t)avail + 1 > cap) {
            size_t ncap = (len + (size_t)avail + 1) * 2;
            if (ncap < 65536) ncap = 65536;
            char *tmp = realloc(buf, ncap);
            if (!tmp) goto fail;
            buf = tmp;
            cap = ncap;
        }

        DWORD got = 0;
        if (!WinHttpReadData(req, buf + len, avail, &got)) goto fail;
        if (got == 0) break;
        len += got;
        if (len >= max_body) break;
    }
    if (wf_stopped(cancel)) goto fail;

    if (!buf) {
        buf = malloc(1);
        if (!buf) goto fail;
    }
    buf[len] = '\0';

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(connect);
    if (own_session) WinHttpCloseHandle(session);
    *status   = (int)code;
    *body     = buf;
    *body_len = len;
    return 0;

fail:
    free(buf);
    if (req)     WinHttpCloseHandle(req);
    if (connect) WinHttpCloseHandle(connect);
    if (own_session) WinHttpCloseHandle(session);   /* a shared one stays for wf_end() */
    *status = 0;
    return -1;
}

/* Stateless: every hop opens and closes its own session. */
static const WebFetchBackend g_wf_backend = { NULL, wf_resolve, wf_get, wf_now_ms, NULL };

const WebFetchBackend *web_fetch_default_backend(void)
{
    return &g_wf_backend;
}

/* The default backend for one fetch: `own` is filled in, with `fetch` as its
 * ctx, so the hops share one session that wf_end() closes. */
static const WebFetchBackend *default_backend_for_fetch(WebFetchBackend *own,
                                                        WfFetch *fetch)
{
    fetch->session = NULL;
    own->ctx     = fetch;
    own->resolve = wf_resolve;
    own->get     = wf_get;
    own->now_ms  = wf_now_ms;
    own->end     = wf_end;
    return own;
}

#else /* !_WIN32 */

typedef int WfFetch;   /* no per-fetch state without WinHTTP */

const WebFetchBackend *web_fetch_default_backend(void)
{
    return NULL;
}

static const WebFetchBackend *default_backend_for_fetch(WebFetchBackend *own,
                                                        WfFetch *fetch)
{
    (void)own;
    (void)fetch;
    return NULL;
}

#endif

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
    WebFetchBackend own;
    WfFetch         fetch;
    memset(&own, 0, sizeof(own));
    const WebFetchBackend *be = (ctx && ctx->backend)
                                    ? ctx->backend
                                    : default_backend_for_fetch(&own, &fetch);
    if (!be || !be->resolve || !be->get || !be->now_ms)
        return fetch_fail(result_buf, result_len,
                          "Web fetch not available (non-Windows build)");

    return fetch_loop(be, ctx, url_copy, cancel_flag,
                      result_buf, result_len, was_truncated);
}
