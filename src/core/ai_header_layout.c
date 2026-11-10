#include "ai_header_layout.h"

static int nonneg(int v) { return v < 0 ? 0 : v; }

void ai_header_layout(const AiHeaderMeasure *m, AiHeaderLayout *out)
{
    if (!m || !out) return;

    int pad = nonneg(m->pad), gap = nonneg(m->gap);
    int buttons_w = nonneg(m->buttons_w), btn_h = nonneg(m->btn_h);
    int chip_w = nonneg(m->chip_w), chip_h = nonneg(m->chip_h);
    int status_w = nonneg(m->status_w), name_w = nonneg(m->name_w);

    int left = m->header.x + pad;
    int right = m->header.x + m->header.w - pad;

    /* Buttons: flush against the right pad. */
    out->buttons.x = right - buttons_w;
    out->buttons.y = m->header.y + (m->header.h - btn_h) / 2;
    out->buttons.w = buttons_w;
    out->buttons.h = btn_h;
    if (buttons_w > 0) right = out->buttons.x - gap;

    /* Chip: a gap left of the buttons, narrowed if the row is too short. */
    int room = nonneg(right - left);
    int cw = chip_w < room ? chip_w : room;
    out->chip.x = right - cw;
    out->chip.y = m->header.y + (m->header.h - chip_h) / 2;
    out->chip.w = cw;
    out->chip.h = chip_h;
    if (cw > 0) right = out->chip.x - gap;

    /* The status word and the name share what is left. */
    int avail = nonneg(right - left);

    out->status.y = m->header.y;
    out->status.h = m->header.h;
    out->status.w = 0;
    out->status.x = left;
    out->status_shown = 0;
    out->name.x = left;
    out->name.y = m->header.y;
    out->name.h = m->header.h;
    out->name_clipped = 0;

    int name_gap = name_w > 0 ? gap : 0;
    if (status_w > 0 && name_w + name_gap + status_w <= avail) {
        /* Everything fits: the word follows the name. */
        out->name.w = name_w;
        out->status.x = left + name_w + name_gap;
        out->status.w = status_w;
        out->status_shown = 1;
    } else if (status_w > 0 && status_w <= avail) {
        /* The name is too long: the word keeps its place against the chip
         * and the name is cut to the room that remains. */
        out->status.x = right - status_w;
        out->status.w = status_w;
        out->status_shown = 1;
        out->name.w = nonneg(avail - status_w - gap);
        out->name_clipped = 1;
    } else {
        /* No word, or no room for it: the name gets the whole span. */
        out->name.w = name_w < avail ? name_w : avail;
        out->name_clipped = name_w > avail;
    }
}
