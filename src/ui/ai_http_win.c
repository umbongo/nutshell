#include "ai_http.h"
#include "winhttp_open.h"
#include "secure_zero.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

/* Parse a URL to extract host, path, port, and whether it's HTTPS. */
static int parse_url(const char *url, wchar_t *host, size_t host_sz,
                     wchar_t *path, size_t path_sz,
                     int *port, int *use_ssl)
{
    *use_ssl = 0;
    *port = 80;

    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) {
        *use_ssl = 1;
        *port = 443;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else {
        return -1;
    }

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    size_t host_len;
    if (colon && (!slash || colon < slash)) {
        host_len = (size_t)(colon - p);
        *port = atoi(colon + 1);
    } else if (slash) {
        host_len = (size_t)(slash - p);
    } else {
        host_len = strlen(p);
    }

    if (host_len >= host_sz) return -1;
    for (size_t i = 0; i < host_len; i++) host[i] = (wchar_t)p[i];
    host[host_len] = L'\0';

    if (slash) {
        size_t path_len = strlen(slash);
        if (path_len >= path_sz) return -1;
        for (size_t i = 0; i <= path_len; i++) path[i] = (wchar_t)slash[i];
    } else {
        path[0] = L'/';
        path[1] = L'\0';
    }

    return 0;
}

/* The request handle is armed on the caller's token while its calls block;
 * the UI thread firing the token closes it and wakes the call. Whoever the
 * disarm says owns the handle closes it: after a fire it is already closed,
 * and its value may have been reused, so it is never touched again. With no
 * token (NULL) disarm says "yours" and this is a plain close. */
static void http_close_cb(uintptr_t h)
{
    WinHttpCloseHandle((HINTERNET)h);
}

static void req_close(HINTERNET req, AiNetCancel *nc)
{
    if (ai_nc_disarm(nc)) WinHttpCloseHandle(req);
}

/* Arm a fresh request handle. 0 to go on; -1 when the token had already
 * fired: the handle is closed here and the caller closes the rest. */
static int req_arm(HINTERNET req, AiNetCancel *nc)
{
    if (ai_nc_arm(nc, (uintptr_t)req, http_close_cb) == 0) return 0;
    WinHttpCloseHandle(req);
    return -1;
}

#endif /* _WIN32 */

/* A content type that cannot be put on a header line safely. */
static int content_type_ok(const char *ct)
{
    if (!ct || !*ct) return 0;
    size_t n = strlen(ct);
    if (n > 200) return 0;
    return strpbrk(ct, "\r\n") == NULL;
}

#ifdef _WIN32

typedef struct { int resolve, connect, send, recv; } HttpTimeouts;

/* Closes a session this file opened; a caller's own session is left alone. */
static void sess_close(HINTERNET s, int own)
{
    if (own) WinHttpCloseHandle(s);
}

static volatile LONG g_fail_redirect_policy;

void ai_http_test_fail_redirect_policy(int on)
{
    InterlockedExchange(&g_fail_redirect_policy, on ? 1 : 0);
}

