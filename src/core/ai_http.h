#ifndef NUTSHELL_AI_HTTP_H
#define NUTSHELL_AI_HTTP_H

#include <stddef.h>
#include "ai_netcancel.h"

typedef struct {
    int    status_code;   /* HTTP status (200, 401, etc.) or 0 on transport error */
    char  *body;          /* heap-allocated response body (caller must free) */
    size_t body_len;
    char   error[256];    /* human-readable error message */
} AiHttpResponse;

/*
 * Synchronous HTTP POST — designed to be called from a background thread.
 * url:     full endpoint URL
 * headers: NULL-terminated array of raw header strings
 *          (e.g. {"x-api-key: sk-xxx", "anthropic-version: 2023-06-01", NULL})
 *          May be NULL for no extra headers.
 * body:    JSON request body
 * body_len: length of body
 * resp:    output response struct
 *
 * nc:      the caller's cancel token, or NULL. The request handle is armed on
 *          it while the call blocks, so another thread firing it makes the
 *          call return at once with a "cancelled" error (same for the
 *          streaming POST and the GET below).
 *
 * Returns 0 on success (check resp->status_code), -1 on transport error.
 */
int ai_http_post(const char *url, const char * const *headers,
                 const char *body, size_t body_len,
                 AiHttpResponse *resp, AiNetCancel *nc);

/*
 * The same POST with the caller's Content-Type (an upload, say a multipart
 * body). A content type with CR or LF, or over 200 bytes, fails with
 * "Invalid content type". Its WinHTTP timeouts are the short upload set:
 * resolve 10 s, connect 10 s, send 30 s, receive 60 s (ai_http_post keeps the
 * chat's 60 s connect and 120 s receive).
 */
int ai_http_post_ct(const char *url, const char * const *headers,
                    const char *content_type, const char *body, size_t body_len,
                    AiHttpResponse *resp, AiNetCancel *nc);

/*
 * ai_http_post_ct on a session the caller owns (a WinHTTP HINTERNET passed as
 * void *, so this header stays free of windows.h): the call never opens or
 * closes it, so one session serves a first try and its retry. The timeouts are
 * resolve 10 s, connect 10 s and the given send and receive values (ms), set
 * on the session with WinHttpSetTimeouts; do not share the session between
 * concurrent calls. A NULL session fails with "No session". Off Windows it
 * behaves as the stub of ai_http_post_ct.
 */
int ai_http_post_ct_ex(void *session, const char *url, const char * const *headers,
                       const char *content_type, const char *body, size_t body_len,
                       int send_ms, int recv_ms, AiHttpResponse *resp, AiNetCancel *nc);

/*
 * Synchronous HTTP GET — designed to be called from a background thread.
 * url:         full endpoint URL
 * headers:     NULL-terminated array of raw header strings
 *              (e.g. {"Authorization: Bearer sk-xxx", NULL}), may be NULL
 * resp:        output response struct
 *
 * Returns 0 on success (check resp->status_code), -1 on transport error.
 */
int ai_http_get(const char *url, const char * const *headers,
                AiHttpResponse *resp, AiNetCancel *nc);

/* Callback for streaming HTTP POST. Called for each chunk of data received.
 * data: raw bytes, len: byte count, userdata: caller context.
 * Return 0 to continue, non-zero to abort the stream. */
typedef int (*AiStreamCallback)(const char *data, size_t len, void *userdata);

/*
 * Streaming HTTP POST — calls cb for each network chunk as it arrives.
 * url:        full endpoint URL
 * headers:    NULL-terminated array of raw header strings, may be NULL
 * body:       JSON request body
 * body_len:   length of body
 * cb:         callback invoked for each chunk
 * userdata:   passed to callback
 * status_out: receives HTTP status code (may be NULL)
 * error:      error message buffer (may be NULL)
 * error_size: size of error buffer
 *
 * nc:        cancel token or NULL (see ai_http_post). A cancelled call
 *            returns -1 with the error "cancelled".
 *
 * Returns 0 on success, -1 on transport error. -1 with a 2xx *status_out
 * means the body broke after the headers: `error` then holds "connection
 * lost during reply" (the reply may or may not have arrived whole).
 */
int ai_http_post_stream(const char *url, const char * const *headers,
                        const char *body, size_t body_len,
                        AiStreamCallback cb, void *userdata,
                        int *status_out, char *error, size_t error_size,
                        AiNetCancel *nc);

/*
 * ai_http_post_stream on a session the caller owns (a WinHTTP HINTERNET passed as
 * void *): the call never opens or closes it. Timeouts: resolve 10 s, connect 10 s,
 * send 30 s and receive recv_ms, set on the session. It never follows a redirect
 * (WINHTTP_OPTION_REDIRECT_POLICY_NEVER), so a bearer key in `headers` is never
 * re-sent to another host: a 3xx comes back as its status, rc -1, the body in
 * `error`. Same callback, status and error contract as ai_http_post_stream; a NULL
 * session fails with the error "No session". Do not share the session between
 * concurrent calls. Off Windows it behaves as the stub of ai_http_post_stream.
 */
int ai_http_post_stream_ex(void *session, const char *url, const char * const *headers,
                           const char *body, size_t body_len, int recv_ms,
                           AiStreamCallback cb, void *userdata,
                           int *status_out, char *error, size_t error_size,
                           AiNetCancel *nc);

/* Free the response body. Safe to call with NULL body. */
void ai_http_response_free(AiHttpResponse *resp);

#endif /* NUTSHELL_AI_HTTP_H */
