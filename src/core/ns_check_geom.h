/* ns_check_geom.h -- geometry of the themed checkbox (src/ui/ns_check.c).
 *
 * Portable (ns_scale.h and ns_type.h only) so the native tests can check the
 * box, the label area and the check mark at every DPI. The Win32 painter
 * draws from the same structs. */
#ifndef NS_CHECK_GEOM_H
#define NS_CHECK_GEOM_H

typedef struct {
    int box_x, box_y, box_w, box_h;     /* the square, client coordinates */
    int label_x, label_y, label_w, label_h;  /* where the label text goes */
    int radius;                         /* box corner radius, pixels */
} NsCheckLayout;

/* Lays the box and label out in a control of ctl_w x ctl_h pixels: a square
 * of ns_scale(SZ_CHECK, dpi) (shrunk to ctl_h, or ctl_w, when the control is
 * smaller), flush left and centred vertically; the label starts an SP_SM gap
 * after it and fills the rest of the width and the full height. A control with
 * no area gives an all-zero layout. dpi <= 0 is treated as 96. */
void ns_check_layout(int ctl_w, int ctl_h, int dpi, NsCheckLayout *out);

/* The three vertices of the check mark (short stroke down, long stroke up) as
 * x,y pairs in client coordinates, for a box at (bx,by) of side bw. */
void ns_check_mark(int bx, int by, int bw, float xy[6]);

/* Pen width of the check mark in pixels: ns_scale(2, dpi), never below 2. */
int ns_check_mark_width(int dpi);

#endif
