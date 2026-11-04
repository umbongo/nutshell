#include "net_guard.h"

#include "paste_filter.h"

#include <stdlib.h>
#include <string.h>

int net_ipv4_is_internal(const unsigned char a[4])
{
    if (a[0] == 127 || a[0] == 10 || a[0] == 0) return 1;
    if (a[0] == 172 && (a[1] & 0xF0) == 16) return 1;
    if (a[0] == 192 && a[1] == 168) return 1;
    if (a[0] == 169 && a[1] == 254) return 1;
    return 0;
}

int net_ipv6_is_internal(const unsigned char a[16])
{
    static const unsigned char zero10[10] = {0};
    int i;
    int low_zero = 1;

    for (i = 0; i < 15; i++) {
        if (a[i] != 0) { low_zero = 0; break; }
    }
    if (low_zero && (a[15] == 0 || a[15] == 1)) return 1; /* :: and ::1 */

    if ((a[0] & 0xFE) == 0xFC) return 1;                   /* fc00::/7 */
    if (a[0] == 0xFE && (a[1] & 0xC0) == 0x80) return 1;   /* fe80::/10 */

    if (memcmp(a, zero10, sizeof zero10) == 0) {
        if (a[10] == 0xFF && a[11] == 0xFF)                /* ::ffff:0:0/96 */
            return net_ipv4_is_internal(a + 12);
        if (a[10] == 0 && a[11] == 0)                      /* ::/96 */
            return net_ipv4_is_internal(a + 12);
    }
    return 0;
}

int net_addr_is_internal(const NetAddr *a)
{
    if (!a) return 1;
    return a->v6 ? net_ipv6_is_internal(a->b) : net_ipv4_is_internal(a->b);
}

static char lower_c(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Case-insensitive prefix test; returns the prefix length or 0. */
static size_t scheme_len(const char *url, int *https)
{
    static const char http[] = "http://";
    static const char https_s[] = "https://";
    size_t i;
    for (i = 0; i < sizeof http - 1; i++)
        if (lower_c(url[i]) != http[i]) break;
    if (i == sizeof http - 1) { *https = 0; return i; }
    for (i = 0; i < sizeof https_s - 1; i++)
        if (lower_c(url[i]) != https_s[i]) break;
    if (i == sizeof https_s - 1) { *https = 1; return i; }
    return 0;
}

int net_url_parse(const char *url, NetUrlParts *out)
{
    size_t len = 0, pos, auth_end, at, hlen;
    const char *auth, *host, *rest;
    int https = 0;
    unsigned long port = 0;

    if (!url || !out) return -1;

    for (len = 0; len < NET_URL_MAX && url[len]; len++) {
        unsigned char c = (unsigned char)url[len];
        if (c < 0x21 || c == 0x7F || c == '\\') return -1;
    }
    if (len >= NET_URL_MAX) return -1;

    pos = scheme_len(url, &https);
    if (pos == 0) return -1;

    auth = url + pos;
    auth_end = 0;
    while (auth[auth_end] && auth[auth_end] != '/' && auth[auth_end] != '?' &&
           auth[auth_end] != '#')
        auth_end++;

    /* Userinfo is everything up to the last '@' in the authority. */
    at = 0;
    for (hlen = 0; hlen < auth_end; hlen++)
        if (auth[hlen] == '@') at = hlen + 1;
    host = auth + at;
    hlen = auth_end - at;           /* host[0..hlen) is host[:port] */

    if (hlen > 0 && host[0] == '[') {
        size_t close = 1;
        while (close < hlen && host[close] != ']') close++;
        if (close >= hlen) return -1;            /* no closing bracket */
        if (close == 1) return -1;               /* empty "[]" */
        if (close - 1 >= NET_HOST_MAX) return -1;
        {   /* an IPv6 literal only: hex digits, ':' and '.', and a ':' */
            size_t k;
            int colons = 0;
            for (k = 1; k < close; k++) {
                char c = host[k];
                if (c == ':') colons++;
                else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                           (c >= 'A' && c <= 'F') || c == '.'))
                    return -1;
            }
            if (colons == 0) return -1;
        }
        memcpy(out->host, host + 1, close - 1);
        out->host[close - 1] = '\0';
        rest = host + close + 1;
        hlen -= close + 1;                       /* rest[0..hlen) is ":port" or empty */
        if (hlen > 0 && rest[0] != ':') return -1;
    } else {
        size_t n = 0;
        while (n < hlen && host[n] != ':') n++;
        if (n == 0) return -1;                   /* empty host */
        if (n >= NET_HOST_MAX) return -1;
        memcpy(out->host, host, n);
        out->host[n] = '\0';
        rest = host + n;
        hlen -= n;
    }

    {   /* ASCII only: a byte >= 0x80 would reach GetAddrInfoW (IDN mapping,
         * so full-width digits can name 127.0.0.1) and WinHttpConnect (which
         * wants punycode) behind the guard's back. The path may keep them. */
        const char *h;
        for (h = out->host; *h; h++)
            if ((unsigned char)*h >= 0x80) return -1;
    }

    if (hlen > 0) {                              /* rest is ':' digits* */
        size_t i;
        for (i = 1; i < hlen; i++) {
            if (rest[i] < '0' || rest[i] > '9') return -1;
            port = port * 10 + (unsigned long)(rest[i] - '0');
            if (port > 65535) return -1;
        }
        if (hlen > 1 && port == 0) return -1;    /* ":0" */
    }
    if (hlen <= 1) port = https ? 443 : 80;      /* none, or an empty ":" */

    out->https = https;
    out->port = (unsigned short)port;

    /* Path plus query, no fragment, always beginning with '/'. */
    {
        const char *p = auth + auth_end;
        size_t n = 0, w = 0;
        out->path[w++] = '/';
        if (*p == '/') p++;
        while (p[n] && p[n] != '#') n++;
        /* url < NET_URL_MAX bytes, so path (<= url) plus '/' and NUL fits */
        memcpy(out->path + w, p, n);
        w += n;
        out->path[w] = '\0';
    }
    return 0;
}

