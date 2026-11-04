#ifndef NET_GUARD_H
#define NET_GUARD_H

/* Pure network policy: which addresses a guarded fetch must refuse, a strict
 * http(s) URL parser, and the clear-text test used by the Settings warning.
 * Text and bytes only: no sockets, no DNS, no Windows headers. */

#include <stddef.h>

#define NET_URL_MAX  2048
#define NET_HOST_MAX 256

/* An address as bytes. IPv4 occupies b[0..3] when v6 == 0. */
typedef struct {
    int v6;
    unsigned char b[16];
} NetAddr;

/* Internal means 127.0.0.0/8, 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16,
 * 169.254.0.0/16 and 0.0.0.0/8. 100.64.0.0/10 and multicast are not refused. */
int net_ipv4_is_internal(const unsigned char a[4]);

/* Internal means ::1, ::, fc00::/7, fe80::/10, and an IPv4-mapped
 * (::ffff:0:0/96) or IPv4-compatible (::/96) address whose embedded IPv4
 * address is internal. NAT64 and multicast are not refused. */
int net_ipv6_is_internal(const unsigned char a[16]);

/* Dispatches on a->v6. A NULL address returns 1 (fail closed). */
int net_addr_is_internal(const NetAddr *a);

typedef struct {
    int https;
    char host[NET_HOST_MAX];   /* userinfo dropped, brackets removed */
    unsigned short port;       /* explicit, or 80 / 443 */
    char path[NET_URL_MAX];    /* path plus query, never the fragment; starts with '/' */
} NetUrlParts;

/* Strict http:// or https:// parse (scheme case-insensitive). Refuses any
 * byte below 0x21, 0x7F or a backslash anywhere, a URL of NET_URL_MAX bytes or
 * more, an empty host, a host of NET_HOST_MAX bytes or more, a host with a
 * byte >= 0x80 (write an IDN host as punycode; the path and query may keep
 * such bytes) and a port that is not empty or 1-65535. Returns 0, or -1 (out is then unspecified). */
int net_url_parse(const char *url, NetUrlParts *out);

/* 1 when url is an http:// URL whose host is not loopback (localhost,
 * localhost., 127.x.x.x written as four decimal parts, or [::1]). An http://
 * URL that does not parse returns 1. 0 for https, an empty string, NULL and
 * anything that is not http(s). Text only, never a DNS lookup. */
int net_url_cleartext_remote(const char *url);

/* Resolve a redirect Location against the URL it came from. location may be
 * absolute (http or https only, any case), scheme-relative ("//host/p"),
 * an absolute path, a query ("?q"), a fragment or a relative path (replacing
 * everything after the base's last '/'). Dot segments are kept as written.
 * Refuses (-1) a base that is not http(s), a NULL or empty location, a
 * location byte below 0x21, 0x7F or a backslash, any other scheme, and a
 * result that does not pass net_url_parse or does not fit out_sz. On 0, out
 * holds the NUL-terminated URL; on -1 it is unspecified. */
int net_url_resolve(const char *base, const char *location, char *out, size_t out_sz);

#define NET_URL_DISPLAY_MAX 80

/* A form of url that is safe to print in a chat row, whether or not it
 * parses: userinfo (up to the last '@' in the authority), the query and the
 * fragment are dropped textually, control and bidi characters (and TAB, LF,
 * CR) are removed, and text over NET_URL_DISPLAY_MAX bytes is cut on a UTF-8
 * boundary with "..." appended. Always NUL-terminates within cap (cap 0
 * writes nothing). NULL url gives "". Returns the length written. */
size_t net_url_display(const char *url, char *out, size_t cap);

#endif
