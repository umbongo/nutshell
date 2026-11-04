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
