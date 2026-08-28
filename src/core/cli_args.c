#include "cli_args.h"
#include <stdio.h>
#include <string.h>

static const char USAGE[] =
    "Usage: nutshell.exe [option]\n"
    "\n"
    "  -sn, --session-name <name>   Connect to the saved session with this name\n"
    "  -h,  --host <host>           Connect to the saved session with this host\n"
    "  -nc, --no-connect            Start without auto-connecting\n"
    "  -l,  --list                  List saved sessions\n"
    "  -v,  --version               Show version\n"
    "  -?,  --help                  Show this help\n";

const char *cli_usage_text(void)
{
    return USAGE;
}

static int flag_eq(const char *arg, const char *s, const char *l)
{
    return strcmp(arg, s) == 0 || strcmp(arg, l) == 0;
}

/* Literal format strings only — -Wformat-nonliteral is fatal. */
static void set_error(CliOptions *out, const char *prefix, const char *what)
{
    out->action = CLI_ERROR;
    (void)snprintf(out->error, sizeof(out->error), "%s%s", prefix, what);
}

void cli_parse(int argc, char **argv, CliOptions *out)
{
    memset(out, 0, sizeof(*out));
    out->action = CLI_RUN;

    if (argc <= 1 || !argv) {
        return;
    }

    int seen = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        CliAction act;
        int takes_value = 0;

        if (flag_eq(a, "-sn", "--session-name")) {
            act = CLI_CONNECT_NAME;
            takes_value = 1;
        } else if (flag_eq(a, "-h", "--host")) {
            act = CLI_CONNECT_HOST;
            takes_value = 1;
        } else if (flag_eq(a, "-nc", "--no-connect")) {
            act = CLI_RUN_NO_CONNECT;
        } else if (flag_eq(a, "-l", "--list")) {
            act = CLI_LIST;
        } else if (flag_eq(a, "-v", "--version")) {
            act = CLI_VERSION;
        } else if (flag_eq(a, "-?", "--help")) {
            act = CLI_HELP;
        } else {
            set_error(out, "Unknown option: ", a);
            return;
        }

        if (seen) {
            set_error(out, "Options are mutually exclusive: ", a);
            return;
        }
        seen = 1;

        if (takes_value) {
            if (i + 1 >= argc) {
                set_error(out, "Missing value for ", a);
                return;
            }
            i++;
            (void)snprintf(out->arg, sizeof(out->arg), "%s", argv[i]);
        }
        out->action = act;
    }
}
