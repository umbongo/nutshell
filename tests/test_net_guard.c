#include "test_framework.h"
#include "net_guard.h"
#include <string.h>
#include <stdlib.h>

static const unsigned char *v4(unsigned a, unsigned b, unsigned c, unsigned d)
{
    static unsigned char buf[4];
    buf[0] = (unsigned char)a;
    buf[1] = (unsigned char)b;
    buf[2] = (unsigned char)c;
    buf[3] = (unsigned char)d;
    return buf;
}

/* Minimal IPv6 text parser for the tests: hex groups, one "::", and an
 * optional trailing dotted quad. Returns a static 16-byte buffer. */
static const unsigned char *v6(const char *s)
{
    static unsigned char out[16];
    unsigned char head[16], tail[16];
    int nh = 0, nt = 0, in_tail = 0, i = 0;
    memset(out, 0, sizeof out);
    memset(head, 0, sizeof head);
    memset(tail, 0, sizeof tail);
    if (s[0] == ':' && s[1] == ':') { in_tail = 1; i = 2; }
    while (s[i]) {
        unsigned char *dst = in_tail ? tail : head;
        int *n = in_tail ? &nt : &nh;
        const char *e = s + i;
        const char *dot = e;
        while (*dot && *dot != ':' && *dot != '.') dot++;
        if (*dot == '.') {
            unsigned q[4];
            int k;
            for (k = 0; k < 4; k++) {
                char *end;
                q[k] = (unsigned)strtoul(e, &end, 10);
                e = end + 1;
            }
            for (k = 0; k < 4; k++) dst[(*n)++] = (unsigned char)q[k];
            break;
        } else {
            char *end;
            unsigned long w = strtoul(e, &end, 16);
            dst[(*n)++] = (unsigned char)(w >> 8);
            dst[(*n)++] = (unsigned char)(w & 0xFF);
            i = (int)(end - s);
            if (s[i] == ':' && s[i + 1] == ':') { in_tail = 1; i += 2; }
            else if (s[i] == ':') i++;
        }
    }
    memcpy(out, head, (size_t)nh);
    memcpy(out + 16 - nt, tail, (size_t)nt);
    return out;
}

int test_net_ipv4_internal_ranges(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(net_ipv4_is_internal(v4(127, 0, 0, 1)));
    ASSERT_TRUE(net_ipv4_is_internal(v4(127, 255, 255, 255)));
    ASSERT_TRUE(net_ipv4_is_internal(v4(10, 0, 0, 0)));
    ASSERT_TRUE(net_ipv4_is_internal(v4(10, 255, 255, 255)));
    ASSERT_TRUE(net_ipv4_is_internal(v4(172, 16, 0, 0)));
    ASSERT_TRUE(net_ipv4_is_internal(v4(172, 31, 255, 255)));
    ASSERT_TRUE(net_ipv4_is_internal(v4(192, 168, 0, 1)));
    ASSERT_TRUE(net_ipv4_is_internal(v4(169, 254, 169, 254)));
    ASSERT_TRUE(net_ipv4_is_internal(v4(0, 0, 0, 0)));
    ASSERT_TRUE(net_ipv4_is_internal(v4(0, 255, 255, 255)));
    TEST_END();
}

int test_net_ipv4_public_edges(void)
{
    TEST_BEGIN();
    ASSERT_FALSE(net_ipv4_is_internal(v4(126, 255, 255, 255)));
    ASSERT_FALSE(net_ipv4_is_internal(v4(128, 0, 0, 0)));
    ASSERT_FALSE(net_ipv4_is_internal(v4(9, 255, 255, 255)));
    ASSERT_FALSE(net_ipv4_is_internal(v4(11, 0, 0, 0)));
    ASSERT_FALSE(net_ipv4_is_internal(v4(172, 15, 255, 255)));
    ASSERT_FALSE(net_ipv4_is_internal(v4(172, 32, 0, 0)));
    ASSERT_FALSE(net_ipv4_is_internal(v4(192, 167, 255, 255)));
    ASSERT_FALSE(net_ipv4_is_internal(v4(192, 169, 0, 0)));
    ASSERT_FALSE(net_ipv4_is_internal(v4(169, 253, 255, 255)));
    ASSERT_FALSE(net_ipv4_is_internal(v4(169, 255, 0, 0)));
    ASSERT_FALSE(net_ipv4_is_internal(v4(1, 0, 0, 0)));
    ASSERT_FALSE(net_ipv4_is_internal(v4(8, 8, 8, 8)));
    ASSERT_FALSE(net_ipv4_is_internal(v4(100, 64, 0, 1)));  /* CGNAT: not refused */
    ASSERT_FALSE(net_ipv4_is_internal(v4(224, 0, 0, 1)));   /* multicast: not refused */
    TEST_END();
}

