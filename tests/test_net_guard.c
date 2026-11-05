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

int test_net_url_parse_refuses_non_ascii_hosts(void)
{
    TEST_BEGIN();
    NetUrlParts p;
    /* full-width digits: U+FF11 U+FF12 U+FF17 then ".0.0.1" -- IDN mapping
     * would turn it into 127.0.0.1 behind the guard's back */
    ASSERT_EQ(net_url_parse("http://\xef\xbc\x91\xef\xbc\x92\xef\xbc\x97.0.0.1/", &p), -1);
    /* a UTF-8 IDN host: WinHttpConnect needs punycode, so it is refused */
    ASSERT_EQ(net_url_parse("http://caf\xc3\xa9.test/", &p), -1);
    ASSERT_EQ(net_url_parse("https://caf\xc3\xa9.test:8443/x", &p), -1);
    ASSERT_EQ(net_url_parse("https://user@caf\xc3\xa9.test/", &p), -1);
    /* an ASCII host with a non-ASCII path or query still parses */
    ASSERT_EQ(net_url_parse("http://example.com/caf\xc3\xa9?q=\xc3\xa9", &p), 0);
    ASSERT_STR_EQ(p.host, "example.com");
    ASSERT_STR_EQ(p.path, "/caf\xc3\xa9?q=\xc3\xa9");
    /* an unparseable http URL still warns about cleartext, and the display
     * form is untouched */
    ASSERT_EQ(net_url_cleartext_remote("http://caf\xc3\xa9.test/"), 1);
    ASSERT_EQ(net_url_cleartext_remote("http://\xef\xbc\x91\xef\xbc\x92\xef\xbc\x97.0.0.1/"), 1);
    char shown[NET_URL_MAX];
    net_url_display("http://example.com/caf\xc3\xa9", shown, sizeof shown);
    ASSERT_STR_EQ(shown, "http://example.com/caf\xc3\xa9");
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

/* True when s is well-formed UTF-8 (no overlongs checked; enough for the
 * cut-boundary assertions below). */
static int nt_valid_utf8(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int extra = *p < 0x80 ? 0 : (*p >> 5) == 6 ? 1 : (*p >> 4) == 14 ? 2
                  : (*p >> 3) == 30 ? 3 : -1;
        int i;
        if (extra < 0) return 0;
        p++;
        for (i = 0; i < extra; i++, p++)
            if ((*p & 0xC0) != 0x80) return 0;
    }
    return 1;
}

