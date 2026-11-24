/* src/core/ns_slider_math.c -- see ns_slider_math.h. */
#include "ns_slider_math.h"

/* Windows virtual-key codes, spelled out so this file stays portable. */
#define NSK_PRIOR 0x21
#define NSK_NEXT  0x22
#define NSK_END   0x23
#define NSK_HOME  0x24
#define NSK_LEFT  0x25
#define NSK_UP    0x26
#define NSK_RIGHT 0x27
#define NSK_DOWN  0x28
#define NS_SLIDER_PAGE 6

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

int ns_slider_value_at(int x, int track_x, int track_w, int min, int max)
{
    if (max < min) max = min;
    if (track_w <= 0) return min;
    long off = (long)x - track_x;
    if (off <= 0) return min;
    if (off >= track_w) return max;
    long span = (long)max - min;
    long num = off * span;
    long v = (num * 2 + track_w) / ((long)track_w * 2);   /* round half up */
    return clampi((int)(min + v), min, max);
}

int ns_slider_x_of(int value, int track_x, int track_w, int min, int max)
{
    if (max < min) max = min;
    if (track_w <= 0 || max == min) return track_x;
    value = clampi(value, min, max);
    long span = (long)max - min;
    long num = (long)(value - min) * track_w;
    return track_x + (int)((num * 2 + span) / (span * 2));
}

int ns_slider_key(int value, unsigned vk, int min, int max)
{
    if (max < min) max = min;
    int v = value;
    switch (vk) {
    case NSK_LEFT: case NSK_DOWN:  v = value - 1; break;
    case NSK_RIGHT: case NSK_UP:   v = value + 1; break;
    case NSK_NEXT:                 v = value - NS_SLIDER_PAGE; break;
    case NSK_PRIOR:                v = value + NS_SLIDER_PAGE; break;
    case NSK_HOME:                 v = min; break;
    case NSK_END:                  v = max; break;
    default: return value;
    }
    return clampi(v, min, max);
}
