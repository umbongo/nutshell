/* tests/http_loopback_win.c -- see http_loopback_win.h. Built only by `make wintest`. */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "http_loopback_win.h"

struct HttpLoop {
    int              mode;
    HlAudio          audio;
    char             sse_path[512];
    SOCKET           ls;
    int              port;
    HANDLE           thread;
    volatile LONG    stop;
    volatile LONG    requests;
    CRITICAL_SECTION lock;
    char             head[4096];
    unsigned char   *body;       /* the last request body */
    size_t           body_len;
};

#define HL_SSE_DEFAULT "tests/fixtures/voice_tts/provisional_mistral_wav.sse"
#define HL_SSE_ERRFILE "tests/fixtures/voice_tts/provisional_mistral_error.sse"

static int send_all(SOCKET s, const void *p, size_t n)
{
    const char *c = (const char *)p;
    while (n) {
        int k = send(s, c, (int)(n > 65536 ? 65536 : n), 0);
        if (k <= 0) return -1;
        c += k;
        n -= (size_t)k;
    }
    return 0;
}

static void put_u16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void put_u32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

unsigned char *hl_wav_build(const HlAudio *a, int rate, size_t *len)
{
    int ch = (a && a->channels > 0) ? a->channels : 1;
    int fl = a ? a->float32 : 0;
    double sec = (a && a->seconds > 0) ? a->seconds : 0.25;
    size_t frames = (size_t)(sec * rate);
    size_t bps = fl ? 4 : 2;
    size_t data = frames * (size_t)ch * bps;
    size_t total = 12 + 24 + 20 + 8 + data;     /* RIFF, fmt, LIST, data header, samples */
    unsigned char *w = (unsigned char *)calloc(1, total);
    if (!w) return NULL;
    memcpy(w, "RIFF", 4);
    put_u32(w + 4, 0xFFFFFFFFul);
    memcpy(w + 8, "WAVE", 4);
    memcpy(w + 12, "fmt ", 4);
    put_u32(w + 16, 16);
    put_u16(w + 20, fl ? 3u : 1u);
    put_u16(w + 22, (unsigned)ch);
    put_u32(w + 24, (unsigned long)rate);
    put_u32(w + 28, (unsigned long)((size_t)rate * (size_t)ch * bps));
    put_u16(w + 32, (unsigned)((size_t)ch * bps));
    put_u16(w + 34, (unsigned)(bps * 8));
    memcpy(w + 36, "LIST", 4);
    put_u32(w + 40, 12);
    memcpy(w + 44, "INFO", 4);
    memcpy(w + 48, "ISFT", 4);
    put_u32(w + 52, 0);                         /* an empty ISFT entry */
    memcpy(w + 56, "data", 4);
    put_u32(w + 60, 0xFFFFFFFFul);
    unsigned char *d = w + 64;
    for (size_t i = 0; i < frames; i++) {
        double v = 0.5 * sin(2.0 * 3.14159265358979 * 440.0 * (double)i / (double)rate);
        for (int c = 0; c < ch; c++) {
            if (fl) {
                float f = (float)v;
                memcpy(d, &f, 4);
                d += 4;
            } else {
                short s = (short)(v * 32767.0);
                put_u16(d, (unsigned)(unsigned short)s);
                d += 2;
            }
        }
    }
    *len = total;
    return w;
}

/* One chunk of a chunked body. */
static int send_chunk(SOCKET s, const void *p, size_t n)
{
    char hdr[24];
    int k = snprintf(hdr, sizeof(hdr), "%zx\r\n", n);
    if (send_all(s, hdr, (size_t)k) != 0 || send_all(s, p, n) != 0 ||
        send_all(s, "\r\n", 2) != 0) return -1;
    return 0;
}

static int send_head(SOCKET s, const char *status, const char *ctype, const char *extra,
                     long content_length)
{
    char b[512];
    int k;
    if (content_length >= 0)
        k = snprintf(b, sizeof(b), "HTTP/1.1 %s\r\nContent-Type: %s\r\n%sContent-Length: %ld\r\n"
                     "Connection: close\r\n\r\n", status, ctype, extra, content_length);
    else
        k = snprintf(b, sizeof(b), "HTTP/1.1 %s\r\nContent-Type: %s\r\n%sTransfer-Encoding: chunked\r\n"
                     "Connection: close\r\n\r\n", status, ctype, extra);
    return send_all(s, b, (size_t)k);
}

