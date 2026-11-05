/* src/core/ssh_auth_plan.c */
#include "ssh_auth_plan.h"

#include <stdio.h>
#include <string.h>

/* One trimmed item of a comma list: [*s, *s + len). Advances *s past the
 * item and its comma. Returns 0 at the end of the list. */
static int next_item(const char **s, const char **item, size_t *len)
{
    if (**s == '\0') return 0;
    const char *b = *s;
    const char *e = b;
    while (*e != '\0' && *e != ',') e++;
    *s = (*e == ',') ? e + 1 : e;
    while (b < e && (*b == ' ' || *b == '\t')) b++;
    while (e > b && (e[-1] == ' ' || e[-1] == '\t')) e--;
    *item = b;
    *len = (size_t)(e - b);
    return 1;
}

static int item_is(const char *item, size_t len, const char *name)
{
    return strlen(name) == len && memcmp(item, name, len) == 0;
}

static unsigned item_bit(const char *item, size_t len)
{
    if (item_is(item, len, "publickey")) return SSH_AM_PUBLICKEY;
    if (item_is(item, len, "password")) return SSH_AM_PASSWORD;
    if (item_is(item, len, "keyboard-interactive")) return SSH_AM_KBDINT;
    return SSH_AM_OTHER;
}

/* 1 when an earlier item of `list` (before `upto`) equals the item. */
static int seen_before(const char *list, const char *upto, const char *item, size_t len)
{
    const char *s = list, *it;
    size_t n;
    while (s < upto && next_item(&s, &it, &n)) {
        if (it >= upto) break;
        if (n == len && memcmp(it, item, len) == 0) return 1;
    }
    return 0;
}

unsigned ssh_auth_methods_parse(const char *list, char *display, size_t cap)
{
    unsigned bits = 0;
    size_t lim = 0;                 /* usable display bytes, excluding the NUL */
    size_t used = 0;
    int full = 0;

    if (display && cap > 0) {
        display[0] = '\0';
        lim = cap - 1;
        if (lim > SSH_AUTH_DISPLAY_MAX - 1) lim = SSH_AUTH_DISPLAY_MAX - 1;
    }
    if (!list) return 0;

    const char *s = list, *item;
    size_t len;
    while (next_item(&s, &item, &len)) {
        if (len == 0) continue;
        if (seen_before(list, item, item, len)) continue;
        bits |= item_bit(item, len);

        if (!display || full) continue;
        char name[SSH_AUTH_DISPLAY_MAX];
        size_t nl = 0;
        for (size_t i = 0; i < len && nl < sizeof name - 1; i++) {
            unsigned char c = (unsigned char)item[i];
            if (c >= 0x20 && c <= 0x7e) name[nl++] = (char)c;
        }
        if (nl == 0) continue;
        size_t need = nl + (used ? 2 : 0);
        if (used + need > lim) { full = 1; continue; }
        if (used) { display[used++] = ','; display[used++] = ' '; }
        memcpy(display + used, name, nl);
        used += nl;
        display[used] = '\0';
    }
    return bits;
}

void ssh_auth_plan_init(SshAuthPlan *p, int is_key, const char *saved, unsigned offered)
{
    memset(p, 0, sizeof *p);
    p->is_key = is_key ? 1 : 0;
    p->has_saved = (!is_key && saved && saved[0] != '\0') ? 1 : 0;
    p->offered = offered;
}

static int was_tried(const SshAuthPlan *p, SshAuthStep s)
{
    return (p->tried & (1u << (unsigned)s)) != 0;
}

SshAuthStep ssh_auth_next(const SshAuthPlan *p)
{
    if (p->attempts >= SSH_AUTH_MAX_ATTEMPTS) return SSH_STEP_DONE;

    if (p->is_key) {
        if ((p->offered & SSH_AM_PUBLICKEY) && !was_tried(p, SSH_STEP_PUBLICKEY))
            return SSH_STEP_PUBLICKEY;
        if ((p->offered & SSH_AM_KBDINT) && !was_tried(p, SSH_STEP_KBDINT))
            return SSH_STEP_KBDINT;
        return SSH_STEP_DONE;
    }

    if (p->has_saved && (p->offered & SSH_AM_PASSWORD) &&
        !was_tried(p, SSH_STEP_PASSWORD_SAVED))
        return SSH_STEP_PASSWORD_SAVED;
    if ((p->offered & SSH_AM_KBDINT) && !was_tried(p, SSH_STEP_KBDINT))
        return SSH_STEP_KBDINT;
    if (was_tried(p, SSH_STEP_KBDINT) && p->kbd_sent_saved &&
        !(p->offered & SSH_AM_PASSWORD) && !was_tried(p, SSH_STEP_KBDINT_RETRY))
        return SSH_STEP_KBDINT_RETRY;
    if ((p->offered & SSH_AM_PASSWORD) && !was_tried(p, SSH_STEP_PASSWORD_ASK))
        return SSH_STEP_PASSWORD_ASK;
    return SSH_STEP_DONE;
}