int test_net_ipv6_internal(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(net_ipv6_is_internal(v6("::1")));
    ASSERT_TRUE(net_ipv6_is_internal(v6("::")));
    ASSERT_TRUE(net_ipv6_is_internal(v6("fc00::")));
    ASSERT_TRUE(net_ipv6_is_internal(v6("fdff:ffff::1")));
    ASSERT_TRUE(net_ipv6_is_internal(v6("fe80::1")));
    ASSERT_TRUE(net_ipv6_is_internal(v6("febf::1")));
    ASSERT_TRUE(net_ipv6_is_internal(v6("::ffff:127.0.0.1")));
    ASSERT_TRUE(net_ipv6_is_internal(v6("::ffff:10.1.2.3")));
    ASSERT_TRUE(net_ipv6_is_internal(v6("::127.0.0.1")));
    ASSERT_TRUE(net_ipv6_is_internal(v6("::ffff:169.254.169.254")));
    TEST_END();
}

int test_net_ipv6_public(void)
{
    TEST_BEGIN();
    ASSERT_FALSE(net_ipv6_is_internal(v6("2001:db8::1")));
    ASSERT_FALSE(net_ipv6_is_internal(v6("2606:4700::1111")));
    ASSERT_FALSE(net_ipv6_is_internal(v6("fec0::1")));
    ASSERT_FALSE(net_ipv6_is_internal(v6("fe7f::1")));
    ASSERT_FALSE(net_ipv6_is_internal(v6("fbff::1")));
    ASSERT_FALSE(net_ipv6_is_internal(v6("::ffff:8.8.8.8")));
    ASSERT_FALSE(net_ipv6_is_internal(v6("::8.8.8.8")));
    ASSERT_FALSE(net_ipv6_is_internal(v6("64:ff9b::7f00:1"))); /* NAT64: not refused */
    ASSERT_FALSE(net_ipv6_is_internal(v6("ff02::1")));         /* multicast: not refused */
    TEST_END();
}

int test_net_addr_is_internal_dispatch(void)
{
    TEST_BEGIN();
    NetAddr a;
    memset(&a, 0, sizeof a);
    a.v6 = 0;
    a.b[0] = 10; a.b[3] = 1;
    ASSERT_TRUE(net_addr_is_internal(&a));
    /* v4 uses only b[0..3]: stray bytes beyond are ignored */
    a.b[0] = 8; a.b[1] = 8; a.b[2] = 8; a.b[3] = 8; a.b[4] = 0xFF;
    ASSERT_FALSE(net_addr_is_internal(&a));
    /* v6 uses all 16 */
    a.v6 = 1;
    memset(a.b, 0, sizeof a.b);
    a.b[0] = 0x20; a.b[1] = 0x01; a.b[15] = 1;
    ASSERT_FALSE(net_addr_is_internal(&a));
    memset(a.b, 0, sizeof a.b);
    a.b[15] = 1; /* ::1 */
    ASSERT_TRUE(net_addr_is_internal(&a));
    /* b[0] = 127 with v6 = 0 is internal, with v6 = 1 (127::) it is not */
    memset(a.b, 0, sizeof a.b);
    a.b[0] = 127; a.b[3] = 1;
    a.v6 = 0;
    ASSERT_TRUE(net_addr_is_internal(&a));
    a.v6 = 1;
    ASSERT_FALSE(net_addr_is_internal(&a));
    ASSERT_TRUE(net_addr_is_internal(NULL));
    TEST_END();
}

