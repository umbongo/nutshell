#ifndef NUTSHELL_SETTINGS_TAB_H
#define NUTSHELL_SETTINGS_TAB_H

/*
 * settings_tab -- the Tab order of the Settings dialog, as portable
 * (Win32-free) logic. The dialog describes each control that could take
 * focus (its group, where it sits, whether it is enabled and shown); this
 * module says which ones Tab visits and in what order:
 *   - the category list first, then the controls of the open page, then the
 *     OK and Cancel buttons;
 *   - within the page, top to bottom, left to right (a label column before
 *     its control follows from x);
 *   - disabled and hidden controls are skipped;
 *   - ties keep creation order.
 */

enum {
    STAB_GROUP_NAV = 0,     /* the category list      */
    STAB_GROUP_PAGE = 1,    /* the open page's controls */
    STAB_GROUP_BUTTONS = 2  /* OK / Cancel            */
};

typedef struct {
    int group;     /* STAB_GROUP_*                               */
    int x, y;      /* top-left, in one common coordinate space   */
    int enabled;   /* non-zero = can take focus                  */
    int visible;   /* non-zero = shown                           */
} SettingsTabItem;

/* Write the indexes of the items Tab visits, in order, to out[] (room for n)
 * and return how many. 0 for NULL or empty input. */
int settings_tab_order(const SettingsTabItem *items, int n, int *out);

/* The item after (dir > 0) or before (dir < 0) `current` in order[0..count),
 * wrapping at the ends. A `current` that is not in the order enters at the
 * first (dir > 0) or last (dir < 0) item. -1 when the order is empty. */
int settings_tab_step(const int *order, int count, int current, int dir);

#endif