static int ai_http_post_impl(const char *url, const char * const *headers,
                             const char *content_type,
                             const char *body, size_t body_len,
                             const HttpTimeouts *to, void *session,
                             AiHttpResponse *resp, AiNetCancel *nc)
{
    if (!resp) return -1;
    memset(resp, 0, sizeof(*resp));
    if (!content_type_ok(content_type)) {
        snprintf(resp->error, sizeof(resp->error), "Invalid content type");
        return -1;
    }

    if (!url || !body) {
        snprintf(resp->error, sizeof(resp->error), "NULL url or body");
        return -1;
    }

    wchar_t host[256], path[1024];
    int port, use_ssl;
    if (parse_url(url, host, 256, path, 1024, &port, &use_ssl) != 0) {
        snprintf(resp->error, sizeof(resp->error), "Invalid URL");
        return -1;
    }

    /* A caller's session (session != NULL, the voice opener's) is used as it is and
     * never opened or closed here; its timeouts are set on the session itself,
     * exactly as for an own one, so the caller must not share it between
     * concurrent calls with different timeouts. */
    const int own = session == NULL;
    HINTERNET hSession = own ? ns_winhttp_open(NULL) : (HINTERNET)session;
    if (!hSession) {
        snprintf(resp->error, sizeof(resp->error), "WinHttpOpen failed: %lu",
                 GetLastError());
        return -1;
    }

    if (!WinHttpSetTimeouts(hSession, to->resolve, to->connect, to->send, to->recv)) {
        snprintf(resp->error, sizeof(resp->error), "WinHttpSetTimeouts failed: %lu",
                 GetLastError());
        sess_close(hSession, own);
        return -1;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host, (INTERNET_PORT)port, 0);
    if (!hConnect) {
        snprintf(resp->error, sizeof(resp->error), "WinHttpConnect failed: %lu",
                 GetLastError());
        sess_close(hSession, own);
        return -1;
    }

    DWORD flags = use_ssl ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path,
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            flags);
    if (!hRequest) {
        snprintf(resp->error, sizeof(resp->error), "WinHttpOpenRequest failed: %lu",
                 GetLastError());
        WinHttpCloseHandle(hConnect);
        sess_close(hSession, own);
        return -1;
    }
    if (!own) {
        /* A caller's session carries a bearer key (the voice opener's): never follow a
         * redirect, which would re-send it to wherever the server points. Fail closed:
         * nothing has been sent yet. */
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        BOOL ok = !g_fail_redirect_policy &&
                  WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
        if (!ok) {
            snprintf(resp->error, sizeof(resp->error), "WinHTTP redirect policy refused: %lu",
                     GetLastError());
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            return -1;
        }
    }
    if (req_arm(hRequest, nc) != 0) {
        snprintf(resp->error, sizeof(resp->error), "cancelled");
        WinHttpCloseHandle(hConnect);
        sess_close(hSession, own);
        return -1;
    }

    /* Set headers */
    {
        wchar_t ct_w[256];
        int cn = MultiByteToWideChar(CP_UTF8, 0, "Content-Type: ", -1, ct_w, 20);
        int cm = MultiByteToWideChar(CP_UTF8, 0, content_type, -1,
                                     ct_w + (cn - 1), 256 - (cn - 1) - 2);
        if (cn > 0 && cm > 0) {
            size_t cl = (size_t)(cn - 1) + (size_t)(cm - 1);
            ct_w[cl] = L'\r'; ct_w[cl + 1] = L'\n'; ct_w[cl + 2] = L'\0';
            WinHttpAddRequestHeaders(hRequest, ct_w, (DWORD)-1,
                                     WINHTTP_ADDREQ_FLAG_ADD);
        }
    }

    if (headers) {
        for (int i = 0; headers[i]; i++) {
            wchar_t hdr_w[600];
            int len = MultiByteToWideChar(CP_UTF8, 0, headers[i], -1, hdr_w, 590);
            if (len > 0) {
                size_t wlen = (size_t)(len - 1);
                if (wlen + 2 < 600) {
                    hdr_w[wlen]     = L'\r';
                    hdr_w[wlen + 1] = L'\n';
                    hdr_w[wlen + 2] = L'\0';
                }
                WinHttpAddRequestHeaders(hRequest, hdr_w, (DWORD)-1,
                                         WINHTTP_ADDREQ_FLAG_ADD);
            }
            secure_zero(hdr_w, sizeof(hdr_w));   /* it may hold a bearer key */
        }
    }

    /* Send request */
    BOOL sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   (LPVOID)body, (DWORD)body_len,
                                   (DWORD)body_len, 0);
    if (!sent) {
        snprintf(resp->error, sizeof(resp->error), "WinHttpSendRequest failed: %lu",
                 GetLastError());
        req_close(hRequest, nc);
        WinHttpCloseHandle(hConnect);
        sess_close(hSession, own);
        return -1;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        snprintf(resp->error, sizeof(resp->error),
                 "WinHttpReceiveResponse failed: %lu", GetLastError());
        req_close(hRequest, nc);
        WinHttpCloseHandle(hConnect);
        sess_close(hSession, own);
        return -1;
    }

    /* Get status code */
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    resp->status_code = (int)status;

    if (!own && status >= 300 && status < 400) {
        /* The redirect was not followed (policy above): an error that says so, not a reply. */
        snprintf(resp->error, sizeof(resp->error), "Redirect refused (HTTP %lu)",
                 (unsigned long)status);
        req_close(hRequest, nc);
        WinHttpCloseHandle(hConnect);
        sess_close(hSession, own);
        return -1;
    }

    /* Read response body */
    size_t total = 0;
    size_t cap = 4096;
    char *buf = malloc(cap);
    if (!buf) {
        snprintf(resp->error, sizeof(resp->error), "Out of memory");
        req_close(hRequest, nc);
        WinHttpCloseHandle(hConnect);
        sess_close(hSession, own);
        return -1;
    }

    DWORD bytes_read;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
        if (avail == 0) break;

        if (total + avail + 1 > cap) {
            cap = (total + avail + 1) * 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); buf = NULL; break; }
            buf = nb;
        }

        if (!WinHttpReadData(hRequest, buf + total, avail, &bytes_read)) break;
        total += bytes_read;
    }

    if (ai_nc_cancelled(nc)) {
        /* fired during the read: what arrived is a cut body, not a reply */
        free(buf);
        snprintf(resp->error, sizeof(resp->error), "cancelled");
        req_close(hRequest, nc);
        WinHttpCloseHandle(hConnect);
        sess_close(hSession, own);
        return -1;
    }

    if (buf) {
        buf[total] = '\0';
        resp->body = buf;
        resp->body_len = total;
    }

    req_close(hRequest, nc);
    WinHttpCloseHandle(hConnect);
    sess_close(hSession, own);
    return 0;
}