static void send_status_body(SOCKET s, const char *status, const char *body)
{
    if (send_head(s, status, "application/json", "", (long)strlen(body)) == 0)
        send_all(s, body, strlen(body));
}

/* The chunked WAV, in writes of 1, 7, 4095 and the rest. */
static void send_wavs(SOCKET s, const unsigned char *w, size_t n, int times)
{
    if (send_head(s, "200 OK", "audio/wav", "", -1) != 0) return;
    static const size_t cuts[3] = { 1, 7, 4095 };
    for (int t = 0; t < times; t++) {
        size_t off = 0;
        for (int i = 0; i < 3 && off < n; i++) {
            size_t k = cuts[i] < n - off ? cuts[i] : n - off;
            if (send_chunk(s, w + off, k) != 0) return;
            off += k;
            Sleep(2);                           /* a separate segment, a separate read */
        }
        if (off < n && send_chunk(s, w + off, n - off) != 0) return;
    }
    send_all(s, "0\r\n\r\n", 5);
}

static void send_sse(SOCKET s, const unsigned char *p, size_t n)
{
    if (send_head(s, "200 OK", "text/event-stream", "Cache-Control: no-cache\r\n", -1) != 0) return;
    for (size_t off = 0; off < n; off += 2048) {
        size_t k = n - off < 2048 ? n - off : 2048;
        if (send_chunk(s, p + off, k) != 0) return;
        Sleep(1);
    }
    send_all(s, "0\r\n\r\n", 5);
}

static unsigned char *read_file(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    unsigned char *b = (unsigned char *)malloc((size_t)sz + 1);
    if (b && fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); b = NULL; }
    fclose(f);
    if (b) *n = (size_t)sz;
    return b;
}

/* Waits up to 50 ms for s to be readable. 1 readable, 0 timeout. */
static int readable(SOCKET s)
{
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    struct timeval tv = { 0, 50000 };
    return select(0, &fds, NULL, NULL, &tv) > 0;
}

/* Reads the request head and body. 0 ok, -1 failed or stopped. */
static int read_request(HttpLoop *h, SOCKET c)
{
    char head[4096];
    size_t have = 0;
    char *end = NULL;
    while (!end) {
        if (h->stop) return -1;
        if (!readable(c)) continue;
        if (have >= sizeof(head) - 1) return -1;
        int k = recv(c, head + have, (int)(sizeof(head) - 1 - have), 0);
        if (k <= 0) return -1;
        have += (size_t)k;
        head[have] = '\0';
        end = strstr(head, "\r\n\r\n");
    }
    size_t head_len = (size_t)(end - head) + 4;
    long cl = 0;
    for (const char *p = head; p < end; p++) {
        if (_strnicmp(p, "Content-Length:", 15) == 0) { cl = atol(p + 15); break; }
        const char *nl = strstr(p, "\r\n");
        if (!nl) break;
        p = nl + 1;
    }
    if (cl < 0 || cl > (4L << 20)) return -1;
    unsigned char *body = (unsigned char *)malloc((size_t)cl + 1);
    if (!body) return -1;
    size_t got = have - head_len;
    if (got > (size_t)cl) got = (size_t)cl;
    memcpy(body, head + head_len, got);
    while (got < (size_t)cl) {
        if (h->stop) { free(body); return -1; }
        if (!readable(c)) continue;
        int k = recv(c, (char *)body + got, (int)((size_t)cl - got), 0);
        if (k <= 0) { free(body); return -1; }
        got += (size_t)k;
    }
    EnterCriticalSection(&h->lock);
    memcpy(h->head, head, head_len);
    h->head[head_len] = '\0';
    free(h->body);
    h->body = body;
    h->body_len = (size_t)cl;
    h->body[cl] = '\0';
    LeaveCriticalSection(&h->lock);
    InterlockedIncrement(&h->requests);
    return 0;
}

