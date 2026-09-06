#include "ns_type.h"

/* ---- Type ramp -------------------------------------------------------------- */

static const NsFontSpec NS_RAMP[FONT_ROLE_COUNT] = {
    /* size_pt, weight, line_height, is_mono */
    { 9,  400, 1.3,  0 }, /* FONT_CAPTION */
    { 10, 400, 1.4,  0 }, /* FONT_BODY */
    { 11, 600, 1.3,  0 }, /* FONT_TITLE */
    { 14, 600, 1.25, 0 }, /* FONT_HEADING */
    { 0,  400, 1.0,  1 }, /* FONT_MONO (size_pt supplied by caller) */
};

const NsFontSpec *ns_type_font(NsFontRole role)
{
    if (role < 0 || role >= FONT_ROLE_COUNT) role = FONT_BODY;
    return &NS_RAMP[role];
}

int ns_type_font_px(NsFontRole role, int dpi, int mono_size_pt)
{
    if (dpi <= 0) dpi = 96;

    const NsFontSpec *f = ns_type_font(role);
    int pt = f->is_mono ? mono_size_pt : f->size_pt;
    if (pt < 1) pt = 1; /* never hand CreateFont a non-positive height */

    long long px = ((long long)pt * dpi + 36) / 72; /* points -> pixels, round to nearest */
    if (px < 1) px = 1;

    return (int)px;
}

/* ---- Font cache slot key ---------------------------------------------------- */

static const int NS_DPI_TABLE[NS_DPI_TABLE_COUNT] = { 96, 120, 144, 168, 192, 216, 240, 288 };

static int ns_dpi_nearest_index(int dpi)
{
    if (dpi <= 0) dpi = 96;

    int best_index = 0;
    int best_diff = dpi - NS_DPI_TABLE[0];
    if (best_diff < 0) best_diff = -best_diff;

    for (int i = 1; i < NS_DPI_TABLE_COUNT; i++) {
        int diff = dpi - NS_DPI_TABLE[i];
        if (diff < 0) diff = -diff;
        if (diff < best_diff) { /* strict: ties keep the earlier (lower) entry */
            best_diff = diff;
            best_index = i;
        }
    }
    return best_index;
}

int ns_type_font_slot(NsFontRole role, int dpi, int face_id)
{
    int role_index = (int)role;
    if (role_index < 0 || role_index >= FONT_ROLE_COUNT) role_index = (int)FONT_BODY;
    if (face_id < 0) face_id = 0;
    if (face_id >= NS_FACE_COUNT) face_id = NS_FACE_COUNT - 1;

    int dpi_index = ns_dpi_nearest_index(dpi);

    return role_index * (NS_DPI_TABLE_COUNT * NS_FACE_COUNT) + dpi_index * NS_FACE_COUNT + face_id;
}

int ns_type_font_face_id(NsFontRole role)
{
    return (role == FONT_MONO) ? 2 : 0;
}

/* ---- Pill radius ------------------------------------------------------------ */

int ns_type_pill(int height)
{
    return height / 2;
}
