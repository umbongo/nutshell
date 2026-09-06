#include "ns_layout.h"
#include "ns_scale.h"
#include "ns_type.h"
#include <string.h>

/* Fixed tag-chip width: wide enough for "CRITICAL" in a caption-size font
 * at 96 DPI, scaled like every other size in this file. Not one of the
 * named grid tokens (it isn't reused elsewhere), but kept a multiple of 4
 * to stay on the same grid. */
#define NS_TAG_W 64

static int point_in(NsRect r, int x, int y)
{
    return r.w > 0 && r.h > 0 &&
           x >= r.x && x < r.x + r.w &&
           y >= r.y && y < r.y + r.h;
}

void ns_button_layout(NsRect r, int has_icon, int dpi, NsButtonLayout *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    int pad = ns_scale(SP_SM, dpi);

    if (has_icon) {
        int icon_sz = ns_scale(SZ_ICON, dpi);
        out->icon.x = r.x + pad;
        out->icon.y = r.y + (r.h - icon_sz) / 2;
        out->icon.w = icon_sz;
        out->icon.h = icon_sz;

        out->label.x = out->icon.x + icon_sz + pad;
        out->label.y = r.y;
        out->label.w = (r.x + r.w) - out->label.x - pad;
        out->label.h = r.h;
    } else {
        out->label.x = r.x + pad;
        out->label.y = r.y;
        out->label.w = r.w - 2 * pad;
        out->label.h = r.h;
    }
    if (out->label.w < 0) out->label.w = 0;
    if (out->label.h < 0) out->label.h = 0;
}

void ns_card_layout(NsRect r, int dpi, NsCardLayout *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    int pad = ns_scale(SP_MD, dpi);
    out->inset.x = r.x + pad;
    out->inset.y = r.y + pad;
    out->inset.w = r.w - 2 * pad;
    out->inset.h = r.h - 2 * pad;
    if (out->inset.w < 0) out->inset.w = 0;
    if (out->inset.h < 0) out->inset.h = 0;

    int header_h = ns_scale(SZ_CTRL_H, dpi);
    if (header_h > out->inset.h) header_h = out->inset.h;

    out->header.x = out->inset.x;
    out->header.y = out->inset.y;
    out->header.w = out->inset.w;
    out->header.h = header_h;

    int gap = ns_scale(SP_SM, dpi);
    out->body.x = out->inset.x;
    out->body.y = out->inset.y + header_h + gap;
    out->body.w = out->inset.w;

    int inset_bottom = out->inset.y + out->inset.h;
    if (out->body.y > inset_bottom) out->body.y = inset_bottom;
    out->body.h = inset_bottom - out->body.y;
    if (out->body.h < 0) out->body.h = 0;
}

void approval_card_layout(NsRect r, int n, const int *cmd_text_w, int text_h,
                          int dpi, ApprovalCardLayout *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (n <= 0) return;
    if (n > APPROVAL_MAX_CMDS) n = APPROVAL_MAX_CMDS;

    int pad     = ns_scale(SP_MD, dpi);
    int gap_sm  = ns_scale(SP_SM, dpi);
    int tag_h   = ns_scale(SZ_TAG_H, dpi);
    int tag_w   = ns_scale(NS_TAG_W, dpi);
    int ctrl_h  = ns_scale(SZ_CTRL_H, dpi);
    int btn_w   = ns_scale(SZ_BTN_MIN_W, dpi);
    int chk_sz  = ns_scale(SZ_ICON, dpi);

    int row_h = ctrl_h;
    int min_row_h = text_h + 2 * ns_scale(SP_XS, dpi);
    if (min_row_h > row_h) row_h = min_row_h;

    NsRect area;
    area.x = r.x + pad;
    area.y = r.y + pad;
    area.w = r.w - 2 * pad;
    area.h = r.h - 2 * pad;
    if (area.w < 0) area.w = 0;
    if (area.h < 0) area.h = 0;

    int visible_n = n;
    out->scrollable = (n > APPROVAL_VISIBLE_MAX) ? 1 : 0;
    if (out->scrollable) visible_n = APPROVAL_VISIBLE_MAX;

    out->n_rows = n;
    out->first_visible = 0;

    int viewport_h = visible_n * row_h;

    out->viewport.x = area.x;
    out->viewport.y = area.y;
    out->viewport.w = area.w;
    out->viewport.h = viewport_h;

    for (int i = 0; i < n; i++) {
        ApprovalRowLayout *row = &out->rows[i];
        if (i >= visible_n) {
            memset(row, 0, sizeof(*row));
            continue;
        }

        int row_top = area.y + i * row_h;

        row->tag.x = area.x;
        row->tag.y = row_top + (row_h - tag_h) / 2;
        row->tag.w = tag_w;
        row->tag.h = tag_h;

        row->deny.w = btn_w;
        row->deny.h = ctrl_h;
        row->deny.y = row_top + (row_h - ctrl_h) / 2;
        row->deny.x = area.x + area.w - btn_w;

        row->allow.w = btn_w;
        row->allow.h = ctrl_h;
        row->allow.y = row->deny.y;
        row->allow.x = row->deny.x - gap_sm - btn_w;

        row->checkbox.w = chk_sz;
        row->checkbox.h = chk_sz;
        row->checkbox.y = row_top + (row_h - chk_sz) / 2;
        row->checkbox.x = row->allow.x - gap_sm - chk_sz;

        row->text.x = row->tag.x + tag_w + gap_sm;
        row->text.y = row_top;
        row->text.w = row->checkbox.x - gap_sm - row->text.x;
        row->text.h = row_h;
        if (row->text.w < 0) row->text.w = 0;

        row->ellipsis = (cmd_text_w && cmd_text_w[i] > row->text.w) ? 1 : 0;
    }

    int actions_y = area.y + viewport_h + gap_sm;

    out->cancel.w = btn_w;
    out->cancel.h = ctrl_h;
    out->cancel.y = actions_y;
    out->cancel.x = area.x + area.w - btn_w;

    out->allow_all.w = btn_w;
    out->allow_all.h = ctrl_h;
    out->allow_all.y = actions_y;
    out->allow_all.x = out->cancel.x - gap_sm - btn_w;
}

int approval_card_hit(const ApprovalCardLayout *l, int x, int y, int *row_out)
{
    if (row_out) *row_out = -1;
    if (!l) return HIT_NONE;

    if (point_in(l->allow_all, x, y)) return HIT_ALLOW_ALL;
    if (point_in(l->cancel, x, y)) return HIT_CANCEL;

    int n = l->n_rows;
    if (n > APPROVAL_MAX_CMDS) n = APPROVAL_MAX_CMDS;

    for (int i = 0; i < n; i++) {
        const ApprovalRowLayout *row = &l->rows[i];
        if (point_in(row->allow, x, y)) { if (row_out) *row_out = i; return HIT_ALLOW; }
        if (point_in(row->deny, x, y)) { if (row_out) *row_out = i; return HIT_DENY; }
        if (point_in(row->checkbox, x, y)) { if (row_out) *row_out = i; return HIT_CHECKBOX; }
        if (point_in(row->tag, x, y)) { if (row_out) *row_out = i; return HIT_TAG; }
        if (point_in(row->text, x, y)) { if (row_out) *row_out = i; return HIT_TEXT; }
    }
    return HIT_NONE;
}