int test_net_url_parse_basic(void)
{
    TEST_BEGIN();
    NetUrlParts p;
    ASSERT_EQ(net_url_parse("http://example.com", &p), 0);
    ASSERT_EQ(p.https, 0);
    ASSERT_STR_EQ(p.host, "example.com");
    ASSERT_EQ(p.port, 80);
    ASSERT_STR_EQ(p.path, "/");

    ASSERT_EQ(net_url_parse("https://example.com:8443/a?b#c", &p), 0);
    ASSERT_EQ(p.https, 1);
    ASSERT_STR_EQ(p.host, "example.com");
    ASSERT_EQ(p.port, 8443);
    ASSERT_STR_EQ(p.path, "/a?b");

    ASSERT_EQ(net_url_parse("https://example.com", &p), 0);
    ASSERT_EQ(p.port, 443);

    ASSERT_EQ(net_url_parse("HTTP://Example.com/x", &p), 0);
    ASSERT_EQ(p.https, 0);
    ASSERT_EQ(p.port, 80);
    ASSERT_STR_EQ(p.path, "/x");

    ASSERT_EQ(net_url_parse("HtTpS://Example.com/x", &p), 0);
    ASSERT_EQ(p.https, 1);

    ASSERT_EQ(net_url_parse("http://example.com?q=1", &p), 0);
    ASSERT_STR_EQ(p.host, "example.com");
    ASSERT_STR_EQ(p.path, "/?q=1");

    ASSERT_EQ(net_url_parse("http://example.com:/x", &p), 0);
    ASSERT_EQ(p.port, 80);
    ASSERT_STR_EQ(p.path, "/x");

    ASSERT_EQ(net_url_parse("http://example.com#frag", &p), 0);
    ASSERT_STR_EQ(p.path, "/");
    TEST_END();
}

int test_net_url_parse_userinfo_and_delims(void)
{
    TEST_BEGIN();
    NetUrlParts p;
    ASSERT_EQ(net_url_parse("http://user:pw@10.0.0.1/", &p), 0);
    ASSERT_STR_EQ(p.host, "10.0.0.1");
    ASSERT_EQ(net_url_parse("http://evil.test@127.0.0.1/", &p), 0);
    ASSERT_STR_EQ(p.host, "127.0.0.1");
    ASSERT_EQ(net_url_parse("http://127.0.0.1#@evil.test/", &p), 0);
    ASSERT_STR_EQ(p.host, "127.0.0.1");
    ASSERT_STR_EQ(p.path, "/");
    ASSERT_EQ(net_url_parse("http://127.0.0.1?@evil.test/", &p), 0);
    ASSERT_STR_EQ(p.host, "127.0.0.1");
    ASSERT_STR_EQ(p.path, "/?@evil.test/");
    /* the last @ in the authority wins */
    ASSERT_EQ(net_url_parse("http://a@b@10.0.0.2:81/p", &p), 0);
    ASSERT_STR_EQ(p.host, "10.0.0.2");
    ASSERT_EQ(p.port, 81);
    TEST_END();
}

int test_net_url_parse_ipv6_literal(void)
{
    TEST_BEGIN();
    NetUrlParts p;
    ASSERT_EQ(net_url_parse("http://[::1]:8080/x", &p), 0);
    ASSERT_STR_EQ(p.host, "::1");
    ASSERT_EQ(p.port, 8080);
    ASSERT_STR_EQ(p.path, "/x");
    ASSERT_EQ(net_url_parse("https://[fe80::1]/", &p), 0);
    ASSERT_STR_EQ(p.host, "fe80::1");
    ASSERT_EQ(p.port, 443);
    TEST_END();
}

