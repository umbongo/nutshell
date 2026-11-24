#ifndef NUTSHELL_CORE_VOICE_RESAMPLE_H
#define NUTSHELL_CORE_VOICE_RESAMPLE_H

#include <stddef.h>
#include <stdint.h>

/* Streaming 16 kHz -> 24 kHz mono resampler (ratio 3/2), used for the
 * providers that accept only 24 kHz PCM.
 *
 * A polyphase FIR: the input is conceptually upsampled by 3 to 48 kHz,
 * low-passed, and every second sample kept.  3 phases of VRS_TAPS taps; the
 * prototype is a 48-tap Kaiser-windowed sinc (beta 6) with its cutoff at
 * 7.6 kHz (-6 dB there; flat within 0.15 dB to 6 kHz, -2.3 dB at 7 kHz,
 * -30 dB at 9 kHz).  Coefficients are a precomputed Q15 table; every
 * phase sums to exactly 32768, so DC gain is exactly 1.  Accumulation is
 * int64, rounded and saturated to int16.
 *
 * Output count: the state starts with zero history, so after total_in input
 * samples exactly ceil(total_in * 3 / 2) samples have been produced, however
 * the input is chunked.  The filter is causal with a group delay of 23.5
 * samples at 48 kHz (about 11.75 output samples, 0.49 ms): output sample m
 * describes the signal at input time (2m / 3) - 7.83 samples.  The first ~12
 * output samples are the start-up transient.
 *
 * The state carries the last VRS_TAPS inputs and the phase, so chunking never
 * changes the output (byte for byte). */

#define VRS_TAPS 16

typedef struct {
    int16_t hist[VRS_TAPS];  /* last inputs, oldest first, zero at start */
    unsigned phase;          /* 48 kHz offset of the next output, 0..2 */
    size_t nhist;            /* inputs seen so far, saturating at VRS_TAPS */
} VoiceResampler;

void   voice_rs_init(VoiceResampler *r);

/* Upper bound on the samples one call can produce for n_in input samples,
 * whatever the prior state. */
size_t voice_rs_out_max(size_t n_in);

/* Resample n samples; returns the count written to out.  cap should be at
 * least voice_rs_out_max(n); output beyond cap is dropped (never written). */
size_t voice_rs_16_to_24(VoiceResampler *r, const int16_t *in, size_t n,
                         int16_t *out, size_t cap);

#endif
