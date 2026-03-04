#ifndef CONGA_CONFIG_PROFILE_H
#define CONGA_CONFIG_PROFILE_H

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#ifndef MAX_PATH
#define MAX_PATH 4096
#endif
#endif

typedef enum {
    AUTH_PASSWORD = 0,
    AUTH_KEY = 1
} AuthType;

typedef struct {
    char name[256];
    char host[256];
    int port;
    char username[256];
    AuthType auth_type;
    char password[256]; // Or passphrase for key
    char key_path[MAX_PATH];
} Profile;

#endif