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

/* Free the response body. Safe to call with NULL body. */
void ai_http_response_free(AiHttpResponse *resp);

#endif /* NUTSHELL_AI_HTTP_H */
