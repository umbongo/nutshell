#include "telnet_session.h"
#include "telnet_proto.h"
#include "telnet_login.h"
#include "string_utils.h"
#include "secure_zero.h"
#include "dlog.h"
#include <stdlib.h>
#include <string.h>

#define TN_CRED_MAX   256u
#define TN_ECHO_PEND  2048u
#define TN_ROW_MAX    256u

struct TnSession {
    TnProto proto;
    TnLogin login;
    TnEchoState echo;
    char user[TN_CRED_MAX];
    char pass[TN_CRED_MAX];
    char reg[2][TN_CRED_MAX];         /* what dlog_secret_add took (profile pw, box pw), removed at free */
    int nreg;
    unsigned char *q;
    size_t qlen, qcap;
    unsigned char pend[TN_ECHO_PEND]; /* local echo awaiting the next tick */
    size_t pend_len;
    int ask_pending;
    unsigned log_id;
};

/* ---- echo filter ---- */

size_t tn_echo_filter(TnEchoState *st, const unsigned char *in, size_t len,
                      unsigned char *out, size_t cap)
{
    size_t n = 0;
    int prev_cr = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = in[i];
        if (st->esc == 1) {               /* after ESC */
            st->esc = (c == '[') ? 2 : (c == 'O') ? 3 : 0;
            continue;
        }
        if (st->esc == 2) {               /* inside CSI */
            if (c >= 0x40 && c <= 0x7E) st->esc = 0;
            continue;
        }
        if (st->esc == 3) { st->esc = 0; continue; }   /* SS3 final byte */
        if (c == 0x1B) { st->esc = 1; prev_cr = 0; continue; }
        if (c == '\r') {
            if (n + 2 > cap) break;
            out[n++] = '\r'; out[n++] = '\n';
            prev_cr = 1;
            continue;
        }
        if (c == '\n' && prev_cr) { prev_cr = 0; continue; }
        prev_cr = 0;
        if (c == 0x08 || c == 0x7F) {
            if (n + 3 > cap) break;
            out[n++] = '\b'; out[n++] = ' '; out[n++] = '\b';
            continue;
        }
        if (c >= 0x20) {
            if (n + 1 > cap) break;
            out[n++] = c;
        }
    }
    return n;
}

/* ---- queue ---- */

static int q_reserve(TnSession *s, size_t extra)
{
    size_t need = s->qlen + extra;
    if (need <= s->qcap) return 1;
    size_t cap = s->qcap ? s->qcap : 256u;
    while (cap < need) cap *= 2u;
    unsigned char *nq = (unsigned char *)malloc(cap);
    if (!nq) return 0;
    if (s->qlen) memcpy(nq, s->q, s->qlen);
    if (s->q) { secure_zero(s->q, s->qcap); free(s->q); }
    s->q = nq;
    s->qcap = cap;
    return 1;
}

static void q_raw(TnSession *s, const unsigned char *d, size_t n)
{
    if (n == 0u || !q_reserve(s, n)) return;
    memcpy(s->q + s->qlen, d, n);
    s->qlen += n;
}

/* Encode plain bytes (IAC doubled, CR -> CR LF) straight into the queue. */
static void q_encode(TnSession *s, const unsigned char *d, size_t n)
{
    if (n == 0u || !q_reserve(s, 2u * n)) return;
    s->qlen += tn_proto_encode(&s->proto, d, n, s->q + s->qlen, 2u * n);
}

/* ---- credentials ---- */

/* Scrub the password from the debug log for the rest of the session, even
 * after it is sent (a device may echo it back). Shorter than dlog's minimum or
 * a full table: simply not registered. Removed exactly as added, at free. */
static void register_secret(TnSession *s, const char *secret)
{
    if (!secret || s->nreg >= 2) return;
    char *slot = s->reg[s->nreg];
    snprintf(slot, TN_CRED_MAX, "%s", secret);
    if (dlog_secret_add(slot)) s->nreg++;
    else secure_zero(slot, TN_CRED_MAX);
}

