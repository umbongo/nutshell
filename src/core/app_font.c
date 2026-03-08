#include "app_font.h"
#include <stdlib.h>   /* abs() */

const int k_app_font_sizes[APP_FONT_NUM_SIZES] = { 6, 8, 10, 12, 14, 16, 18, 20 };

int app_font_snap_size(int raw_size)
{
    int best = k_app_font_sizes[0];
    int best_dist = abs(raw_size - best);
    for (int i = 1; i < APP_FONT_NUM_SIZES; i++) {
        int dist = abs(raw_size - k_app_font_sizes[i]);
        if (dist < best_dist) {
            best = k_app_font_sizes[i];
            best_dist = dist;
        }
    }
    return best;
}

int app_font_zoom(int current_size, int delta)
{
    /* Find the index of the largest allowed size <= current_size. */
    int idx = 0;
    for (int i = 0; i < APP_FONT_NUM_SIZES; i++) {
        if (k_app_font_sizes[i] <= current_size)
            idx = i;
    }

    int next = idx + delta;
    if (next < 0 || next >= APP_FONT_NUM_SIZES)
        return current_size;

    int new_size = k_app_font_sizes[next];
    if (new_size == current_size)
        return current_size;

    return new_size;
}