int test_net_url_parse_refuses(void)
{
    TEST_BEGIN();
    NetUrlParts p;
    char longurl[NET_URL_MAX + 8];
    char longhost[NET_HOST_MAX + 32];
    ASSERT_EQ(net_url_parse(NULL, &p), -1);
    ASSERT_EQ(net_url_parse("", &p), -1);
    ASSERT_EQ(net_url_parse("ftp://x/", &p), -1);
    ASSERT_EQ(net_url_parse("file:///etc/passwd", &p), -1);
    ASSERT_EQ(net_url_parse("http://", &p), -1);
    ASSERT_EQ(net_url_parse("http:///x", &p), -1);
    ASSERT_EQ(net_url_parse("http://:80/", &p), -1);
    ASSERT_EQ(net_url_parse("http://host:0/", &p), -1);
    ASSERT_EQ(net_url_parse("http://host:65536/", &p), -1);
    ASSERT_EQ(net_url_parse("http://host:8a/", &p), -1);
    ASSERT_EQ(net_url_parse("http://[::1/", &p), -1);
    ASSERT_EQ(net_url_parse("http://a\\@b/", &p), -1);
    ASSERT_EQ(net_url_parse("http://a b/", &p), -1);
    ASSERT_EQ(net_url_parse("http://a\tb/", &p), -1);
    ASSERT_EQ(net_url_parse("http://a/\x7f", &p), -1);
    ASSERT_EQ(net_url_parse("http://a/\x01", &p), -1);
    ASSERT_EQ(net_url_parse("http://a/\n", &p), -1);
    ASSERT_EQ(net_url_parse("http://example.com", NULL), -1);
    /* brackets hold an IPv6 literal only: hex digits, ':' and '.', with a ':' */
    ASSERT_EQ(net_url_parse("http://[localhost]/", &p), -1);
    ASSERT_EQ(net_url_parse("http://[127.0.0.1]/", &p), -1);
    ASSERT_EQ(net_url_parse("http://[fe80::1%25eth0]/", &p), -1);
    ASSERT_EQ(net_url_parse("http://[]/", &p), -1);
    ASSERT_EQ(net_url_parse("http://[::1]x/", &p), -1);
    /* regression pins */
    ASSERT_EQ(net_url_parse("http://127.0.0.1\\@evil.test/", &p), -1);
    ASSERT_EQ(net_url_parse("http://host:00/", &p), -1);
    ASSERT_EQ(net_url_parse("http://a@/", &p), -1);
    ASSERT_EQ(net_url_parse("http://host:65535/", &p), 0);
    ASSERT_EQ(p.port, 65535);

    /* exactly NET_URL_MAX bytes is refused, one fewer is fine */
    memset(longurl, 'a', sizeof longurl);
    memcpy(longurl, "http://h/", 9);
    longurl[NET_URL_MAX] = '\0';
    ASSERT_EQ(net_url_parse(longurl, &p), -1);
    longurl[NET_URL_MAX - 1] = '\0';
    ASSERT_EQ(net_url_parse(longurl, &p), 0);

    /* a host of NET_HOST_MAX bytes is refused, one shorter is accepted */
    memset(longhost, 'h', sizeof longhost);
    memcpy(longhost, "http://", 7);
    memcpy(longhost + 7 + NET_HOST_MAX, "/x", 3);
    ASSERT_EQ(net_url_parse(longhost, &p), -1);
    memcpy(longhost + 7 + NET_HOST_MAX - 1, "/x", 3);
    ASSERT_EQ(net_url_parse(longhost, &p), 0);
    ASSERT_EQ(strlen(p.host), (size_t)(NET_HOST_MAX - 1));
    TEST_END();
}

