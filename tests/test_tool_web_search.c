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
