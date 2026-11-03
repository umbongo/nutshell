#ifndef NUTSHELL_TELNET_LOGIN_H
#define NUTSHELL_TELNET_LOGIN_H

/* Telnet auto-login: prompt matcher and one-shot state machine. Pure: the
 * caller feeds it the cursor line's text once output pauses and performs the
 * returned action. It never retries and never sends anything on its own. */

#define TN_LOGIN_SETTLE_MS   300u
#define TN_LOGIN_TIMEOUT_MS  20000u

typedef enum { TN_LOGIN_OFF = 0, TN_LOGIN_ARMED, TN_LOGIN_USER_SENT, TN_LOGIN_ASKING } TnLoginPhase;
typedef enum { TN_LOGIN_NONE = 0, TN_LOGIN_SEND_USER, TN_LOGIN_SEND_PASSWORD, TN_LOGIN_ASK_PASSWORD } TnLoginAction;

typedef struct {
    TnLoginPhase phase;
    int has_user, has_password;
    int fresh;
    unsigned long last_rx_ms, deadline_ms;
} TnLogin;

/* Line tests: trailing spaces and tabs ignored, tail compared case-insensitively. NULL -> 0. */
int tn_line_is_login_prompt(const char *line);
int tn_line_is_password_prompt(const char *line);
int tn_line_is_device_prompt(const char *line);

void tn_login_init(TnLogin *l, int has_user, int has_password, unsigned long now_ms);
void tn_login_on_rx(TnLogin *l, unsigned long now_ms);   /* bytes arrived */
void tn_login_on_user_input(TnLogin *l);                 /* -> OFF */
/* cursor_line: the cursor row's text, NULL when there is none (alt screen, text after the cursor). */
TnLoginAction tn_login_step(TnLogin *l, const char *cursor_line, unsigned long now_ms);
void tn_login_ask_done(TnLogin *l);                      /* box answered or cancelled -> OFF */
int tn_login_armed(const TnLogin *l);                    /* phase != OFF */

#endif
