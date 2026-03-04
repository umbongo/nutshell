#ifndef CONGA_CORE_SNAP_H
#define CONGA_CORE_SNAP_H

/*
 * Pure arithmetic helpers for snapping a window to whole terminal-cell
 * multiples.  No Win32 types are used so the module is fully testable on
 * Linux.
 *
 * Edge constants match the Win32 WMSZ_* values so WM_SIZING's wParam can
 * be passed directly to snap_adjust().
 */

#define SNAP_EDGE_LEFT        1
#define SNAP_EDGE_RIGHT       2
#define SNAP_EDGE_TOP         3
#define SNAP_EDGE_TOPLEFT     4
#define SNAP_EDGE_TOPRIGHT    5
#define SNAP_EDGE_BOTTOM      6
#define SNAP_EDGE_BOTTOMLEFT  7
#define SNAP_EDGE_BOTTOMRIGHT 8

/*
 * snap_calc — compute snapped window dimensions.
 *
 *   client_w / client_h  proposed client-area size (pixels)
 *   char_w   / char_h    character cell size (pixels)
 *   nc_w     / nc_h      non-client (border + title) size (pixels)
 *   tab_height           tab-strip height (pixels)
 *
 * All output pointers may be NULL to skip that value.
 *   out_cols / out_rows  resulting cell count (clamped to >= 1)
 *   out_win_w / out_win_h snapped total window size (pixels)
 */
void snap_calc(int client_w, int client_h,
               int char_w,   int char_h,
               int nc_w,     int nc_h,
               int tab_height,
               int *out_cols, int *out_rows,
               int *out_win_w, int *out_win_h);

/*
 * snap_adjust — move the dragged edge(s) of a sizing rect to achieve the
 * snapped dimensions.
 *
 *   left/top/right/bottom  rect coordinates modified in place
 *   snapped_w / snapped_h  desired total window dimensions
 *   edge                   SNAP_EDGE_* constant (== Win32 WMSZ_*)
 *
 * Left-family edges (SNAP_EDGE_LEFT / _TOPLEFT / _BOTTOMLEFT) adjust left;
 * all others adjust right.  Top-family edges adjust top; all others adjust
 * bottom.
 */
void snap_adjust(int *left, int *top, int *right, int *bottom,
                 int snapped_w, int snapped_h, int edge);

#endif
