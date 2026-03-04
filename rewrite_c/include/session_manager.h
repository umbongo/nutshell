#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#ifdef _WIN32
#include <windows.h>
#endif
#include "config.h"

typedef struct {
    char name[64];
    char host[256];
    int port;
    char username[64];
    char password[256];
    int auth_type; /* 0=Password, 1=Key */
} SessionInfo;

typedef void (*SessionConnectCallback)(const SessionInfo *info, void *user_data);

#ifdef _WIN32
void session_manager_show(HWND parent, Config *cfg, SessionConnectCallback on_connect, void *user_data);

#endif

#endif /* SESSION_MANAGER_H */