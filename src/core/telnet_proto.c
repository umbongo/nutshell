#include "telnet_proto.h"

#include <string.h>

enum { ST_DATA = 0, ST_IAC, ST_VERB, ST_SB, ST_SB_IAC };

/* Append n bytes to reply only if they all fit; a partial answer would
 * desynchronise the peer, so it is all or nothing. */
static void reply_put(unsigned char *reply, size_t cap, size_t *len,
                      const unsigned char *b, size_t n)
{
    if (n > cap || *len > cap - n) return;
    memcpy(reply + *len, b, n);
    *len += n;
}

static void reply_cmd(unsigned char *reply, size_t cap, size_t *len,
                      unsigned char verb, unsigned char opt)
{
    unsigned char b[3];
    b[0] = TN_IAC; b[1] = verb; b[2] = opt;
    reply_put(reply, cap, len, b, sizeof(b));
}

static int clamp_size(int v)
{
    if (v < 0) return 0;
    if (v > 65535) return 65535;
    return v;
}

/* IAC SB NAWS w1 w0 h1 h0 IAC SE, every 0xFF data byte doubled: <= 13 bytes. */
static size_t build_naws(int cols, int rows, unsigned char *out, size_t cap)
{
    unsigned c = (unsigned)clamp_size(cols), r = (unsigned)clamp_size(rows);
    unsigned char v[4];
    unsigned char tmp[13];
    size_t n = 0;
    v[0] = (unsigned char)((c >> 8) & 0xFFu);
    v[1] = (unsigned char)(c & 0xFFu);
    v[2] = (unsigned char)((r >> 8) & 0xFFu);
    v[3] = (unsigned char)(r & 0xFFu);
    tmp[n++] = TN_IAC; tmp[n++] = TN_SB; tmp[n++] = TN_OPT_NAWS;
    for (size_t i = 0; i < 4; i++) {
        tmp[n++] = v[i];
        if (v[i] == TN_IAC) tmp[n++] = TN_IAC;
    }
    tmp[n++] = TN_IAC; tmp[n++] = TN_SE;
    if (n > cap) return 0;
    memcpy(out, tmp, n);
    return n;
}

void tn_proto_init(TnProto *p, int cols, int rows)
{
    memset(p, 0, sizeof(*p));
    p->cols = cols;
    p->rows = rows;
}

static void handle_verb(TnProto *p, unsigned char verb, unsigned char opt,
                        unsigned char *reply, size_t cap, size_t *rl)
{
    switch (verb) {
    case TN_WILL:
        if (opt == TN_OPT_ECHO || opt == TN_OPT_SGA) {
            if (!p->him[opt]) {
                p->him[opt] = 1;
                reply_cmd(reply, cap, rl, TN_DO, opt);
            }
        } else {
            reply_cmd(reply, cap, rl, TN_DONT, opt);
        }
        break;
    case TN_WONT:
        if (p->him[opt]) {
            p->him[opt] = 0;
            reply_cmd(reply, cap, rl, TN_DONT, opt);
        }
        break;
    case TN_DO:
        if (opt == TN_OPT_SGA || opt == TN_OPT_TTYPE || opt == TN_OPT_NAWS) {
            if (!p->us[opt]) {
                p->us[opt] = 1;
                reply_cmd(reply, cap, rl, TN_WILL, opt);
                if (opt == TN_OPT_NAWS) {
                    unsigned char sb[13];
                    size_t n = build_naws(p->cols, p->rows, sb, sizeof(sb));
                    reply_put(reply, cap, rl, sb, n);
                }
            }
        } else {
            reply_cmd(reply, cap, rl, TN_WONT, opt);
        }
        break;
    case TN_DONT:
        if (p->us[opt]) {
            p->us[opt] = 0;
            reply_cmd(reply, cap, rl, TN_WONT, opt);
        }
        break;
    default:
        break;
    }
}

