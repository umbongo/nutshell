#ifndef NUTSHELL_AI_HEADER_LAYOUT_H
#define NUTSHELL_AI_HEADER_LAYOUT_H

/*
 * ai_header_layout -- pure geometry for the AI panel's header row, laid out
 * right to left so nothing the user needs to read is ever painted under
 * something else: the icon-button cluster at the right edge, the model chip
 * a gap to its left, the status word (activity dot + one word) a gap left of
 * the chip, and the session name taking what is left of the row, ellipsised
 * when it does not fit. Portable, no windows.h; src/ui/ai_chat_view.c
 * measures the texts and paints into the rects this returns.
 *
 * All inputs and outputs are device pixels in the header's own coordinate
 * space (the caller scales the SP_ and SZ_ sizes through ns_scale() first).
 */

#include "ns_layout.h"

typedef struct {
    NsRect header;   /* the header row, as ai_panel_layout() tiles it */
    int pad;         /* inset from the header's left and right edges */
    int gap;         /* between neighbouring items */
    int buttons_w;   /* width of the whole button cluster; 0 = none */
    int btn_h;       /* height of a button */
    int chip_w;      /* width of the model chip; 0 = none (takes no gap) */
    int chip_h;      /* height of the chip */
    int status_w;    /* dot + gap + word; 0 = no activity, nothing to place */
    int name_w;      /* the session name's natural text width */
} AiHeaderMeasure;

typedef struct {
    NsRect buttons;      /* the cluster; w = 0 when there are none */
    NsRect chip;         /* w = 0 when there is no chip or no room for it */
    NsRect status;       /* w = 0 when idle or dropped */
    NsRect name;         /* the session label's rect (may be w = 0) */
    int    status_shown; /* 1 when status holds a placed word */
    int    name_clipped; /* 1 when name.w is narrower than the name's text */
} AiHeaderLayout;

/* Places the four items. The status word has priority over the name: while
 * name + gap + word fit, the word follows the name directly; once they do
 * not, the word is flush against the chip (one gap left of it) and the name
 * is cut to the room that remains. When even the word does not fit between
 * the left pad and the chip it is dropped (status_shown = 0, w = 0) and the
 * name gets the whole span. A chip that cannot fit is narrowed rather than
 * pushed over the buttons. No two returned rects with a non-zero size ever
 * overlap. NULL `m` or `out` is a no-op; negative widths count as 0. */
void ai_header_layout(const AiHeaderMeasure *m, AiHeaderLayout *out);

#endif
