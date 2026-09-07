#ifndef NUTSHELL_SETTINGS_DLG_H
#define NUTSHELL_SETTINGS_DLG_H

#ifdef _WIN32
#include <windows.h>
#include "../config/config.h"

/* initial_page: a SETTINGS_PAGE_* (src/core/settings_layout.h) to open on,
 * or -1 for the default (first selectable nav entry). An unrecognised
 * page id falls back to the default too. */
void settings_dlg_show(HWND parent, Config *cfg, int initial_page);

#endif
#endif
