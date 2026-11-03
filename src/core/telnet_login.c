#include "telnet_login.h"

#include <stddef.h>
#include <string.h>

/* Length of line without trailing spaces and tabs. */
static size_t trimmed_len(const char *line)
{
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t')) n--;
    return n;
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int ends_with(const char *line, const char *suffix)
{
    size_t n = trimmed_len(line);
    size_t s = strlen(suffix);
    if (n < s) return 0;
    for (size_t i = 0; i < s; i++) {
        if (lower(line[n - s + i]) != suffix[i]) return 0;
    }
    return 1;
}

int tn_line_is_login_prompt(const char *line)
{
    if (!line) return 0;
    return ends_with(line, "login:") || ends_with(line, "username:") ||
           ends_with(line, "user name:") || ends_with(line, "user:");
}

int tn_line_is_password_prompt(const char *line)
{
    if (!line) return 0;
    return ends_with(line, "password:") || ends_with(line, "passcode:");
}

int tn_line_is_device_prompt(const char *line)
{
    size_t n;
    char c;
    if (!line) return 0;
    n = trimmed_len(line);
    if (n == 0) return 0;
    c = line[n - 1];
    return c == '>' || c == '#' || c == '$';
}

void tn_login_init(TnLogin *l, int has_user, int has_password, unsigned long now_ms)
{
    l->has_user = has_user;
    l->has_password = has_password;
    l->phase = (has_user || has_password) ? TN_LOGIN_ARMED : TN_LOGIN_OFF;
    l->fresh = 0;
    l->last_rx_ms = now_ms;
    l->deadline_ms = now_ms + TN_LOGIN_TIMEOUT_MS;
}

void tn_login_on_rx(TnLogin *l, unsigned long now_ms)
{
    l->fresh = 1;
    l->last_rx_ms = now_ms;
}

void tn_login_on_user_input(TnLogin *l)
{
    l->phase = TN_LOGIN_OFF;
}

void tn_login_ask_done(TnLogin *l)
{
    l->phase = TN_LOGIN_OFF;
}

int tn_login_armed(const TnLogin *l)
{
    return l->phase != TN_LOGIN_OFF;
}

TnLoginAction tn_login_step(TnLogin *l, const char *cursor_line, unsigned long now_ms)
{
    if (l->phase == TN_LOGIN_OFF || l->phase == TN_LOGIN_ASKING) return TN_LOGIN_NONE;
    if (now_ms >= l->deadline_ms) {
        l->phase = TN_LOGIN_OFF;
        return TN_LOGIN_NONE;
    }
    if (!l->fresh || now_ms - l->last_rx_ms < TN_LOGIN_SETTLE_MS) return TN_LOGIN_NONE;
    l->fresh = 0;
    if (!cursor_line) return TN_LOGIN_NONE;
    if (tn_line_is_device_prompt(cursor_line)) {
        l->phase = TN_LOGIN_OFF;
        return TN_LOGIN_NONE;
    }
    if (l->phase == TN_LOGIN_ARMED && tn_line_is_login_prompt(cursor_line)) {
        if (!l->has_user) {
            l->phase = TN_LOGIN_OFF;
            return TN_LOGIN_NONE;
        }
        l->phase = TN_LOGIN_USER_SENT;
        l->deadline_ms = now_ms + TN_LOGIN_TIMEOUT_MS;
        return TN_LOGIN_SEND_USER;
    }
    if (tn_line_is_password_prompt(cursor_line)) {
        if (l->has_password) {
            l->phase = TN_LOGIN_OFF;
            return TN_LOGIN_SEND_PASSWORD;
        }
        l->phase = TN_LOGIN_ASKING;
        return TN_LOGIN_ASK_PASSWORD;
    }
    if (l->phase == TN_LOGIN_USER_SENT && tn_line_is_login_prompt(cursor_line)) {
        l->phase = TN_LOGIN_OFF;
        return TN_LOGIN_NONE;
    }
    return TN_LOGIN_NONE;
}
