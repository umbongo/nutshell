#include "test_framework.h"
#include "ai_tool_web_search.h"
#include <string.h>

/* ---- ddg_parse_api_json tests -------------------------------------------- */

int test_ddg_api_article(void)
{
    TEST_BEGIN();
    const char *json =
        "{"
        "  \"Type\": \"A\","
        "  \"Heading\": \"Linux\","
        "  \"AbstractText\": \"Linux is a family of open-source operating systems.\","
        "  \"AbstractURL\": \"https://en.wikipedia.org/wiki/Linux\","
        "  \"RelatedTopics\": ["
        "    {"
        "      \"Text\": \"Linux kernel - The core of the Linux OS\","
        "      \"FirstURL\": \"https://en.wikipedia.org/wiki/Linux_kernel\""
        "    },"
        "    {"
        "      \"Text\": \"GNU/Linux naming - Naming controversy\","
        "      \"FirstURL\": \"https://en.wikipedia.org/wiki/GNU/Linux_naming_controversy\""
        "    }"
        "  ]"
        "}";
    char out[4096];
    int n = ddg_parse_api_json(json, strlen(json), 7, out, sizeof(out));
    ASSERT_TRUE(n >= 1);
    ASSERT_TRUE(strstr(out, "Linux") != NULL);
    ASSERT_TRUE(strstr(out, "Wikipedia") != NULL ||
                strstr(out, "wikipedia.org") != NULL);
    TEST_END();
}

int test_ddg_api_disambiguation(void)
{
    TEST_BEGIN();
    const char *json =
        "{"
        "  \"Type\": \"D\","
        "  \"Heading\": \"Java\","
        "  \"AbstractText\": \"\","
        "  \"AbstractURL\": \"\","
        "  \"RelatedTopics\": ["
        "    {"
        "      \"Text\": \"Java (programming language) - A general-purpose language\","
        "      \"FirstURL\": \"https://en.wikipedia.org/wiki/Java_(programming_language)\""
        "    },"
        "    {"
        "      \"Text\": \"Java (island) - An island of Indonesia\","
        "      \"FirstURL\": \"https://en.wikipedia.org/wiki/Java\""
        "    }"
        "  ]"
        "}";
    char out[4096];
    int n = ddg_parse_api_json(json, strlen(json), 7, out, sizeof(out));
    ASSERT_TRUE(n >= 2);
    ASSERT_TRUE(strstr(out, "Java") != NULL);
    ASSERT_TRUE(strstr(out, "programming language") != NULL ||
                strstr(out, "island") != NULL);
    TEST_END();
}

int test_ddg_api_empty(void)
{
    TEST_BEGIN();
    const char *json =
        "{"
        "  \"Type\": \"\","
        "  \"Heading\": \"\","
        "  \"AbstractText\": \"\","
        "  \"AbstractURL\": \"\","
        "  \"RelatedTopics\": []"
        "}";
    char out[4096];
    int n = ddg_parse_api_json(json, strlen(json), 7, out, sizeof(out));
    ASSERT_EQ(n, 0);
    TEST_END();
}

int test_ddg_api_invalid(void)
{
    TEST_BEGIN();
    const char *json = "not valid json {{{";
    char out[4096];
    int n = ddg_parse_api_json(json, strlen(json), 7, out, sizeof(out));
    ASSERT_EQ(n, -1);
    TEST_END();
}

/* ---- ddg_parse_html tests ------------------------------------------------ */

/* Minimal DDG HTML lite structure */
static const char DDG_HTML_TWO_RESULTS[] =
    "<html><body>"
    "<div class=\"web-result\">"
    "  <div class=\"result__body\">"
    "    <a class=\"result__a\" href=\"/l/?uddg=https%3A%2F%2Fexample.com%2Fpage1\">First Result</a>"
    "    <div class=\"result__snippet\">A snippet about the <b>first</b> result.</div>"
    "  </div>"
    "</div>"
    "<div class=\"web-result\">"
    "  <div class=\"result__body\">"
    "    <a class=\"result__a\" href=\"/l/?uddg=https%3A%2F%2Fexample.com%2Fpage2\">Second Result</a>"
    "    <div class=\"result__snippet\">A snippet about the <b>second</b> result.</div>"
    "  </div>"
    "</div>"
    "</body></html>";

