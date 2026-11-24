/* src/core/ns_slider_math.h -- pure maths of the themed slider (ns_slider). */
#ifndef NUTSHELL_NS_SLIDER_MATH_H
#define NUTSHELL_NS_SLIDER_MATH_H

/* Value under client x on a track starting at track_x, track_w wide;
 * rounded to the nearest integer and clamped to [min, max]. A track with no
 * width gives min. */
int ns_slider_value_at(int x, int track_x, int track_w, int min, int max);

/* Client x of `value` on that track (value clamped first); inverse of
 * ns_slider_value_at at the integer steps. */
int ns_slider_x_of(int value, int track_x, int track_w, int min, int max);

/* The value after virtual key `vk`: Left/Down -1, Right/Up +1, PageDown -6,
 * PageUp +6, Home min, End max; any other key leaves it. Clamped. */
int ns_slider_key(int value, unsigned vk, int min, int max);

#endif
