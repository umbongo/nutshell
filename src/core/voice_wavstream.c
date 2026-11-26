#include "voice_wavstream.h"
#include "secure_zero.h"

#include <string.h>

enum { VWS_E_NONE = 0, VWS_E_FORMAT, VWS_E_NOTWAV, VWS_E_TOOLONG,
       VWS_E_SECOND, VWS_E_SMALLCAP };

static void fail(VoiceWavStream *w, int code)
{
    w->state = VWS_ERROR;
    w->err = code;
}

void voice_wavs_init(VoiceWavStream *w, VoiceWavRaw raw)
{
    memset(w, 0, sizeof *w);
    if (raw == VWS_RAW_F32_MONO) {
        w->state = VWS_DATA;
        w->fmt_tag = 3;
        w->bits = 32;
        w->channels = 1;
    } else {
        w->state = VWS_HEADER;
    }
}

size_t voice_wavs_out_max(size_t n_in) { return n_in / 2 + 1; }
VoiceWavState voice_wavs_state(const VoiceWavStream *w) { return w->state; }
uint64_t voice_wavs_frames(const VoiceWavStream *w) { return w->frames; }

int voice_wavs_finish_ok(const VoiceWavStream *w)
{
    return w->state == VWS_DATA && w->carry_n == 0 && w->frames > 0;
}

const char *voice_wavs_error(const VoiceWavStream *w)
{
    switch (w->err) {
    case VWS_E_FORMAT:   return "unsupported audio format";
    case VWS_E_NOTWAV:   return "not a WAV stream";
    case VWS_E_TOOLONG:  return "WAV header too long";
    case VWS_E_SECOND:   return "second WAV header";
    case VWS_E_SMALLCAP: return "output buffer too small";
    default:             return "";
    }
}

int voice_wavs_error_is_format(const VoiceWavStream *w)
{
    return w->err == VWS_E_FORMAT;
}

int voice_wavs_error_is_notwav(const VoiceWavStream *w)
{
    return w->err == VWS_E_NOTWAV;
}

