#include "ai_tool_web_search.h"
#include "html_util.h"
#include "json_parser.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include "winhttp_open.h"
#endif

/* ---- Internal helpers ---------------------------------------------------- */

/* Append formatted result entry to out_buf. Returns new out_pos, or 0 on overflow. */
static size_t append_result(char *out_buf, size_t out_max, size_t out_pos,
                             int num, const char *title, const char *url,
                             const char *snippet)
{
    int n = snprintf(out_buf + out_pos, out_max - out_pos,
                     "%d. %s\n   %s\n   %s\n\n",
                     num, title ? title : "", url ? url : "",
                     snippet ? snippet : "");
    if (n < 0 || (size_t)n >= out_max - out_pos) return 0;
    return out_pos + (size_t)n;
}

/* ---- ws_build_search_url ------------------------------------------------- */

/* Append the RFC 3986 percent-encoding of `q` (every byte except
 * A-Z a-z 0-9 - . _ ~ becomes %XX, upper-case hex) to out at *pos.
 * Returns 0, or -1 when it does not fit; nothing partial is kept. */
static int ws_append_encoded(char *out, size_t out_max, size_t *pos,
                             const char *q)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t p = *pos;
    for (const unsigned char *c = (const unsigned char *)q; *c; c++) {
        int plain = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') ||
                    (*c >= '0' && *c <= '9') || *c == '-' || *c == '.' ||
                    *c == '_' || *c == '~';
        size_t need = plain ? 1 : 3;
        if (p + need >= out_max) return -1;
        if (plain) {
            out[p++] = (char)*c;
        } else {
            out[p++] = '%';
            out[p++] = hex[*c >> 4];
            out[p++] = hex[*c & 0x0F];
        }
    }
    out[p] = '\0';
    *pos = p;
    return 0;
}

const char *ws_url_error_text(int rc)
{
    if (rc == -2) return "The custom search URL must not contain a # fragment";
    if (rc == -3)
        return "The custom search URL must not contain a space or control character";
    return "Search query is too long for a search URL";
}

int ws_build_search_url(const char *provider, const char *custom_url,
                        int force_html, const char *query,
                        char *out, size_t out_max)
{
    if (!out || out_max == 0) return -1;
    out[0] = '\0';
    if (!query) query = "";
    if (!provider) provider = "";

    const char *prefix;
    const char *suffix = "";
    char sep_buf[2] = "";
    if (!force_html && custom_url && custom_url[0] != '\0' &&
        strcmp(provider, "custom") == 0) {
        /* A fragment would swallow the query, so the URL is refused. */
        if (strchr(custom_url, '#')) return -2;
        /* A space, tab, CR, LF or other control byte would reach the request
         * line (or split it): refused, not encoded. */
        for (const unsigned char *c = (const unsigned char *)custom_url; *c; c++)
            if (*c <= 0x20 || *c == 0x7F) return -3;
        size_t cl = strlen(custom_url);
        if (cl >= out_max) return -1;
        memcpy(out, custom_url, cl + 1);
        size_t pos = cl;
        char last = custom_url[cl - 1];
        if (last != '?' && last != '&') {
            sep_buf[0] = strchr(custom_url, '?') ? '&' : '?';
            if (pos + 1 >= out_max) { out[0] = '\0'; return -1; }
            out[pos++] = sep_buf[0];
        }
        if (pos + 2 >= out_max) { out[0] = '\0'; return -1; }
        out[pos++] = 'q';
        out[pos++] = '=';
        out[pos] = '\0';
        if (ws_append_encoded(out, out_max, &pos, query) != 0) {
            out[0] = '\0';
            return -1;
        }
        return 0;
    }
    if (force_html || strcmp(provider, "duckduckgo-html") == 0) {
        prefix = "https://html.duckduckgo.com/html/?q=";
    } else {
        prefix = "https://api.duckduckgo.com/?q=";
        suffix = "&format=json&no_html=1&no_redirect=1";
    }
    size_t pl = strlen(prefix), sl = strlen(suffix);
    if (pl >= out_max) return -1;   /* out[0] is still empty here */
    memcpy(out, prefix, pl + 1);
    size_t pos = pl;
    if (ws_append_encoded(out, out_max, &pos, query) != 0 ||
        pos + sl >= out_max) {
        out[0] = '\0';
        return -1;
    }
    memcpy(out + pos, suffix, sl + 1);
    return 0;
}

