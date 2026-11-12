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
