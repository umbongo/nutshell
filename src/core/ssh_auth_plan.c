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
