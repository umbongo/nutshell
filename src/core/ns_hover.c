#include "ns_hover.h"
#include <stddef.h>

void ns_hover_init(NsHover *h)
{
    if (!h) return;
    h->hot_id = -1;
    h->pressed_id = -1;
}

NsHoverChange ns_hover_move(NsHover *h, int hit_id)
{
    NsHoverChange c = { 0, -1, -1 };
    if (!h) return c;

    c.old_id = h->hot_id;
    c.new_id = hit_id;
    c.changed = (c.old_id != c.new_id);
    h->hot_id = hit_id;
    return c;
}

NsHoverChange ns_hover_leave(NsHover *h)
{
    return ns_hover_move(h, -1);
}

NsHoverChange ns_hover_press(NsHover *h, int hit_id)
{
    NsHoverChange c = { 0, -1, -1 };
    if (!h) return c;

    c.old_id = h->pressed_id;
    c.new_id = hit_id;
    c.changed = (c.old_id != c.new_id);
    h->pressed_id = hit_id;
    return c;
}

NsHoverChange ns_hover_release(NsHover *h)
{
    NsHoverChange c = { 0, -1, -1 };
    if (!h) return c;

    c.old_id = h->pressed_id;
    c.new_id = -1;
    c.changed = (c.old_id != -1);
    h->pressed_id = -1;
    return c;
}

int ns_hover_state_for(const NsHover *h, int id)
{
    if (!h || id < 0) return 0;
    if (h->pressed_id == id) return 2;
    if (h->hot_id == id) return 1;
    return 0;
}
