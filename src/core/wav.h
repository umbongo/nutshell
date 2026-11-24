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

/* Software microphone gain. pcm_gain_q16 clamps db to the voice range and
 * returns the linear gain in Q16 (0 dB = 65536). pcm_apply_gain_q16 scales
 * in place, rounding to nearest and saturating at int16; 65536, NULL or n == 0
 * leaves the samples alone. */
uint32_t pcm_gain_q16(int db);
void     pcm_apply_gain_q16(int16_t *s, size_t n, uint32_t q16);

typedef enum { PCM_SPEECH = 0, PCM_SILENT = 1, PCM_ALL_ZERO = 2, PCM_EMPTY = 3 } PcmVerdict;
#define PCM_WINDOW 320          /* 20 ms at 16 kHz */
#define PCM_SPEECH_RMS 250      /* int16 RMS, about -42 dBFS */
PcmVerdict pcm_classify(const int16_t *s, size_t n);
/* 1 when any of the n samples is not 0. */
int pcm_any_nonzero(const int16_t *s, size_t n);
/* pcm_classify of gained samples, told whether the raw samples before the gain
 * had any non-zero: a quiet mic's +-1 noise rounds to 0 at a negative gain, and
 * that is silence, not a dead device (PCM_ALL_ZERO becomes PCM_SILENT). */
PcmVerdict pcm_classify_raw(const int16_t *s, size_t n, int raw_nonzero);

#endif
