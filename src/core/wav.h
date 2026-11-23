#ifndef NUTSHELL_CORE_WAV_H
#define NUTSHELL_CORE_WAV_H

#include <stddef.h>
#include <stdint.h>

#define WAV_RATE 16000
#define WAV_HEADER_BYTES 44

/* RIFF/WAVE PCM header, little-endian, for 1 channel 16-bit at `rate`. */
void wav_header_write(unsigned char out[WAV_HEADER_BYTES], unsigned rate, size_t data_bytes);

typedef struct { int16_t *s; size_t n, cap, max; } PcmBuf;   /* max = hard cap in samples */

void   pcm_init(PcmBuf *b, size_t max_samples);
size_t pcm_append(PcmBuf *b, const int16_t *src, size_t n);  /* returns samples taken; stops at max */
int    pcm_full(const PcmBuf *b);
void   pcm_free(PcmBuf *b);                                   /* secure_zero, free, reset */

/* Heap WAV (header + data); *out_len set; NULL on OOM. Caller secure_zeroes + frees. */
unsigned char *pcm_to_wav(const PcmBuf *b, unsigned rate, size_t *out_len);

double pcm_peak(const int16_t *s, size_t n);   /* 0..1 of full scale */
double pcm_rms(const int16_t *s, size_t n);    /* 0..1 of full scale */

typedef enum { PCM_SPEECH = 0, PCM_SILENT = 1, PCM_ALL_ZERO = 2, PCM_EMPTY = 3 } PcmVerdict;
#define PCM_WINDOW 320          /* 20 ms at 16 kHz */
#define PCM_SPEECH_RMS 250      /* int16 RMS, about -42 dBFS */
PcmVerdict pcm_classify(const int16_t *s, size_t n);

#endif
