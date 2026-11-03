#ifndef NUTSHELL_TELNET_IO_H
#define NUTSHELL_TELNET_IO_H

/* Winsock glue for a telnet tab: a blocking connect for a worker thread and a
 * non-blocking SessionIo (kind SESSION_TELNET) over TnSession. All protocol
 * logic lives in src/core/telnet_session.c; this file only moves bytes. */

#include <winsock2.h>
#include <windows.h>
#include "session_io.h"
#include "telnet_session.h"
#include "telnet_status.h"

/* Worker thread only: resolve, try each address with a timeout_ms
 * non-blocking connect, merge outcomes with tn_conn_merge. On TN_CONN_OK *out
 * is a connected socket (still blocking). WSAStartup must already have run. */
TnConnOutcome telnet_io_connect(const char *host, int port, unsigned timeout_ms,
                                SOCKET *out);

/* UI thread: take ownership of sock (made non-blocking, TCP_NODELAY) and fill
 * a SESSION_TELNET vtable. On failure the socket is closed and ctx is NULL. */
SessionIo session_io_telnet(SOCKET sock, const TnSessionOpts *opts);

int      telnet_io_take_password_request(void *ctx);
void     telnet_io_answer_password(void *ctx, const char *password); /* NULL = cancelled */
unsigned telnet_io_serial(const void *ctx);   /* process-unique, nonzero, per transport */

#endif
