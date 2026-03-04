#include "zoom.h"

int zoom_font_fits(int client_w, int term_h, int char_w, int char_h)
{
    if (char_w <= 0 || char_h <= 0) return 0;
    return (client_w % char_w == 0) && (term_h % char_h == 0);
}
