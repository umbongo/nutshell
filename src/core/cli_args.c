#include "cli_args.h"
#include "ui_demo.h"
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

/* Matches "--name" (bare) or "--name=<value>". Returns 1 on match and sets
 * *has_inline_out to whether '=' was present (value starts right after). */
static int long_flag_match(const char *arg, const char *name, size_t name_len,
                           int *has_inline_out)
{
    if (strncmp(arg, name, name_len) != 0) return 0;
    if (arg[name_len] == '\0') { *has_inline_out = 0; return 1; }
    if (arg[name_len] == '=')  { *has_inline_out = 1; return 1; }
    return 0;
}

void cli_parse(int argc, char **argv, CliOptions *out)
{
    memset(out, 0, sizeof(*out));
    out->action = CLI_RUN;

    if (argc <= 1 || !argv) {
        return;
    }

    int seen = 0;
    int theme_seen = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        CliAction act;
        int takes_value = 0;
        int is_ui_demo = 0;
        int has_inline = 0;
        const char *inline_value = NULL;

        if (long_flag_match(a, "--ui-demo", 9, &has_inline)) {
            act = CLI_UI_DEMO;
            is_ui_demo = 1;
            if (has_inline) inline_value = a + 10; /* past "--ui-demo=" */
        } else if (long_flag_match(a, "--theme", 7, &has_inline)) {
            const char *value;
            if (has_inline) {
                value = a + 8; /* past "--theme=" */
            } else {
                if (i + 1 >= argc) {
                    set_error(out, "Missing value for ", a);
                    return;
                }
                i++;
                value = argv[i];
            }
            (void)snprintf(out->theme, sizeof(out->theme), "%s", value);
            theme_seen = 1;
            continue; /* --theme is a modifier, not a mutually-exclusive action */
        } else if (flag_eq(a, "-sn", "--session-name")) {
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

        if (is_ui_demo) {
            const char *state = inline_value ? inline_value : "all";
            if (!ui_demo_state_valid(state)) {
                set_error(out, "Unknown demo state: ", state);
                return;
            }
            (void)snprintf(out->demo_state, sizeof(out->demo_state), "%s", state);
        } else if (takes_value) {
            if (i + 1 >= argc) {
                set_error(out, "Missing value for ", a);
                return;
            }
            i++;
            (void)snprintf(out->arg, sizeof(out->arg), "%s", argv[i]);
        }
        out->action = act;
    }

    if (theme_seen && out->action != CLI_UI_DEMO) {
        set_error(out, "--theme requires ", "--ui-demo");
        return;
    }
}
