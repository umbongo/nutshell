#ifndef NUTSHELL_EDIT_SCROLL_H
#define NUTSHELL_EDIT_SCROLL_H

/*
 * Portable helpers for syncing a multiline EDIT control with a custom
 * scrollbar.  All functions are pure arithmetic — no Win32 calls — so
 * they can be unit-tested on any platform.
 */

/* Number of fully visible text lines in an edit control.
 * Returns 0 if line_height is zero. */
int edit_scroll_visible_lines(int edit_height, int line_height);

/* Compute custom-scrollbar parameters from edit control state.
 *   total_lines   — EM_GETLINECOUNT result
 *   first_visible — EM_GETFIRSTVISIBLELINE result
 *   edit_height   — client height in px
 *   line_height   — single-line height in px
 * Writes nMin, nMax, nPage, nPos suitable for csb_set_range / csb_set_pos. */
void edit_scroll_params(int total_lines, int first_visible,
                        int edit_height, int line_height,
                        int *nMin, int *nMax, int *nPage, int *nPos);

/* Delta to pass to EM_LINESCROLL to scroll from current position to target.
 * Positive = scroll down, negative = scroll up. */
int edit_scroll_line_delta(int target_pos, int current_first_visible);

/* Compute the EM_LINESCROLL delta for a mouse wheel event.
 * wheel_delta: raw WM_MOUSEWHEEL delta (positive = scroll up).
 * notch_size:  WHEEL_DELTA constant (typically 120).
 * lines_per_notch: lines to scroll per wheel notch (typically 3).
 * Returns negative to scroll up, positive to scroll down. */
int edit_scroll_wheel_delta(int wheel_delta, int notch_size,
                            int lines_per_notch);

/* Accumulator-based wheel delta for high-precision mice/trackpads.
 * Collects sub-notch deltas across events and only returns a scroll
 * value once a full notch threshold is crossed.  Remainder is kept
 * in *accumulator for the next call.
 * Returns 0 when accumulated delta has not yet reached a full notch. */
int edit_scroll_wheel_accum(int wheel_delta, int notch_size,
                            int lines_per_notch, int *accumulator);

#endif /* NUTSHELL_EDIT_SCROLL_H */
