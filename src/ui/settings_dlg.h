#ifndef NUTSHELL_SETTINGS_DLG_H
#define NUTSHELL_SETTINGS_DLG_H

#ifdef _WIN32
#include <windows.h>
#include "../config/config.h"

/* config_path - absolute path passed to config_save() on OK; the caller's
 *   resolved startup path (never the bare CONFIG_FILENAME -- a relative
 *   name saves into whatever the process's current directory has become,
 *   e.g. after a GetOpenFileName browse dialog).
 * initial_page - a SETTINGS_PAGE_* (src/core/settings_layout.h) to open on,
 *   or -1 for the default (first selectable nav entry). An unrecognised
 *   page id falls back to the default too.
 * Returns 1 when it ran a dialog (shown and closed), 0 when it ran none:
 *   one is already open (it is brought to the front), cfg is NULL, or the
 *   window could not be created. The caller must not refresh anything the
 *   dialog could have changed on 0. */
int settings_dlg_show(HWND parent, Config *cfg, const char *config_path,
                      int initial_page);

/* --ui-demo=settings-debug: Settings opened on the Debugging page with
 * "Enable debugging" ticked in memory only. Save and Cancel both just close;
 * nothing is written and cfg is not touched. */
void settings_dlg_show_demo_debug(HWND parent, Config *cfg);

#endif
#endif
