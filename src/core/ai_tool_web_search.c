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

/* ---- tool_web_search_execute --------------------------------------------- */

int tool_web_search_execute(const char *input_json, void *tool_data,
                            volatile int *cancel_flag,
                            char **result_buf, size_t *result_len,
                            int *was_truncated)
{
    (void)cancel_flag;

    if (!input_json || !result_buf || !result_len || !was_truncated)
        return -1;

    *was_truncated = 0;

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
    if (qlen >= sizeof(query_copy)) qlen = sizeof(query_copy) - 1;
    memcpy(query_copy, query, qlen);
    query_copy[qlen] = '\0';

    json_free(input);

#ifdef _WIN32
    /* ---- Windows WinHTTP implementation ---------------------------------- */

    (void)provider; /* used below in URL construction */

    /* Build DDG API URL */
    char url[1024];
    if (ctx && strcmp(provider, "custom") == 0 && ctx->search_url[0] != '\0') {
        snprintf(url, sizeof(url), "%s?q=%s", ctx->search_url, query_copy);
    } else if (strcmp(provider, "duckduckgo-html") == 0) {
        snprintf(url, sizeof(url),
                 "https://html.duckduckgo.com/html/?q=%s", query_copy);
    } else {
        snprintf(url, sizeof(url),
                 "https://api.duckduckgo.com/?q=%s&format=json&no_html=1&no_redirect=1",
                 query_copy);
    }

    /* Convert URL to wide string */
    wchar_t w_url[1024];
    MultiByteToWideChar(CP_UTF8, 0, url, -1, w_url, 1024);

    /* Parse URL components */
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t w_host[256] = {0};
    wchar_t w_path[768] = {0};
    uc.lpszHostName    = w_host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath     = w_path;
    uc.dwUrlPathLength = 768;

    if (!WinHttpCrackUrl(w_url, 0, 0, &uc)) {
        const char *err = "Failed to parse search URL";
        *result_buf = malloc(strlen(err) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, err);
        *result_len = strlen(err);
        return -1;
    }

    HINTERNET session = WinHttpOpen(
        L"Nutshell/1.0 (Windows NT 10.0; Win64; x64)",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        const char *err = "WinHttpOpen failed";
        *result_buf = malloc(strlen(err) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, err);
        *result_len = strlen(err);
        return -1;
    }

    DWORD port = (uc.nPort == 0) ? INTERNET_DEFAULT_HTTPS_PORT : (DWORD)uc.nPort;
    HINTERNET conn = WinHttpConnect(session, w_host, (INTERNET_PORT)port, 0);
    if (!conn) {
        WinHttpCloseHandle(session);
        const char *err = "WinHttpConnect failed";
        *result_buf = malloc(strlen(err) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, err);
        *result_len = strlen(err);
        return -1;
    }

    HINTERNET req = WinHttpOpenRequest(
        conn, L"GET", w_path, NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!req) {
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        const char *err = "WinHttpOpenRequest failed";
        *result_buf = malloc(strlen(err) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, err);
        *result_len = strlen(err);
        return -1;
    }

    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, NULL)) {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        const char *err = "HTTP request failed";
        *result_buf = malloc(strlen(err) + 1);
        if (!*result_buf) return -1;
        strcpy(*result_buf, err);
        *result_len = strlen(err);
        return -1;
    }

    /* Read response body */
    size_t resp_cap = 65536;
    size_t resp_len = 0;
    char *resp_buf  = malloc(resp_cap);
    if (!resp_buf) {
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return -1;
    }

    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
        if (resp_len + avail + 1 > resp_cap) {
            resp_cap = (resp_len + avail + 1) * 2;
            char *new_buf = realloc(resp_buf, resp_cap);
            if (!new_buf) { free(resp_buf); resp_buf = NULL; break; }
            resp_buf = new_buf;
        }
        DWORD read = 0;
        WinHttpReadData(req, resp_buf + resp_len, avail, &read);
        resp_len += read;
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);

    if (!resp_buf) return -1;
    resp_buf[resp_len] = '\0';

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
            /* API returned no results — fall back to HTML endpoint */
            free(out);
            free(resp_buf);

            char html_url[1024];
            snprintf(html_url, sizeof(html_url),
                     "https://html.duckduckgo.com/html/?q=%s", query_copy);

            wchar_t w_html_url[1024];
            MultiByteToWideChar(CP_UTF8, 0, html_url, -1, w_html_url, 1024);

            URL_COMPONENTS uc2;
            memset(&uc2, 0, sizeof(uc2));
            uc2.dwStructSize = sizeof(uc2);
            wchar_t w_host2[256] = {0};
            wchar_t w_path2[768] = {0};
            uc2.lpszHostName    = w_host2;
            uc2.dwHostNameLength = 256;
            uc2.lpszUrlPath     = w_path2;
            uc2.dwUrlPathLength = 768;

            if (!WinHttpCrackUrl(w_html_url, 0, 0, &uc2)) {
                out = malloc(64);
                if (!out) return -1;
                strcpy(out, "No results found.");
                *result_buf = out;
                *result_len = strlen(out);
                return 0;
            }

            HINTERNET session2 = WinHttpOpen(
                L"Nutshell/1.0 (Windows NT 10.0; Win64; x64)",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!session2) {
                out = malloc(64);
                if (!out) return -1;
                strcpy(out, "No results found.");
                *result_buf = out;
                *result_len = strlen(out);
                return 0;
            }

            DWORD port2 = (uc2.nPort == 0) ? INTERNET_DEFAULT_HTTPS_PORT
                                            : (DWORD)uc2.nPort;
            HINTERNET conn2 = WinHttpConnect(session2, w_host2,
                                              (INTERNET_PORT)port2, 0);
            if (!conn2) {
                WinHttpCloseHandle(session2);
                out = malloc(64);
                if (!out) return -1;
                strcpy(out, "No results found.");
                *result_buf = out;
                *result_len = strlen(out);
                return 0;
            }

            HINTERNET req2 = WinHttpOpenRequest(
                conn2, L"GET", w_path2, NULL,
                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                WINHTTP_FLAG_SECURE);
            if (!req2) {
                WinHttpCloseHandle(conn2);
                WinHttpCloseHandle(session2);
                out = malloc(64);
                if (!out) return -1;
                strcpy(out, "No results found.");
                *result_buf = out;
                *result_len = strlen(out);
                return 0;
            }

            if (!WinHttpSendRequest(req2, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
                !WinHttpReceiveResponse(req2, NULL)) {
                WinHttpCloseHandle(req2);
                WinHttpCloseHandle(conn2);
                WinHttpCloseHandle(session2);
                out = malloc(64);
                if (!out) return -1;
                strcpy(out, "No results found.");
                *result_buf = out;
                *result_len = strlen(out);
                return 0;
            }

            /* Read HTML response */
            size_t html_cap = 131072;
            size_t html_len = 0;
            char *html_buf = malloc(html_cap);
            if (!html_buf) {
                WinHttpCloseHandle(req2);
                WinHttpCloseHandle(conn2);
                WinHttpCloseHandle(session2);
                return -1;
            }

            DWORD avail2 = 0;
            while (WinHttpQueryDataAvailable(req2, &avail2) && avail2 > 0) {
                if (html_len + avail2 + 1 > html_cap) {
                    html_cap = (html_len + avail2 + 1) * 2;
                    char *new_buf = realloc(html_buf, html_cap);
                    if (!new_buf) { free(html_buf); html_buf = NULL; break; }
                    html_buf = new_buf;
                }
                DWORD read2 = 0;
                WinHttpReadData(req2, html_buf + html_len, avail2, &read2);
                html_len += read2;
            }

            WinHttpCloseHandle(req2);
            WinHttpCloseHandle(conn2);
            WinHttpCloseHandle(session2);

            if (!html_buf) return -1;
            html_buf[html_len] = '\0';

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
