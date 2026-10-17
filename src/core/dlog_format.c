/* src/core/dlog_format.c -- the pure half of dlog: the line formatter (a
 * printf subset that quotes and redacts per field), key-name redaction, the
 * final-line secret scanner, known-secret scrubbing and the terminal byte
 * escaper. No clock, no I/O, no lock beyond the tiny secrets spinlock.
 * Tested natively by tests/test_dlog.c. */
#include "dlog.h"
#include "dlog_internal.h"
#include "secure_zero.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- UTF-8 ---------------------------------------------------------------- */

/* Length (1-4) of the valid UTF-8 sequence at s (n bytes available), else 0. */
static size_t utf8_seq(const unsigned char *s, size_t n)
{
    if (n == 0) return 0;
    unsigned char c = s[0];
    if (c < 0x80) return 1;
    size_t need;
    unsigned char lo = 0x80, hi = 0xBF;
    if (c >= 0xC2 && c <= 0xDF) need = 1;
    else if (c == 0xE0) { need = 2; lo = 0xA0; }
    else if (c == 0xED) { need = 2; hi = 0x9F; }
    else if (c >= 0xE1 && c <= 0xEF) need = 2;
    else if (c == 0xF0) { need = 3; lo = 0x90; }
    else if (c >= 0xF1 && c <= 0xF3) need = 3;
    else if (c == 0xF4) { need = 3; hi = 0x8F; }
    else return 0;
    if (n < need + 1) return 0;
    if (s[1] < lo || s[1] > hi) return 0;
    for (size_t i = 2; i <= need; i++)
        if ((s[i] & 0xC0) != 0x80) return 0;
    return need + 1;
}

static unsigned char lower_c(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c - 'A' + 'a') : c;
}

/* ---- The output cursor ----------------------------------------------------- */

void dlog_out_init(DlogOut *o, char *buf, size_t cap, size_t prefix_len, size_t reserve)
{
    o->buf = buf;
    o->cap = cap;
    o->limit = (cap > reserve) ? cap - reserve : 0;
    o->len = prefix_len;
    o->cut = 0;
    o->quote_open = 0;
    if (prefix_len > o->limit) { o->cut = 1; o->len = 0; }
}

static void out_unit(DlogOut *o, const char *s, size_t n)
{
    if (o->cut) return;
    if (o->len + n > o->limit) { o->cut = 1; return; }
    memcpy(o->buf + o->len, s, n);
    o->len += n;
}

/* Writes into the reserve (the closing quote, the marker, the LF). */
static void out_force(DlogOut *o, const char *s, size_t n)
{
    if (o->len + n > o->cap) return;
    memcpy(o->buf + o->len, s, n);
    o->len += n;
}

static void out_hex(DlogOut *o, unsigned char c)
{
    char e[4];
    e[0] = '\\';
    e[1] = 'x';
    e[2] = "0123456789abcdef"[(c >> 4) & 0xF];
    e[3] = "0123456789abcdef"[c & 0xF];
    out_unit(o, e, 4);
}

size_t dlog_out_finish(DlogOut *o, int newline)
{
    if (o->cut) {
        if (o->quote_open) { out_force(o, "\"", 1); o->quote_open = 0; }
        out_force(o, " truncated=1", 12);
    } else if (o->quote_open) {
        out_force(o, "\"", 1);
        o->quote_open = 0;
    }
    if (newline) out_force(o, "\n", 1);
    o->buf[o->len] = '\0';
    return o->len;
}

void dlog_out_event(DlogOut *o, const char *event)
{
    if (!event || !event[0]) { out_unit(o, "-", 1); return; }
    for (const char *p = event; *p; p++) {
        char c = *p;
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                 c == '.' || c == '_' || c == '-';
        if (!ok) c = '_';
        out_unit(o, &c, 1);
    }
}

/* ---- Names that must not be logged ----------------------------------------------- */

static const char *const k_safe_keys[] = {
    "auth_method", "auth_type", "auth_result", "auth_ok",
    "key_name", "key_type", "key_mods",
    "tokens", "tokens_in", "tokens_out", "token_count", "tokens_total",
    "input_tokens", "output_tokens", "max_tokens", "total_tokens",
    /* real diagnostics whose names merely contain "key": a keystroke latency and a host-key policy flag */
    "keystroke_ms", "strict_hostkey"
};

static const char *const k_bad_parts[] = { "pass", "secret", "token", "key", "auth", "cookie" };

int dlog_key_is_sensitive(const char *key)
{
    if (!key || !key[0]) return 0;
    char low[80];
    size_t n = 0;
    for (; key[n] && n + 1 < sizeof(low); n++) low[n] = (char)lower_c((unsigned char)key[n]);
    low[n] = '\0';
    for (size_t i = 0; i < sizeof(k_safe_keys) / sizeof(k_safe_keys[0]); i++)
        if (strcmp(low, k_safe_keys[i]) == 0) return 0;
    for (size_t i = 0; i < sizeof(k_bad_parts) / sizeof(k_bad_parts[0]); i++)
        if (strstr(low, k_bad_parts[i])) return 1;
    return 0;
}

