#ifndef NUTSHELL_TABS_H
#define NUTSHELL_TABS_H

#include "tab_manager.h"   /* TabStatus, TabEntry, TabManager, TABS_MAX */

#ifdef _WIN32
#include <windows.h>

typedef void (*TabSelectCallback)(int index, void *user_data);
typedef void (*TabNewCallback)(void);
typedef void (*TabCloseCallback)(int index, void *user_data);
typedef void (*TabSettingsCallback)(void);
typedef void (*TabLogToggleCallback)(int index, void *user_data);

void      tabs_init       (HINSTANCE hInstance);
HWND      tabs_create     (HWND parent, int x, int y, int width, int height);
int       tabs_add        (HWND hwnd, const char *title, void *user_data);
void      tabs_remove     (HWND hwnd, int index);
void      tabs_set_callbacks(HWND hwnd,
                             TabSelectCallback   on_select,
                             TabNewCallback      on_new,
                             TabCloseCallback    on_close,
                             TabSettingsCallback  on_settings,
                             TabLogToggleCallback on_log_toggle);
void      tabs_clear      (HWND hwnd);
void      tabs_set_active (HWND hwnd, int index);
int       tabs_get_active (HWND hwnd);
void     *tabs_get_user_data(HWND hwnd, int index);
void      tabs_set_status (HWND hwnd, int index, TabStatus status);
TabStatus tabs_get_status (HWND hwnd, int index);
/* Returns the index of the tab whose user_data equals the given pointer, or -1. */
int       tabs_find       (HWND hwnd, void *user_data);
/* Store username / host / connect_ms in a tab for use by tooltips. */
void      tabs_set_connect_info(HWND hwnd, int index,
                                const char *username, const char *host,
                                unsigned long long connect_ms);
void      tabs_set_logging(HWND hwnd, int index, int logging);
int       tabs_get_logging(HWND hwnd, int index);

#endif
#endif
