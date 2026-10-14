#include "paint_cover.h"

int paint_covers_rect(long paint_left, long paint_top,
                      long paint_right, long paint_bottom,
                      long rect_left, long rect_top,
                      long rect_right, long rect_bottom)
{
    /* An empty or inverted rectangle has no pixels to miss: treat it as
     * covered so a row is never pinned dirty by a degenerate geometry
     * (cols or charHeight of zero, which breaks rendering anyway). */
    if (rect_right <= rect_left || rect_bottom <= rect_top) {
        return 1;
    }
    return (paint_left <= rect_left) && (paint_right >= rect_right) &&
           (paint_top <= rect_top) && (paint_bottom >= rect_bottom);
}
