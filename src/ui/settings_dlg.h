#ifndef NUTSHELL_SETTINGS_DLG_H
#define NUTSHELL_SETTINGS_DLG_H

#ifdef _WIN32
#include <windows.h>
#include "../config/config.h"

void settings_dlg_show(HWND parent, Config *cfg);

#endif
#endif