/* session NULL: opens (and closes) its own with the 5 minute streaming timeouts and
 * follows redirects as WinHTTP does; a caller's session (the _ex variant) is never
 * closed here, gets 10/10/30 s/recv_ms and a request that never follows a redirect. */
static int ai_http_post_stream_impl(void *session, int recv_ms,
                                    const char *url, const char * const *headers,
                                    const char *body, size_t body_len,
                                    AiStreamCallback cb, void *userdata,
                                    int *status_out, char *error, size_t error_size,
                                    AiNetCancel *nc)
{
    const int own = (session == NULL);
    if (status_out) *status_out = 0;
    if (error && error_size > 0) error[0] = '\0';

    if (!url || !body || !cb) {
        if (error) snprintf(error, error_size, "NULL url, body, or callback");
        return -1;
    }

    wchar_t host[256], path[1024];
    int port, use_ssl;
    if (parse_url(url, host, 256, path, 1024, &port, &use_ssl) != 0) {
        if (error) snprintf(error, error_size, "Invalid URL");
        return -1;
    }

    if (!own && recv_ms <= 0) {     /* a speech worker never blocks unbounded */
        if (error) snprintf(error, error_size, "Invalid receive timeout");
        return -1;
    }

    HINTERNET hSession = own ? ns_winhttp_open(NULL) : (HINTERNET)session;
    if (!hSession) {
        if (error) snprintf(error, error_size, "WinHttpOpen failed: %lu",
                            GetLastError());
        return -1;
    }

    if (own) {  /* Streaming can take a long time — 5 min receive timeout */
        WinHttpSetTimeouts(hSession, 0, 60000, 30000, 300000);
    } else if (!WinHttpSetTimeouts(hSession, 10000, 10000, 30000, recv_ms)) {
        if (error) snprintf(error, error_size, "WinHttpSetTimeouts failed: %lu",
                            GetLastError());
        return -1;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host, (INTERNET_PORT)port, 0);
    if (!hConnect) {
        if (error) snprintf(error, error_size, "WinHttpConnect failed: %lu",
                            GetLastError());
        sess_close(hSession, own);
        return -1;
    }

    DWORD flags = use_ssl ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path,
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            flags);
    if (!hRequest) {
        if (error) snprintf(error, error_size, "WinHttpOpenRequest failed: %lu",
                            GetLastError());
        WinHttpCloseHandle(hConnect);
        sess_close(hSession, own);
        return -1;
    }
    if (!own) {
        /* Fail closed: a request under the default policy would follow a redirect
         * and re-send the Authorization header. Nothing has been sent yet. */
        DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        BOOL ok = !g_fail_redirect_policy &&
                  WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
        if (!ok) {
            if (error) snprintf(error, error_size, "WinHTTP redirect policy refused: %lu",
                                GetLastError());
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            return -1;
        }
    }
    if (req_arm(hRequest, nc) != 0) {
        if (error) snprintf(error, error_size, "cancelled");
        WinHttpCloseHandle(hConnect);
        sess_close(hSession, own);
        return -1;
    }

    WinHttpAddRequestHeaders(hRequest,
        L"Content-Type: application/json\r\n",
        (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    if (headers) {
        for (int i = 0; headers[i]; i++) {
            wchar_t hdr_w[600];
            int len = MultiByteToWideChar(CP_UTF8, 0, headers[i], -1, hdr_w, 590);
            if (len > 0) {
                size_t wlen = (size_t)(len - 1);
                if (wlen + 2 < 600) {
                    hdr_w[wlen]     = L'\r';
                    hdr_w[wlen + 1] = L'\n';
                    hdr_w[wlen + 2] = L'\0';
                }
                WinHttpAddRequestHeaders(hRequest, hdr_w, (DWORD)-1,
                                         WINHTTP_ADDREQ_FLAG_ADD);
            }
            secure_zero(hdr_w, sizeof(hdr_w));   /* it may hold a bearer key */
        }
    }

    BOOL sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   (LPVOID)body, (DWORD)body_len,
                                   (DWORD)body_len, 0);
    if (!sent) {
        if (error) snprintf(error, error_size, "WinHttpSendRequest failed: %lu",
                            GetLastError());
        req_close(hRequest, nc);
        WinHttpCloseHandle(hConnect);
        sess_close(hSession, own);
        return -1;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        if (error) snprintf(error, error_size,
                            "WinHttpReceiveResponse failed: %lu", GetLastError());
        req_close(hRequest, nc);
        WinHttpCloseHandle(hConnect);
        sess_close(hSession, own);
        return -1;
    }

    DWORD http_status = 0, ssz = sizeof(http_status);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &http_status, &ssz, WINHTTP_NO_HEADER_INDEX);
    if (status_out) *status_out = (int)http_status;

    /* On HTTP error, read body into the error buffer instead of streaming */
    if (http_status >= 300) {
        size_t err_pos = 0;
        char chunk_buf[4096];
        DWORD bytes_read;
        for (;;) {
            if (ai_nc_cancelled(nc)) break;   /* a fired token closed hRequest */
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
            if (avail == 0) break;
            DWORD to_read = avail < sizeof(chunk_buf) - 1
                           ? avail : (DWORD)(sizeof(chunk_buf) - 1);
            if (!WinHttpReadData(hRequest, chunk_buf, to_read, &bytes_read))
                break;
            if (bytes_read == 0) break;
            /* Append to error buffer */
            if (error && err_pos + (size_t)bytes_read < error_size - 1) {
                memcpy(error + err_pos, chunk_buf, (size_t)bytes_read);
                err_pos += (size_t)bytes_read;
                error[err_pos] = '\0';
            }
        }
        req_close(hRequest, nc);
        WinHttpCloseHandle(hConnect);
        sess_close(hSession, own);
        return -1;
    }

    /* Read chunks and pass to callback */
    int ret = 0;
    char chunk_buf[4096];
    DWORD bytes_read;
    for (;;) {
        if (ai_nc_cancelled(nc)) { ret = -1; break; }   /* hRequest is closed */
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail)) {
            ret = -1;   /* the body broke after a 200: not a clean end */
            break;
        }
        if (avail == 0) break;

        DWORD to_read = avail < sizeof(chunk_buf) - 1
                       ? avail : (DWORD)(sizeof(chunk_buf) - 1);
        if (!WinHttpReadData(hRequest, chunk_buf, to_read, &bytes_read)) {
            ret = -1;
            break;
        }
        if (bytes_read == 0) break;

        chunk_buf[bytes_read] = '\0';
        if (cb(chunk_buf, (size_t)bytes_read, userdata) != 0) {
            ret = 0; /* caller aborted — not an error */
            break;
        }
    }

    if (ret != 0 && error && error_size > 0)
        snprintf(error, error_size, "connection lost during reply");

    secure_zero(chunk_buf, sizeof(chunk_buf));   /* it carried speech audio */
    req_close(hRequest, nc);
    WinHttpCloseHandle(hConnect);
    sess_close(hSession, own);
    return ret;
}