/* ---- Values --------------------------------------------------------------------- */

/* Write a string as a logfmt value. wrapped = the format already has the quotes
 * around the %s: escape but add none. */
static void put_value(DlogOut *o, const char *s, size_t n, int wrapped)
{
    const unsigned char *u = (const unsigned char *)s;
    int need = (n == 0 && !wrapped);
    for (size_t i = 0; i < n && !need; ) {
        unsigned char c = u[i];
        if (c == ' ' || c == '"' || c == '=' || c == '\\' || c < 0x20 || c == 0x7F) { need = 1; break; }
        if (c >= 0x80) {
            size_t k = utf8_seq(u + i, n - i);
            if (k == 0) { need = 1; break; }
            i += k;
        } else {
            i++;
        }
    }
    int quote = need && !wrapped;
    if (quote) {
        out_unit(o, "\"", 1);
        if (!o->cut) o->quote_open = 1;
    }
    for (size_t i = 0; i < n && !o->cut; ) {
        unsigned char c = u[i];
        if (c == '"') { out_unit(o, "\\\"", 2); i++; }
        else if (c == '\\') { out_unit(o, "\\\\", 2); i++; }
        else if (c < 0x20 || c == 0x7F) { out_hex(o, c); i++; }
        else if (c >= 0x80) {
            size_t k = utf8_seq(u + i, n - i);
            if (k == 0) { out_hex(o, c); i++; }
            else { out_unit(o, s + i, k); i += k; }
        } else { out_unit(o, s + i, 1); i++; }
    }
    if (quote && !o->cut) { out_force(o, "\"", 1); o->quote_open = 0; }
}

size_t dlog_fmt_value(char *out, size_t cap, const char *value)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (cap < 4) return 0;
    DlogOut o;
    dlog_out_init(&o, out, cap - 1, 0, 2);
    if (!value) value = "-";
    put_value(&o, value, strlen(value), 0);
    if (o.quote_open) { out_force(&o, "\"", 1); o.quote_open = 0; }
    out[o.len] = '\0';
    return o.len;
}

/* ---- The printf subset ---------------------------------------------------------------- */

typedef struct {
    int left, plus, space, alt, zero;
    int width, prec;
} FmtSpec;

static void pad_and_put(DlogOut *o, const char *body, size_t n, const FmtSpec *sp)
{
    char tmp[160];
    size_t w = (sp->width > 0) ? (size_t)sp->width : 0;
    if (w > 100) w = 100;
    if (n > 100) n = 100;
    size_t pad = (w > n) ? w - n : 0;
    size_t k = 0;
    if (!sp->left) for (size_t i = 0; i < pad; i++) tmp[k++] = ' ';
    memcpy(tmp + k, body, n);
    k += n;
    if (sp->left) for (size_t i = 0; i < pad; i++) tmp[k++] = ' ';
    out_unit(o, tmp, k);
}

static void put_int(DlogOut *o, int neg, unsigned long long v, unsigned base, int upper, const FmtSpec *sp)
{
    char dig[72];
    size_t nd = 0;
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) dig[nd++] = '0';
    while (v > 0) { dig[nd++] = set[v % base]; v /= base; }
    int zero_value = (nd == 1 && dig[0] == '0');
    size_t zeros = (sp->prec >= 0 && (size_t)sp->prec > nd) ? (size_t)sp->prec - nd : 0;
    char sign[3];
    size_t sl = 0;
    if (neg) sign[sl++] = '-';
    else if (base == 10 && sp->plus) sign[sl++] = '+';
    else if (base == 10 && sp->space) sign[sl++] = ' ';
    if (base == 16 && sp->alt && !zero_value) { sign[sl++] = '0'; sign[sl++] = upper ? 'X' : 'x'; }
    size_t w = (sp->width > 0 && sp->width <= 100) ? (size_t)sp->width : 0;
    size_t len = sl + zeros + nd;
    size_t pad = (w > len) ? w - len : 0;
    char tmp[200];
    size_t k = 0;
    if (!sp->left && !(sp->zero && sp->prec < 0)) for (size_t i = 0; i < pad; i++) tmp[k++] = ' ';
    for (size_t i = 0; i < sl; i++) tmp[k++] = sign[i];
    if (!sp->left && sp->zero && sp->prec < 0) for (size_t i = 0; i < pad; i++) tmp[k++] = '0';
    for (size_t i = 0; i < zeros && k < 120; i++) tmp[k++] = '0';
    while (nd > 0 && k < sizeof(tmp) - 1) tmp[k++] = dig[--nd];
    if (sp->left) for (size_t i = 0; i < pad; i++) tmp[k++] = ' ';
    out_unit(o, tmp, k);
}

