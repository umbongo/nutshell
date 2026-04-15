#include "ai_tool_web_fetch.h"
#include "html_util.h"
#include "json_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
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

/* ---- tool_web_fetch_execute ---------------------------------------------- */

int tool_web_fetch_execute(const char *input_json, void *tool_data,
                           volatile int *cancel_flag,
                           char **result_buf, size_t *result_len,
                           int *was_truncated)
{
    (void)cancel_flag;

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

    /* Copy URL before freeing the JSON tree */
    char url_copy[2048];
    size_t ulen = strlen(url);
    if (ulen >= sizeof(url_copy)) ulen = sizeof(url_copy) - 1;
    memcpy(url_copy, url, ulen);
    url_copy[ulen] = '\0';

    json_free(input);

#ifdef _WIN32
    /* ---- Windows WinHTTP implementation ---------------------------------- */

    WebFetchContext *ctx    = (WebFetchContext *)tool_data;
    int              timeout = (ctx && ctx->timeout_ms > 0) ? ctx->timeout_ms : 10000;

    /* Convert URL to wide string */
    wchar_t w_url[2048];
    MultiByteToWideChar(CP_UTF8, 0, url_copy, -1, w_url, 2048);

    /* Parse URL components */
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t w_host[512] = {0};
    wchar_t w_path[1536] = {0};
    uc.lpszHostName     = w_host;
    uc.dwHostNameLength = 512;
    uc.lpszUrlPath      = w_path;
    uc.dwUrlPathLength  = 1536;

    if (!WinHttpCrackUrl(w_url, 0, 0, &uc)) {
        const char *err = "Failed to parse URL";
        *result_buf = malloc(strlen(err) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, err);
        *result_len = strlen(err);
        return -1;
    }

    HINTERNET session = WinHttpOpen(
        L"Nutshell/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        const char *err = "WinHTTP session open failed";
        *result_buf = malloc(strlen(err) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, err);
        *result_len = strlen(err);
        return -1;
    }

    INTERNET_PORT port = (uc.nScheme == INTERNET_SCHEME_HTTPS)
                         ? INTERNET_DEFAULT_HTTPS_PORT
                         : INTERNET_DEFAULT_HTTP_PORT;

    HINTERNET connect = WinHttpConnect(session, w_host, port, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        const char *err = "WinHTTP connect failed";
        *result_buf = malloc(strlen(err) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, err);
        *result_len = strlen(err);
        return -1;
    }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(
        connect, L"GET", w_path,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        const char *err = "WinHTTP request open failed";
        *result_buf = malloc(strlen(err) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, err);
        *result_len = strlen(err);
        return -1;
    }

    /* Set timeout */
    WinHttpSetTimeouts(request, timeout, timeout, timeout, timeout);

    /* Set User-Agent header */
    wchar_t w_ua[128];
    MultiByteToWideChar(CP_UTF8, 0, AI_TOOL_USER_AGENT, -1, w_ua, 128);
    WinHttpAddRequestHeaders(request, L"User-Agent: ", (DWORD)-1,
                             WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        const char *err = "HTTP request failed";
        *result_buf = malloc(strlen(err) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, err);
        *result_len = strlen(err);
        return -1;
    }

    /* Read response body into a dynamically grown buffer */
    size_t body_cap  = 65536;
    size_t body_len  = 0;
    char  *body      = malloc(body_cap);
    if (!body) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return -1;
    }

    DWORD bytes_read = 0;
    DWORD bytes_avail = 0;
    while (WinHttpQueryDataAvailable(request, &bytes_avail) && bytes_avail > 0) {
        /* Cap at AI_TOOL_RESULT_MAX to avoid reading huge pages */
        if (body_len + bytes_avail > AI_TOOL_RESULT_MAX * 4)
            bytes_avail = (DWORD)(AI_TOOL_RESULT_MAX * 4 - body_len);

        if (body_len + bytes_avail + 1 > body_cap) {
            body_cap = (body_len + bytes_avail + 1) * 2;
            char *tmp = realloc(body, body_cap);
            if (!tmp) {
                free(body);
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                return -1;
            }
            body = tmp;
        }

        if (!WinHttpReadData(request, body + body_len, bytes_avail, &bytes_read))
            break;
        body_len += bytes_read;

        if (body_len >= AI_TOOL_RESULT_MAX * 4) break;
    }
    body[body_len] = '\0';

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    /* Convert HTML to plain text */
    int rc = web_fetch_html_to_text(body, body_len,
                                     result_buf, result_len, was_truncated);
    free(body);
    return rc;

#else
    /* Non-Windows: not implemented */
    (void)tool_data;
    const char *err = "Web fetch not available (non-Windows build)";
    *result_buf = malloc(strlen(err) + 1);
    if (!*result_buf) return -1;
    strcpy(*result_buf, err);
    *result_len = strlen(err);
    return -1;
#endif
}