int test_net_url_cleartext_remote(void)
{
    TEST_BEGIN();
    ASSERT_EQ(net_url_cleartext_remote("http://example.com/v1"), 1);
    ASSERT_EQ(net_url_cleartext_remote("http://myhost/"), 1);
    ASSERT_EQ(net_url_cleartext_remote("http://10.0.0.5/"), 1);
    ASSERT_EQ(net_url_cleartext_remote("http://127.1/"), 1);
    ASSERT_EQ(net_url_cleartext_remote("http://256.0.0.1/"), 1);
    ASSERT_EQ(net_url_cleartext_remote("http://127.0.0.1.evil.test/"), 1);
    ASSERT_EQ(net_url_cleartext_remote("http://localhost.evil.test/"), 1);
    ASSERT_EQ(net_url_cleartext_remote("http://localhost@evil.test/"), 1);
    ASSERT_EQ(net_url_cleartext_remote("http://"), 1);
    ASSERT_EQ(net_url_cleartext_remote("http://[localhost]/"), 1);
    ASSERT_EQ(net_url_cleartext_remote("http://[127.0.0.1]/"), 1);

    ASSERT_EQ(net_url_cleartext_remote("http://localhost:11434/v1"), 0);
    ASSERT_EQ(net_url_cleartext_remote("http://LOCALHOST./v1"), 0);
    ASSERT_EQ(net_url_cleartext_remote("http://127.0.0.1:8080"), 0);
    ASSERT_EQ(net_url_cleartext_remote("http://127.5.6.7/"), 0);
    ASSERT_EQ(net_url_cleartext_remote("http://[::1]:8080/"), 0);
    ASSERT_EQ(net_url_cleartext_remote("http://user@localhost/"), 0);
    ASSERT_EQ(net_url_cleartext_remote("https://example.com"), 0);
    ASSERT_EQ(net_url_cleartext_remote(""), 0);
    ASSERT_EQ(net_url_cleartext_remote(NULL), 0);
    ASSERT_EQ(net_url_cleartext_remote("ftp://example.com"), 0);
    TEST_END();
}

static int resolve_eq(const char *base, const char *loc, const char *want)
{
    char out[NET_URL_MAX];
    if (net_url_resolve(base, loc, out, sizeof out) != 0) return 0;
    return strcmp(out, want) == 0;
}

int test_net_url_resolve_absolute(void)
{
    char out[NET_URL_MAX];
    TEST_BEGIN();
    ASSERT_TRUE(resolve_eq("http://a.test/x", "https://b.test/y", "https://b.test/y"));
    ASSERT_EQ(net_url_resolve("http://a.test/x", "HTTPS://b.test/", out, sizeof out), 0);
    ASSERT_TRUE(resolve_eq("https://a.test/", "http://b.test:81/q?z=1", "http://b.test:81/q?z=1"));
    TEST_END();
}

int test_net_url_resolve_scheme_relative(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(resolve_eq("https://a.test/x", "//b.test/p", "https://b.test/p"));
    ASSERT_TRUE(resolve_eq("http://a.test/x", "//b.test", "http://b.test"));
    TEST_END();
}

int test_net_url_resolve_absolute_path(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(resolve_eq("http://a.test:8080/x/y?q", "/z", "http://a.test:8080/z"));
    ASSERT_TRUE(resolve_eq("https://u:p@a.test/x#f", "/z", "https://u:p@a.test/z"));
    ASSERT_TRUE(resolve_eq("http://a.test", "/z?k=v", "http://a.test/z?k=v"));
    TEST_END();
}

int test_net_url_resolve_relative(void)
{
    TEST_BEGIN();
    ASSERT_TRUE(resolve_eq("http://a.test/x/y", "z?k=1", "http://a.test/x/z?k=1"));
    ASSERT_TRUE(resolve_eq("http://a.test", "z", "http://a.test/z"));
    ASSERT_TRUE(resolve_eq("http://a.test/x/y", "../z", "http://a.test/x/../z"));
    ASSERT_TRUE(resolve_eq("http://a.test/x?old", "?new", "http://a.test/x?new"));
    /* a '/' inside the base query is not a path separator */
    ASSERT_TRUE(resolve_eq("http://a.test/x/y?r=/a/b", "z", "http://a.test/x/z"));
    ASSERT_TRUE(resolve_eq("http://a.test/x/y#f/g", "z", "http://a.test/x/z"));
    ASSERT_TRUE(resolve_eq("http://a.test/x?old#f", "?new", "http://a.test/x?new"));
    /* a ':' after the first '/' is not a scheme */
    ASSERT_TRUE(resolve_eq("http://a.test/x/y", "z/a:b", "http://a.test/x/z/a:b"));
    TEST_END();
}