static void put_double(DlogOut *o, double d, char conv, const FmtSpec *sp)
{
    char tmp[128];
    int p = (sp->prec < 0) ? 6 : (sp->prec > 40 ? 40 : sp->prec);
    int w;
    switch (conv) {
    case 'e': w = snprintf(tmp, sizeof(tmp), "%.*e", p, d); break;
    case 'E': w = snprintf(tmp, sizeof(tmp), "%.*E", p, d); break;
    case 'g': w = snprintf(tmp, sizeof(tmp), "%.*g", p, d); break;
    case 'G': w = snprintf(tmp, sizeof(tmp), "%.*G", p, d); break;
    default:  w = snprintf(tmp, sizeof(tmp), "%.*f", p, d); break;
    }
    if (w < 0) return;
    if ((size_t)w >= sizeof(tmp)) w = (int)sizeof(tmp) - 1;
    if (sp->plus && tmp[0] != '-' && (size_t)w + 1 < sizeof(tmp)) {
        memmove(tmp + 1, tmp, (size_t)w + 1);
        tmp[0] = '+';
        w++;
    }
    pad_and_put(o, tmp, (size_t)w, sp);
}

typedef enum { LEN_NONE, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_Z, LEN_J, LEN_T, LEN_BIGL } LenMod;

/* The key= that ends just before fmt position `at` (the '%'), "" when the
 * conversion is not a value of some key. */
static void key_before(const char *fmt, const char *at, char *key, size_t cap)
{
    key[0] = '\0';
    const char *e = at;
    if (e > fmt && e[-1] == '"') e--;
    if (e <= fmt || e[-1] != '=') return;
    e--;
    const char *b = e;
    while (b > fmt) {
        char c = b[-1];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '.' || c == '-')
            b--;
        else
            break;
    }
    size_t n = (size_t)(e - b);
    if (n == 0 || n >= cap) return;
    memcpy(key, b, n);
    key[n] = '\0';
}

static void fmt_fields(DlogOut *o, const char *fmt, va_list ap)
{
    const char *p = fmt;
    while (*p && !o->cut) {
        if (*p != '%') {
            const char *q = p;
            while (*q && *q != '%') q++;
            out_unit(o, p, (size_t)(q - p));
            p = q;
            continue;
        }
        const char *start = p++;
        FmtSpec sp;
        memset(&sp, 0, sizeof(sp));
        sp.prec = -1;
        for (;; p++) {
            if (*p == '-') sp.left = 1;
            else if (*p == '+') sp.plus = 1;
            else if (*p == ' ') sp.space = 1;
            else if (*p == '#') sp.alt = 1;
            else if (*p == '0') sp.zero = 1;
            else break;
        }
        if (*p == '*') {
            sp.width = va_arg(ap, int);
            if (sp.width < 0) { sp.left = 1; sp.width = -sp.width; }
            p++;
        } else {
            while (*p >= '0' && *p <= '9') { if (sp.width < 10000) sp.width = sp.width * 10 + (*p - '0'); p++; }
        }
        if (*p == '.') {
            p++;
            if (*p == '*') { sp.prec = va_arg(ap, int); p++; }
            else { sp.prec = 0; while (*p >= '0' && *p <= '9') { if (sp.prec < 10000) sp.prec = sp.prec * 10 + (*p - '0'); p++; } }
        }
        LenMod lm = LEN_NONE;
        if (*p == 'h') { p++; if (*p == 'h') { lm = LEN_HH; p++; } else lm = LEN_H; }
        else if (*p == 'l') { p++; if (*p == 'l') { lm = LEN_LL; p++; } else lm = LEN_L; }
        else if (*p == 'z') { lm = LEN_Z; p++; }
        else if (*p == 'j') { lm = LEN_J; p++; }
        else if (*p == 't') { lm = LEN_T; p++; }
        else if (*p == 'L') { lm = LEN_BIGL; p++; }
        char conv = *p;
        if (conv) p++;

        char key[64];
        key_before(fmt, start, key, sizeof(key));
        int redact = dlog_key_is_sensitive(key);
        int wrapped = (start > fmt && start[-1] == '"');

        switch (conv) {
        case '%':
            out_unit(o, "%", 1);
            break;
        case 'd': case 'i': {
            long long v;
            switch (lm) {
            case LEN_HH: v = (signed char)va_arg(ap, int); break;
            case LEN_H:  v = (short)va_arg(ap, int); break;
            case LEN_L:  v = va_arg(ap, long); break;
            case LEN_LL: v = va_arg(ap, long long); break;
            case LEN_Z: case LEN_T: v = (long long)va_arg(ap, ptrdiff_t); break;
            case LEN_J:  v = (long long)va_arg(ap, long long); break;
            default:     v = va_arg(ap, int); break;
            }
            if (redact) { out_unit(o, "[REDACTED]", 10); break; }
            put_int(o, v < 0, v < 0 ? 0ULL - (unsigned long long)v : (unsigned long long)v, 10, 0, &sp);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            unsigned long long v;
            switch (lm) {
            case LEN_HH: v = (unsigned char)va_arg(ap, unsigned); break;
            case LEN_H:  v = (unsigned short)va_arg(ap, unsigned); break;
            case LEN_L:  v = va_arg(ap, unsigned long); break;
            case LEN_LL: v = va_arg(ap, unsigned long long); break;
            case LEN_Z:  v = (unsigned long long)va_arg(ap, size_t); break;
            case LEN_T:  v = (unsigned long long)va_arg(ap, ptrdiff_t); break;
            case LEN_J:  v = va_arg(ap, unsigned long long); break;
            default:     v = va_arg(ap, unsigned); break;
            }
            if (redact) { out_unit(o, "[REDACTED]", 10); break; }
            put_int(o, 0, v, conv == 'u' ? 10u : (conv == 'o' ? 8u : 16u), conv == 'X', &sp);
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            double d = (lm == LEN_BIGL) ? (double)va_arg(ap, long double) : va_arg(ap, double);
            if (redact) { out_unit(o, "[REDACTED]", 10); break; }
            put_double(o, d, conv, &sp);
            break;
        }
        case 'c': {
            char ch = (char)va_arg(ap, int);
            if (redact) { out_unit(o, "[REDACTED]", 10); break; }
            put_value(o, &ch, 1, wrapped);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (redact) { out_unit(o, "[REDACTED]", 10); break; }
            if (!s) s = "-";
            size_t n = 0;
            while (s[n] && (sp.prec < 0 || (int)n < sp.prec)) n++;
            put_value(o, s, n, wrapped);
            break;
        }
        case 'p': {
            void *ptr = va_arg(ap, void *);
            if (redact) { out_unit(o, "[REDACTED]", 10); break; }
            char tmp[40];
            int w = snprintf(tmp, sizeof(tmp), "%p", ptr);
            if (w > 0) out_unit(o, tmp, (size_t)w);
            break;
        }
        default:
            /* unknown conversion: the text as written */
            out_unit(o, start, (size_t)(p - start));
            break;
        }
    }
}