int test_net_url_display_marked(void)
{
    TEST_BEGIN();
    char out[256];
    char big[400];
    size_t n;

    /* a dropped query is marked */
    n = net_url_display_marked("https://a.test/p?key=secret", out, sizeof out);
    ASSERT_STR_EQ(out, "https://a.test/p?\xE2\x80\xA6");
    ASSERT_EQ(n, strlen(out));
    ASSERT_TRUE(strstr(out, "secret") == NULL);

    /* an empty query is still a dropped query */
    net_url_display_marked("https://a.test/p?", out, sizeof out);
    ASSERT_STR_EQ(out, "https://a.test/p?\xE2\x80\xA6");

    /* no query: unchanged */
    n = net_url_display_marked("https://a.test/p", out, sizeof out);
    ASSERT_STR_EQ(out, "https://a.test/p");
    ASSERT_EQ(n, (size_t)16);

    /* a fragment alone is dropped without a mark */
    net_url_display_marked("https://a.test/p#frag", out, sizeof out);
    ASSERT_STR_EQ(out, "https://a.test/p");

    /* a '?' after the fragment starts nothing */
    net_url_display_marked("https://a.test/p#f?x=1", out, sizeof out);
    ASSERT_STR_EQ(out, "https://a.test/p");

    /* userinfo is dropped, query too */
    net_url_display_marked("https://user:pw@a.test/p?q=1", out, sizeof out);
    ASSERT_STR_EQ(out, "https://a.test/p?\xE2\x80\xA6");
    ASSERT_TRUE(strstr(out, "pw") == NULL);

    /* "://" inside the query is no scheme: the userinfo is still dropped */
    net_url_display_marked("user:pw@host/p?x=http://y", out, sizeof out);
    ASSERT_STR_EQ(out, "host/p?\xE2\x80\xA6");

    /* a long path is cut and marked with U+2026, never "..." */
    memcpy(big, "https://a.test/", 15);
    memset(big + 15, 'p', 185);
    big[200] = '\0';
    n = net_url_display_marked(big, out, sizeof out);
    ASSERT_EQ(n, strlen(out));
    ASSERT_TRUE(n >= 3);
    ASSERT_STR_EQ(out + n - 3, "\xE2\x80\xA6");
    ASSERT_TRUE(strstr(out, "...") == NULL);
    ASSERT_TRUE(nt_valid_utf8(out));
    ASSERT_EQ(n, (size_t)(NET_URL_DISPLAY_MAX + 3));

    /* a cut that would split a multi-byte character backs up */
    memset(big, 'a', 77);
    memcpy(big + 77, "\xE2\x82\xAC", 3);     /* euro sign spans bytes 77..79 */
    memcpy(big + 80, "\xE2\x82\xAC", 3);     /* and another, 80..82 */
    memset(big + 83, 'b', 50);
    big[133] = '\0';
    n = net_url_display_marked(big, out, sizeof out);
    ASSERT_TRUE(nt_valid_utf8(out));
    ASSERT_STR_EQ(out + n - 3, "\xE2\x80\xA6");

    /* long path and a query: both marks, query last */
    memcpy(big, "https://a.test/", 15);
    memset(big + 15, 'p', 185);
    big[200] = '?';
    memcpy(big + 201, "k=v", 4);
    n = net_url_display_marked(big, out, sizeof out);
    ASSERT_STR_EQ(out + n - 7, "\xE2\x80\xA6?\xE2\x80\xA6");
    ASSERT_TRUE(strstr(out, "k=v") == NULL);

    /* controls and bidi are removed, exactly as net_url_display does */
    net_url_display_marked("https://a.test/a\x01" "b\xE2\x80\xAE" "c", out, sizeof out);
    ASSERT_STR_EQ(out, "https://a.test/abc");
    net_url_display_marked("https://a.test/a\r\nb\tc?x", out, sizeof out);
    ASSERT_STR_EQ(out, "https://a.test/abc?\xE2\x80\xA6");

    /* small caps: NUL-terminated, within cap, valid UTF-8 */
    for (n = 1; n < 24; n++) {
        char small[32];
        size_t w;
        memset(small, 'x', sizeof small);
        w = net_url_display_marked("https://a.test/path?q=secret", small, n);
        ASSERT_TRUE(strlen(small) < n);
        ASSERT_EQ(w, strlen(small));
        ASSERT_TRUE(nt_valid_utf8(small));
    }
    ASSERT_EQ(net_url_display_marked("x", out, 0), (size_t)0);
    ASSERT_EQ(net_url_display_marked("x", NULL, 10), (size_t)0);
    n = net_url_display_marked(NULL, out, sizeof out);
    ASSERT_EQ(n, (size_t)0);
    ASSERT_STR_EQ(out, "");

    /* net_url_display itself still ends in ASCII dots */
    n = net_url_display("https://a.test/p?key=secret", out, sizeof out);
    ASSERT_STR_EQ(out, "https://a.test/p");
    TEST_END();
}

