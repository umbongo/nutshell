#include "snap.h"

void snap_calc(int client_w, int client_h,
               int char_w,   int char_h,
               int nc_w,     int nc_h,
               int tab_height,
               int *out_cols, int *out_rows,
               int *out_win_w, int *out_win_h)
{
    /* Terminal area height: clamp to at least one full cell. */
    int term_h = client_h - tab_height;
    if (term_h < char_h) term_h = char_h;

    int cols = client_w / char_w;
    int rows = term_h   / char_h;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    if (out_cols)  *out_cols  = cols;
    if (out_rows)  *out_rows  = rows;
    if (out_win_w) *out_win_w = cols * char_w + nc_w;
    if (out_win_h) *out_win_h = rows * char_h + tab_height + nc_h;
}

void snap_adjust(int *left, int *top, int *right, int *bottom,
                 int snapped_w, int snapped_h, int edge)
{
    /* Horizontal: left-family fixes the right edge; all others fix the left. */
    switch (edge) {
        case SNAP_EDGE_LEFT:
        case SNAP_EDGE_TOPLEFT:
        case SNAP_EDGE_BOTTOMLEFT:
            *left = *right - snapped_w;
            break;
        default:
            *right = *left + snapped_w;
            break;
    }

    /* Vertical: top-family fixes the bottom edge; all others fix the top. */
    switch (edge) {
        case SNAP_EDGE_TOP:
        case SNAP_EDGE_TOPLEFT:
        case SNAP_EDGE_TOPRIGHT:
            *top = *bottom - snapped_h;
            break;
        default:
            *bottom = *top + snapped_h;
            break;
    }
}
