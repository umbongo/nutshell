#ifndef NUTSHELL_UI_H
#define NUTSHELL_UI_H
#include <windows.h>
#include "../core/cli_args.h"

/* Record the startup action parsed from the command line. Must be called
 * BEFORE ui_init(); the window resolves it after config load.
 * demo_state and theme are only meaningful when action is CLI_UI_DEMO
 * (demo_state names the canned state, theme is the optional --theme
 * override); pass "" or NULL for either otherwise. */
void ui_set_startup_action(CliAction action, const char *arg,
                           const char *demo_state, const char *theme);

/* --self-report=<path>: after startup the main window writes the DLL record
 * and module list to path and closes. Call before ui_init(); "" or NULL
 * leaves it off. */
void ui_set_self_report(const char *path);

/* --self-report-sapi: 1 to hold an SpVoice while the report is written. Inert
 * without a report path or with voice off. */
void ui_set_self_report_sapi(int on);

/* 1 when --self-report was given and writing the file failed. */
int ui_self_report_failed(void);

void ui_init(HINSTANCE instance);
void ui_run(void);
#endif