static int ai_http_get_impl(const char *url, const char * const *headers,
                            AiHttpResponse *resp, AiNetCancel *nc)
{
    if (!resp) return -1;
    memset(resp, 0, sizeof(*resp));

    if (!url) {
        snprintf(resp->error, sizeof(resp->error), "NULL url");
        return -1;
    }

    wchar_t host[256], path[1024];
    int port, use_ssl;
    if (parse_url(url, host, 256, path, 1024, &port, &use_ssl) != 0) {
        snprintf(resp->error, sizeof(resp->error), "Invalid URL");
        return -1;
    }

    HINTERNET hSession = ns_winhttp_open(NULL);
    if (!hSession) {
        snprintf(resp->error, sizeof(resp->error), "WinHttpOpen failed: %lu",
                 GetLastError());
        return -1;
    }

    /* AI responses can take 60+ seconds; raise receive timeout to 120s */
    WinHttpSetTimeouts(hSession, 0, 60000, 30000, 120000);

    HINTERNET hConnect = WinHttpConnect(hSession, host, (INTERNET_PORT)port, 0);
    if (!hConnect) {
        snprintf(resp->error, sizeof(resp->error), "WinHttpConnect failed: %lu",
                 GetLastError());
        WinHttpCloseHandle(hSession);
        return -1;
    }

    DWORD flags = use_ssl ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path,
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            flags);
    if (!hRequest) {
        snprintf(resp->error, sizeof(resp->error), "WinHttpOpenRequest failed: %lu",
                 GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }
    if (req_arm(hRequest, nc) != 0) {
        snprintf(resp->error, sizeof(resp->error), "cancelled");
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    /* Add all caller-supplied headers */
    if (headers) {
        for (int i = 0; headers[i]; i++) {
            wchar_t hdr_w[600];
            int len = MultiByteToWideChar(CP_UTF8, 0, headers[i], -1, hdr_w, 590);
            if (len > 0) {
                /* Append \r\n if not already present */
                size_t wlen = (size_t)(len - 1);
                if (wlen + 2 < 600) {
                    hdr_w[wlen]     = L'\r';
                    hdr_w[wlen + 1] = L'\n';
                    hdr_w[wlen + 2] = L'\0';
                }
                WinHttpAddRequestHeaders(hRequest, hdr_w, (DWORD)-1,
                                         WINHTTP_ADDREQ_FLAG_ADD);
            }
        }
    }

    BOOL sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!sent) {
        snprintf(resp->error, sizeof(resp->error), "WinHttpSendRequest failed: %lu",
                 GetLastError());
        req_close(hRequest, nc);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        snprintf(resp->error, sizeof(resp->error),
                 "WinHttpReceiveResponse failed: %lu", GetLastError());
        req_close(hRequest, nc);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    resp->status_code = (int)status;

    size_t total = 0;
    size_t cap = 4096;
    char *buf = malloc(cap);
    if (!buf) {
        snprintf(resp->error, sizeof(resp->error), "Out of memory");
        req_close(hRequest, nc);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    DWORD bytes_read;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
        if (avail == 0) break;

        if (total + avail + 1 > cap) {
            cap = (total + avail + 1) * 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); buf = NULL; break; }
            buf = nb;
        }

        if (!WinHttpReadData(hRequest, buf + total, avail, &bytes_read)) break;
        total += bytes_read;
    }

    if (ai_nc_cancelled(nc)) {
        /* fired during the read: what arrived is a cut body, not a reply */
        free(buf);
        snprintf(resp->error, sizeof(resp->error), "cancelled");
        req_close(hRequest, nc);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    if (buf) {
        buf[total] = '\0';
        resp->body = buf;
        resp->body_len = total;
    }

    req_close(hRequest, nc);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return 0;
}

