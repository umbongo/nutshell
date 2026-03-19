#include "edit_scroll.h"

int edit_scroll_visible_lines(int edit_height, int line_height)
{
    if (line_height <= 0 || edit_height <= 0)
        return 0;
    return edit_height / line_height;
}

void edit_scroll_params(int total_lines, int first_visible,
                        int edit_height, int line_height,
                        int *nMin, int *nMax, int *nPage, int *nPos)
{
    int vis = edit_scroll_visible_lines(edit_height, line_height);
    *nMin  = 0;
    *nMax  = (total_lines > 1) ? total_lines - 1 : 0;
    *nPage = vis;
    *nPos  = first_visible;
}

int edit_scroll_needed(int total_lines, int edit_height, int line_height)
{
    if (total_lines <= 0) return 0;
    int vis = edit_scroll_visible_lines(edit_height, line_height);
    if (vis <= 0) return 1;  /* can't show anything -> scrollbar needed */
    return total_lines > vis ? 1 : 0;
}

int edit_scroll_line_delta(int target_pos, int current_first_visible)
{
    return target_pos - current_first_visible;
}

int edit_scroll_wheel_delta(int wheel_delta, int notch_size,
                            int lines_per_notch)
{
    if (notch_size == 0) return 0;
    /* Wheel up (positive delta) should scroll content up (negative EM_LINESCROLL).
     * Wheel down (negative delta) should scroll content down (positive). */
    return -(wheel_delta / notch_size * lines_per_notch);
}

int edit_scroll_wheel_accum(int wheel_delta, int notch_size,
                            int lines_per_notch, int *accumulator)
{
    if (notch_size == 0 || !accumulator) return 0;

    /* If direction changed, reset the accumulator */
    if ((*accumulator > 0 && wheel_delta < 0) ||
        (*accumulator < 0 && wheel_delta > 0)) {
        *accumulator = 0;
    }

    *accumulator += wheel_delta;

    /* Consume full notches from the accumulator */
    int notches = *accumulator / notch_size;
    if (notches == 0) return 0;

    *accumulator -= notches * notch_size;
    return -(notches * lines_per_notch);
}
