/* notice_layout.c -- see notice_layout.h. */

#include "notice_layout.h"
#include "ns_scale.h"
#include "ns_type.h"
#include <string.h>

#define NOTICE_CLIENT_W 460
#define NOTICE_BAR_W    4
#define NOTICE_GLYPH    16
#define NOTICE_ACTION_W (SZ_BTN_MIN_W + SP_XXL + SP_XL)

static NsRect rect(int x, int y, int w, int h)
{
    NsRect r;
    r.x = x; r.y = y; r.w = w; r.h = h;
    return r;
}

void notice_layout(const NoticeLayoutIn *in, NoticeLayout *out)
{
    int dpi = in->dpi;
    int pad = ns_scale(SP_LG, dpi);
    int gap = ns_scale(SP_MD, dpi);
    int small = ns_scale(SP_SM, dpi);
    int row_gap = ns_scale(SP_XS, dpi);
    int ctlh = ns_scale(SZ_CTRL_H, dpi);   /* buttons */
    int bar_w = ns_scale(NOTICE_BAR_W, dpi);
    int glyph = ns_scale(NOTICE_GLYPH, dpi);
    int n = in->item_count;
    if (n < 0) n = 0;
    if (n > NOTICE_ITEMS_MAX) n = NOTICE_ITEMS_MAX;

    memset(out, 0, sizeof(*out));
    out->client_w = ns_scale(NOTICE_CLIENT_W, dpi);
    int x0 = bar_w + pad;
    int right = out->client_w - pad;
    out->text_w = right - x0;

    int y = pad;
    int title_row = in->title_h > glyph ? in->title_h : glyph;
    out->icon = rect(x0, y + (title_row - glyph) / 2, glyph, glyph);
    int tx = x0 + glyph + small;
    out->title = rect(tx, y, right - tx, title_row);
    y += title_row + gap;

    out->opening = rect(x0, y, out->text_w, in->opening_h);
    y += in->opening_h;

    if (n > 0) {
        y += gap;
        int vx = x0 + in->label_w + small;
        for (int i = 0; i < n; i++) {
            int h = in->value_h[i] > 0 ? in->value_h[i] : 1;
            out->label[i] = rect(x0, y, in->label_w, h);
            int vw = right - vx;
            if (vw < 0) vw = 0;   /* a label wider than the column */
            out->value[i] = rect(vx, y, vw, h);
            y += h;
            if (i + 1 < n) y += row_gap;
        }
    }

    if (in->closing_h > 0) {
        y += gap;
        out->closing = rect(x0, y, out->text_w, in->closing_h);
        y += in->closing_h;
    }

    y += pad;
    int bw = ns_scale(SZ_BTN_MIN_W, dpi);
    out->ok = rect(right - bw, y, bw, ctlh);
    if (in->has_action) {
        int aw = ns_scale(NOTICE_ACTION_W, dpi);
        out->action = rect(out->ok.x - small - aw, y, aw, ctlh);
    }
    out->client_h = y + ctlh + pad;
    out->bar = rect(0, 0, bar_w, out->client_h);
}
