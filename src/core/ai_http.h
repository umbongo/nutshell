#ifndef NUTSHELL_AI_HTTP_H
#define NUTSHELL_AI_HTTP_H

#include <stddef.h>

typedef struct {
    int    status_code;   /* HTTP status (200, 401, etc.) or 0 on transport error */
    char  *body;          /* heap-allocated response body (caller must free) */
    size_t body_len;
    char   error[256];    /* human-readable error message */
} AiHttpResponse;

/*
 * Synchronous HTTP POST — designed to be called from a background thread.
 * url:         full endpoint URL
 * auth_header: "Bearer sk-xxx..." (Authorization header value)
 * body:        JSON request body
 * body_len:    length of body
 * resp:        output response struct
 *
 * Returns 0 on success (check resp->status_code), -1 on transport error.
 */
int ai_http_post(const char *url, const char *auth_header,
                 const char *body, size_t body_len,
                 AiHttpResponse *resp);

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
                AiHttpResponse *resp);

/* Callback for streaming HTTP POST. Called for each chunk of data received.
 * data: raw bytes, len: byte count, userdata: caller context.
 * Return 0 to continue, non-zero to abort the stream. */
typedef int (*AiStreamCallback)(const char *data, size_t len, void *userdata);

/*
 * Streaming HTTP POST — calls cb for each network chunk as it arrives.
 * url:         full endpoint URL
 * auth_header: "Bearer sk-xxx..." (Authorization header value)
 * body:        JSON request body
 * body_len:    length of body
 * cb:          callback invoked for each chunk
 * userdata:    passed to callback
 * status_out:  receives HTTP status code (may be NULL)
 * error:       error message buffer (may be NULL)
 * error_size:  size of error buffer
 *
 * Returns 0 on success, -1 on transport error.
 */
int ai_http_post_stream(const char *url, const char *auth_header,
                        const char *body, size_t body_len,
                        AiStreamCallback cb, void *userdata,
                        int *status_out, char *error, size_t error_size);

/* Free the response body. Safe to call with NULL body. */
void ai_http_response_free(AiHttpResponse *resp);

#endif /* NUTSHELL_AI_HTTP_H */