/* ---- ddg_parse_api_json -------------------------------------------------- */

int ddg_parse_api_json(const char *json_response, size_t json_len,
                       int max_results, char *out_buf, size_t out_max)
{
    if (!json_response || json_len == 0 || !out_buf || out_max == 0)
        return -1;

    /* NUL-terminate a copy for the JSON parser */
    char *json_copy = malloc(json_len + 1);
    if (!json_copy) return -1;
    memcpy(json_copy, json_response, json_len);
    json_copy[json_len] = '\0';

    JsonNode *root = json_parse(json_copy);
    free(json_copy);
    if (!root) return -1;

    const char *type = json_obj_str(root, "Type");
    if (!type) {
        json_free(root);
        return -1;
    }

    /* Empty type means no structured result — caller should fall back */
    if (type[0] == '\0') {
        json_free(root);
        return 0;
    }

    int count = 0;
    size_t out_pos = 0;
    out_buf[0] = '\0';

    if (type[0] == 'A') {
        /* Article type: emit Heading + AbstractText + AbstractURL as first result */
        const char *heading  = json_obj_str(root, "Heading");
        const char *abstract = json_obj_str(root, "AbstractText");
        const char *abs_url  = json_obj_str(root, "AbstractURL");

        if (heading && heading[0] != '\0' && count < max_results) {
            size_t new_pos = append_result(out_buf, out_max, out_pos,
                                            count + 1, heading,
                                            abs_url  ? abs_url  : "",
                                            abstract ? abstract : "");
            if (new_pos > 0) {
                out_pos = new_pos;
                count++;
            }
        }
    }

    /* RelatedTopics array — used for both "A" and "D" types */
    JsonNode *topics = json_obj_get(root, "RelatedTopics");
    if (topics && topics->type == JSON_ARRAY) {
        for (int i = 0; i < (int)topics->as.arr.size && count < max_results; i++) {
            JsonNode *item = (JsonNode *)topics->as.arr.data[i];
            if (!item || item->type != JSON_OBJECT) continue;

            const char *text    = json_obj_str(item, "Text");
            const char *first_u = json_obj_str(item, "FirstURL");

            if (!text || text[0] == '\0') continue;

            /* Title is up to the first ' - ' separator in Text */
            char title[256];
            const char *dash = strstr(text, " - ");
            if (dash) {
                size_t tlen = (size_t)(dash - text);
                if (tlen >= sizeof(title)) tlen = sizeof(title) - 1;
                memcpy(title, text, tlen);
                title[tlen] = '\0';
                text = dash + 3; /* snippet is after ' - ' */
            } else {
                size_t tlen = strlen(text);
                if (tlen >= sizeof(title)) tlen = sizeof(title) - 1;
                memcpy(title, text, tlen);
                title[tlen] = '\0';
                text = "";
            }

            size_t new_pos = append_result(out_buf, out_max, out_pos,
                                            count + 1, title,
                                            first_u ? first_u : "",
                                            text);
            if (new_pos > 0) {
                out_pos = new_pos;
                count++;
            }
        }
    }

    json_free(root);
    return count;
}

/* ---- ddg_parse_html ------------------------------------------------------ */

/* Find the next occurrence of class_value in html starting at *pos.
 * Updates *pos to start of matching element's content. Returns 1 on found. */
static int find_next_result_div(const char *html, size_t html_len,
                                 size_t *pos,
                                 const char *class_value,
                                 const char **content_out,
                                 size_t *content_len_out)
{
    if (*pos >= html_len) return 0;
    const char *p = html + *pos;
    size_t rem = html_len - *pos;
    size_t clen;
    const char *content = html_find_by_class(p, rem, class_value, &clen);
    if (!content) return 0;

    *content_out    = content;
    *content_len_out = clen;
    /* Advance pos past this result's content */
    *pos = (size_t)(content - html) + clen;
    return 1;
}

