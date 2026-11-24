#ifndef NUTSHELL_AI_HEADER_LAYOUT_H
#define NUTSHELL_AI_HEADER_LAYOUT_H

/*
 * ai_header_layout -- pure geometry for the AI panel's header row, laid out
 * so nothing the user needs to read is ever painted under something else:
 * the icon-button cluster at the right edge, the session name at the left,
 * and between them a status slot (activity dot and/or text) that follows the
 * name and takes the free width up to the buttons minus a gap. The name
 * keeps at least `min_name_w` before the status is narrowed; the status is
 * dropped only when its slot would be under `min_status_w`. Portable, no
 * windows.h; src/ui/ai_chat_view.c measures the texts and paints into the
 * rects this returns.
 *
 * All inputs and outputs are device pixels in the header's own coordinate
 * space (the caller scales the SP_ and SZ_ sizes through ns_scale() first).
 */

#include "ns_layout.h"

typedef struct {
    NsRect header;     /* the header row, as ai_panel_layout() tiles it */
    int pad;           /* inset from the header's left and right edges */
    int gap;           /* between neighbouring items */
    int buttons_w;     /* width of the whole button cluster; 0 = none */
    int btn_h;         /* height of a button */
    int status_w;      /* natural width of the status (dot + gap + text); 0 = nothing */
    int name_w;        /* the session name's natural text width */
    int min_name_w;    /* the least the name keeps before the status narrows */
    int min_status_w;  /* a status slot narrower than this is dropped */
} AiHeaderMeasure;

typedef struct {
    NsRect buttons;        /* the cluster; w = 0 when there are none */
    NsRect status;         /* w = 0 when nothing to show or dropped */
    NsRect name;           /* the session label's rect (may be w = 0) */
    int    status_shown;   /* 1 when status holds a placed slot */
    int    status_clipped; /* 1 when status.w is narrower than status_w */
    int    name_clipped;   /* 1 when name.w is narrower than the name's text */
} AiHeaderLayout;

/* Places the three items. While name + gap + status fit, the status follows
 * the name directly. Otherwise the status keeps what it needs (the name is
 * cut to the rest) until the name would fall under min(min_name_w, name_w);
 * from there the status is narrowed (status_clipped = 1). If that slot is
 * under min_status_w the status is dropped and the name gets the whole span.
 * No two returned rects with a non-zero size ever overlap. NULL `m` or `out`
 * is a no-op; negative widths count as 0. */
void ai_header_layout(const AiHeaderMeasure *m, AiHeaderLayout *out);

/* The text the header shows: the first non-empty of `transient` (a status
 * note), `voice_cue` (while recording or transcribing) and `activity_word`;
 * NULL when all are empty or NULL. */
const char *ai_header_status_pick(const char *transient, const char *voice_cue,
                                  const char *activity_word);

#endif
