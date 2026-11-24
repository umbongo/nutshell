#ifndef NUTSHELL_TESTS_WS_LOOPBACK_WIN_H
#define NUTSHELL_TESTS_WS_LOOPBACK_WIN_H

#include <stddef.h>

/*
 * ws_loopback_win -- a one-connection WebSocket server on 127.0.0.1 for the
 * wintests (the voice WebSocket client, and the live session after it). It
 * answers the upgrade (Sec-WebSocket-Accept from OpenSSL SHA1 + base64),
 * unmasks client frames (126 and 127 length forms handled), sends unmasked
 * server frames and answers a close frame. Every thread it starts is joined
 * in ws_loop_stop.
 */

typedef struct WsLoop WsLoop;

enum {
    WSL_ECHO = 0,       /* sends every text message it receives back */
    WSL_SILENT,         /* upgrades, then never sends (answers a close) */
    WSL_NO_UPGRADE,     /* accepts TCP, never answers the request */
    WSL_REJECT_403      /* answers the request with 403 and closes */
};

/* Binds 127.0.0.1 port 0 and starts the server thread. NULL on failure. */
WsLoop *ws_loop_start(int mode);
int     ws_loop_port(WsLoop *w);
/* Closes everything and joins every thread; frees w. */
void    ws_loop_stop(WsLoop *w);

/* Called on the server thread for every complete text message received. A
 * non-zero return ends the connection. */
void ws_loop_set_script(WsLoop *w,
                        int (*on_msg)(WsLoop *, const char *msg, size_t n, void *u),
                        void *u);

/* Send one text message to the client (waits up to 3 s for the upgrade). 0 ok. */
int ws_loop_send_text(WsLoop *w, const char *s);
/* The same message split into `parts` frames (a text frame, continuations,
 * the last with FIN). 0 ok. */
int ws_loop_send_fragmented(WsLoop *w, const char *s, size_t n, int parts);

/* The raw request head (up to the blank line) of the accepted connection;
 * valid until ws_loop_stop. Empty until a request has arrived. */
const char *ws_loop_request(WsLoop *w);
/* 1 once a client close frame has been answered. */
int ws_loop_close_seen(WsLoop *w);

#endif
