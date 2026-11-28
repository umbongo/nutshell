#include "settings_tab.h"
#include <stddef.h>

/* Does a sort before b? Group, then row (y), then column (x); ties keep
 * creation order because the sort below is stable. */
static int before(const SettingsTabItem *a, const SettingsTabItem *b)
{
    if (a->group != b->group) return a->group < b->group;
    if (a->y != b->y) return a->y < b->y;
    return a->x < b->x;
}

int settings_tab_order(const SettingsTabItem *items, int n, int *out)
{
    if (!items || !out || n <= 0) return 0;
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (!items[i].enabled || !items[i].visible) continue;
        /* Stable insertion: shift later-sorting entries right. */
        int j = count;
        while (j > 0 && before(&items[i], &items[out[j - 1]])) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = i;
        count++;
    }
    return count;
}

int settings_tab_step(const int *order, int count, int current, int dir)
{
    if (!order || count <= 0) return -1;
    int pos = -1;
    for (int i = 0; i < count; i++) {
        if (order[i] == current) { pos = i; break; }
    }
    if (pos < 0) return (dir >= 0) ? order[0] : order[count - 1];
    if (dir >= 0) return order[(pos + 1) % count];
    return order[(pos + count - 1) % count];
}
