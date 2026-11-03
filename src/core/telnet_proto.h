#ifndef NUTSHELL_TELNET_PROTO_H
#define NUTSHELL_TELNET_PROTO_H

#include <stddef.h>

/* Telnet (RFC 854/855) IAC parser, passive option negotiator and encoder.
 * Pure: no sockets, no allocation. Nutshell never starts a negotiation, it
 * only answers what the server asks, and never answers a request that would
 * not change state (RFC 1143 loop protection). */

enum { TN_SE = 240, TN_SB = 250, TN_WILL = 251, TN_WONT = 252, TN_DO = 253, TN_DONT = 254, TN_IAC = 255 };
enum { TN_OPT_ECHO = 1, TN_OPT_SGA = 3, TN_OPT_TTYPE = 24, TN_OPT_NAWS = 31 };
enum { TN_TTYPE_IS = 0, TN_TTYPE_SEND = 1 };

/* Reply buffer that can never overflow for n input bytes. */
#define TN_REPLY_CAP(n) (4u * (size_t)(n) + 32u)

typedef struct {
    int state;
    unsigned char verb;                  /* parser */
    unsigned char sb[32];
    size_t sb_len;
    int sb_overflow;
    unsigned char us[256], him[256];     /* option enabled on our side / the server's side */
    int cols, rows;
    int cr_expanded;                     /* last encode ended in a CR we expanded */
} TnProto;

void tn_proto_init(TnProto *p, int cols, int rows);

/* Strip IAC from `in`; terminal bytes go to data (capacity >= len), answers
 * to reply (capacity reply_cap, TN_REPLY_CAP(len) always suffices). */
void tn_proto_rx(TnProto *p, const unsigned char *in, size_t len,
                 unsigned char *data, size_t *data_len,
                 unsigned char *reply, size_t reply_cap, size_t *reply_len);

/* Escape 0xFF and expand lone CR to CR LF; out capacity >= 2*len. Returns
 * bytes written. */
size_t tn_proto_encode(TnProto *p, const unsigned char *in, size_t len,
                       unsigned char *out, size_t cap);

/* Record the size; returns the NAWS subnegotiation to send (<= 13 bytes),
 * 0 unless NAWS is agreed. */
size_t tn_proto_resize(TnProto *p, int cols, int rows, unsigned char *out, size_t cap);

/* him[ECHO]: the server echoes, so local echo stops. */
int tn_proto_server_echoes(const TnProto *p);

#endif