static void answer(HttpLoop *h, SOCKET c)
{
    size_t n = 0;
    switch (h->mode) {
    case HLS_WAV:
    case HLS_WAV_BADRATE:
    case HLS_WAV_TWICE: {
        unsigned char *w = hl_wav_build(&h->audio, h->mode == HLS_WAV_BADRATE ? 22050 : 24000, &n);
        if (w) { send_wavs(c, w, n, h->mode == HLS_WAV_TWICE ? 2 : 1); free(w); }
        break;
    }
    case HLS_EMPTY:
        send_head(c, "200 OK", "audio/wav", "", 0);
        break;
    case HLS_SSE:
    case HLS_SSE_ERROR: {
        const char *path = h->mode == HLS_SSE_ERROR ? HL_SSE_ERRFILE
                         : (h->sse_path[0] ? h->sse_path : HL_SSE_DEFAULT);
        unsigned char *b = read_file(path, &n);
        if (b) { send_sse(c, b, n); free(b); }
        else send_status_body(c, "500 Internal Server Error", "{\"error\":\"fixture missing\"}");
        break;
    }
    case HLS_SSE_DONE_ONLY: {
        static const char done[] =
            "event: speech.audio.done\n"
            "data: {\"type\": \"speech.audio.done\", \"usage\": {\"prompt_tokens\": 1, "
            "\"total_tokens\": 1, \"completion_tokens\": 0}}\n\n";
        send_sse(c, (const unsigned char *)done, sizeof(done) - 1);
        break;
    }
    case HLS_401: send_status_body(c, "401 Unauthorized", "{\"error\":\"invalid key\"}"); break;
    case HLS_404: send_status_body(c, "404 Not Found", "{\"error\":\"no such model\"}"); break;
    case HLS_422: send_status_body(c, "422 Unprocessable Entity", "{\"error\":\"unknown field\"}"); break;
    case HLS_429: send_status_body(c, "429 Too Many Requests", "{\"error\":\"slow down\"}"); break;
    case HLS_302: {
        char loc[96];
        snprintf(loc, sizeof(loc), "Location: http://127.0.0.1:%d/elsewhere\r\n", h->port);
        send_head(c, "302 Found", "text/plain", loc, 0);
        break;
    }
    case HLS_HANG:
        while (!h->stop) Sleep(20);
        break;
    default:
        break;
    }
}

static DWORD WINAPI loop_thread(LPVOID p)
{
    HttpLoop *h = (HttpLoop *)p;
    while (!h->stop) {
        if (!readable(h->ls)) continue;
        SOCKET c = accept(h->ls, NULL, NULL);
        if (c == INVALID_SOCKET) continue;
        if (read_request(h, c) == 0) answer(h, c);
        shutdown(c, SD_SEND);
        closesocket(c);
    }
    return 0;
}

HttpLoop *hl_start(int mode, const HlAudio *a)
{
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) return NULL;
    HttpLoop *h = (HttpLoop *)calloc(1, sizeof(*h));
    if (!h) { WSACleanup(); return NULL; }
    h->mode = mode;
    if (a) {
        h->audio = *a;
        h->audio.sse_file = NULL;
        if (a->sse_file) snprintf(h->sse_path, sizeof(h->sse_path), "%s", a->sse_file);
    }
    InitializeCriticalSection(&h->lock);
    h->ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int salen = (int)sizeof(sa);
    if (h->ls == INVALID_SOCKET || bind(h->ls, (struct sockaddr *)&sa, sizeof(sa)) != 0 ||
        listen(h->ls, 4) != 0 || getsockname(h->ls, (struct sockaddr *)&sa, &salen) != 0) {
        if (h->ls != INVALID_SOCKET) closesocket(h->ls);
        DeleteCriticalSection(&h->lock);
        free(h);
        WSACleanup();
        return NULL;
    }
    h->port = ntohs(sa.sin_port);
    h->thread = CreateThread(NULL, 0, loop_thread, h, 0, NULL);
    if (!h->thread) {
        closesocket(h->ls);
        DeleteCriticalSection(&h->lock);
        free(h);
        WSACleanup();
        return NULL;
    }
    return h;
}

int hl_port(HttpLoop *h) { return h ? h->port : 0; }

void hl_stop(HttpLoop *h)
{
    if (!h) return;
    InterlockedExchange(&h->stop, 1);
    WaitForSingleObject(h->thread, INFINITE);
    CloseHandle(h->thread);
    closesocket(h->ls);
    free(h->body);
    DeleteCriticalSection(&h->lock);
    free(h);
    WSACleanup();
}

const char *hl_request_head(HttpLoop *h) { return h ? h->head : ""; }

const char *hl_request_body(HttpLoop *h, size_t *len)
{
    if (len) *len = h ? h->body_len : 0;
    return h && h->body ? (const char *)h->body : "";
}

int hl_requests(HttpLoop *h) { return h ? (int)h->requests : 0; }

#endif /* _WIN32 */