static void release_password(TnSession *s)
{
    secure_zero(s->pass, sizeof(s->pass));
}

/* Secret + Enter into the queue: never echoed, never logged. */
static void q_secret_line(TnSession *s, const char *secret)
{
    unsigned char plain[TN_CRED_MAX + 1u];
    size_t n = strnlen(secret, TN_CRED_MAX - 1u);
    memcpy(plain, secret, n);
    plain[n++] = '\r';
    q_encode(s, plain, n);
    secure_zero(plain, sizeof(plain));
}

/* ---- log ---- */

static void log_chunk(FILE *f, const char *data, size_t len)
{
    if (!f || len == 0u) return;
    char strip_buf[4096];
    while (len > 0u) {
        size_t slice = (len < sizeof(strip_buf) - 1u) ? len : (sizeof(strip_buf) - 1u);
        size_t stripped = ansi_strip(strip_buf, sizeof(strip_buf), data, slice);
        if (stripped > 0u) fwrite(strip_buf, 1u, stripped, f);
        data += slice;
        len -= slice;
    }
    fflush(f);
}

/* ---- lifecycle ---- */

TnSession *tn_session_new(const TnSessionOpts *o)
{
    if (!o) return NULL;
    TnSession *s = (TnSession *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    const char *u = o->username ? o->username : "";
    const char *p = o->password ? o->password : "";
    snprintf(s->user, sizeof(s->user), "%s", u);
    snprintf(s->pass, sizeof(s->pass), "%s", p);
    s->log_id = o->log_id;
    tn_proto_init(&s->proto, o->cols, o->rows);
    tn_login_init(&s->login, s->user[0] != '\0', s->pass[0] != '\0', o->now_ms);
    if (s->pass[0] != '\0') register_secret(s, s->pass);
    return s;
}

void tn_session_free(TnSession *s)
{
    if (!s) return;
    release_password(s);
    for (int i = 0; i < s->nreg; i++) {
        dlog_secret_remove(s->reg[i]);
        secure_zero(s->reg[i], sizeof(s->reg[i]));
    }
    secure_zero(s->user, sizeof(s->user));
    secure_zero(s->pend, sizeof(s->pend));
    if (s->q) { secure_zero(s->q, s->qcap); free(s->q); }
    secure_zero(s, sizeof(*s));
    free(s);
}

/* ---- receive ---- */

size_t tn_session_rx(TnSession *s, Terminal *term, FILE *log,
                     const unsigned char *buf, size_t len, unsigned long now_ms)
{
    if (!s || !buf || len == 0u) return 0;
    unsigned char *data = (unsigned char *)malloc(len);
    size_t reply_cap = TN_REPLY_CAP(len);
    unsigned char *reply = (unsigned char *)malloc(reply_cap);
    if (!data || !reply) { free(data); free(reply); return 0; }
    size_t dlen = 0, rlen = 0;
    tn_proto_rx(&s->proto, buf, len, data, &dlen, reply, reply_cap, &rlen);
    q_raw(s, reply, rlen);
    if (dlen > 0u) {
        if (term) term_process(term, (const char *)data, dlen);
        log_chunk(log, (const char *)data, dlen);
        DLOG_CONTENT(DLOG_TERM, DLOG_TRACE, s->log_id, "term.rx", "data",
                     (const char *)data, dlen);
        tn_login_on_rx(&s->login, now_ms);
    }
    free(data);
    free(reply);
    return dlen;
}

/* ---- tick ---- */

void tn_session_flush_echo(TnSession *s, Terminal *term, FILE *log)
{
    if (!s || s->pend_len == 0u) return;
    char row[TN_ROW_MAX];
    int have = term && term_cursor_row_prompt_text(term, row, sizeof(row));
    if (!have || !tn_line_is_password_prompt(row)) {
        if (term) term_process(term, (const char *)s->pend, s->pend_len);
        log_chunk(log, (const char *)s->pend, s->pend_len);
    }
    secure_zero(s->pend, s->pend_len);
    s->pend_len = 0;
}

static void write_plain(TnSession *s, const char *data, size_t len);

void tn_session_tick(TnSession *s, Terminal *term, FILE *log, unsigned long now_ms)
{
    if (!s) return;
    tn_session_flush_echo(s, term, log);

    char row[TN_ROW_MAX];
    const char *line = (term && term_cursor_row_prompt_text(term, row, sizeof(row))) ? row : NULL;
    TnLoginAction act = tn_login_step(&s->login, line, now_ms);
    if (act == TN_LOGIN_SEND_USER) {
        write_plain(s, s->user, strlen(s->user));
        write_plain(s, "\r", 1u);
    } else if (act == TN_LOGIN_SEND_PASSWORD) {
        if (s->pass[0] != '\0') q_secret_line(s, s->pass);
        release_password(s);
    } else if (act == TN_LOGIN_ASK_PASSWORD) {
        s->ask_pending = 1;
    }
    if (!tn_login_armed(&s->login)) release_password(s);
}

/* ---- user input ---- */

static void write_plain(TnSession *s, const char *data, size_t len)
{
    q_encode(s, (const unsigned char *)data, len);
    if (tn_proto_server_echoes(&s->proto)) return;
    unsigned char out[3u * 256u];
    size_t off = 0;
    while (off < len) {
        size_t slice = len - off < 256u ? len - off : 256u;
        size_t n = tn_echo_filter(&s->echo, (const unsigned char *)data + off, slice, out, sizeof(out));
        if (s->pend_len + n <= sizeof(s->pend)) {   /* echo is cosmetic: drop the overflow */
            memcpy(s->pend + s->pend_len, out, n);
            s->pend_len += n;
        }
        off += slice;
    }
    secure_zero(out, sizeof(out));
}

int tn_session_user_write(TnSession *s, const char *data, size_t len)
{
    if (!s || (!data && len > 0u)) return -1;
    if (len > TN_OUTQ_MAX || s->qlen + 2u * len > TN_OUTQ_MAX) return -1;
    tn_login_on_user_input(&s->login);
    write_plain(s, data, len);
    return (int)len;
}

void tn_session_resize(TnSession *s, int cols, int rows)
{
    if (!s) return;
    unsigned char sb[16];
    size_t n = tn_proto_resize(&s->proto, cols, rows, sb, sizeof(sb));
    q_raw(s, sb, n);
}

int tn_session_take_password_request(TnSession *s)
{
    if (!s || !s->ask_pending) return 0;
    s->ask_pending = 0;
    return 1;
}

int tn_session_answer_password(TnSession *s, Terminal *term, const char *password)
{
    if (!s) return 0;
    s->ask_pending = 0;
    tn_login_ask_done(&s->login);
    if (!password) return 0;
    /* The box was open while output kept arriving: send only if the cursor
     * line is still a password prompt, never into a login or device prompt. */
    char row[TN_ROW_MAX];
    int have = term && term_cursor_row_prompt_text(term, row, sizeof(row));
    if (!have || !tn_line_is_password_prompt(row)) {
        secure_zero(row, sizeof(row));
        if (term) {
            static const char msg[] =
                "\r\nPassword not sent: the device is no longer asking for it\r\n";
            term_process(term, msg, sizeof(msg) - 1u);
        }
        return 0;
    }
    register_secret(s, password);
    q_secret_line(s, password);
    return 1;
}

const unsigned char *tn_session_outq(const TnSession *s, size_t *len)
{
    if (len) *len = s ? s->qlen : 0u;
    return s ? s->q : NULL;
}

void tn_session_outq_consume(TnSession *s, size_t n)
{
    if (!s || n == 0u) return;
    if (n > s->qlen) n = s->qlen;
    size_t rest = s->qlen - n;
    if (rest) memmove(s->q, s->q + n, rest);
    secure_zero(s->q + rest, n);
    s->qlen = rest;
}

int tn_session_login_armed(const TnSession *s)
{
    return s ? tn_login_armed(&s->login) : 0;
}
