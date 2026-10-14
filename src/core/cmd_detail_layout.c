#include "cmd_detail_layout.h"
#include "ns_scale.h"
#include "ns_type.h"

static int clamp0(int v) { return v < 0 ? 0 : v; }

static NsRect make_rect(int x, int y, int w, int h)
{
    NsRect r;
    r.x = clamp0(x);
    r.y = clamp0(y);
    r.w = clamp0(w);
    r.h = clamp0(h);
    return r;
}

void cmd_detail_layout(int client_w, int client_h, int dpi, int note_h,
                       int tag_w, CmdDetailLayout *out)
{
    if (!out) return;

    client_w = clamp0(client_w);
    client_h = clamp0(client_h);
    note_h   = clamp0(note_h);
    tag_w    = clamp0(tag_w);

    const int margin  = ns_scale(SP_LG, dpi);
    const int gap     = ns_scale(SP_SM, dpi);
    const int gap_xs  = ns_scale(SP_XS, dpi);
    const int label_h = ns_scale(SZ_ICON, dpi);
    const int tag_h   = ns_scale(SZ_TAG_H, dpi);
    const int btn_w   = ns_scale(SZ_BTN_MIN_W, dpi);
    const int btn_h   = ns_scale(SZ_CTRL_H, dpi);

    const int content_w = clamp0(client_w - 2 * margin);
    if (tag_w > content_w) tag_w = content_w;

    /* Everything above the command box other than the note, plus the gap
     * and the button row below it; what is left is the most height the
     * note can take before the command box would go negative. */
    const int fixed_h = margin + label_h + gap_xs + gap + tag_h + gap
                      + gap + btn_h + margin;
    const int note_max = clamp0(client_h - fixed_h);
    if (note_h > note_max) note_h = note_max;

    int y = margin;
    out->label = make_rect(margin, y, content_w, label_h);
    y += label_h + gap_xs;

    out->note = make_rect(margin, y, content_w, note_h);
    y += note_h + gap;

    out->tag = make_rect(margin, y, tag_w, tag_h);
    y += tag_h + gap;

    const int btn_y = client_h - margin - btn_h;
    out->command = make_rect(margin, y, content_w, btn_y - gap - y);

    out->close_btn = make_rect(client_w - margin - btn_w, btn_y, btn_w, btn_h);
    out->copy_btn  = make_rect(out->close_btn.x - gap - btn_w, btn_y,
                               btn_w, btn_h);
}
