#ifndef NUTSHELL_TAB_MANAGER_H
#define NUTSHELL_TAB_MANAGER_H

/* Pure, portable tab-manager logic (no Win32 dependency).
 * Provides the data model behind the Win32 tab-strip widget in tabs.c,
 * and is the layer exercised by the native-Linux unit tests. */

#define TABS_MAX 32

typedef enum {
    TAB_IDLE = 0,
    TAB_CONNECTING,
    TAB_CONNECTED,
    TAB_DISCONNECTED
} TabStatus;

typedef struct {
    char               title[64];
    void              *user_data;
    TabStatus          status;
    int                id;            /* monotonically increasing unique id */
    /* Connection info used by tooltips */
    char               username[64];
    char               host[128];
    unsigned long long connect_ms;    /* millisecond timestamp at connect; 0 = not set */
    int                logging;       /* 1 = session logging active */
} TabEntry;

typedef struct {
    TabEntry tabs[TABS_MAX];
    int      count;
    int      active_index;  /* -1 when no tabs */
    int      next_id;       /* next id to assign */
} TabManager;

void      tabmgr_init        (TabManager *m);
/* Returns new tab index, or -1 if at capacity (TABS_MAX). */
int       tabmgr_add         (TabManager *m, const char *title, void *user_data);
/* Remove the tab at index.  active_index keeps pointing at the same tab when
 * a different one is closed (it shifts down when a tab to its left goes).
 * Returns 1 when the closed tab *was* the active one and another tab has
 * taken its place — the caller must then treat that survivor as newly
 * selected (the Win32 strip fires on_select so window.c reattaches its
 * active session).  Returns 0 otherwise: a non-active tab was closed, no tab
 * remains, or index was invalid. */
int       tabmgr_remove      (TabManager *m, int index);
void      tabmgr_set_active  (TabManager *m, int index);
int       tabmgr_get_active  (const TabManager *m);
void     *tabmgr_get_user_data(const TabManager *m, int index);
void      tabmgr_set_status  (TabManager *m, int index, TabStatus status);
TabStatus tabmgr_get_status  (const TabManager *m, int index);
int       tabmgr_count       (const TabManager *m);
int       tabmgr_get_id      (const TabManager *m, int index);
/* Find by user_data pointer; returns index or -1. */
int       tabmgr_find        (const TabManager *m, const void *user_data);
/* Navigate to the next/prev tab.  delta = -1 (left) or +1 (right).
 * Wraps around.  Returns new active_index, or -1 if no tabs. */
int       tabmgr_navigate     (TabManager *m, int delta);
/* Store connection info (username, host, connect timestamp) in a tab entry. */
void      tabmgr_set_connect_info(TabManager *m, int index,
                                   const char *username, const char *host,
                                   unsigned long long connect_ms);
void      tabmgr_set_logging   (TabManager *m, int index, int logging);
int       tabmgr_get_logging   (const TabManager *m, int index);

/* Where the tab strip's fixed buttons sit, in client x, at `dpi`. The one
 * geometry that layout, hit-testing, hover, paint, click and the tooltip all
 * read. add_x is the [+] button; left_x and right_x the arrows; ai_x the AI
 * button, or -1 when ai_visible is 0, in which case its slot is reclaimed
 * and the arrows sit one slot further right. */
typedef struct {
    int btn;        /* button width and height */
    int add_x, left_x, right_x, ai_x;
    int ai_visible;
} TabsBtnGeom;
void tabs_btn_geometry(int client_width, int dpi, int ai_visible,
                       TabsBtnGeom *out);

/* Return tooltip label for the tab-strip button at pixel position mx,
 * given total client width, DPI and whether the AI button is shown.
 * Returns NULL if mx is not over a (visible) button. */
const char *tabs_btn_tooltip_at(int mx, int client_width, int dpi,
                                int ai_visible);

#endif