void dlog_out_fmt(DlogOut *o, const char *fmt, va_list ap)
{
    if (!fmt || !fmt[0]) return;
    size_t before = o->len;
    out_unit(o, " ", 1);
    if (o->cut) return;
    fmt_fields(o, fmt, ap);
    if (!o->cut && o->len == before + 1) o->len = before;     /* produced nothing: no trailing space */
}

void dlog_out_raw_fields(DlogOut *o, const char *fields)
{
    if (!fields || !fields[0]) return;
    out_unit(o, " ", 1);
    const unsigned char *u = (const unsigned char *)fields;
    size_t n = strlen(fields);
    size_t i = 0;
    while (i < n && !o->cut) {
        size_t k = 1;
        unsigned char c = u[i];
        if (c == '\\' && i + 1 < n) {
            k = (u[i + 1] == 'x' && i + 3 < n) ? 4 : 2;
        } else if (c == '"') {
            o->quote_open = !o->quote_open;
        } else if (c >= 0x80) {
            k = utf8_seq(u + i, n - i);
            if (k == 0) k = 1;
        } else if (c < 0x20 || c == 0x7F) {
            /* a raw control byte would split the line */
            if (c == '\n' || c == '\r' || c == '\t' || c < 0x20 || c == 0x7F) {
                out_hex(o, c);
                i++;
                continue;
            }
        }
        out_unit(o, fields + i, k);
        i += k;
    }
}

/* ---- The prefix and whole lines ------------------------------------------------------------ */

