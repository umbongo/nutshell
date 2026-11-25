#ifndef NUTSHELL_TESTS_HTTP_LOOPBACK_WIN_H
#define NUTSHELL_TESTS_HTTP_LOOPBACK_WIN_H

#include <stddef.h>

/*
 * http_loopback_win -- a small HTTP/1.1 server on 127.0.0.1 for the wintests
 * (the AI speech fetch, Task 5 onwards). One thread accepts and answers one
 * connection at a time; every answer closes the connection. Every thread it
 * starts is joined in hl_stop.
 *
 * Audio modes build their WAV in code: 24,000 Hz (HLS_WAV_BADRATE 22,050 Hz),
 * RIFF and data sizes 0xFFFFFFFF (a streaming server cannot know them), a LIST
 * chunk before data, sent chunked in writes of 1, 7, 4,095 and the rest.
 */

typedef struct HttpLoop HttpLoop;

enum {
    HLS_WAV = 0,        /* chunked WAV, unknown sizes, a LIST before data */
    HLS_WAV_BADRATE,    /* the same at 22,050 Hz */
    HLS_WAV_TWICE,      /* two WAVs back to back */
    HLS_EMPTY,          /* 200 with an empty body */
    HLS_SSE,            /* text/event-stream: the provisional Mistral fixture, or HlAudio.sse_file */
    HLS_SSE_ERROR,      /* an SSE stream that ends in an error event (the provisional error fixture) */
    HLS_SSE_DONE_ONLY,  /* an SSE stream that carries only speech.audio.done */
    HLS_401,            /* each answers its status with a small JSON error body */
    HLS_404,
    HLS_422,
    HLS_429,
    HLS_HANG,           /* reads the request, never answers (until hl_stop) */
    HLS_302             /* answers every request with a 302 to another path */
};

typedef struct {
    int         channels;   /* 0 means 1 */
    int         float32;    /* 0: 16-bit PCM, 1: 32-bit float */
    double      seconds;    /* 0 means 0.25 */
    const char *sse_file;   /* HLS_SSE: a path, or NULL for the provisional fixture */
} HlAudio;

/* Binds 127.0.0.1 port 0 and starts the server thread. `a` may be NULL. NULL on failure. */
HttpLoop *hl_start(int mode, const HlAudio *a);
int       hl_port(HttpLoop *h);
/* Closes everything and joins every thread; frees h. */
void      hl_stop(HttpLoop *h);

/* The raw head (up to the blank line) and the body of the last request read;
 * valid until hl_stop. Empty until a request has arrived. */
const char *hl_request_head(HttpLoop *h);
const char *hl_request_body(HttpLoop *h, size_t *len);
/* How many requests have been read (answered or not). */
int         hl_requests(HttpLoop *h);

/* The WAV the audio modes send (one copy), so a test can compare byte for byte.
 * malloc'd; free() it. rate is in Hz. */
unsigned char *hl_wav_build(const HlAudio *a, int rate, size_t *len);

#endif
