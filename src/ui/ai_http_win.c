#include "ai_http.h"
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

int ai_http_post(const char *url, const char *auth_header,
                 const char *body, size_t body_len,
                 AiHttpResponse *resp)
{
    if (!resp) return -1;
    memset(resp, 0, sizeof(*resp));

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

    HINTERNET hSession = WinHttpOpen(L"Nutshell/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        snprintf(resp->error, sizeof(resp->error), "WinHttpOpen failed: %lu",
                 GetLastError());
        return -1;
    }

    /* Require TLS 1.2+ for modern AI APIs */
    if (use_ssl) {
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
                        | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS,
                         &protocols, sizeof(protocols));
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
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path,
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

    /* Set headers */
    WinHttpAddRequestHeaders(hRequest,
        L"Content-Type: application/json\r\n",
        (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    if (auth_header && auth_header[0]) {
        wchar_t auth_w[512];
        int len = MultiByteToWideChar(CP_UTF8, 0, auth_header, -1, auth_w, 512);
        if (len > 0) {
            wchar_t hdr[600];
            swprintf(hdr, 600, L"Authorization: %ls\r\n", auth_w);
            WinHttpAddRequestHeaders(hRequest, hdr, (DWORD)-1,
                                     WINHTTP_ADDREQ_FLAG_ADD);
        }
    }

    /* Send request */
    BOOL sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   (LPVOID)body, (DWORD)body_len,
                                   (DWORD)body_len, 0);
    if (!sent) {
        snprintf(resp->error, sizeof(resp->error), "WinHttpSendRequest failed: %lu",
                 GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        snprintf(resp->error, sizeof(resp->error),
                 "WinHttpReceiveResponse failed: %lu", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    /* Get status code */
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    resp->status_code = (int)status;

    /* Read response body */
    size_t total = 0;
    size_t cap = 4096;
    char *buf = malloc(cap);
    if (!buf) {
        snprintf(resp->error, sizeof(resp->error), "Out of memory");
        WinHttpCloseHandle(hRequest);
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

    if (buf) {
        buf[total] = '\0';
        resp->body = buf;
        resp->body_len = total;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return 0;
}

int ai_http_post_stream(const char *url, const char *auth_header,
                        const char *body, size_t body_len,
                        AiStreamCallback cb, void *userdata,
                        int *status_out, char *error, size_t error_size)
{
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

    HINTERNET hSession = WinHttpOpen(L"Nutshell/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        if (error) snprintf(error, error_size, "WinHttpOpen failed: %lu",
                            GetLastError());
        return -1;
    }

    if (use_ssl) {
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
                        | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS,
                         &protocols, sizeof(protocols));
    }

    /* Streaming can take a long time — 5 min receive timeout */
    WinHttpSetTimeouts(hSession, 0, 60000, 30000, 300000);

    HINTERNET hConnect = WinHttpConnect(hSession, host, (INTERNET_PORT)port, 0);
    if (!hConnect) {
        if (error) snprintf(error, error_size, "WinHttpConnect failed: %lu",
                            GetLastError());
        WinHttpCloseHandle(hSession);
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
        WinHttpCloseHandle(hSession);
        return -1;
    }

    WinHttpAddRequestHeaders(hRequest,
        L"Content-Type: application/json\r\n",
        (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    if (auth_header && auth_header[0]) {
        wchar_t auth_w[512];
        int len = MultiByteToWideChar(CP_UTF8, 0, auth_header, -1, auth_w, 512);
        if (len > 0) {
            wchar_t hdr[600];
            swprintf(hdr, 600, L"Authorization: %ls\r\n", auth_w);
            WinHttpAddRequestHeaders(hRequest, hdr, (DWORD)-1,
                                     WINHTTP_ADDREQ_FLAG_ADD);
        }
    }

    BOOL sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   (LPVOID)body, (DWORD)body_len,
                                   (DWORD)body_len, 0);
    if (!sent) {
        if (error) snprintf(error, error_size, "WinHttpSendRequest failed: %lu",
                            GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        if (error) snprintf(error, error_size,
                            "WinHttpReceiveResponse failed: %lu", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
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
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    /* Read chunks and pass to callback */
    int ret = 0;
    char chunk_buf[4096];
    DWORD bytes_read;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail)) break;
        if (avail == 0) break;

        DWORD to_read = avail < sizeof(chunk_buf) - 1
                       ? avail : (DWORD)(sizeof(chunk_buf) - 1);
        if (!WinHttpReadData(hRequest, chunk_buf, to_read, &bytes_read)) break;
        if (bytes_read == 0) break;

        chunk_buf[bytes_read] = '\0';
        if (cb(chunk_buf, (size_t)bytes_read, userdata) != 0) {
            ret = 0; /* caller aborted — not an error */
            break;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ret;
}

int ai_http_get(const char *url, const char * const *headers,
                AiHttpResponse *resp)
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

    HINTERNET hSession = WinHttpOpen(L"Nutshell/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        snprintf(resp->error, sizeof(resp->error), "WinHttpOpen failed: %lu",
                 GetLastError());
        return -1;
    }

    /* Require TLS 1.2+ for modern AI APIs */
    if (use_ssl) {
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
                        | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS,
                         &protocols, sizeof(protocols));
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
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        snprintf(resp->error, sizeof(resp->error),
                 "WinHttpReceiveResponse failed: %lu", GetLastError());
        WinHttpCloseHandle(hRequest);
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
        WinHttpCloseHandle(hRequest);
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

    if (buf) {
        buf[total] = '\0';
        resp->body = buf;
        resp->body_len = total;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return 0;
}

#else /* !_WIN32 — stub for non-Windows builds */

int ai_http_post(const char *url, const char *auth_header,
                 const char *body, size_t body_len,
                 AiHttpResponse *resp)
{
    (void)url; (void)auth_header; (void)body; (void)body_len;
    if (!resp) return -1;
    memset(resp, 0, sizeof(*resp));
    snprintf(resp->error, sizeof(resp->error), "HTTP not available on this platform");
    return -1;
}

int ai_http_get(const char *url, const char * const *headers,
                AiHttpResponse *resp)
{
    (void)url; (void)headers;
    if (!resp) return -1;
    memset(resp, 0, sizeof(*resp));
    snprintf(resp->error, sizeof(resp->error), "HTTP not available on this platform");
    return -1;
}

int ai_http_post_stream(const char *url, const char *auth_header,
                        const char *body, size_t body_len,
                        AiStreamCallback cb, void *userdata,
                        int *status_out, char *error, size_t error_size)
{
    (void)url; (void)auth_header; (void)body; (void)body_len;
    (void)cb; (void)userdata; (void)status_out;
    if (error && error_size > 0)
        snprintf(error, error_size, "HTTP not available on this platform");
    return -1;
}

#endif /* _WIN32 */
