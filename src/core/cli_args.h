#ifndef NUTSHELL_CLI_ARGS_H
#define NUTSHELL_CLI_ARGS_H

#include <stddef.h>

/* Command-line action, parsed by cli_parse(). */
typedef enum {
    CLI_RUN = 0,        /* no args — normal start (auto-connect may apply)  */
    CLI_RUN_NO_CONNECT, /* -nc: start without auto-connecting               */
    CLI_CONNECT_NAME,   /* -sn <name>: connect to session by name           */
    CLI_CONNECT_HOST,   /* -h <host>:  connect to session by host           */
    CLI_LIST,           /* -l: list saved sessions                          */
    CLI_VERSION,        /* -v: show version                                 */
    CLI_HELP,           /* -?: show usage                                   */
    CLI_UI_DEMO,        /* --ui-demo[=<state>]: hidden screenshot harness   */
    CLI_ERROR           /* bad command line — see .error                    */
} CliAction;

#define CLI_ARG_MAX ((size_t)256)
#define CLI_DEMO_STATE_MAX ((size_t)32)
#define CLI_THEME_MAX ((size_t)64)

typedef struct {
    CliAction action;
    char arg[CLI_ARG_MAX];    /* session name or host for connect actions */
    char error[CLI_ARG_MAX];  /* human-readable message when CLI_ERROR    */
    char demo_state[CLI_DEMO_STATE_MAX]; /* CLI_UI_DEMO: "chat","all",... */
    char theme[CLI_THEME_MAX];           /* --theme <name>, only with --ui-demo */
} CliOptions;

/* Parse argv (argv[0] ignored). Never fails hard: bad input yields
 * CLI_ERROR with a message in out->error. All options are mutually
 * exclusive, with one exception: --theme <name> (also --theme=<name>) is
 * accepted only alongside --ui-demo[=<state>] and is CLI_ERROR on its own
 * or combined with any other action. --ui-demo and --theme are hidden —
 * they never appear in cli_usage_text(). */
void cli_parse(int argc, char **argv, CliOptions *out);

/* Static usage text listing every flag. */
const char *cli_usage_text(void);

#endif
