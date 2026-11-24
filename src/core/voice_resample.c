#include "voice_resample.h"

/* Generated once from a Kaiser(beta 6) windowed sinc, fc = 7.6 kHz of 48 kHz,
 * 48 taps split into 3 phases (tap j of phase p is h[3j + p]) and each phase
 * normalised to sum 32768.  Tap j multiplies the input j samples back. */
static const int32_t k_taps[3][VRS_TAPS] = {
    { -19, 105, -299, 654, -1225, 2097, -3545, 7385, 29816, -2807, 709, -54, -142, 150, -95, 38 },
    { -15, 81, -262, 660, -1432, 2897, -6111, 20566, 20566, -6111, 2897, -1432, 660, -262, 81, -15 },
    { 38, -95, 150, -142, -54, 709, -2807, 29816, 7385, -3545, 2097, -1225, 654, -299, 105, -19 }
};

void voice_rs_init(VoiceResampler *r)
{
    for (int i = 0; i < VRS_TAPS; i++) r->hist[i] = 0;
    r->phase = 0;
    r->nhist = 0;
}

size_t voice_rs_out_max(size_t n_in)
{
    /* at most ceil((3n + phase) / 2) with phase <= 2 */
    return (3 * n_in) / 2 + 2;
}

size_t voice_rs_16_to_24(VoiceResampler *r, const int16_t *in, size_t n,
                         int16_t *out, size_t cap)
{
    size_t produced = 0;
    for (size_t s = 0; s < n; s++) {
        for (int i = 0; i < VRS_TAPS - 1; i++) r->hist[i] = r->hist[i + 1];
        r->hist[VRS_TAPS - 1] = in[s];
        if (r->nhist < VRS_TAPS) r->nhist++;

        while (r->phase < 3) {
            if (produced < cap) {
                const int32_t *tp = k_taps[r->phase];
                int64_t acc = 16384; /* rounding */
                for (int j = 0; j < VRS_TAPS; j++)
                    acc += (int64_t)tp[j] * r->hist[VRS_TAPS - 1 - j];
                acc >>= 15;
                if (acc > 32767) acc = 32767;
                if (acc < -32768) acc = -32768;
                out[produced] = (int16_t)acc;
                produced++;
            }
            r->phase += 2;
        }
        r->phase -= 3;
    }
    return produced;
}