int test_net_url_resolve_refuses(void)
{
    char out[NET_URL_MAX];
    char small[16];
    TEST_BEGIN();
    ASSERT_EQ(net_url_resolve("http://a.test/", "ftp://x/", out, sizeof out), -1);
    ASSERT_EQ(net_url_resolve("http://a.test/", "file:///etc/passwd", out, sizeof out), -1);
    ASSERT_EQ(net_url_resolve("http://a.test/", "javascript:alert(1)", out, sizeof out), -1);
    ASSERT_EQ(net_url_resolve("http://a.test/", "data:text/html,x", out, sizeof out), -1);
    ASSERT_EQ(net_url_resolve("http://a.test/", "mailto:a@b", out, sizeof out), -1);
    ASSERT_EQ(net_url_resolve("http://a.test/", "", out, sizeof out), -1);
    ASSERT_EQ(net_url_resolve("http://a.test/", NULL, out, sizeof out), -1);
    ASSERT_EQ(net_url_resolve("http://a.test/", "/a b", out, sizeof out), -1);
    ASSERT_EQ(net_url_resolve("http://a.test/", "/a\r\nSet-Cookie: x", out, sizeof out), -1);
    ASSERT_EQ(net_url_resolve("http://a.test/", "/a\\b", out, sizeof out), -1);
    ASSERT_EQ(net_url_resolve("http://a.test/", "/a\x7f", out, sizeof out), -1);
    ASSERT_EQ(net_url_resolve("http://a.test/", "http://", out, sizeof out), -1);
    ASSERT_EQ(net_url_resolve("http://a.test/", "http:z", out, sizeof out), -1);
    ASSERT_EQ(net_url_resolve("http://a.test/", "//", out, sizeof out), -1);

    ASSERT_EQ(net_url_resolve("ftp://a/", "/x", out, sizeof out), -1);
    ASSERT_EQ(net_url_resolve("a.test/x", "/x", out, sizeof out), -1);
    ASSERT_EQ(net_url_resolve("", "/x", out, sizeof out), -1);
    ASSERT_EQ(net_url_resolve(NULL, "/x", out, sizeof out), -1);
    ASSERT_EQ(net_url_resolve("http://a.test/", "/x", NULL, sizeof out), -1);
    ASSERT_EQ(net_url_resolve("http://a.test/", "/x", out, 0), -1);

    /* "http://a.test/0123456789012" is 27 bytes; it does not fit 16 */
    ASSERT_EQ(net_url_resolve("http://a.test/x", "/0123456789012", small, sizeof small), -1);
    /* exactly fitting: "http://a.test/0" is 15 bytes + NUL */
    ASSERT_EQ(net_url_resolve("http://a.test/x", "/01", small, sizeof small), -1);
    ASSERT_EQ(net_url_resolve("http://a.test/x", "/0", small, sizeof small), 0);
    ASSERT_STR_EQ(small, "http://a.test/0");
    TEST_END();
}

