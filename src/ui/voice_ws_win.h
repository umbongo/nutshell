#ifndef NUTSHELL_VOICE_WS_WIN_H
#define NUTSHELL_VOICE_WS_WIN_H

#include <stddef.h>
#include <winsock2.h>
#include <windows.h>
#include "ai_netcancel.h"

/*
 * voice_ws_win -- a WinHTTP WebSocket client for the live transcription
 * session. The session handle comes from voice_net_open(VNK_LIVE_WS) (gate
 * checked, counted); the connection owns it and closes it in voice_ws_free.
 * One thread may send while another receives. WinHTTP is delay-loaded, so
 * every WinHttp* function used here is listed in
 * build-support/delayload/winhttp.def.
 *
 * Cancelling: the request handle is armed on the caller's AiNetCancel during
 * the handshake (ai_nc_fire aborts it); after the upgrade the request handle
 * is closed and the WebSocket handle is armed on the same token, so exactly
 * one side ever closes it. voice_ws_shutdown is the polite cancel (a close
 * frame, which ends a pending receive); voice_ws_abort closes the handle.
 */

typedef struct VoiceWs VoiceWs;

/* Blocks: session from voice_net_open(VNK_LIVE_WS), connect, GET with the
 * upgrade option and the extra headers ("Name: value" lines, NULL-terminated
 * array), 101 check, CompleteUpgrade. ai_nc_fire from another thread aborts
 * it. A redirect is never followed: a 3xx comes back as *http_status with NULL.
 * NULL with err (and *http_status when the server answered something other
 * than 101; 0 otherwise). `nc` may be NULL: the connect itself is then not
 * cancellable by the caller, but voice_ws_abort still works (through an
 * internal token). */
VoiceWs *voice_ws_connect(const char *host, int port, int secure, const char *path,
                          const char *const *headers, AiNetCancel *nc,
                          int *http_status, char *err, size_t errcap);

int  voice_ws_send_text(VoiceWs *ws, const char *s, size_t n);          /* 0 ok */

/* One whole message (fragments joined), heap, NUL-terminated; *is_close set
 * on a close frame. Messages over 1 MiB fail. 0 ok, -1 error or cancelled
 * (a receive ended by voice_ws_shutdown or voice_ws_abort is always -1). */
int  voice_ws_recv(VoiceWs *ws, char **msg, size_t *len, int *is_close);

/* The polite cancel: WinHttpWebSocketClose with
 * WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS (close timeout 2000 ms, set at
 * connect), which ends a pending receive. Returns the Close result: NO_ERROR
 * on success, or when an earlier call already closed it. Close fails (for
 * example ERROR_INVALID_OPERATION while a send is pending on another thread)
 * without waking the receiver; the caller then escalates to voice_ws_abort. A
 * later voice_ws_recv fails either way, and a failed call may be retried.
 * It can block up to the 2 s close timeout: call it from a worker (the
 * sender), never the UI thread. */
DWORD voice_ws_shutdown(VoiceWs *ws);

/* Last resort: WinHttpCloseHandle on the WebSocket handle (through the
 * AiNetCancel token it is armed on), waking a send or receive the close did
 * not. Fires the token. */
void voice_ws_abort(VoiceWs *ws);

/* After both workers are joined: closes what is left. NULL is fine. */
void voice_ws_free(VoiceWs *ws);

#endif