int test_ddg_html_organic(void)
{
    TEST_BEGIN();
    char out[4096];
    int n = ddg_parse_html(DDG_HTML_TWO_RESULTS, strlen(DDG_HTML_TWO_RESULTS),
                            7, out, sizeof(out));
    ASSERT_TRUE(n >= 1);
    ASSERT_TRUE(strstr(out, "Result") != NULL);
    TEST_END();
}

static const char DDG_HTML_WITH_AD[] =
    "<html><body>"
    "<div class=\"web-result result--ad\">"
    "  <div class=\"result__body\">"
    "    <a class=\"result__a\" href=\"/l/?uddg=https%3A%2F%2Fad.example.com\">Ad Result</a>"
    "    <div class=\"result__snippet\">This is an ad.</div>"
    "  </div>"
    "</div>"
    "<div class=\"web-result\">"
    "  <div class=\"result__body\">"
    "    <a class=\"result__a\" href=\"/l/?uddg=https%3A%2F%2Fexample.com%2Forganic\">Organic Result</a>"
    "    <div class=\"result__snippet\">This is organic.</div>"
    "  </div>"
    "</div>"
    "</body></html>";

int test_ddg_html_ad_filtered(void)
{
    TEST_BEGIN();
    char out[4096];
    int n = ddg_parse_html(DDG_HTML_WITH_AD, strlen(DDG_HTML_WITH_AD),
                            7, out, sizeof(out));
    /* Should have at least the organic result */
    ASSERT_TRUE(n >= 0);
    /* Ad should be skipped */
    ASSERT_TRUE(strstr(out, "ad.example.com") == NULL);
    TEST_END();
}

static const char DDG_HTML_CAPTCHA[] =
    "<html><body>"
    "<div class=\"anomaly-modal\">"
    "  <p>Please verify you are a human.</p>"
    "</div>"
    "</body></html>";

int test_ddg_html_captcha(void)
{
    TEST_BEGIN();
    char out[4096];
    int n = ddg_parse_html(DDG_HTML_CAPTCHA, strlen(DDG_HTML_CAPTCHA),
                            7, out, sizeof(out));
    ASSERT_EQ(n, 0);
    TEST_END();
}

static const char DDG_HTML_NO_RESULTS[] =
    "<html><body>"
    "<div class=\"no-results\">"
    "  <p>No results found.</p>"
    "</div>"
    "</body></html>";

int test_ddg_html_no_results(void)
{
    TEST_BEGIN();
    char out[4096];
    int n = ddg_parse_html(DDG_HTML_NO_RESULTS, strlen(DDG_HTML_NO_RESULTS),
                            7, out, sizeof(out));
    ASSERT_EQ(n, 0);
    TEST_END();
}

/* ---- ws_build_search_url tests ------------------------------------------- */

#define WS_API "https://api.duckduckgo.com/?q="
#define WS_API_TAIL "&format=json&no_html=1&no_redirect=1"

static int ws_url_is(const char *provider, const char *custom, int force,
                     const char *query, const char *want)
{
    char url[1024];
    if (ws_build_search_url(provider, custom, force, query, url, sizeof(url)) != 0)
        return 0;
    return strcmp(url, want) == 0;
}

int test_ws_url_plain_and_space(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(ws_url_is("duckduckgo-api", NULL, 0, "a-b._~Z9",
                          WS_API "a-b._~Z9" WS_API_TAIL));
    ASSERT_TRUE(ws_url_is("duckduckgo-api", NULL, 0, "red fox",
                          WS_API "red%20fox" WS_API_TAIL));
    ASSERT_TRUE(ws_url_is("duckduckgo-html", NULL, 0, "red fox",
                          "https://html.duckduckgo.com/html/?q=red%20fox"));
    ASSERT_TRUE(ws_url_is("duckduckgo-api", NULL, 1, "red fox",
                          "https://html.duckduckgo.com/html/?q=red%20fox"));
    TEST_END();
}

