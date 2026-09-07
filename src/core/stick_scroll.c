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
