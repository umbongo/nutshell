#ifndef TABS_H
#define TABS_H

#ifdef _WIN32
#include <windows.h>
#include <stdbool.h>

/* Callback types */
typedef void (*TabSelectCallback)(int index, void *user_data);
typedef void (*TabNewCallback)(void);
typedef void (*TabCloseCallback)(int index, void *user_data);
typedef void (*TabSettingsCallback)(void);

/* Public API */
void tabs_init(HINSTANCE hInstance);
HWND tabs_create(HWND parent, int x, int y, int width, int height);

int tabs_add(HWND hwnd, const char *title, void *user_data);
void tabs_remove(HWND hwnd, int index);
void tabs_clear(HWND hwnd);

void tabs_set_active(HWND hwnd, int index);
int tabs_get_active(HWND hwnd);
void *tabs_get_user_data(HWND hwnd, int index);

void tabs_set_callbacks(HWND hwnd, TabSelectCallback on_select, TabNewCallback on_new, TabCloseCallback on_close, TabSettingsCallback on_settings);

#endif
#endif /* TABS_H */