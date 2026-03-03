#ifndef CONGA_TAB_MANAGER_H
#define CONGA_TAB_MANAGER_H

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
void      tabmgr_remove      (TabManager *m, int index);
void      tabmgr_set_active  (TabManager *m, int index);
int       tabmgr_get_active  (const TabManager *m);
void     *tabmgr_get_user_data(const TabManager *m, int index);
void      tabmgr_set_status  (TabManager *m, int index, TabStatus status);
TabStatus tabmgr_get_status  (const TabManager *m, int index);
int       tabmgr_count       (const TabManager *m);
int       tabmgr_get_id      (const TabManager *m, int index);
/* Find by user_data pointer; returns index or -1. */
int       tabmgr_find        (const TabManager *m, const void *user_data);
/* Store connection info (username, host, connect timestamp) in a tab entry. */
void      tabmgr_set_connect_info(TabManager *m, int index,
                                   const char *username, const char *host,
                                   unsigned long long connect_ms);

#endif