int test_net_url_display(void)
{
    char out[256];
    char big[400];
    size_t n;
    TEST_BEGIN();
    n = net_url_display("https://example.com/p?key=secret#frag", out, sizeof out);
    ASSERT_STR_EQ(out, "https://example.com/p");
    ASSERT_EQ(n, strlen(out));
    net_url_display("http://user:pw@example.com/p", out, sizeof out);
    ASSERT_STR_EQ(out, "http://example.com/p");
    net_url_display("http://a@b@example.com/p", out, sizeof out);
    ASSERT_STR_EQ(out, "http://example.com/p");
    /* an '@' after the authority belongs to the path */
    net_url_display("http://example.com/p@q", out, sizeof out);
    ASSERT_STR_EQ(out, "http://example.com/p@q");
    net_url_display("http://ex\xE2\x80\xAE" "ample.com/", out, sizeof out);
    ASSERT_STR_EQ(out, "http://example.com/");
    net_url_display("http://ex\x01" "ample.com/", out, sizeof out);
    ASSERT_STR_EQ(out, "http://example.com/");
    net_url_display("http://example.com/a\r\nb\tc", out, sizeof out);
    ASSERT_STR_EQ(out, "http://example.com/abc");

    /* not parseable: userinfo, query and fragment are still dropped */
    net_url_display("ftp://user:pw@host/p?x=1#y", out, sizeof out);
    ASSERT_STR_EQ(out, "ftp://host/p");
    net_url_display("user:pw@host/p?x=1", out, sizeof out);
    ASSERT_STR_EQ(out, "host/p");
    net_url_display("http://host?secret", out, sizeof out);
    ASSERT_STR_EQ(out, "http://host");
    net_url_display("http://us\xE2\x80\xAE" "er:pw@host/", out, sizeof out);
    ASSERT_STR_EQ(out, "http://host/");

    /* long URL: cut and marked */
    memcpy(big, "http://example.com/", 19);
    memset(big + 19, 'a', 181);
    big[200] = '\0';
    n = net_url_display(big, out, sizeof out);
    ASSERT_EQ(n, strlen(out));
    ASSERT_TRUE(n <= (size_t)NET_URL_DISPLAY_MAX + 3);
    ASSERT_STR_EQ(out + n - 3, "...");
    ASSERT_EQ(n, (size_t)NET_URL_DISPLAY_MAX + 3);

    /* exactly NET_URL_DISPLAY_MAX bytes is not cut */
    memcpy(big, "http://example.com/", 19);
    memset(big + 19, 'a', (size_t)NET_URL_DISPLAY_MAX - 19);
    big[NET_URL_DISPLAY_MAX] = '\0';
    n = net_url_display(big, out, sizeof out);
    ASSERT_EQ(n, (size_t)NET_URL_DISPLAY_MAX);
    ASSERT_STR_EQ(out, big);

    /* the cut (byte 80) falls inside a 3-byte character starting at 79 */
    memcpy(big, "http://example.com/", 19);
    memset(big + 19, 'a', 60);
    memcpy(big + 79, "\xE2\x82\xAC", 3);
    memset(big + 82, 'b', 50);
    big[132] = '\0';
    n = net_url_display(big, out, sizeof out);
    ASSERT_EQ(n, (size_t)82);
    ASSERT_EQ(out[78], 'a');
    ASSERT_STR_EQ(out + 79, "...");

    /* a cut right after a whole 3-byte character keeps it */
    memcpy(big + 77, "\xE2\x82\xAC", 3);
    memset(big + 80, 'b', 52);
    big[132] = '\0';
    n = net_url_display(big, out, sizeof out);
    ASSERT_EQ(n, (size_t)83);
    ASSERT_EQ((unsigned char)out[79], 0xAC);
    ASSERT_STR_EQ(out + 80, "...");

    /* small buffers: always NUL-terminated and within cap */
    memset(out, 'x', sizeof out);
    n = net_url_display("http://example.com/path", out, 8);
    ASSERT_TRUE(strlen(out) < 8);
    ASSERT_EQ(n, strlen(out));
    ASSERT_STR_EQ(out, "http...");
    memset(out, 'x', sizeof out);
    n = net_url_display("http://example.com/path", out, 1);
    ASSERT_EQ(n, (size_t)0);
    ASSERT_EQ(out[0], '\0');
    memset(out, 'x', sizeof out);
    n = net_url_display("http://example.com/path", out, 3);
    ASSERT_EQ(n, (size_t)2);
    ASSERT_STR_EQ(out, "..");

    n = net_url_display(NULL, out, sizeof out);
    ASSERT_EQ(n, (size_t)0);
    ASSERT_STR_EQ(out, "");
    ASSERT_EQ(net_url_display("x", NULL, 10), (size_t)0);
    ASSERT_EQ(net_url_display("x", out, 0), (size_t)0);
    TEST_END();
}
