#include "wav.h"
#include "secure_zero.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
}

static void put32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

void wav_header_write(unsigned char out[WAV_HEADER_BYTES], unsigned rate, size_t data_bytes)
{
    unsigned long d = (unsigned long)data_bytes;
    memcpy(out, "RIFF", 4);
    put32(out + 4, 36UL + d);
    memcpy(out + 8, "WAVE", 4);
    memcpy(out + 12, "fmt ", 4);
    put32(out + 16, 16UL);
    put16(out + 20, 1);
    put16(out + 22, 1);
    put32(out + 24, rate);
    put32(out + 28, (unsigned long)rate * 2UL);
    put16(out + 32, 2);
    put16(out + 34, 16);
    memcpy(out + 36, "data", 4);
    put32(out + 40, d);
}

void pcm_init(PcmBuf *b, size_t max_samples)
{
    b->s = NULL;
    b->n = 0;
    b->cap = 0;
    b->max = max_samples;
}

size_t pcm_append(PcmBuf *b, const int16_t *src, size_t n)
{
    if (!b || !src || n == 0 || b->n >= b->max) return 0;
    size_t take = b->max - b->n;
    if (n < take) take = n;
    size_t need = b->n + take;
    if (need > b->cap) {
        size_t cap = b->cap ? b->cap : 16000;
        while (cap < need) cap *= 2;
        if (cap > b->max) cap = b->max;
        int16_t *ns = (int16_t *)malloc(cap * sizeof(int16_t));
        if (!ns) return 0;
        if (b->s) {
            memcpy(ns, b->s, b->n * sizeof(int16_t));
            secure_zero(b->s, b->cap * sizeof(int16_t));
            free(b->s);
        }
        b->s = ns;
        b->cap = cap;
    }
    memcpy(b->s + b->n, src, take * sizeof(int16_t));
    b->n += take;
    return take;
}

int pcm_full(const PcmBuf *b)
{
    return b->n >= b->max;
}

void pcm_free(PcmBuf *b)
{
    if (b->s) {
        secure_zero(b->s, b->cap * sizeof(int16_t));
        free(b->s);
    }
    b->s = NULL;
    b->n = 0;
    b->cap = 0;
}

unsigned char *pcm_to_wav(const PcmBuf *b, unsigned rate, size_t *out_len)
{
    size_t data = b->n * 2;
    unsigned char *w = (unsigned char *)malloc(WAV_HEADER_BYTES + data);
    if (!w) return NULL;
    wav_header_write(w, rate, data);
    for (size_t i = 0; i < b->n; i++) {
        unsigned v = (unsigned)(uint16_t)b->s[i];
        w[WAV_HEADER_BYTES + 2 * i] = (unsigned char)(v & 0xFFu);
        w[WAV_HEADER_BYTES + 2 * i + 1] = (unsigned char)(v >> 8);
    }
    if (out_len) *out_len = WAV_HEADER_BYTES + data;
    return w;
}

double pcm_peak(const int16_t *s, size_t n)
{
    int peak = 0;
    for (size_t i = 0; s && i < n; i++) {
        int v = s[i] < 0 ? -(int)s[i] : (int)s[i];
        if (v > peak) peak = v;
    }
    return (double)peak / 32768.0 > 1.0 ? 1.0 : (double)peak / 32768.0;
}

static double sumsq(const int16_t *s, size_t n)
{
    double acc = 0.0;
    for (size_t i = 0; i < n; i++) acc += (double)s[i] * (double)s[i];
    return acc;
}

double pcm_rms(const int16_t *s, size_t n)
{
    if (!s || n == 0) return 0.0;
    return sqrt(sumsq(s, n) / (double)n) / 32768.0;
}

PcmVerdict pcm_classify(const int16_t *s, size_t n)
{
    if (!s || n == 0) return PCM_EMPTY;
    size_t i = 0;
    while (i < n && s[i] == 0) i++;
    if (i == n) return PCM_ALL_ZERO;
    const double thr = (double)PCM_SPEECH_RMS * (double)PCM_SPEECH_RMS;
    for (size_t off = 0; off < n; off += PCM_WINDOW) {
        size_t len = n - off < PCM_WINDOW ? n - off : PCM_WINDOW;
        if (sumsq(s + off, len) / (double)len >= thr) return PCM_SPEECH;
    }
    return PCM_SILENT;
}