void voice_wavs_wipe(VoiceWavStream *w)
{
    secure_zero(w, sizeof *w);
}

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static unsigned rd16(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

/* Walks the buffered header. 0: need more bytes; 1: the data chunk header is
 * complete, *data_off is the first data byte; -1: failed (w->err set). */
static int parse_header(VoiceWavStream *w, size_t *data_off)
{
    static const unsigned char riff[4] = { 'R', 'I', 'F', 'F' };
    static const unsigned char wave[4] = { 'W', 'A', 'V', 'E' };
    const unsigned char *h = w->hdr;
    uint64_t len = w->hdr_len, pos;
    uint64_t i;
    int have_fmt = 0;
    unsigned tag = 0, bits = 0, ch = 0;

    for (i = 0; i < len && i < 4; i++)
        if (h[i] != riff[i]) { fail(w, VWS_E_NOTWAV); return -1; }
    for (i = 8; i < len && i < 12; i++)
        if (h[i] != wave[i - 8]) { fail(w, VWS_E_NOTWAV); return -1; }
    pos = 12;
    while (pos + 8 <= len) {
        uint64_t size = rd32(h + pos + 4);
        uint64_t body = pos + 8;
        if (memcmp(h + pos, "data", 4) == 0) {
            if (!have_fmt) { fail(w, VWS_E_NOTWAV); return -1; }
            w->fmt_tag = tag;
            w->bits = bits;
            w->channels = ch;
            *data_off = (size_t)body;
            return 1;
        }
        if (memcmp(h + pos, "fmt ", 4) == 0) {
            if (size < 16) { fail(w, VWS_E_NOTWAV); return -1; }
            if (body + size > VOICE_WAVS_HDR_MAX) { fail(w, VWS_E_TOOLONG); return -1; }
            if (body + 16 > len) return 0;
            tag = rd16(h + body);
            ch = rd16(h + body + 2);
            bits = rd16(h + body + 14);
            if (!((tag == 1 && bits == 16) || (tag == 3 && bits == 32))
                || (ch != 1 && ch != 2) || rd32(h + body + 4) != VOICE_WAVS_RATE) {
                fail(w, VWS_E_FORMAT);
                return -1;
            }
            have_fmt = 1;
        }
        /* the next chunk: size plus the RIFF pad byte of an odd size */
        pos = body + size + (size & 1);
        if (pos > VOICE_WAVS_HDR_MAX) { fail(w, VWS_E_TOOLONG); return -1; }
    }
    if (len >= VOICE_WAVS_HDR_MAX) { fail(w, VWS_E_TOOLONG); return -1; }
    return 0;
}

static int16_t f32_to_s16(const unsigned char *p)
{
    uint32_t u = rd32(p);
    float f;
    double d;
    memcpy(&f, &u, sizeof f);
    if (f != f) return 0;                      /* NaN */
    if (f > 1.0f) f = 1.0f;
    if (f < -1.0f) f = -1.0f;
    d = (double)f * 32767.0;
    return (int16_t)(d >= 0 ? (int)(d + 0.5) : -(int)(0.5 - d));
}

static int16_t sample_at(const VoiceWavStream *w, const unsigned char *p)
{
    return w->bits == 16 ? (int16_t)(uint16_t)rd16(p) : f32_to_s16(p);
}

static int16_t decode_frame(const VoiceWavStream *w, const unsigned char *p)
{
    int16_t a = sample_at(w, p);
    int sum;
    if (w->channels == 1) return a;
    sum = (int)a + (int)sample_at(w, p + w->bits / 8);
    return (int16_t)(sum >= 0 ? (sum + 1) / 2 : -((1 - sum) / 2));
}

/* The data path. out has room (checked by the caller). Returns samples. */
static size_t process(VoiceWavStream *w, const unsigned char *p, size_t n,
                      int16_t *out)
{
    size_t fs = (size_t)w->channels * (w->bits / 8), k, produced = 0;

    /* second-header window over the raw bytes, before any is decoded */
    for (k = 0; k < n; k++) {
        w->win[w->win_head] = p[k];
        w->win_head = (w->win_head + 1) % 12;
        if (w->win_n < 12) w->win_n++;
        if (p[k] == 'E' && w->win_n == 12) {
            unsigned char t[12];
            size_t j;
            for (j = 0; j < 12; j++) t[j] = w->win[(w->win_head + j) % 12];
            if (memcmp(t, "RIFF", 4) == 0 && memcmp(t + 8, "WAVE", 4) == 0) {
                fail(w, VWS_E_SECOND);
                return 0;
            }
        }
    }
    k = 0;
    if (w->carry_n > 0) {
        while (k < n && w->carry_n < fs) w->carry[w->carry_n++] = p[k++];
        if (w->carry_n < fs) return 0;
        out[produced++] = decode_frame(w, w->carry);
        w->carry_n = 0;
    }
    while (n - k >= fs) {
        out[produced++] = decode_frame(w, p + k);
        k += fs;
    }
    while (k < n) w->carry[w->carry_n++] = p[k++];
    w->frames += produced;
    return produced;
}

size_t voice_wavs_feed(VoiceWavStream *w, const unsigned char *in, size_t n,
                       int16_t *out, size_t cap)
{
    size_t produced = 0;
    if (w->state == VWS_ERROR || n == 0) return 0;
    if (cap < voice_wavs_out_max(n)) { fail(w, VWS_E_SMALLCAP); return 0; }
    while (n > 0 && w->state == VWS_HEADER) {
        size_t room = VOICE_WAVS_HDR_MAX - w->hdr_len;
        size_t k = n < room ? n : room, off = 0;
        int r;
        memcpy(w->hdr + w->hdr_len, in, k);
        w->hdr_len += k;
        in += k;
        n -= k;
        r = parse_header(w, &off);
        if (r < 0) return 0;
        if (r > 0) {
            size_t left = w->hdr_len - off;
            w->state = VWS_DATA;
            w->hdr_len = 0;
            produced = process(w, w->hdr + off, left, out);
            if (w->state == VWS_ERROR) return 0;
            out += produced;
            break;
        }
    }
    if (w->state == VWS_DATA && n > 0) {
        size_t more = process(w, in, n, out);
        if (w->state == VWS_ERROR) return 0;
        produced += more;
    }
    return produced;
}
