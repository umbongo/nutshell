/* ns_check_geom.c -- see ns_check_geom.h. */
#include "ns_check_geom.h"
#include "ns_scale.h"
#include "ns_type.h"
#include <string.h>

void ns_check_layout(int ctl_w, int ctl_h, int dpi, NsCheckLayout *out)
{
    if (!out) return;
    memset(out, 0, sizeof *out);
    if (ctl_w <= 0 || ctl_h <= 0) return;

    int margin = ns_scale(SP_XS, dpi);   /* room for the focus ring */
    if (margin * 2 + 1 > ctl_w) margin = 0;
    int side = ns_scale(SZ_CHECK, dpi);
    if (side > ctl_h) side = ctl_h;
    if (side > ctl_w - margin) side = ctl_w - margin;

    out->box_x = margin;
    out->box_y = (ctl_h - side) / 2;
    out->box_w = side;
    out->box_h = side;
    out->radius = ns_scale(R_CHECK, dpi);
    if (out->radius * 2 > side) out->radius = side / 2;

    int lx = out->box_x + side + ns_scale(SP_SM, dpi);
    if (lx > ctl_w) lx = ctl_w;
    out->label_x = lx;
    out->label_y = 0;
    out->label_w = ctl_w - lx;
    out->label_h = ctl_h;
}

void ns_check_mark(int bx, int by, int bw, float xy[6])
{
    float w = (float)bw;
    xy[0] = (float)bx + 0.27f * w;  xy[1] = (float)by + 0.52f * w;
    xy[2] = (float)bx + 0.43f * w;  xy[3] = (float)by + 0.68f * w;
    xy[4] = (float)bx + 0.74f * w;  xy[5] = (float)by + 0.34f * w;
}

int ns_check_mark_width(int dpi)
{
    int w = ns_scale(2, dpi);
    return w < 2 ? 2 : w;
}
