#include "ai_header_layout.h"

static int nonneg(int v) { return v < 0 ? 0 : v; }

void ai_header_layout(const AiHeaderMeasure *m, AiHeaderLayout *out)
{
    if (!m || !out) return;

    int pad = nonneg(m->pad), gap = nonneg(m->gap);
    int buttons_w = nonneg(m->buttons_w), btn_h = nonneg(m->btn_h);
    int status_w = nonneg(m->status_w), name_w = nonneg(m->name_w);
    int min_name = nonneg(m->min_name_w), min_status = nonneg(m->min_status_w);

    int left = m->header.x + pad;
    int right = m->header.x + m->header.w - pad;

    /* Buttons: flush against the right pad. */
    out->buttons.x = right - buttons_w;
    out->buttons.y = m->header.y + (m->header.h - btn_h) / 2;
    out->buttons.w = buttons_w;
    out->buttons.h = btn_h;
    if (buttons_w > 0) right = out->buttons.x - gap;

    int avail = nonneg(right - left);

    out->status.y = m->header.y;
    out->status.h = m->header.h;
    out->status.w = 0;
    out->status.x = left;
    out->status_shown = 0;
    out->status_clipped = 0;
    out->name.x = left;
    out->name.y = m->header.y;
    out->name.h = m->header.h;
    out->name_clipped = 0;

    int name_gap = name_w > 0 ? gap : 0;
    if (min_name > name_w) min_name = name_w;
    if (min_name > avail) min_name = avail;

    if (status_w > 0 && name_w + name_gap + status_w <= avail) {
        /* Everything fits: the status follows the name. */
        out->name.w = name_w;
        out->status.x = left + name_w + name_gap;
        out->status.w = status_w;
        out->status_shown = 1;
        return;
    }
    if (status_w > 0) {
        /* The status takes what it needs, down to the name's minimum. */
        int slot = avail - min_name - (min_name > 0 ? gap : 0);
        int sw = status_w < slot ? status_w : slot;
        if (sw >= min_status && sw > 0) {
            out->status.w = sw;
            out->status.x = right - sw;
            out->status_shown = 1;
            out->status_clipped = sw < status_w;
            int nw = avail - sw - gap;
            if (nw < min_name) nw = min_name;   /* only when sw was clipped */
            if (nw > name_w) nw = name_w;
            out->name.w = nonneg(nw);
            out->name_clipped = out->name.w < name_w;
            return;
        }
    }
    /* No status, or no room for it: the name gets the whole span. */
    out->name.w = name_w < avail ? name_w : avail;
    out->name_clipped = name_w > avail;
}

const char *ai_header_status_pick(const char *transient, const char *voice_cue,
                                  const char *activity_word)
{
    if (transient && transient[0]) return transient;
    if (voice_cue && voice_cue[0]) return voice_cue;
    if (activity_word && activity_word[0]) return activity_word;
    return NULL;
}