/* The public calls: a call that failed after its token fired failed because
 * the handle was closed under it, so it says "cancelled" whatever WinHTTP
 * reported. */
static void note_cancel(AiNetCancel *nc, char *err, size_t cap)
{
    if (ai_nc_cancelled(nc) && err && cap > 0)
        snprintf(err, cap, "cancelled");
}

static int post_with(const char *url, const char * const *headers,
                     const char *ct, const char *body, size_t body_len,
                     const HttpTimeouts *to, void *session,
                     AiHttpResponse *resp, AiNetCancel *nc)
{
    int rc = ai_http_post_impl(url, headers, ct, body, body_len, to, session, resp, nc);
    if (rc != 0 && resp) note_cancel(nc, resp->error, sizeof(resp->error));
    return rc;
}

int ai_http_post(const char *url, const char * const *headers,
                 const char *body, size_t body_len,
                 AiHttpResponse *resp, AiNetCancel *nc)
{
    /* AI responses can take 60+ seconds: receive timeout 120 s */
    static const HttpTimeouts chat = { 0, 60000, 30000, 120000 };
    return post_with(url, headers, "application/json", body, body_len, &chat, NULL, resp, nc);
}

int ai_http_post_ct(const char *url, const char * const *headers,
                    const char *content_type, const char *body, size_t body_len,
                    AiHttpResponse *resp, AiNetCancel *nc)
{
    static const HttpTimeouts upload = { 10000, 10000, 30000, 60000 };
    return post_with(url, headers, content_type, body, body_len, &upload, NULL, resp, nc);
}

