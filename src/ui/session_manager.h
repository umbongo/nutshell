#ifndef NUTSHELL_UI_SESSION_MANAGER_H
#define NUTSHELL_UI_SESSION_MANAGER_H

#include <windows.h>
#include "../config/profile.h"
#include "../config/config.h"

/*
 * Show the two-column Session Manager dialog.
 *
 * cfg         - live Config; the profiles vector is read and written in place.
 * config_path - path passed to config_save() whenever profiles are saved/deleted.
 * out_profile - populated with the selected/filled profile when Connect is clicked.
 *
 * Returns 1 if Connect was clicked and out_profile is populated, 0 otherwise.
 */
int SessionManager_Show(HINSTANCE hInstance, HWND parent,
                        Config *cfg, const char *config_path,
                        Profile *out_profile);

#endif