static int is_loopback_host(const char *h)
{
    static const char lh[] = "localhost";
    size_t i, n = strlen(h);

    /* localhost, with or without one trailing dot */
    if (n == sizeof lh - 1 || n == sizeof lh) {
        int match = 1;
        for (i = 0; i < sizeof lh - 1; i++)
            if (lower_c(h[i]) != lh[i]) { match = 0; break; }
        if (match && (n == sizeof lh - 1 || h[n - 1] == '.')) return 1;
    }

    if (strcmp(h, "::1") == 0) return 1;

    /* dotted quad: four decimal parts of 1-3 digits, each 0-255, first 127 */
    {
        int parts = 0;
        unsigned long first = 0;
        const char *p = h;
        while (parts < 4) {
            unsigned long v = 0;
            int digits = 0;
            while (*p >= '0' && *p <= '9') {
                v = v * 10 + (unsigned long)(*p - '0');
                digits++;
                p++;
                if (digits > 3) return 0;
            }
            if (digits == 0 || v > 255) return 0;
            if (parts == 0) first = v;
            parts++;
            if (parts < 4) {
                if (*p != '.') return 0;
                p++;
            }
        }
        if (*p != '\0') return 0;
        return first == 127;
    }
}

int net_url_cleartext_remote(const char *url)
{
    NetUrlParts p;
    int https = 0;

    if (!url) return 0;
    if (scheme_len(url, &https) == 0 || https) return 0;
    if (net_url_parse(url, &p) != 0) return 1;   /* unparseable http: warn */
    return is_loopback_host(p.host) ? 0 : 1;
}

/* Offset of the end of the authority in a URL that starts with a scheme
 * prefix of scheme_n bytes. */
static size_t authority_end(const char *url, size_t scheme_n)
{
    size_t i = scheme_n;
    while (url[i] && url[i] != '/' && url[i] != '?' && url[i] != '#') i++;
    return i;
}

