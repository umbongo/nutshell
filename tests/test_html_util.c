#include "test_framework.h"
#include "html_util.h"
#include <string.h>

/* ---- html_strip_tags tests ----------------------------------------------- */

int test_html_strip_tags_basic(void)
{
    TEST_BEGIN();
    char out[256];
    size_t n = html_strip_tags("<p>Hello <b>world</b></p>",
                               25, out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(out, "Hello world");
    TEST_END();
}

int test_html_strip_tags_script(void)
{
    TEST_BEGIN();
    char out[256];
    const char *html = "text<script>var x=1;</script>more";
    size_t n = html_strip_tags(html, strlen(html), out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(out, "text more");
    TEST_END();
}

int test_html_strip_tags_style(void)
{
    TEST_BEGIN();
    char out[256];
    const char *html = "text<style>.foo{color:red}</style>more";
    size_t n = html_strip_tags(html, strlen(html), out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(out, "text more");
    TEST_END();
}

int test_html_strip_tags_nested(void)
{
    TEST_BEGIN();
    char out[256];
    const char *html = "<div><p>inner</p></div>";
    size_t n = html_strip_tags(html, strlen(html), out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(out, "inner");
    TEST_END();
}

int test_html_strip_tags_self_closing(void)
{
    TEST_BEGIN();
    char out[256];
    const char *html = "text<br/>more";
    size_t n = html_strip_tags(html, strlen(html), out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(out, "text more");
    TEST_END();
}

int test_html_strip_tags_empty(void)
{
    TEST_BEGIN();
    char out[256];
    size_t n = html_strip_tags("", 0, out, sizeof(out));
    ASSERT_EQ(n, 0);
    ASSERT_STR_EQ(out, "");
    TEST_END();
}

int test_html_strip_tags_unclosed(void)
{
    TEST_BEGIN();
    char out[256];
    const char *html = "<div>text";
    size_t n = html_strip_tags(html, strlen(html), out, sizeof(out));
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(out, "text");
    TEST_END();
}

/* ---- html_decode_entities tests ------------------------------------------ */

int test_html_decode_entities_named(void)
{
    TEST_BEGIN();
    char buf[] = "&amp;&lt;&gt;&quot;";
    size_t n = html_decode_entities(buf, strlen(buf));
    ASSERT_TRUE(n == 4);
    ASSERT_STR_EQ(buf, "&<>\"");
    TEST_END();
}

int test_html_decode_entities_numeric(void)
{
    TEST_BEGIN();
    char buf[] = "&#65;&#x42;";
    size_t n = html_decode_entities(buf, strlen(buf));
    ASSERT_EQ(n, (size_t)2);
    ASSERT_STR_EQ(buf, "AB");
    TEST_END();
}

int test_html_decode_entities_apos(void)
{
    TEST_BEGIN();
    char buf[] = "&#x27;";
    size_t n = html_decode_entities(buf, strlen(buf));
    ASSERT_EQ(n, (size_t)1);
    ASSERT_STR_EQ(buf, "'");
    TEST_END();
}

int test_html_decode_entities_malformed(void)
{
    TEST_BEGIN();
    char buf[] = "&notreal;";
    size_t orig = strlen(buf);
    size_t n = html_decode_entities(buf, orig);
    /* Unknown entity is kept as-is */
    ASSERT_EQ(n, orig);
    ASSERT_STR_EQ(buf, "&notreal;");
    TEST_END();
}

int test_html_decode_entities_consecutive(void)
{
    TEST_BEGIN();
    char buf[] = "&amp;&amp;";
    size_t n = html_decode_entities(buf, strlen(buf));
    ASSERT_EQ(n, (size_t)2);
    ASSERT_STR_EQ(buf, "&&");
    TEST_END();
}

/* ---- html_url_decode tests ----------------------------------------------- */

int test_html_url_decode_basic(void)
{
    TEST_BEGIN();
    char buf[] = "hello%20world";
    size_t n = html_url_decode(buf, strlen(buf));
    ASSERT_EQ(n, (size_t)11);
    ASSERT_STR_EQ(buf, "hello world");
    TEST_END();
}

int test_html_url_decode_plus(void)
{
    TEST_BEGIN();
    char buf[] = "hello+world";
    size_t n = html_url_decode(buf, strlen(buf));
    ASSERT_EQ(n, (size_t)11);
    ASSERT_STR_EQ(buf, "hello world");
    TEST_END();
}

int test_html_url_decode_double_encoded(void)
{
    TEST_BEGIN();
    /* %25 decodes to '%', then %20 decodes to space */
    char buf[] = "hello%2520world";
    size_t n = html_url_decode(buf, strlen(buf));
    ASSERT_EQ(n, (size_t)11);
    ASSERT_STR_EQ(buf, "hello world");
    TEST_END();
}

int test_html_url_decode_passthrough(void)
{
    TEST_BEGIN();
    char buf[] = "hello world";
    size_t n = html_url_decode(buf, strlen(buf));
    ASSERT_EQ(n, (size_t)11);
    ASSERT_STR_EQ(buf, "hello world");
    TEST_END();
}

/* ---- html_strip_tag_by_name tests ---------------------------------------- */

int test_html_strip_tag_by_name_bold(void)
{
    TEST_BEGIN();
    char buf[] = "hello <b>bold</b> text";
    size_t n = html_strip_tag_by_name(buf, strlen(buf), "b");
    ASSERT_TRUE(n > 0);
    ASSERT_STR_EQ(buf, "hello bold text");
    TEST_END();
}

/* ---- html_find_by_class tests -------------------------------------------- */

int test_html_find_by_class_basic(void)
{
    TEST_BEGIN();
    const char *html = "<div class=\"result__a\">Link text</div>";
    size_t out_len = 0;
    const char *found = html_find_by_class(html, strlen(html),
                                            "result__a", &out_len);
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(out_len, (size_t)9);
    ASSERT_TRUE(strncmp(found, "Link text", out_len) == 0);
    TEST_END();
}

int test_html_find_by_class_not_found(void)
{
    TEST_BEGIN();
    const char *html = "<div class=\"other\">text</div>";
    size_t out_len = 0;
    const char *found = html_find_by_class(html, strlen(html),
                                            "result__a", &out_len);
    ASSERT_NULL(found);
    TEST_END();
}

int test_html_find_by_class_multi_class(void)
{
    TEST_BEGIN();
    const char *html = "<div class=\"foo bar baz\">content</div>";
    size_t out_len = 0;
    const char *found = html_find_by_class(html, strlen(html),
                                            "bar", &out_len);
    ASSERT_NOT_NULL(found);
    ASSERT_EQ(out_len, (size_t)7);
    ASSERT_TRUE(strncmp(found, "content", out_len) == 0);
    TEST_END();
}
