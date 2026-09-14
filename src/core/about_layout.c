#include "about_layout.h"
#include "ns_scale.h"
#include "ns_type.h"

static int clamp0(int v) { return v < 0 ? 0 : v; }

void about_layout_compute(AboutLayout *out, int dpi, int icon_px,
                          int title_h, int tagline_h, int copyright_h)
{
    if (!out) return;

    icon_px     = clamp0(icon_px);
    title_h     = clamp0(title_h);
    tagline_h   = clamp0(tagline_h);
    copyright_h = clamp0(copyright_h);

    const int margin_top    = ns_scale(SP_XL, dpi); /* above the icon */
    const int gap_icon      = ns_scale(SP_LG, dpi); /* icon -> title */
    const int gap_title     = ns_scale(SP_SM, dpi); /* title -> tagline */
    const int gap_tagline   = ns_scale(SP_XS, dpi); /* tagline -> copyright */
    const int gap_button    = ns_scale(SP_XL, dpi); /* text block -> button */
    const int margin_bottom = ns_scale(SP_LG, dpi); /* below the button */

    const int cw = ns_scale(ABOUT_CLIENT_W_96, dpi);
    out->client_w = cw;

    int y = margin_top;

    out->icon.x = (cw - icon_px) / 2;
    out->icon.y = y;
    out->icon.w = icon_px;
    out->icon.h = icon_px;
    y += icon_px + gap_icon;

    out->title.x = 0;
    out->title.y = y;
    out->title.w = cw;
    out->title.h = title_h;
    y += title_h + gap_title;

    out->tagline.x = 0;
    out->tagline.y = y;
    out->tagline.w = cw;
    out->tagline.h = tagline_h;
    y += tagline_h + gap_tagline;

    out->copyright.x = 0;
    out->copyright.y = y;
    out->copyright.w = cw;
    out->copyright.h = copyright_h;
    y += copyright_h + gap_button;

    out->button.w = ns_scale(SZ_BTN_MIN_W, dpi);
    out->button.h = ns_scale(SZ_CTRL_H, dpi);
    out->button.x = (cw - out->button.w) / 2;
    out->button.y = y;

    out->client_h = out->button.y + out->button.h + margin_bottom;
}