int test_ws_url_reserved_bytes_cannot_inject(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(ws_url_is("duckduckgo-html", NULL, 0, "a&b=c#d?e/f+g\"h",
        "https://html.duckduckgo.com/html/?q=a%26b%3Dc%23d%3Fe%2Ff%2Bg%22h"));
    /* An already-encoded %20 is encoded again, not decoded. */
    ASSERT_TRUE(ws_url_is("duckduckgo-html", NULL, 0, "x%20y",
        "https://html.duckduckgo.com/html/?q=x%2520y"));
    /* Control bytes (no NUL) cannot add a header line. */
    ASSERT_TRUE(ws_url_is("duckduckgo-html", NULL, 0, "a\r\nHost: x\t\x01\x7f",
        "https://html.duckduckgo.com/html/?q=a%0D%0AHost%3A%20x%09%01%7F"));
    /* Scheme, host and path stay out of reach. */
    ASSERT_TRUE(ws_url_is("duckduckgo-api", NULL, 0, "@evil.example/../x:80",
        WS_API "%40evil.example%2F..%2Fx%3A80" WS_API_TAIL));
    TEST_END();
}

int test_ws_url_utf8_bytes(void)
{
    TEST_BEGIN();
    /* U+1F600 (4 bytes) and U+00E9 (2 bytes), upper-case hex. */
    ASSERT_TRUE(ws_url_is("duckduckgo-html", NULL, 0, "\xF0\x9F\x98\x80 caf\xC3\xA9",
        "https://html.duckduckgo.com/html/?q=%F0%9F%98%80%20caf%C3%A9"));
    TEST_END();
}

int test_ws_url_empty_query(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(ws_url_is("duckduckgo-html", NULL, 0, "",
                          "https://html.duckduckgo.com/html/?q="));
    ASSERT_TRUE(ws_url_is("duckduckgo-api", NULL, 0, NULL,
                          WS_API WS_API_TAIL));
    TEST_END();
}

int test_ws_url_custom_separator(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(ws_url_is("custom", "https://search.example.com/s", 0, "a b",
                          "https://search.example.com/s?q=a%20b"));
    ASSERT_TRUE(ws_url_is("custom", "https://search.example.com/s?lang=en", 0, "a&b",
                          "https://search.example.com/s?lang=en&q=a%26b"));
    ASSERT_TRUE(ws_url_is("custom", "https://search.example.com/s?", 0, "x",
                          "https://search.example.com/s?q=x"));
    /* Empty custom URL falls back to the API; the HTML fallback ignores it. */
    ASSERT_TRUE(ws_url_is("custom", "", 0, "x", WS_API "x" WS_API_TAIL));
    ASSERT_TRUE(ws_url_is("custom", "https://search.example.com/s", 1, "x",
                          "https://html.duckduckgo.com/html/?q=x"));
    TEST_END();
}

int test_ws_url_custom_fragment_refused(void)
{
    TEST_BEGIN();
    char url[1024];
    ASSERT_EQ(ws_build_search_url("custom", "https://search.example.com/s#top", 0,
                                  "x", url, sizeof(url)), -2);
    ASSERT_EQ((int)url[0], 0);
    TEST_END();
}

int test_ws_url_too_long_is_refused_not_truncated(void)
{
    TEST_BEGIN();
    char url[1024];
    char q[400];
    /* 330 spaces encode to 990 bytes: with prefix and tail it does not fit 1024
     * yet 330 plain bytes would. */
    memset(q, ' ', 330); q[330] = '\0';
    ASSERT_EQ(ws_build_search_url("duckduckgo-api", NULL, 0, q, url, sizeof(url)), -1);
    ASSERT_EQ((int)url[0], 0);
    memset(q, ' ', 400); q[399] = '\0';
    ASSERT_EQ(ws_build_search_url("duckduckgo-html", NULL, 0, q, url, sizeof(url)), -1);
    ASSERT_EQ((int)url[0], 0);
    /* Exact fit boundary: buffer of strlen(want)+1 works, one less refuses. */
    const char *want = "https://html.duckduckgo.com/html/?q=a%20b";
    char exact[64];
    ASSERT_EQ(ws_build_search_url("duckduckgo-html", NULL, 0, "a b", exact,
                                  strlen(want) + 1), 0);
    ASSERT_STR_EQ(exact, want);
    ASSERT_EQ(ws_build_search_url("duckduckgo-html", NULL, 0, "a b", exact,
                                  strlen(want)), -1);
    TEST_END();
}