int test_net_url_detail(void)
{
    TEST_BEGIN();
    char out[1100];
    char big[1600];
    size_t n;

    /* userinfo removed, query and fragment kept */
    n = net_url_detail("https://u:pw@a.test/p?k=v#f", out, sizeof out);
    ASSERT_STR_EQ(out, "https://a.test/p?k=v#f");
    ASSERT_EQ(n, strlen(out));
    net_url_detail("https://u:pw@a.test/p?k=v", out, sizeof out);
    ASSERT_STR_EQ(out, "https://a.test/p?k=v");

    /* the last '@' of the authority ends the userinfo; one past the path does not */
    net_url_detail("http://a@b@example.com/p", out, sizeof out);
    ASSERT_STR_EQ(out, "http://example.com/p");
    net_url_detail("http://example.com/p@q?x=a@b", out, sizeof out);
    ASSERT_STR_EQ(out, "http://example.com/p@q?x=a@b");
    net_url_detail("http://host?u@x", out, sizeof out);
    ASSERT_STR_EQ(out, "http://host?u@x");

    /* no scheme */
    net_url_detail("user:pw@host/p", out, sizeof out);
    ASSERT_STR_EQ(out, "host/p");

    net_url_detail("user:pw@host/p?x=http://y", out, sizeof out);
    ASSERT_STR_EQ(out, "host/p?x=http://y");

    /* a bidi override inside the userinfo goes with the userinfo */
    net_url_detail("http://us\xE2\x80\xAE" "er:pw@host/", out, sizeof out);
    ASSERT_STR_EQ(out, "http://host/");

    /* C0 and DEL as \xNN, uppercase hex */
    net_url_detail("https://a.test/\x01", out, sizeof out);
    ASSERT_STR_EQ(out, "https://a.test/\\x01");
    net_url_detail("https://a.test/\x1B[2J\x7F\x0A\x09", out, sizeof out);
    ASSERT_STR_EQ(out, "https://a.test/\\x1B[2J\\x7F\\x0A\\x09");
    net_url_detail("https://a.test/\x1F", out, sizeof out);
    ASSERT_STR_EQ(out, "https://a.test/\\x1F");

    /* bidi controls as \uXXXX, uppercase hex */
    net_url_detail("https://a.test/\xE2\x80\xAE", out, sizeof out);
    ASSERT_STR_EQ(out, "https://a.test/\\u202E");
    net_url_detail("a\xE2\x80\xAA" "b\xE2\x80\xAB" "c\xE2\x80\xAC" "d\xE2\x80\xAD", out, sizeof out);
    ASSERT_STR_EQ(out, "a\\u202Ab\\u202Bc\\u202Cd\\u202D");
    net_url_detail("\xE2\x81\xA6\xE2\x81\xA7\xE2\x81\xA8\xE2\x81\xA9", out, sizeof out);
    ASSERT_STR_EQ(out, "\\u2066\\u2067\\u2068\\u2069");
    net_url_detail("\xE2\x80\x8E\xE2\x80\x8F", out, sizeof out);
    ASSERT_STR_EQ(out, "\\u200E\\u200F");

    /* C1 controls (UTF-8 C2 80..9F) are escaped too */
    net_url_detail("a\xC2\x85" "b\xC2\x9F", out, sizeof out);
    ASSERT_STR_EQ(out, "a\\u0085b\\u009F");

    /* neighbours of the bidi ranges and ordinary non-ASCII pass through */
    net_url_detail("\xE2\x80\xA9\xE2\x80\xAF\xE2\x81\xA5\xE2\x81\xAA\xE2\x80\x8D", out, sizeof out);
    ASSERT_STR_EQ(out, "\xE2\x80\xA9\xE2\x80\xAF\xE2\x81\xA5\xE2\x81\xAA\xE2\x80\x8D");
    net_url_detail("http://a.test/caf\xC3\xA9\xE2\x82\xAC", out, sizeof out);
    ASSERT_STR_EQ(out, "http://a.test/caf\xC3\xA9\xE2\x82\xAC");

    /* a fitting URL is never marked */
    memcpy(big, "https://a.test/", 15);
    memset(big + 15, 'p', 1000);
    big[1015] = '\0';
    n = net_url_detail(big, out, sizeof out);
    ASSERT_EQ(n, (size_t)1015);
    ASSERT_STR_EQ(out, big);

    /* cap 8: NUL-terminated within cap */
    memset(out, 'x', sizeof out);
    n = net_url_detail("https://a.test/path", out, 8);
    ASSERT_TRUE(strlen(out) < 8);
    ASSERT_EQ(n, strlen(out));
    ASSERT_TRUE(nt_valid_utf8(out));

    /* a long URL into a small cap ends with U+2026, valid UTF-8, no partial escape */
    {
        const char *u = "https://a.test/\x01\x01\x01\x01\x01\x01\x01\x01";
        char small[24];
        size_t cap;
        for (cap = 1; cap <= sizeof small; cap++) {
            memset(small, 'x', sizeof small);
            n = net_url_detail(u, small, cap);
            ASSERT_TRUE(strlen(small) < cap);
            ASSERT_EQ(n, strlen(small));
            ASSERT_TRUE(nt_valid_utf8(small));
            if (cap >= 4) {
                const char *tail = small + n - 3;
                ASSERT_TRUE(n >= 3);
                ASSERT_STR_EQ(tail, "\xE2\x80\xA6");
                /* every backslash in the kept text starts a whole \xNN */
                {
                    const char *p;
                    for (p = small; *p && p < small + n - 3; p++) {
                        if (*p == '\\') {
                            ASSERT_EQ(p[1], 'x');
                            ASSERT_EQ(p[2], '0');
                            ASSERT_EQ(p[3], '1');
                            p += 3;
                        }
                    }
                }
            }
        }
        /* cap 21 leaves 17 bytes of text before the 3-byte mark: 15 + one whole \x01 would be 19, so one escape is dropped */
        memset(small, 'x', sizeof small);
        n = net_url_detail(u, small, 21);
        ASSERT_STR_EQ(small, "https://a.test/\xE2\x80\xA6");
    }

    /* a long URL into the 1024-byte detail buffer */
    {
        char d[1024];
        memcpy(big, "https://a.test/", 15);
        memset(big + 15, 'p', 1500);
        big[1515] = '\0';
        n = net_url_detail(big, d, sizeof d);
        ASSERT_EQ(n, strlen(d));
        ASSERT_EQ(n, (size_t)1023);
        ASSERT_STR_EQ(d + n - 3, "\xE2\x80\xA6");
        ASSERT_TRUE(nt_valid_utf8(d));
    }

    /* a multi-byte character is never split by the cut */
    {
        char small[24];
        net_url_detail("https://a.test/\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC", small, 24);
        ASSERT_TRUE(nt_valid_utf8(small));
        ASSERT_STR_EQ(small, "https://a.test/\xE2\x82\xAC\xE2\x80\xA6");
        net_url_detail("https://a.test/\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC", small, 16);
        ASSERT_TRUE(nt_valid_utf8(small));
    }

    n = net_url_detail(NULL, out, sizeof out);
    ASSERT_EQ(n, (size_t)0);
    ASSERT_STR_EQ(out, "");
    ASSERT_EQ(net_url_detail("x", NULL, 10), (size_t)0);
    ASSERT_EQ(net_url_detail("x", out, 0), (size_t)0);
    TEST_END();
}

int test_net_text_escape(void)
{
    TEST_BEGIN();
    char out[64];
    size_t n;

    n = net_text_escape("{\"a\":\"\x01\"}", 8, out, sizeof out);
    ASSERT_STR_EQ(out, "{\"a\":\"\\x01\"");
    ASSERT_EQ(n, strlen(out));

    /* an embedded NUL is escaped, since the length is explicit */
    n = net_text_escape("a\0b", 3, out, sizeof out);
    ASSERT_STR_EQ(out, "a\\x00b");

    /* a length shorter than the text stops there */
    net_text_escape("abcdef", 3, out, sizeof out);
    ASSERT_STR_EQ(out, "abc");

    net_text_escape("", 0, out, sizeof out);
    ASSERT_STR_EQ(out, "");
    ASSERT_EQ(net_text_escape("x", 1, out, 0), (size_t)0);
    ASSERT_EQ(net_text_escape("x", 1, NULL, 5), (size_t)0);

    /* the cut ends in U+2026 */
    n = net_text_escape("abcdefghij", 10, out, 8);
    ASSERT_STR_EQ(out, "abcd\xE2\x80\xA6");
    ASSERT_EQ(n, (size_t)7);
    TEST_END();
}