int net_url_resolve(const char *base, const char *location, char *out, size_t out_sz)
{
    char tmp[NET_URL_MAX];
    NetUrlParts parts;
    const char *tail;
    size_t i, head_n, tail_n, loc_n, scheme_n, auth_n, path_end;
    int https = 0, loc_https = 0;

    if (!base || !location || !out || out_sz == 0) return -1;

    scheme_n = scheme_len(base, &https);
    if (scheme_n == 0) return -1;

    for (loc_n = 0; location[loc_n]; loc_n++) {
        unsigned char c = (unsigned char)location[loc_n];
        if (c < 0x21 || c == 0x7F || c == '\\') return -1;
        if (loc_n >= NET_URL_MAX) return -1;
    }
    if (loc_n == 0) return -1;

    auth_n = authority_end(base, scheme_n);     /* base[0..auth_n) is scheme + authority */

    /* A ':' before the first '/', '?' or '#' makes the location a scheme. */
    for (i = 0; i < loc_n; i++) {
        char c = location[i];
        if (c == '/' || c == '?' || c == '#') break;
        if (c == ':') {
            if (scheme_len(location, &loc_https) == 0) return -1;
            head_n = 0;
            tail = location;
            tail_n = loc_n;
            goto build;
        }
    }

    if (location[0] == '/' && location[1] == '/') {
        head_n = https ? 6 : 5;                  /* "https:" / "http:" */
        tail = location;
        tail_n = loc_n;
        if (head_n + tail_n >= sizeof tmp) return -1;
        memcpy(tmp, base, head_n);
    } else if (location[0] == '/') {
        head_n = auth_n;
        tail = location;
        tail_n = loc_n;
        if (head_n + tail_n >= sizeof tmp) return -1;
        memcpy(tmp, base, head_n);
    } else if (location[0] == '?' || location[0] == '#') {
        /* base without its fragment (and, for '?', without its query) */
        head_n = auth_n;
        while (base[head_n] && base[head_n] != '#' &&
               !(location[0] == '?' && base[head_n] == '?'))
            head_n++;
        tail = location;
        tail_n = loc_n;
        if (head_n + tail_n >= sizeof tmp) return -1;
        memcpy(tmp, base, head_n);
    } else {
        /* replace the base path after its last '/' */
        path_end = auth_n;
        while (base[path_end] && base[path_end] != '?' && base[path_end] != '#')
            path_end++;
        head_n = path_end;
        while (head_n > auth_n && base[head_n - 1] != '/') head_n--;
        if (head_n == auth_n) {                  /* no path: "http://host" */
            if (auth_n + 1 + loc_n >= sizeof tmp) return -1;
            memcpy(tmp, base, auth_n);
            tmp[auth_n] = '/';
            head_n = auth_n + 1;
            tail = location;
            tail_n = loc_n;
            memcpy(tmp + head_n, tail, tail_n);
            tmp[head_n + tail_n] = '\0';
            goto check;
        }
        tail = location;
        tail_n = loc_n;
        if (head_n + tail_n >= sizeof tmp) return -1;
        memcpy(tmp, base, head_n);
    }
    memcpy(tmp + head_n, tail, tail_n);
    tmp[head_n + tail_n] = '\0';
    goto check;

build:
    if (tail_n >= sizeof tmp) return -1;
    memcpy(tmp, tail, tail_n);
    tmp[tail_n] = '\0';

check:
    if (net_url_parse(tmp, &parts) != 0) return -1;
    i = strlen(tmp);
    if (i >= out_sz) return -1;
    memcpy(out, tmp, i + 1);
    return 0;
}

size_t net_url_display(const char *url, char *out, size_t cap)
{
    const char *p, *auth, *at;
    char *buf;
    size_t span, auth_n, n, flen, w, keep, dots, avail;

    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!url) return 0;

    /* Where the authority starts: after "://" when there is one. */
    auth = strstr(url, "://");
    auth = auth ? auth + 3 : url;

    /* The text is cut at the first '?' or '#'. */
    span = 0;
    while (url[span] && url[span] != '?' && url[span] != '#') span++;
    if ((size_t)(auth - url) > span) auth = url + span;  /* "://" sat in a query */

    /* Userinfo: up to the last '@' in the authority. */
    auth_n = 0;
    while (auth + auth_n < url + span && auth[auth_n] != '/') auth_n++;
    at = NULL;
    for (n = 0; n < auth_n; n++)
        if (auth[n] == '@') at = auth + n;

    buf = (char *)malloc(span + 1);
    if (!buf) return 0;
    w = 0;
    for (p = url; p < url + span; p++) {
        if (p >= auth && at && p <= at) continue;      /* userinfo and its '@' */
        buf[w++] = *p;
    }
    flen = paste_filter_controls(buf, w, buf, NULL);

    /* TAB, LF and CR survive the paste filter; a chat row must not have them. */
    for (n = 0, w = 0; n < flen; n++) {
        if (buf[n] == '\t' || buf[n] == '\n' || buf[n] == '\r') continue;
        buf[w++] = buf[n];
    }
    flen = w;

    avail = cap - 1;
    if (flen <= NET_URL_DISPLAY_MAX && flen <= avail) {
        memcpy(out, buf, flen);
        out[flen] = '\0';
        free(buf);
        return flen;
    }

    dots = avail < 3 ? avail : 3;
    keep = avail - dots;
    if (keep > NET_URL_DISPLAY_MAX) keep = NET_URL_DISPLAY_MAX;
    if (keep > flen) keep = flen;
    while (keep > 0 && ((unsigned char)buf[keep] & 0xC0) == 0x80) keep--;
    memcpy(out, buf, keep);
    memset(out + keep, '.', dots);
    out[keep + dots] = '\0';
    free(buf);
    return keep + dots;
}
