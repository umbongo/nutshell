#ifndef NUTSHELL_TELNET_SESSION_H
#define NUTSHELL_TELNET_SESSION_H

#include "term.h"
#include <stddef.h>
#include <stdio.h>

/* Transport-free telnet session engine. Owns the protocol state, the
 * auto-login matcher, the credentials, local echo and the outgoing queue;
 * the socket layer only moves bytes in and out of it.
 *
 * Secrets: the profile password and a password-box answer go straight into
 * the outgoing queue (IAC doubled, CR LF), never to the terminal, the
 * session log, DLOG or local echo. Every buffer that held one is wiped with
 * secure_zero(). */

#define TN_OUTQ_MAX ((size_t)256 * 1024)

typedef struct {
    const char *username, *password;  /* copied; either may be "" */
    int cols, rows;
    unsigned log_id;
    unsigned long now_ms;             /* connect time */
} TnSessionOpts;

typedef struct TnSession TnSession;

TnSession *tn_session_new(const TnSessionOpts *o);      /* NULL on OOM */
void tn_session_free(TnSession *s);                      /* wipes credentials and the queue; NULL-safe */

/* Received bytes: strip IAC, queue answers, feed the terminal, the session log
 * (ANSI-stripped) and the term.rx debug event. Returns the number of terminal
 * bytes delivered. */
size_t tn_session_rx(TnSession *s, Terminal *term, FILE *log,
                     const unsigned char *buf, size_t len, unsigned long now_ms);

/* Every poll: flush pending local echo (dropped when the cursor row is a
 * password prompt), then run the matcher. */
void tn_session_tick(TnSession *s, Terminal *term, FILE *log, unsigned long now_ms);

/* Keys, pastes, AI dispatch: switches auto-login off, encodes into the queue,
 * queues local echo when the server does not echo. Returns (int)len, or -1
 * when the queue would pass TN_OUTQ_MAX (nothing queued). */
int  tn_session_user_write(TnSession *s, const char *data, size_t len);

void tn_session_resize(TnSession *s, int cols, int rows);     /* queues NAWS when agreed */
int  tn_session_take_password_request(TnSession *s);          /* 1 once after ASK_PASSWORD */
void tn_session_answer_password(TnSession *s, const char *password); /* NULL = cancelled; never echoed or logged */

const unsigned char *tn_session_outq(const TnSession *s, size_t *len);
void tn_session_outq_consume(TnSession *s, size_t n);          /* the consumed bytes are secure_zero'd */
int  tn_session_login_armed(const TnSession *s);

/* Local echo filter (state carries an escape sequence split across writes).
 * cap must be >= 3*len. */
typedef struct { int esc; } TnEchoState;
size_t tn_echo_filter(TnEchoState *st, const unsigned char *in, size_t len,
                      unsigned char *out, size_t cap);

#endif
