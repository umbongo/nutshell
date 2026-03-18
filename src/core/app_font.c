#include "app_font.h"
#include <stdlib.h>   /* abs() */

#ifdef _WIN32
#include <windows.h>
#include "resource.h"

static HANDLE g_ui_font_regular;
static HANDLE g_ui_font_bold;

static HANDLE load_font_resource(int id)
{
    HRSRC res = FindResource(NULL, MAKEINTRESOURCE(id), RT_RCDATA);
    if (!res) return NULL;
    HGLOBAL mem = LoadResource(NULL, res);
    if (!mem) return NULL;
    void *data = LockResource(mem);
    DWORD sz = SizeofResource(NULL, res);
    if (!data || sz == 0) return NULL;
    DWORD cnt = 0;
    return AddFontMemResourceEx(data, sz, NULL, &cnt);
}

void app_font_load_ui(void)
{
    g_ui_font_regular = load_font_resource(IDR_FONT_INTER);
    g_ui_font_bold    = load_font_resource(IDR_FONT_INTER_BOLD);
}

void app_font_free_ui(void)
{
    if (g_ui_font_regular) { RemoveFontMemResourceEx(g_ui_font_regular); g_ui_font_regular = NULL; }
    if (g_ui_font_bold)    { RemoveFontMemResourceEx(g_ui_font_bold);    g_ui_font_bold = NULL; }
}
#endif /* _WIN32 */

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

    /* If current_size is between table entries, the floor value itself
     * is already a step in the zoom-out direction.  Use it directly
     * instead of stepping past it. */
    if (k_app_font_sizes[idx] != current_size) {
        if (delta < 0)
            return k_app_font_sizes[idx];       /* snap down to floor */
        /* delta > 0: step to idx+1 (next above floor) */
        int above = idx + 1;
        if (above >= APP_FONT_NUM_SIZES)
            return current_size;
        return k_app_font_sizes[above];
    }

    int next = idx + delta;
    if (next < 0 || next >= APP_FONT_NUM_SIZES)
        return current_size;

    int new_size = k_app_font_sizes[next];
    if (new_size == current_size)
        return current_size;

    return new_size;
}