int ddg_parse_html(const char *html, size_t html_len,
                   int max_results, char *out_buf, size_t out_max)
{
    if (!html || html_len == 0 || !out_buf || out_max == 0)
        return 0;

    out_buf[0] = '\0';

    /* Detect CAPTCHA */
    size_t dummy_len;
    if (html_find_by_class(html, html_len, "anomaly-modal", &dummy_len)) {
        return 0;
    }

    int count = 0;
    size_t out_pos = 0;
    size_t search_pos = 0;

    while (count < max_results) {
        const char *result_content;
        size_t result_len;

        if (!find_next_result_div(html, html_len, &search_pos,
                                   "web-result", &result_content, &result_len))
            break;

        /* Skip ads — check the opening tag (before content) for result--ad.
         * result_content points right after '>'; scan back to find '<'. */
        {
            const char *tag_start = result_content - 1;
            while (tag_start > html && *tag_start != '<') tag_start--;
            size_t tag_len = (size_t)(result_content - tag_start);
            /* Case-insensitive search for "result--ad" in the opening tag */
            int is_ad = 0;
            for (size_t k = 0; k + 10 <= tag_len; k++) {
                if (strncmp(tag_start + k, "result--ad", 10) == 0) {
                    is_ad = 1;
                    break;
                }
            }
            if (is_ad) continue;
        }

        /* Extract title + URL from result__a anchor */
        size_t title_content_len;
        const char *title_content = html_find_by_class(result_content, result_len,
                                                        "result__a", &title_content_len);

        char title_buf[256] = {0};
        char url_buf[512]   = {0};

        if (title_content) {
            /* Strip any inner tags from title */
            char tmp[256];
            if (title_content_len >= sizeof(tmp))
                title_content_len = sizeof(tmp) - 1;
            memcpy(tmp, title_content, title_content_len);
            tmp[title_content_len] = '\0';
            size_t stripped = html_strip_tags(tmp, title_content_len,
                                               title_buf, sizeof(title_buf));
            title_buf[stripped] = '\0';
            html_decode_entities(title_buf, stripped);

            /* Now find the href of the <a> tag with this class to extract uddg= */
            /* Search backward from title_content to find the opening <a> tag */
            const char *a_search = result_content;
            size_t a_rem = result_len;
            while (a_rem > 0) {
                /* Find class="result__a" */
                const char *cls = NULL;
                for (size_t ci = 0; ci + 9 < a_rem; ci++) {
                    if (strncmp(a_search + ci, "result__a", 9) == 0) {
                        cls = a_search + ci;
                        break;
                    }
                }
                if (!cls) break;

                /* Walk back to find the start of the tag '<a' */
                const char *tag_start = cls;
                while (tag_start > result_content && *tag_start != '<') tag_start--;

                /* Find href= */
                const char *tag_end = cls;
                while (tag_end < result_content + result_len && *tag_end != '>') tag_end++;

                size_t attr_len = (size_t)(tag_end - tag_start);
                const char *href_pos = NULL;
                for (size_t hi = 0; hi + 5 < attr_len; hi++) {
                    if (strncmp(tag_start + hi, "href=", 5) == 0) {
                        href_pos = tag_start + hi + 5;
                        break;
                    }
                }

                if (href_pos && href_pos < tag_end) {
                    char hq = *href_pos;
                    if (hq == '"' || hq == '\'') {
                        href_pos++;
                        const char *href_end = href_pos;
                        while (href_end < tag_end && *href_end != hq) href_end++;
                        size_t href_len = (size_t)(href_end - href_pos);

                        /* Extract uddg= parameter */
                        char href_tmp[512];
                        if (href_len >= sizeof(href_tmp))
                            href_len = sizeof(href_tmp) - 1;
                        memcpy(href_tmp, href_pos, href_len);
                        href_tmp[href_len] = '\0';

                        const char *uddg = strstr(href_tmp, "uddg=");
                        if (uddg) {
                            uddg += 5; /* skip "uddg=" */
                            size_t ulen = strlen(uddg);
                            /* Strip trailing query params */
                            for (size_t ui = 0; ui < ulen; ui++) {
                                if (uddg[ui] == '&') { ulen = ui; break; }
                            }
                            if (ulen >= sizeof(url_buf)) ulen = sizeof(url_buf) - 1;
                            memcpy(url_buf, uddg, ulen);
                            url_buf[ulen] = '\0';
                            html_url_decode(url_buf, ulen);
                        } else {
                            /* Use href directly */
                            size_t copy_len = href_len < sizeof(url_buf) - 1
                                              ? href_len : sizeof(url_buf) - 1;
                            memcpy(url_buf, href_pos, copy_len);
                            url_buf[copy_len] = '\0';
                            html_url_decode(url_buf, copy_len);
                        }
                    }
                }
                break;
            }
        }

        /* Extract snippet from result__snippet */
        char snippet_buf[512] = {0};
        size_t snip_len;
        const char *snip = html_find_by_class(result_content, result_len,
                                               "result__snippet", &snip_len);
        if (snip) {
            /* Strip <b> tags (keep text) */
            char tmp2[512];
            if (snip_len >= sizeof(tmp2)) snip_len = sizeof(tmp2) - 1;
            memcpy(tmp2, snip, snip_len);
            tmp2[snip_len] = '\0';
            size_t stripped = html_strip_tags(tmp2, snip_len,
                                               snippet_buf, sizeof(snippet_buf));
            snippet_buf[stripped] = '\0';
            html_decode_entities(snippet_buf, stripped);
        }

        if (title_buf[0] == '\0' && url_buf[0] == '\0') continue;

        size_t new_pos = append_result(out_buf, out_max, out_pos,
                                        count + 1, title_buf, url_buf,
                                        snippet_buf);
        if (new_pos == 0) break;
        out_pos = new_pos;
        count++;
    }

    return count;
}