void ssh_auth_plan_record(SshAuthPlan *p, SshAuthStep step, int saved_sent)
{
    p->tried |= 1u << (unsigned)step;
    p->attempts++;
    if (saved_sent) {
        p->saved_used = 1;
        if (step == SSH_STEP_KBDINT) p->kbd_sent_saved = 1;
    }
}

void ssh_auth_plan_count_retry(SshAuthPlan *p)
{
    p->attempts++;
}

const char *ssh_auth_saved_for(const SshAuthPlan *p, SshAuthStep step, const char *saved)
{
    if (p->is_key || !p->has_saved || p->saved_used) return NULL;
    if (!saved || saved[0] == '\0') return NULL;
    if (step == SSH_STEP_PASSWORD_SAVED || step == SSH_STEP_KBDINT) return saved;
    return NULL;
}

int ssh_auth_transport_dead(int last_errno)
{
    return last_errno == -7 || last_errno == -13 || last_errno == -43;
}

int ssh_auth_may_continue(int rc, int timed_out, int last_errno)
{
    if (rc != SSH_RC_AUTH_FAILED && rc != SSH_RC_PK_UNVERIFIED) return 0;
    if (timed_out) return 0;
    return ssh_auth_transport_dead(last_errno) ? 0 : 1;
}

int ssh_auth_should_requery(const SshAuthPlan *p, int rc, int timed_out, int last_errno)
{
    return ssh_auth_may_continue(rc, timed_out, last_errno) &&
           ssh_auth_next(p) == SSH_STEP_DONE &&
           !p->requeried && p->attempts < SSH_AUTH_MAX_ATTEMPTS;
}

void ssh_auth_plan_requeried(SshAuthPlan *p, unsigned offered)
{
    p->offered = offered;
    p->requeried = 1;
}

size_t ssh_auth_fail_text(const char *user, const char *host, int is_key, unsigned offered,
                          const char *display, const char *extra, char *buf, size_t cap)
{
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';

    const char *clause = "The server offers: ";
    if (is_key && !(offered & SSH_AM_PUBLICKEY))
        clause = "The server does not offer key authentication; it offers: ";

    int n = snprintf(buf, cap, "Authentication failed for %s@%s.",
                     user ? user : "", host ? host : "");
    size_t len = (n < 0) ? cap : (size_t)n;
    if (len < cap && display && display[0] != '\0') {
        n = snprintf(buf + len, cap - len, " %s%s.", clause, display);
        len = (n < 0) ? cap : len + (size_t)n;
    }
    if (len < cap && extra && extra[0] != '\0') {
        n = snprintf(buf + len, cap - len, " %s", extra);
        len = (n < 0) ? cap : len + (size_t)n;
    }
    if (len >= cap) {
        buf[0] = '\0';
        return 0;
    }
    return len;
}

/* ---- the password-like matcher (spec section 4) ------------------------- */

