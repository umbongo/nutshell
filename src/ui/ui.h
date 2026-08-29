#ifndef NUTSHELL_UI_H
#define NUTSHELL_UI_H
#include <windows.h>
#include "../core/cli_args.h"

/* Record the startup action parsed from the command line. Must be called
 * BEFORE ui_init(); the window resolves it after config load. Only
 * CLI_RUN, CLI_RUN_NO_CONNECT, CLI_CONNECT_NAME and CLI_CONNECT_HOST are
 * meaningful here. */
void ui_set_startup_action(CliAction action, const char *arg);

void ui_init(HINSTANCE instance);
void ui_run(void);
#endif