size_t dlog_make_prefix(char *buf, size_t cap, long long unix_ms, int offset_min,
                        int ev_level, int cat, unsigned sid, const char *thread)
{
    char ts[DLOG_TS_LEN + 4];
    dlog_format_timestamp(unix_ms, offset_min, ts);
    const char *lv = dlog_ev_level_tag(ev_level);
    const char *cn = dlog_cat_name(cat);
    char sidbuf[16];
    if (sid == 0) { sidbuf[0] = '-'; sidbuf[1] = '\0'; }
    else (void)snprintf(sidbuf, sizeof(sidbuf), "s%u", sid);
    char th[8];
    size_t t = 0;
    if (thread) for (; thread[t] && t < 7; t++) th[t] = thread[t];
    if (t == 0) th[t++] = '?';
    th[t] = '\0';
    int n = snprintf(buf, cap, "%s %s %s %s %s ", ts, lv ? lv : "?????", cn ? cn : "?", sidbuf, th);
    if (n < 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

size_t dlog_format_line(char *buf, size_t cap, long long unix_ms, int offset_min,
                        int ev_level, int cat, unsigned sid, const char *thread,
                        const char *event, const char *fields)
{
    if (!buf || cap < 2) return 0;
    buf[0] = '\0';
    size_t eff = cap - 1;
    if (eff > DLOG_LINE_MAX) eff = DLOG_LINE_MAX;
    size_t pl = dlog_make_prefix(buf, eff + 1, unix_ms, offset_min, ev_level, cat, sid, thread);
    if (pl == 0) { buf[0] = '\0'; return 0; }
    DlogOut o;
    dlog_out_init(&o, buf, eff, pl, DLOG_OUT_RESERVE);
    dlog_out_event(&o, event);
    dlog_out_raw_fields(&o, fields);
    return dlog_out_finish(&o, 1);
}

size_t dlog_format_body(char *out, size_t cap, const char *event, const char *fmt, ...)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (cap < 2) return 0;
    size_t eff = cap - 1;
    if (eff > DLOG_LINE_MAX) eff = DLOG_LINE_MAX;
    DlogOut o;
    dlog_out_init(&o, out, eff, 0, DLOG_OUT_RESERVE);
    dlog_out_event(&o, event);
    va_list ap;
    va_start(ap, fmt);
    dlog_out_fmt(&o, fmt, ap);
    va_end(ap);
    return dlog_out_finish(&o, 0);
}

/* ---- The final-line scanner (ported from the second implementation) -------------------------- */

#define RD_MARK     "[REDACTED]"
#define RD_MARK_LEN 10u

static const char *const k_rd_keys[] = {
    "password", "passphrase", "pass", "secret", "token",
    "api_key", "apikey", "authorization", "private_key"
};

static int rd_alnum(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static int rd_key_char(unsigned char c) { return rd_alnum(c) || c == '_'; }

static int rd_is_secret_key(const char *k, size_t n)
{
    for (size_t i = 0; i < sizeof(k_rd_keys) / sizeof(k_rd_keys[0]); i++) {
        const char *w = k_rd_keys[i];
        if (strlen(w) != n) continue;
        size_t j = 0;
        while (j < n && lower_c((unsigned char)k[j]) == (unsigned char)w[j]) j++;
        if (j == n) return 1;
    }
    return 0;
}

/* An environment-style name: only [A-Z0-9_] and containing SECRET, TOKEN, PASSWORD,
 * PASSWD or API_KEY (export FOO_TOKEN=..., GITHUB_TOKEN=..., DB_PASSWORD=...). */
static int rd_is_env_secret(const char *k, size_t n)
{
    static const char *const parts[] = { "SECRET", "TOKEN", "PASSWORD", "PASSWD", "API_KEY" };
    for (size_t i = 0; i < n; i++) {
        char c = k[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return 0;
    }
    for (size_t p = 0; p < sizeof(parts) / sizeof(parts[0]); p++) {
        size_t m = strlen(parts[p]);
        for (size_t i = 0; i + m <= n; i++)
            if (memcmp(k + i, parts[p], m) == 0) return 1;
    }
    return 0;
}

static size_t rd_value_end(const char *s, size_t n, size_t vs)
{
    size_t k = vs;
    if (k < n && s[k] == '"') {
        k++;
        while (k < n) {
            if (s[k] == '\\' && k + 1 < n) k += 2;
            else if (s[k] == '"') return k + 1;
            else k++;
        }
        while (k > vs && (s[k - 1] == '\n' || s[k - 1] == '\r')) k--;
        return k;
    }
    while (k < n) {
        char c = s[k];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '"') break;
        if (c == '\\' && k + 1 < n && s[k + 1] == '"') break;
        k++;
    }
    return k;
}

static int rd_starts_ci(const char *s, size_t n, const char *prefix)
{
    size_t m = strlen(prefix);
    if (n < m) return 0;
    for (size_t i = 0; i < m; i++)
        if (lower_c((unsigned char)s[i]) != (unsigned char)prefix[i]) return 0;
    return 1;
}

static int rd_token_char(unsigned char c)
{
    return rd_alnum(c) || c == '.' || c == '_' || c == '~' || c == '+' || c == '/' || c == '=' || c == '-';
}

static int rd_sk_char(unsigned char c) { return rd_alnum(c) || c == '_' || c == '-'; }

static const char *rd_find(const char *s, size_t n, const char *needle)
{
    size_t m = strlen(needle);
    if (m == 0 || n < m) return NULL;
    for (size_t i = 0; i + m <= n; i++)
        if (memcmp(s + i, needle, m) == 0) return s + i;
    return NULL;
}

/* "-----BEGIN [A-Z ]*PRIVATE KEY-----" at s[i]: the end of the block or 0. */
static size_t rd_pem_end(const char *s, size_t n, size_t i)
{
    static const char begin[] = "-----BEGIN ";
    const size_t blen = sizeof(begin) - 1;
    if (n - i < blen || memcmp(s + i, begin, blen) != 0) return 0;
    size_t k = i + blen;
    while (k < n && ((s[k] >= 'A' && s[k] <= 'Z') || s[k] == ' ')) k++;
    if (k - (i + blen) < 11 || k + 5 > n || memcmp(s + k, "-----", 5) != 0) return 0;
    if (memcmp(s + k - 11, "PRIVATE KEY", 11) != 0) return 0;
    size_t he = k + 5;
    const char *end = rd_find(s + he, n - he, "-----END ");
    if (end) {
        size_t from = (size_t)(end - s);
        const char *pk = rd_find(s + from, n - from, "PRIVATE KEY-----");
        if (pk) return (size_t)(pk - s) + 16;
    }
    size_t e = n;
    while (e > he && (s[e - 1] == '\n' || s[e - 1] == '\r')) e--;
    return e;
}

size_t dlog_redact_line(char *line, size_t len, size_t cap)
{
    if (!line || len == 0 || cap < 2) return len;
    char tmp[DLOG_LINE_MAX + 64];
    size_t max = cap - 1;
    if (max > sizeof(tmp) - 1) max = sizeof(tmp) - 1;
    int had_nl = line[len - 1] == '\n';
    const char *s = line;
    size_t n = len;
    size_t pos = 0;
    int over = 0;
#define RD_PUT(p, k) do { size_t kk_ = (k); if (pos + kk_ <= max) { memcpy(tmp + pos, (p), kk_); pos += kk_; } else over = 1; } while (0)

    size_t i = 0;
    while (i < n && !over) {
        unsigned char c = (unsigned char)s[i];
        int prev_key = i > 0 && rd_key_char((unsigned char)s[i - 1]);
        if (!prev_key && rd_key_char(c)) {
            size_t j = i;
            while (j < n && rd_key_char((unsigned char)s[j])) j++;
            if (j < n && s[j] == '=' && (rd_is_secret_key(s + i, j - i) || rd_is_env_secret(s + i, j - i))) {
                size_t ve = rd_value_end(s, n, j + 1);
                if (ve > j + 1) {
                    RD_PUT(s + i, j + 1 - i);
                    RD_PUT(RD_MARK, RD_MARK_LEN);
                    i = ve;
                    continue;
                }
            }
        }
        if (!prev_key && (c == 'B' || c == 'b') && rd_starts_ci(s + i, n - i, "bearer ")) {
            size_t ts = i + 7, te = ts;
            while (te < n && rd_token_char((unsigned char)s[te])) te++;
            if (te > ts) {
                RD_PUT(s + i, 7);
                RD_PUT(RD_MARK, RD_MARK_LEN);
                i = te;
                continue;
            }
        }
        if (!prev_key && c == 's' && i + 2 < n && s[i + 1] == 'k' && s[i + 2] == '-') {
            size_t k = i + 3;
            while (k < n && rd_sk_char((unsigned char)s[k])) k++;
            if (k - (i + 3) >= 16) {
                RD_PUT(RD_MARK, RD_MARK_LEN);
                i = k;
                continue;
            }
        }
        if (c == '$' && (rd_starts_ci(s + i, n - i, "$dpapi$v1$") || rd_starts_ci(s + i, n - i, "$aes256gcm$v1$"))) {
            size_t k = i;
            while (k < n && s[k] != ' ' && s[k] != '"' && s[k] != '\n' && s[k] != '\r') k++;
            RD_PUT(RD_MARK, RD_MARK_LEN);
            i = k;
            continue;
        }
        if (c == '-') {
            size_t e = rd_pem_end(s, n, i);
            if (e > i) {
                RD_PUT(RD_MARK, RD_MARK_LEN);
                i = e;
                continue;
            }
        }
        RD_PUT(&s[i], 1);
        i++;
    }
#undef RD_PUT
    if (over && had_nl) {
        if (pos >= max && max > 0) { pos = max; tmp[max - 1] = '\n'; }
        else if (pos < max) tmp[pos++] = '\n';
    }
    if (pos == len && memcmp(tmp, line, len) == 0) return len;
    memcpy(line, tmp, pos);
    line[pos] = '\0';
    return pos;
}

/* ---- Repeat comparison ------------------------------------------------------------------------- */

/* Volatile fields: a counter or a clock reading that differs on every line of an
 * otherwise identical run (paint tick=, render.draw us=, poll.* since_paint_ms=). */
static int rk_volatile_key(const char *k, size_t n)
{
    static const char *const fixed[] = { "tick", "us", "ms", "since_paint_ms", "elapsed_ms", "quiet_ms" };
    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++)
        if (strlen(fixed[i]) == n && memcmp(fixed[i], k, n) == 0) return 1;
    if (n > 3 && (memcmp(k + n - 3, "_ms", 3) == 0 || memcmp(k + n - 3, "_us", 3) == 0)) return 1;
    return 0;
}

size_t dlog_repeat_key(char *out, size_t cap, const char *body, size_t len)
{
    if (!out || cap == 0) return 0;
    size_t o = 0;
    size_t i = 0;
    while (i < len && body[i] != ' ' && o + 1 < cap) out[o++] = body[i++];     /* the event */
    while (i < len) {
        if (o + 4 >= cap) { o = cap - 1; break; }                              /* cannot fit: never equal */
        unsigned char c = (unsigned char)body[i];
        int prev_ok = (i == 0) || body[i - 1] == ' ';
        if (prev_ok && rd_key_char(c)) {
            size_t j = i;
            while (j < len && (rd_key_char((unsigned char)body[j]) || body[j] == '.' || body[j] == '-')) j++;
            if (j < len && body[j] == '=' && rk_volatile_key(body + i, j - i)) {
                size_t ve = rd_value_end(body, len, j + 1);
                size_t kl = j + 1 - i;
                if (o + kl + 1 >= cap) { o = cap - 1; break; }
                memcpy(out + o, body + i, kl);
                o += kl;
                out[o++] = '~';
                i = ve;
                continue;
            }
        }
        out[o++] = body[i++];
    }
    out[o] = '\0';
    return o;
}

/* ---- Known secrets ------------------------------------------------------------------------------ */

#define SECRET_CAP 256u
static char g_secrets[DLOG_SECRETS_MAX][SECRET_CAP];
static size_t g_secret_len[DLOG_SECRETS_MAX];
static unsigned g_secret_refs[DLOG_SECRETS_MAX];
static volatile char g_secret_busy;
static unsigned g_secret_epoch;         /* bumped whenever a secret's last registration ends */

unsigned dlog_secret_epoch(void) { return __atomic_load_n(&g_secret_epoch, __ATOMIC_ACQUIRE); }

static void sec_lock(void)
{
    while (__atomic_test_and_set(&g_secret_busy, __ATOMIC_ACQUIRE)) { /* spin: held for a few hundred ns */ }
}
static void sec_unlock(void) { __atomic_clear(&g_secret_busy, __ATOMIC_RELEASE); }

int dlog_secret_add(const char *secret)
{
    if (!secret) return 0;
    size_t n = strlen(secret);
    if (n < 3 || n >= SECRET_CAP) return 0;
    int ok = 0;
    sec_lock();
    int free_slot = -1;
    for (int i = 0; i < DLOG_SECRETS_MAX; i++) {
        if (g_secret_len[i] == n && memcmp(g_secrets[i], secret, n) == 0) {
            g_secret_refs[i]++;                  /* the same secret registered again: counted */
            ok = 1;
            free_slot = -2;
            break;
        }
        if (g_secret_len[i] == 0 && free_slot == -1) free_slot = i;
    }
    if (free_slot >= 0) {
        memcpy(g_secrets[free_slot], secret, n + 1);
        g_secret_len[free_slot] = n;
        g_secret_refs[free_slot] = 1;
        ok = 1;
    }
    sec_unlock();
    return ok;
}

void dlog_secret_remove(const char *secret)
{
    if (!secret) return;
    size_t n = strlen(secret);
    int dropped = 0;
    sec_lock();
    for (int i = 0; i < DLOG_SECRETS_MAX; i++) {
        if (g_secret_len[i] == n && n > 0 && memcmp(g_secrets[i], secret, n) == 0) {
            if (g_secret_refs[i] > 1) { g_secret_refs[i]--; continue; }
            secure_zero(g_secrets[i], SECRET_CAP);
            g_secret_len[i] = 0;
            g_secret_refs[i] = 0;
            dropped = 1;
        }
    }
    sec_unlock();
    if (dropped) {      /* a held tail may be a prefix of it: wipe ours, flag the other threads' */
        (void)__atomic_add_fetch(&g_secret_epoch, 1u, __ATOMIC_ACQ_REL);
        dlog_streams_wipe_tails();
    }
}

void dlog_secret_clear(void)
{
    sec_lock();
    for (int i = 0; i < DLOG_SECRETS_MAX; i++) {
        secure_zero(g_secrets[i], SECRET_CAP);
        g_secret_len[i] = 0;
        g_secret_refs[i] = 0;
    }
    sec_unlock();
    (void)__atomic_add_fetch(&g_secret_epoch, 1u, __ATOMIC_ACQ_REL);
    dlog_streams_wipe_tails();
}

/* The scrub proper, secrets lock held. Copies src[0..stop) to dst replacing each
 * secret that STARTS before `stop` (it may end after it). *used_in = raw bytes
 * consumed. Returns the bytes written; dst is NUL-terminated. */
static size_t scrub_locked(char *dst, size_t dst_cap, const char *src, size_t n, size_t stop, size_t *used_in)
{
    size_t o = 0;
    size_t i = 0;
    while (i < stop) {
        int hit = 0;
        for (int s = 0; s < DLOG_SECRETS_MAX; s++) {
            size_t sl = g_secret_len[s];
            if (sl > 0 && i + sl <= n && src[i] == g_secrets[s][0] && memcmp(src + i, g_secrets[s], sl) == 0) {
                if (o + RD_MARK_LEN >= dst_cap) { hit = -1; break; }
                memcpy(dst + o, RD_MARK, RD_MARK_LEN);
                o += RD_MARK_LEN;
                i += sl;
                hit = 1;
                break;
            }
        }
        if (hit == -1) break;
        if (hit) continue;
        if (o + 1 >= dst_cap) break;
        dst[o++] = src[i++];
    }
    dst[o] = '\0';
    if (used_in) *used_in = i;
    return o;
}

size_t dlog_scrub_copy(char *dst, size_t dst_cap, const char *src, size_t n)
{
    if (!dst || dst_cap == 0) return 0;
    sec_lock();
    size_t o = scrub_locked(dst, dst_cap, src, n, n, NULL);
    sec_unlock();
    return o;
}

size_t dlog_scrub_stream(char *dst, size_t dst_cap, const char *src, size_t n, int final, size_t *consumed)
{
    if (consumed) *consumed = 0;
    if (!dst || dst_cap == 0) return 0;
    sec_lock();
    size_t stop = n;
    if (!final) {                      /* hold back a tail that could be the start of a secret */
        size_t hold = 0;
        for (int s = 0; s < DLOG_SECRETS_MAX; s++) {
            size_t sl = g_secret_len[s];
            if (sl < 2) continue;
            size_t m = (sl - 1 < n) ? sl - 1 : n;
            for (; m > hold; m--)
                if (memcmp(src + n - m, g_secrets[s], m) == 0) { hold = m; break; }
        }
        stop = n - hold;
    }
    size_t used_in = 0;
    size_t o = scrub_locked(dst, dst_cap, src, n, stop, &used_in);
    sec_unlock();
    if (consumed) *consumed = used_in;
    return o;
}

/* ---- Terminal bytes ------------------------------------------------------------------------------ */

void dlog_esc_init(DlogEscState *st)
{
    if (!st) return;
    st->state = 0;
    st->osc_bytes = 0;
}

typedef struct { char *out; size_t cap; size_t o; } EscOut;

static void esc_put(EscOut *e, const char *s, size_t n)
{
    if (e->o + n >= e->cap) return;
    memcpy(e->out + e->o, s, n);
    e->o += n;
}

static void esc_byte(EscOut *e, unsigned char c, const unsigned char *in, size_t i, size_t n, size_t *adv)
{
    *adv = 1;
    switch (c) {
    case 0x1B: esc_put(e, "\\e", 2); return;
    case '\r': esc_put(e, "\\r", 2); return;
    case '\n': esc_put(e, "\\n", 2); return;
    case '\t': esc_put(e, "\\t", 2); return;
    case '"':  esc_put(e, "\\\"", 2); return;
    case '\\': esc_put(e, "\\\\", 2); return;
    default: break;
    }
    if (c < 0x20 || c == 0x7F) {
        char h[4] = { '\\', 'x', "0123456789abcdef"[(c >> 4) & 0xF], "0123456789abcdef"[c & 0xF] };
        esc_put(e, h, 4);
        return;
    }
    if (c >= 0x80) {
        size_t k = utf8_seq(in + i, n - i);
        if (k > 0) { esc_put(e, (const char *)in + i, k); *adv = k; return; }
        char h[4] = { '\\', 'x', "0123456789abcdef"[(c >> 4) & 0xF], "0123456789abcdef"[c & 0xF] };
        esc_put(e, h, 4);
        return;
    }
    esc_put(e, (const char *)&c, 1);
}

size_t dlog_escape_bytes(DlogEscState *st, const void *in, size_t n, char *out, size_t cap, size_t *consumed)
{
    if (consumed) *consumed = 0;
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!st || !in) return 0;
    const unsigned char *u = (const unsigned char *)in;
    EscOut e = { out, cap, 0 };
    size_t i = 0;
    size_t used = n;                          /* input bytes whose output was kept */
    int full = 0;
    while (i < n) {
        if (!full && e.o + 48 >= cap) {       /* no room for the longest token: stop writing, keep tracking */
            full = 1;
            used = i;
            e.cap = e.o;                      /* every later esc_put is refused */
        }
        unsigned char c = u[i];
        size_t adv = 1;
        switch (st->state) {
        case 0:
            if (c == 0x1B) { st->state = 1; i++; continue; }
            esc_byte(&e, c, u, i, n, &adv);
            i += adv;
            continue;
        case 1:
            if (c == ']') { st->state = 2; i++; continue; }
            esc_put(&e, "\\e", 2); st->state = 0; continue;                 /* reprocess c */
        case 2:
            if (c == '5') { st->state = 3; i++; continue; }
            esc_put(&e, "\\e]", 3); st->state = 0; continue;
        case 3:
            if (c == '2') { st->state = 4; i++; continue; }
            esc_put(&e, "\\e]5", 4); st->state = 0; continue;
        case 4:
            if (c == ';') { st->state = 5; st->osc_bytes = 0; i++; continue; }
            esc_put(&e, "\\e]52", 5); st->state = 0; continue;
        case 5:
            if (c == 0x07) {
                char t[48];
                int w = snprintf(t, sizeof(t), "[osc52 bytes=%zu]", st->osc_bytes);
                if (w > 0) esc_put(&e, t, (size_t)w);
                st->state = 0; i++; continue;
            }
            if (c == 0x1B) { st->state = 6; i++; continue; }
            st->osc_bytes++; i++; continue;
        default: /* 6: saw ESC inside the payload */
            if (c == '\\') {
                char t[48];
                int w = snprintf(t, sizeof(t), "[osc52 bytes=%zu]", st->osc_bytes);
                if (w > 0) esc_put(&e, t, (size_t)w);
                st->state = 0; i++; continue;
            }
            st->osc_bytes++; st->state = 5; continue;                         /* the ESC was payload */
        }
    }
    out[e.o] = '\0';
    if (consumed) *consumed = used;
    return e.o;
}
