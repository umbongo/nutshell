#include "stick_scroll.h"

int stick_scroll_after_user(int scroll_y, int max_scroll)
{
    if (max_scroll <= 0) return 1;
    return scroll_y >= max_scroll ? 1 : 0;
}

int stick_scroll_on_layout(int stick, int scroll_y, int max_scroll)
{
    int clamped_max = max_scroll > 0 ? max_scroll : 0;

    if (stick) return clamped_max;

    if (scroll_y > clamped_max) return clamped_max;
    if (scroll_y < 0) return 0;
    return scroll_y;
}

int stick_scroll_anchor(int stick, int scroll_y, int old_top, int new_top,
                        int new_h)
{
    if (stick) return scroll_y;
    int offset = scroll_y - old_top;
    int last = new_h > 0 ? new_h - 1 : 0;
    if (offset > last) offset = last;
    int y = new_top + offset;
    return y < 0 ? 0 : y;
}

int stick_scroll_pick_anchor(const int *heights, const int *redirect, int n,
                             int gap, int scroll_y, int *offset)
{
    if (!heights || !redirect || !offset || n <= 0) return -1;
    int top = gap;
    for (int i = 0; i < n; i++) {
        if (heights[i] <= 0) continue;
        if (top + heights[i] > scroll_y) {
            if (redirect[i] >= 0) {
                *offset = 0;
                return redirect[i];
            }
            *offset = scroll_y - top;
            return i;
        }
        top += heights[i] + gap;
    }
    return -1;
}

int stick_scroll_hold_row(int screen_y, int new_top, int max_scroll)
{
    int y = new_top - screen_y;
    int max = max_scroll > 0 ? max_scroll : 0;
    if (y > max) y = max;
    if (y < 0) y = 0;
    return y;
}