/* ---- the WinHTTP request -------------------------------------------------- */

#ifdef _WIN32

/* A search is one small GET: bound every phase, and the whole call. */
#define WS_RESOLVE_MS   10000
#define WS_CONNECT_MS   10000
#define WS_IO_MS        15000   /* send, and each receive */
#define WS_TOTAL_MS     30000   /* the whole request, body included */
#define WS_MAX_BODY     (1024u * 1024u)   /* more is cut, never grown without limit */

#ifndef WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT
#define WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT 7
#endif

static int ws_stopped(volatile int *cancel, const AiNetCancel *nc)
{
    return (cancel && *cancel) || ai_nc_cancelled(nc);
}

static void ws_close_cb(uintptr_t h)
{
    WinHttpCloseHandle((HINTERNET)h);
}

#define WS_CANCELLED (-2)   /* ws_get result: stopped by the flag or the token */

/* One GET of `url` over https. 0 with a malloc'd, NUL-terminated *body (at
 * most WS_MAX_BODY bytes), or -1 (WS_CANCELLED when stopped) with *err a static text. The request handle
 * is armed on `nc` while the calls run, so the UI thread can close it and
 * wake a blocked call; after any failed call the token is read before the
 * handle is touched again, and the disarm decides who closes it. */
static int ws_get(const char *url, volatile int *cancel, AiNetCancel *nc,
                  char **body, size_t *body_len, const char **err)
{
    *body = NULL;
    *body_len = 0;
    *err = "HTTP request failed";
    if (ws_stopped(cancel, nc)) { *err = "Search cancelled"; return WS_CANCELLED; }

    wchar_t w_url[WS_URL_MAX];
    if (MultiByteToWideChar(CP_UTF8, 0, url, -1, w_url, WS_URL_MAX) <= 0) {
        *err = "Failed to parse search URL";
        return -1;
    }
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t w_host[256] = {0};
    wchar_t w_path[WS_URL_MAX] = {0};   /* as long as the URL: no path is too long to crack */
    uc.lpszHostName     = w_host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath      = w_path;
    uc.dwUrlPathLength  = WS_URL_MAX;
    if (!WinHttpCrackUrl(w_url, 0, 0, &uc)) {
        *err = "Failed to parse search URL";
        return -1;
    }

    HINTERNET session =
        ns_winhttp_open(L"Nutshell/1.0 (Windows NT 10.0; Win64; x64)");
    if (!session) {
        *err = (GetLastError() == NS_ERR_TLS_UNAVAILABLE)
            ? "A secure connection could not be set up: TLS 1.2 or newer is "
              "not available on this system, so nothing was sent"
            : "WinHttpOpen failed";
        return -1;
    }
    (void)WinHttpSetTimeouts(session, WS_RESOLVE_MS, WS_CONNECT_MS,
                             WS_IO_MS, WS_IO_MS);

    HINTERNET conn = NULL, req = NULL;
    int armed = 0;
    char *buf = NULL;
    size_t len = 0, cap = 0;
    int ok = 0;
    unsigned long long end = (unsigned long long)GetTickCount64() + WS_TOTAL_MS;

    DWORD port = (uc.nPort == 0) ? INTERNET_DEFAULT_HTTPS_PORT : (DWORD)uc.nPort;
    conn = WinHttpConnect(session, w_host, (INTERNET_PORT)port, 0);
    if (!conn) { *err = "WinHttpConnect failed"; goto done; }

    req = WinHttpOpenRequest(conn, L"GET", w_path, NULL, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) { *err = "WinHttpOpenRequest failed"; goto done; }
    if (ai_nc_arm(nc, (uintptr_t)req, ws_close_cb) != 0) goto done;   /* fired */
    armed = nc != NULL;

    DWORD resp_ms = WS_IO_MS;   /* the wait for the response headers */
    (void)WinHttpSetOption(req, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT,
                           &resp_ms, sizeof(resp_ms));

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        goto done;
    if (ws_stopped(cancel, nc)) goto done;
    if (!WinHttpReceiveResponse(req, NULL)) goto done;

    for (;;) {
        if (ws_stopped(cancel, nc)) goto done;
        if ((unsigned long long)GetTickCount64() >= end) goto done;
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) goto done;
        if (avail == 0) break;
        size_t room = WS_MAX_BODY - len;
        if ((size_t)avail > room) avail = (DWORD)room;
        if (avail == 0) break;   /* capped: what arrived is parsed */
        if (len + (size_t)avail + 1 > cap) {
            size_t ncap = (len + (size_t)avail + 1) * 2;
            if (ncap < 65536) ncap = 65536;
            char *tmp = (char *)realloc(buf, ncap);
            if (!tmp) goto done;
            buf = tmp;
            cap = ncap;
        }
        DWORD got = 0;
        if (!WinHttpReadData(req, buf + len, avail, &got)) goto done;   /* not a short body */
        if (got == 0) break;
        len += got;
    }
    if (!buf) {
        buf = (char *)malloc(1);
        if (!buf) goto done;
    }
    buf[len] = '\0';
    ok = 1;

