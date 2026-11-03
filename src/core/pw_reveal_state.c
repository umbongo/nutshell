/* src/core/pw_reveal_state.c -- see pw_reveal_state.h. */
#include "pw_reveal_state.h"

#include <stddef.h>

void pw_reveal_init(PwRevealState *s)
{
    if (!s) return;
    s->pressed = 0;
    s->inside = 0;
    s->revealed = 0;
}

int pw_reveal_feed(PwRevealState *s, PwRevealEvent ev, int field_has_text)
{
    if (!s) return 0;
    int before = s->revealed;
    switch (ev) {
    case PWR_PRESS:
        s->pressed = 1;
        s->inside = 1;
        s->revealed = field_has_text ? 1 : 0;
        break;
    case PWR_RELEASE:
    case PWR_CAPTURE_LOST:
    case PWR_DEACTIVATE:
    case PWR_DESTROY:
        s->pressed = 0;
        s->revealed = 0;
        break;
    case PWR_LEAVE:
        s->inside = 0;
        s->revealed = 0;
        break;
    case PWR_ENTER:
        s->inside = 1;
        s->revealed = (s->pressed && field_has_text) ? 1 : 0;
        break;
    case PWR_EMPTIED:
        s->revealed = 0;
        break;
    }
    return s->revealed != before;
}