static int ascii_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int is_alnum_byte(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* 1 when the lower-case ASCII `word` occurs at p inside [b, e) as a whole
 * word: the text's edges or a non-alphanumeric byte on both sides. */
static int word_at(const char *b, const char *e, const char *p, const char *word)
{
    size_t n = strlen(word);
    if ((size_t)(e - p) < n) return 0;
    for (size_t i = 0; i < n; i++)
        if (ascii_lower((unsigned char)p[i]) != (unsigned char)word[i]) return 0;
    if (p > b && is_alnum_byte(p[-1])) return 0;
    if (p + n < e && is_alnum_byte(p[n])) return 0;
    return 1;
}

int ssh_kbd_prompt_is_password(const char *text)
{
    if (!text) return 0;
    const char *b = text;
    const char *e = text + strlen(text);

    while (b < e && is_blank(*b)) b++;
    while (e > b && (is_blank(e[-1]) || e[-1] == ':')) e--;

    /* A leading "<token>@<token>'s " (the token is a run of non-blanks). */
    const char *t = b;
    while (t < e && !is_blank(*t)) t++;
    if (t < e && t - b >= 5 && t[-2] == '\'' && t[-1] == 's') {
        const char *at = b;
        while (at < t && *at != '@') at++;
        if (at > b && at + 1 < t - 2) {          /* a host between '@' and "'s" */
            b = t;
            while (b < e && is_blank(*b)) b++;
        }
    }

    /* A trailing " for <token>". */
    const char *ts = e;
    while (ts > b && !is_blank(ts[-1])) ts--;
    if (ts > b) {
        const char *fe = ts;
        while (fe > b && is_blank(fe[-1])) fe--;
        if (fe - b > 3 && is_blank(fe[-4]) && word_at(b, fe, fe - 3, "for")) {
            e = fe - 3;
            while (e > b && is_blank(e[-1])) e--;
        }
    }

    /* What is left must end with the word "password". */
    if (e - b < 8 || !word_at(b, e, e - 8, "password")) return 0;

    static const char *const banned[] = {
        "passcode", "one-time", "otp", "new", "retype",
        "verification", "token", "pin", "code"
    };
    for (const char *p = b; p < e; p++)
        for (size_t i = 0; i < sizeof banned / sizeof banned[0]; i++)
            if (word_at(b, e, p, banned[i])) return 0;
    return 1;
}

/* ---- the sanitiser (spec section 5) ------------------------------------- */

/* Output state. Blanks and line breaks are held back (`sp`, `nl`) until a
 * visible byte follows, which trims each line's trailing blanks and drops
 * the empty edge lines without a second pass. `trunc` is set when a byte
 * that belongs in the result did not fit under `lim`. */
typedef struct {
    char *out;
    size_t lim, o, sp, nl;
    int has, trunc, single;
} SanOut;

static void san_put(SanOut *s, char c)
{
    if (s->o < s->lim) s->out[s->o++] = c;
    else s->trunc = 1;
}

/* Writes one visible UTF-8 sequence (or '?'), after any held-back layout. */
static void san_content(SanOut *s, const unsigned char *p, size_t n)
{
    if (s->has) {
        for (size_t i = 0; i < s->nl; i++) san_put(s, s->single ? ' ' : '\n');
    }
    for (size_t i = 0; i < s->sp; i++) san_put(s, ' ');
    s->nl = s->sp = 0;
    s->has = 1;
    for (size_t i = 0; i < n; i++) san_put(s, (char)p[i]);
}

/* Length of the valid UTF-8 sequence at p (n bytes left), or 0. */
static size_t utf8_len(const unsigned char *p, size_t n)
{
    unsigned char c = p[0];
    if (c < 0x80) return 1;
    size_t need;
    unsigned char lo = 0x80, hi = 0xBF;
    if (c >= 0xC2 && c <= 0xDF) need = 2;
    else if (c >= 0xE0 && c <= 0xEF) {
        need = 3;
        if (c == 0xE0) lo = 0xA0;
        if (c == 0xED) hi = 0x9F;
    } else if (c >= 0xF0 && c <= 0xF4) {
        need = 4;
        if (c == 0xF0) lo = 0x90;
        if (c == 0xF4) hi = 0x8F;
    } else return 0;
    if (n < need || p[1] < lo || p[1] > hi) return 0;
    for (size_t i = 2; i < need; i++)
        if (p[i] < 0x80 || p[i] > 0xBF) return 0;
    return need;
}

size_t ssh_kbd_sanitize(const char *in, size_t len, char *out, size_t cap, int single_line)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!in) len = 0;

    const unsigned char *s = (const unsigned char *)in;
    SanOut so = { out, cap - 1, 0, 0, 0, 0, 0, single_line != 0 };
    size_t i = 0;

    while (i < len && !so.trunc) {
        unsigned char c = s[i];
        if (c == 0x1B) {
            if (i + 1 >= len) break;                    /* lone trailing ESC */
            unsigned char k = s[i + 1];
            if (k == '[') {                             /* CSI: params, then a final byte */
                size_t j = i + 2;
                while (j < len && s[j] >= 0x20 && s[j] <= 0x3F) j++;
                if (j < len && s[j] >= 0x40 && s[j] <= 0x7E) j++;
                i = j;
            } else if (k == ']') {                      /* OSC: to BEL or ST */
                size_t j = i + 2;
                while (j < len && s[j] != 0x07 && s[j] != 0x1B) j++;
                if (j < len && s[j] == 0x07) j++;
                else if (j + 1 < len && s[j] == 0x1B && s[j + 1] == '\\') j += 2;
                i = j;
            } else if (k == 0x1B) {
                i += 1;                                 /* the next ESC starts afresh */
            } else {
                i += 2;                                 /* two-byte ESC */
            }
        } else if (c == '\r' || c == '\n') {
            i += (c == '\r' && i + 1 < len && s[i + 1] == '\n') ? 2 : 1;
            so.nl++;
            so.sp = 0;
        } else if (c == ' ' || c == '\t') {
            so.sp++;
            i++;
        } else if (c < 0x20 || c == 0x7F) {
            i++;
        } else {
            size_t n = utf8_len(s + i, len - i);
            if (n == 0) {
                san_content(&so, (const unsigned char *)"?", 1);
                i++;
            } else {
                san_content(&so, s + i, n);
                i += n;
            }
        }
    }

    if (so.trunc) {
        /* Replace the tail with "...", starting on a UTF-8 boundary. */
        size_t cut = so.lim >= 3 ? so.lim - 3 : 0;
        while (cut > 0 && ((unsigned char)out[cut] & 0xC0) == 0x80) cut--;
        size_t dots = so.lim - cut < 3 ? so.lim - cut : 3;
        for (size_t d = 0; d < dots; d++) out[cut + d] = '.';
        so.o = cut + dots;
    }
    out[so.o] = '\0';
    return so.o;
}