done:
    if (req && (!armed || ai_nc_disarm(nc))) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    if (ws_stopped(cancel, nc)) {
        free(buf);
        *err = "Search cancelled";
        return WS_CANCELLED;
    }
    if (!ok) {
        free(buf);
        return -1;
    }
    *body = buf;
    *body_len = len;
    return 0;
}

#endif /* _WIN32 */

/* ---- tool_web_search_execute --------------------------------------------- */

int tool_web_search_execute(const char *input_json, void *tool_data,
                            volatile int *cancel_flag,
                            char **result_buf, size_t *result_len,
                            int *was_truncated)
{
    if (!input_json || !result_buf || !result_len || !was_truncated)
        return -1;

    *was_truncated = 0;
    *result_buf = NULL;
    *result_len = 0;

    /* Cancelled before the first request (the flag a Stop sets, or the
     * stream's token): nothing goes out, on every platform. */
    {
        const WebSearchContext *cctx = (const WebSearchContext *)tool_data;
        if ((cancel_flag && *cancel_flag) || (cctx && ai_nc_cancelled(cctx->nc))) {
            const char *err = "Search cancelled";
            *result_buf = malloc(strlen(err) + 1);
            if (!*result_buf) return -1;
            strcpy(*result_buf, err);
            *result_len = strlen(err);
            return -1;
        }
    }

    /* Parse query from input JSON */
    JsonNode *input = json_parse(input_json);
    if (!input) {
        const char *err = "Invalid tool input JSON";
        *result_buf = malloc(strlen(err) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, err);
        *result_len = strlen(err);
        return -1;
    }

    const char *query = json_obj_str(input, "query");
    {
        /* A \u0000 in the query would shorten it at the NUL and a different
         * query would be searched: refuse it. */
        const JsonNode *qn = json_obj_get(input, "query");
        if (query && qn && qn->str_len != strlen(query)) {
            json_free(input);
            const char *err = "The search query contains a character that cannot be sent";
            *result_buf = malloc(strlen(err) + 1);
            if (!*result_buf) return -1;
            strcpy(*result_buf, err);
            *result_len = strlen(err);
            return -1;
        }
    }
    if (!query || query[0] == '\0') {
        json_free(input);
        const char *err = "Missing required parameter: query";
        *result_buf = malloc(strlen(err) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, err);
        *result_len = strlen(err);
        return -1;
    }

    /* Get context */
    WebSearchContext *ctx = (WebSearchContext *)tool_data;
    int max_results = (ctx && ctx->max_search_results > 0)
                      ? ctx->max_search_results : 7;
    const char *provider = (ctx && ctx->search_provider[0] != '\0')
                           ? ctx->search_provider : "duckduckgo-api";

    char query_copy[1024];
    size_t qlen = strlen(query);
    if (qlen >= sizeof(query_copy)) {
        /* Never cut a query short: a different query would be searched. */
        json_free(input);
        const char *err = "Search query is too long for a search URL";
        *result_buf = malloc(strlen(err) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, err);
        *result_len = strlen(err);
        return -1;
    }
    memcpy(query_copy, query, qlen);
    query_copy[qlen] = '\0';

    json_free(input);

#ifdef _WIN32
    /* ---- Windows WinHTTP implementation ---------------------------------- */

    (void)provider; /* used below in URL construction */

    /* Build DDG API URL */
    char url[WS_URL_MAX];
    int url_n = ws_build_search_url(provider,
                                    ctx ? ctx->search_url : NULL, 0,
                                    query_copy, url, sizeof(url));
    if (url_n != 0) {
        const char *err = ws_url_error_text(url_n);
        *result_buf = malloc(strlen(err) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, err);
        *result_len = strlen(err);
        return -1;
    }

    AiNetCancel *nc = ctx ? ctx->nc : NULL;
    char  *resp_buf = NULL;
    size_t resp_len = 0;
    const char *gerr = NULL;
    if (ws_get(url, cancel_flag, nc, &resp_buf, &resp_len, &gerr) != 0) {
        *result_buf = malloc(strlen(gerr) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, gerr);
        *result_len = strlen(gerr);
        return -1;
    }

    /* Parse results */
    size_t out_max = 16384;
    char *out = malloc(out_max);
    if (!out) { free(resp_buf); return -1; }

    int n;
    if (strcmp(provider, "duckduckgo-html") == 0) {
        n = ddg_parse_html(resp_buf, resp_len, max_results, out, out_max);
    } else {
        n = ddg_parse_api_json(resp_buf, resp_len, max_results, out, out_max);
        if (n == 0) {
            /* API returned no results -- fall back to the HTML endpoint */
            free(out);
            free(resp_buf);

            char html_url[WS_URL_MAX];
            int html_n = ws_build_search_url(provider, NULL, 1, query_copy,
                                             html_url, sizeof(html_url));
            if (html_n != 0) {
                const char *err = "Search query is too long for a search URL";
                *result_buf = malloc(strlen(err) + 1);
                if (!*result_buf) return -1;
                strcpy(*result_buf, err);
                *result_len = strlen(err);
                return -1;
            }

            char  *html_buf = NULL;
            size_t html_len = 0;
            const char *herr = NULL;
            int hrc = ws_get(html_url, cancel_flag, nc, &html_buf, &html_len, &herr);
            if (hrc != 0) {
                /* A cancel is a cancel; any other failure of the fallback
                 * reads, as it always did, as an empty search. */
                const char *msg = (hrc == WS_CANCELLED) ? herr : "No results found.";
                *result_buf = malloc(strlen(msg) + 1);
                if (!*result_buf) return -1;
                strcpy(*result_buf, msg);
                *result_len = strlen(msg);
                return hrc == WS_CANCELLED ? -1 : 0;
            }

            out = malloc(out_max);
            if (!out) { free(html_buf); return -1; }

            n = ddg_parse_html(html_buf, html_len, max_results, out, out_max);
            free(html_buf);

            if (n <= 0) {
                strcpy(out, "No results found.");
            }

            *result_buf = out;
            *result_len = strlen(out);
            return 0;
        }
    }

    free(resp_buf);

    if (n < 0) {
        free(out);
        const char *err = "Failed to parse search response";
        *result_buf = malloc(strlen(err) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, err);
        *result_len = strlen(err);
        return -1;
    }

    if (n == 0) {
        strcpy(out, "No results found.");
    }

    *result_buf = out;
    *result_len = strlen(out);
    return 0;

#else
    /* Non-Windows: HTTP not available */
    (void)provider;
    (void)max_results;
    (void)query_copy;

    const char *err = "Web search not available (non-Windows build)";
    *result_buf = malloc(strlen(err) + 1);
    if (!*result_buf) return -1;
    strcpy(*result_buf, err);
    *result_len = strlen(err);
    return -1;
#endif
}
