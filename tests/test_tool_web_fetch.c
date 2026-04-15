#include "test_framework.h"
#include "ai_tool_web_fetch.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ---- web_fetch_html_to_text tests ---------------------------------------- */

int test_web_fetch_html_to_text_basic(void)
{
    TEST_BEGIN();
    const char *html = "<html><body><h1>Title</h1><p>Hello world.</p></body></html>";
    char   *buf = NULL;
    size_t  len = 0;
    int     trunc = 0;

    int rc = web_fetch_html_to_text(html, strlen(html), &buf, &len, &trunc);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(buf);
    ASSERT_TRUE(strstr(buf, "Title") != NULL);
    ASSERT_TRUE(strstr(buf, "Hello world.") != NULL);
    /* No HTML tags should remain */
    ASSERT_TRUE(strstr(buf, "<") == NULL);
    ASSERT_TRUE(strstr(buf, ">") == NULL);
    ASSERT_EQ(trunc, 0);
    free(buf);
    TEST_END();
}

int test_web_fetch_html_to_text_script_removed(void)
{
    TEST_BEGIN();
    const char *html =
        "<html><body><script>var x=1;</script><p>Content</p></body></html>";
    char   *buf = NULL;
    size_t  len = 0;
    int     trunc = 0;

    int rc = web_fetch_html_to_text(html, strlen(html), &buf, &len, &trunc);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(buf);
    ASSERT_TRUE(strstr(buf, "Content") != NULL);
    /* Script content must not appear */
    ASSERT_TRUE(strstr(buf, "var x=1") == NULL);
    ASSERT_EQ(trunc, 0);
    free(buf);
    TEST_END();
}

int test_web_fetch_html_to_text_entities(void)
{
    TEST_BEGIN();
    const char *html = "<p>Tom &amp; Jerry &lt;3</p>";
    char   *buf = NULL;
    size_t  len = 0;
    int     trunc = 0;

    int rc = web_fetch_html_to_text(html, strlen(html), &buf, &len, &trunc);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(buf);
    ASSERT_TRUE(strstr(buf, "Tom & Jerry") != NULL);
    ASSERT_TRUE(strstr(buf, "<3") != NULL);
    ASSERT_EQ(trunc, 0);
    free(buf);
    TEST_END();
}

int test_web_fetch_html_to_text_whitespace(void)
{
    TEST_BEGIN();
    /* Lots of extra spaces and a newline between tags */
    const char *html = "<p>  Hello   world  </p>\n\n\n\n<p>Second</p>";
    char   *buf = NULL;
    size_t  len = 0;
    int     trunc = 0;

    int rc = web_fetch_html_to_text(html, strlen(html), &buf, &len, &trunc);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(buf);
    /* No run of multiple spaces */
    ASSERT_TRUE(strstr(buf, "  ") == NULL);
    /* 3+ consecutive newlines should be collapsed to 2 */
    ASSERT_TRUE(strstr(buf, "\n\n\n") == NULL);
    /* Content should be present */
    ASSERT_TRUE(strstr(buf, "Hello") != NULL);
    ASSERT_TRUE(strstr(buf, "world") != NULL);
    ASSERT_EQ(trunc, 0);
    free(buf);
    TEST_END();
}

int test_web_fetch_html_to_text_truncation(void)
{
    TEST_BEGIN();
    /* Build a string larger than AI_TOOL_RESULT_MAX (1MB).
     * We create 1.1MB of plain text content inside a <p> tag. */
    size_t content_size = (size_t)(AI_TOOL_RESULT_MAX) + (size_t)(AI_TOOL_RESULT_MAX / 10);
    /* Allocate: "<p>" + content + "</p>" + NUL */
    size_t html_size = content_size + 8;
    char *html = malloc(html_size);
    ASSERT_NOT_NULL(html);

    /* Fill with "<p>" + 'A' * content_size + "</p>" */
    html[0] = '<'; html[1] = 'p'; html[2] = '>';
    memset(html + 3, 'A', content_size);
    html[3 + content_size] = '<'; html[4 + content_size] = '/';
    html[5 + content_size] = 'p'; html[6 + content_size] = '>';
    html[7 + content_size] = '\0';

    char   *buf = NULL;
    size_t  len = 0;
    int     trunc = 0;

    int rc = web_fetch_html_to_text(html, 7 + content_size, &buf, &len, &trunc);
    free(html);

    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(buf);
    ASSERT_EQ((int)len, AI_TOOL_RESULT_MAX);
    ASSERT_EQ(trunc, 1);
    free(buf);
    TEST_END();
}

int test_web_fetch_html_to_text_empty(void)
{
    TEST_BEGIN();
    char   *buf = NULL;
    size_t  len = 0;
    int     trunc = 0;

    int rc = web_fetch_html_to_text("", 0, &buf, &len, &trunc);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(buf);
    ASSERT_EQ((int)len, 0);
    ASSERT_EQ(trunc, 0);
    free(buf);
    TEST_END();
}

/* ---- tool_web_fetch_execute URL validation test -------------------------- */

int test_web_fetch_url_validation(void)
{
    TEST_BEGIN();
    const char *input = "{\"url\":\"ftp://example.com\"}";
    char   *buf = NULL;
    size_t  len = 0;
    int     trunc = 0;

    int rc = tool_web_fetch_execute(input, NULL, NULL, &buf, &len, &trunc);
    ASSERT_EQ(rc, -1);
    ASSERT_NOT_NULL(buf);
    /* Error message should mention the scheme problem */
    ASSERT_TRUE(strstr(buf, "scheme") != NULL || strstr(buf, "http") != NULL ||
                strstr(buf, "Invalid") != NULL);
    free(buf);
    TEST_END();
}