int ai_http_post_ct_ex(void *session, const char *url, const char * const *headers,
                       const char *content_type, const char *body, size_t body_len,
                       int send_ms, int recv_ms, AiHttpResponse *resp, AiNetCancel *nc)
{
    const HttpTimeouts to = { 10000, 10000, send_ms, recv_ms };
    if (!session) {
        if (resp) {
            memset(resp, 0, sizeof(*resp));
            snprintf(resp->error, sizeof(resp->error), "No session");
        }
        return -1;
    }
    return post_with(url, headers, content_type, body, body_len, &to, session, resp, nc);
}

int ai_http_get(const char *url, const char * const *headers,
                AiHttpResponse *resp, AiNetCancel *nc)
{
    int rc = ai_http_get_impl(url, headers, resp, nc);
    if (rc != 0 && resp) note_cancel(nc, resp->error, sizeof(resp->error));
    return rc;
}

int ai_http_post_stream(const char *url, const char * const *headers,
                        const char *body, size_t body_len,
                        AiStreamCallback cb, void *userdata,
                        int *status_out, char *error, size_t error_size,
                        AiNetCancel *nc)
{
    int rc = ai_http_post_stream_impl(NULL, 0, url, headers, body, body_len, cb, userdata,
                                      status_out, error, error_size, nc);
    if (rc != 0) note_cancel(nc, error, error_size);
    return rc;
}

