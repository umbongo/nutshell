#ifndef CONGA_TABS_H
#define CONGA_TABS_H

#include "tab_manager.h"   /* TabStatus, TabEntry, TabManager, TABS_MAX */

#ifdef _WIN32
#include <windows.h>

typedef void (*TabSelectCallback)(int index, void *user_data);
typedef void (*TabNewCallback)(void);
typedef void (*TabCloseCallback)(int index, void *user_data);
typedef void (*TabSettingsCallback)(void);

void      tabs_init       (HINSTANCE hInstance);
HWND      tabs_create     (HWND parent, int x, int y, int width, int height);
int       tabs_add        (HWND hwnd, const char *title, void *user_data);
void      tabs_remove     (HWND hwnd, int index);
void      tabs_set_callbacks(HWND hwnd,
                             TabSelectCallback  on_select,
                             TabNewCallback     on_new,
                             TabCloseCallback   on_close,
                             TabSettingsCallback on_settings);
void      tabs_clear      (HWND hwnd);
void      tabs_set_active (HWND hwnd, int index);
int       tabs_get_active (HWND hwnd);
void     *tabs_get_user_data(HWND hwnd, int index);
void      tabs_set_status (HWND hwnd, int index, TabStatus status);
TabStatus tabs_get_status (HWND hwnd, int index);
/* Returns the index of the tab whose user_data equals the given pointer, or -1. */
int       tabs_find       (HWND hwnd, void *user_data);

#endif
#endif
