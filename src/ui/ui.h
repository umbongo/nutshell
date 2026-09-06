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

void ui_init(HINSTANCE instance);
void ui_run(void);
#endif