int ai_http_post_stream_ex(void *session, const char *url, const char * const *headers,
                           const char *body, size_t body_len, int recv_ms,
                           AiStreamCallback cb, void *userdata,
                           int *status_out, char *error, size_t error_size,
                           AiNetCancel *nc)
{
    if (status_out) *status_out = 0;
    if (!session) {
        if (error && error_size > 0) snprintf(error, error_size, "No session");
        return -1;
    }
    int rc = ai_http_post_stream_impl(session, recv_ms, url, headers, body, body_len, cb,
                                      userdata, status_out, error, error_size, nc);
    if (rc != 0) note_cancel(nc, error, error_size);
    return rc;
}

#else /* !_WIN32 — stub for non-Windows builds */

int ai_http_post(const char *url, const char * const *headers,
                 const char *body, size_t body_len,
                 AiHttpResponse *resp, AiNetCancel *nc)
{
    (void)url; (void)headers; (void)body; (void)body_len; (void)nc;
    if (!resp) return -1;
    memset(resp, 0, sizeof(*resp));
    snprintf(resp->error, sizeof(resp->error), "HTTP not available on this platform");
    return -1;
}

int ai_http_post_ct(const char *url, const char * const *headers,
                    const char *content_type, const char *body, size_t body_len,
                    AiHttpResponse *resp, AiNetCancel *nc)
{
    (void)url; (void)headers; (void)body; (void)body_len; (void)nc;
    if (!resp) return -1;
    memset(resp, 0, sizeof(*resp));
    if (!content_type_ok(content_type)) {
        snprintf(resp->error, sizeof(resp->error), "Invalid content type");
        return -1;
    }
    snprintf(resp->error, sizeof(resp->error), "HTTP not available on this platform");
    return -1;
}

int ai_http_post_ct_ex(void *session, const char *url, const char * const *headers,
                       const char *content_type, const char *body, size_t body_len,
                       int send_ms, int recv_ms, AiHttpResponse *resp, AiNetCancel *nc)
{
    (void)session; (void)send_ms; (void)recv_ms;
    return ai_http_post_ct(url, headers, content_type, body, body_len, resp, nc);
}

int ai_http_get(const char *url, const char * const *headers,
                AiHttpResponse *resp, AiNetCancel *nc)
{
    (void)url; (void)headers; (void)nc;
    if (!resp) return -1;
    memset(resp, 0, sizeof(*resp));
    snprintf(resp->error, sizeof(resp->error), "HTTP not available on this platform");
    return -1;
}

int ai_http_post_stream(const char *url, const char * const *headers,
                        const char *body, size_t body_len,
                        AiStreamCallback cb, void *userdata,
                        int *status_out, char *error, size_t error_size,
                        AiNetCancel *nc)
{
    (void)url; (void)headers; (void)body; (void)body_len;
    (void)cb; (void)userdata; (void)status_out; (void)nc;
    if (error && error_size > 0)
        snprintf(error, error_size, "HTTP not available on this platform");
    return -1;
}

void ai_http_test_fail_redirect_policy(int on) { (void)on; }

int ai_http_post_stream_ex(void *session, const char *url, const char * const *headers,
                           const char *body, size_t body_len, int recv_ms,
                           AiStreamCallback cb, void *userdata,
                           int *status_out, char *error, size_t error_size,
                           AiNetCancel *nc)
{
    (void)recv_ms;
    if (!session) {
        if (status_out) *status_out = 0;
        if (error && error_size > 0) snprintf(error, error_size, "No session");
        return -1;
    }
    return ai_http_post_stream(url, headers, body, body_len, cb, userdata,
                               status_out, error, error_size, nc);
}

#endif /* _WIN32 */