static void handle_sb(TnProto *p, unsigned char *reply, size_t cap, size_t *rl)
{
    static const unsigned char name[] = { 'X', 'T', 'E', 'R', 'M' };
    unsigned char b[4 + sizeof(name) + 2];
    size_t n = 0;
    if (p->sb_overflow || p->sb_len != 2) return;
    if (p->sb[0] != TN_OPT_TTYPE || p->sb[1] != TN_TTYPE_SEND || !p->us[TN_OPT_TTYPE]) return;
    b[n++] = TN_IAC; b[n++] = TN_SB; b[n++] = TN_OPT_TTYPE; b[n++] = TN_TTYPE_IS;
    memcpy(b + n, name, sizeof(name));
    n += sizeof(name);
    b[n++] = TN_IAC; b[n++] = TN_SE;
    reply_put(reply, cap, rl, b, n);
}

void tn_proto_rx(TnProto *p, const unsigned char *in, size_t len,
                 unsigned char *data, size_t *data_len,
                 unsigned char *reply, size_t reply_cap, size_t *reply_len)
{
    size_t dl = 0, rl = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = in[i];
        switch (p->state) {
        case ST_DATA:
            if (c == TN_IAC) p->state = ST_IAC;
            else data[dl++] = c;
            break;
        case ST_IAC:
            if (c == TN_IAC) {
                data[dl++] = TN_IAC;
                p->state = ST_DATA;
            } else if (c == TN_SB) {
                p->sb_len = 0;
                p->sb_overflow = 0;
                p->state = ST_SB;
            } else if (c >= TN_WILL && c <= TN_DONT) {
                p->verb = c;
                p->state = ST_VERB;
            } else {
                p->state = ST_DATA;     /* NOP, GA, ...: dropped */
            }
            break;
        case ST_VERB:
            handle_verb(p, p->verb, c, reply, reply_cap, &rl);
            p->state = ST_DATA;
            break;
        case ST_SB:
            if (c == TN_IAC) {
                p->state = ST_SB_IAC;
            } else if (p->sb_len < sizeof(p->sb)) {
                p->sb[p->sb_len++] = c;
            } else {
                p->sb_overflow = 1;
            }
            break;
        case ST_SB_IAC:
            if (c == TN_SE) {
                handle_sb(p, reply, reply_cap, &rl);
                p->state = ST_DATA;
            } else if (c == TN_IAC) {
                if (p->sb_len < sizeof(p->sb)) p->sb[p->sb_len++] = TN_IAC;
                else p->sb_overflow = 1;
                p->state = ST_SB;
            } else {
                p->state = ST_DATA;     /* malformed: abandon the subnegotiation */
            }
            break;
        default:
            p->state = ST_DATA;
            break;
        }
    }
    *data_len = dl;
    *reply_len = rl;
}

size_t tn_proto_encode(TnProto *p, const unsigned char *in, size_t len,
                       unsigned char *out, size_t cap)
{
    size_t n = 0, i = 0;
    int was_cr = p->cr_expanded;
    if (len == 0) return 0;
    p->cr_expanded = 0;
    if (was_cr && in[0] == '\n') i = 1;
    for (; i < len; i++) {
        unsigned char c = in[i];
        if (c == TN_IAC) {
            if (cap - n < 2) break;
            out[n++] = TN_IAC; out[n++] = TN_IAC;
        } else if (c == '\r') {
            if (i + 1 < len && in[i + 1] == '\n') {
                if (cap - n < 2) break;
                out[n++] = '\r'; out[n++] = '\n';
                i++;
            } else {
                if (cap - n < 2) break;
                out[n++] = '\r'; out[n++] = '\n';
                if (i + 1 == len) p->cr_expanded = 1;
            }
        } else {
            if (cap - n < 1) break;
            out[n++] = c;
        }
    }
    return n;
}

size_t tn_proto_resize(TnProto *p, int cols, int rows, unsigned char *out, size_t cap)
{
    p->cols = cols;
    p->rows = rows;
    if (!p->us[TN_OPT_NAWS]) return 0;
    return build_naws(cols, rows, out, cap);
}

int tn_proto_server_echoes(const TnProto *p)
{
    return p->him[TN_OPT_ECHO] != 0;
}
