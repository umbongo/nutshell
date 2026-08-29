# CLI Parameters & Auto-Connect at Startup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Launch Nutshell straight into a saved SSH session from the command line (`-sn <name>` / `-h <host>`), add `-nc`/`-l`/`-v`/`-?` flags, and add an "auto-connect at startup" setting so a pinned taskbar shortcut gives one-click access to a session.

**Architecture:**
- Pure argument parser in `src/core/cli_args.{h,c}` (testable on native Linux, zero Win32).
- Case-insensitive profile lookup helpers in `src/config/loader.c` (testable).
- Two new `Settings` fields (`auto_connect`, `auto_connect_session`) persisted in `nutshell.config`.
- Thin Win32 glue: `WinMain` converts the command line to UTF-8 argv and dispatches; connect requests are stashed via `ui_set_startup_action()` and resolved after config load by posting `WM_STARTUP_CONNECT`, which reuses the existing `on_session_connect()` path.
- Help/version/list output attaches to the parent console when launched from a terminal, otherwise falls back to a MessageBox.

**Tech Stack:** C11, MinGW (`x86_64-w64-mingw32-gcc`), Win32, custom test framework (`tests/test_framework.h`). Native `gcc` for tests.

**Spec:** [docs/superpowers/specs/2026-08-28-cli-params-and-autoconnect-design.md](../specs/2026-08-28-cli-params-and-autoconnect-design.md)

## Global Constraints

- **Version bump is MANDATORY before every Windows build** (`make clean && make release`). Update BOTH: `src/ui/resource.h` (`APP_VERSION` string + `APP_VERSION_BINARY` macro) and `README.md` (`**Version**:` line). Current version at plan time: `1.0.67`. `make test` alone does NOT require a bump.
- **Always `make clean && make release`** — never `make release` alone.
- Code must compile clean under BOTH compilers with `-Werror -Wpedantic -Wshadow -Wconversion -Wformat=2` (cross) and `-Wall -Wextra` (native test).
- `-Wformat=2` includes `-Wformat-nonliteral`: never pass a variable as a printf format string.
- `-Wshadow`: never name a local `msg` inside `WndProc` (parameter is `UINT msg`).
- Any file using `snprintf` must `#include <stdio.h>` explicitly.
- Testable logic goes in `src/core/` or `src/config/` — `src/main.c` and `src/ui/*` are excluded from the test build (`NON_TEST_SRCS`, Makefile:57).
- All option flags are **mutually exclusive**; profile matching is **case-insensitive exact**, empty fields never match, first match wins.
- No AI attribution footers/trailers in commits.

## Build & test commands

- **Tests (native Linux):** `cd /home/thomas/nutshell && make test`
- **Windows release:** `cd /home/thomas/nutshell && make clean && make release` (version bump first!)

## Task list overview

| # | Task | Touches |
|---|---|---|
| 1 | Pure CLI parser | `src/core/cli_args.{h,c}` (new), `tests/test_cli_args.c` (new), `tests/runner.c` |
| 2 | Profile lookup helpers | `src/config/config.h`, `src/config/loader.c`, `tests/test_config.c`, `tests/runner.c` |
| 3 | Auto-connect settings fields | `src/config/config.h`, `src/config/loader.c`, `tests/test_config.c`, `tests/runner.c` |
| 4 | Settings dialog "Startup" section | `src/ui/resource.h`, `src/ui/settings.c` |
| 5 | WinMain dispatch + startup connect | `src/main.c`, `src/ui/ui.h`, `src/ui/window.c`, `Makefile` |
| 6 | Docs + final build + manual verification | `README.md`, `src/ui/help_guide.c`, `src/ui/resource.h` |

---

### Task 1: Pure CLI parser (`src/core/cli_args`)

**Files:**
- Create: `src/core/cli_args.h`
- Create: `src/core/cli_args.c`
- Create: `tests/test_cli_args.c`
- Modify: `tests/runner.c`

**Interfaces:**
- Consumes: nothing (pure C, `<string.h>`/`<stdio.h>` only).
- Produces: `CliAction` enum (`CLI_RUN`, `CLI_RUN_NO_CONNECT`, `CLI_CONNECT_NAME`, `CLI_CONNECT_HOST`, `CLI_LIST`, `CLI_VERSION`, `CLI_HELP`, `CLI_ERROR`), `CliOptions { CliAction action; char arg[256]; char error[256]; }`, `void cli_parse(int argc, char **argv, CliOptions *out)`, `const char *cli_usage_text(void)`. Tasks 5 depends on these exact names.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_cli_args.c`:

```c
#include "test_framework.h"
#include "cli_args.h"
#include <string.h>
#include <stdio.h>

/* ---- no args ---- */

int test_cli_no_args(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell" };
    CliOptions o;
    cli_parse(1, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_RUN);
    ASSERT_STR_EQ(o.arg, "");
    TEST_END();
}

/* ---- connect by name ---- */

int test_cli_session_name_short(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell", "-sn", "prod" };
    CliOptions o;
    cli_parse(3, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_CONNECT_NAME);
    ASSERT_STR_EQ(o.arg, "prod");
    TEST_END();
}

int test_cli_session_name_long(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell", "--session-name", "my server" };
    CliOptions o;
    cli_parse(3, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_CONNECT_NAME);
    ASSERT_STR_EQ(o.arg, "my server");
    TEST_END();
}

/* ---- connect by host ---- */

int test_cli_host_short(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell", "-h", "box.example.com" };
    CliOptions o;
    cli_parse(3, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_CONNECT_HOST);
    ASSERT_STR_EQ(o.arg, "box.example.com");
    TEST_END();
}

int test_cli_host_long(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell", "--host", "10.0.0.5" };
    CliOptions o;
    cli_parse(3, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_CONNECT_HOST);
    ASSERT_STR_EQ(o.arg, "10.0.0.5");
    TEST_END();
}

/* ---- simple actions ---- */

int test_cli_no_connect(void)
{
    TEST_BEGIN();
    char *a1[] = { "nutshell", "-nc" };
    char *a2[] = { "nutshell", "--no-connect" };
    CliOptions o;
    cli_parse(2, a1, &o);
    ASSERT_EQ((int)o.action, (int)CLI_RUN_NO_CONNECT);
    cli_parse(2, a2, &o);
    ASSERT_EQ((int)o.action, (int)CLI_RUN_NO_CONNECT);
    TEST_END();
}

int test_cli_list_version_help(void)
{
    TEST_BEGIN();
    CliOptions o;
    char *l1[] = { "nutshell", "-l" };
    char *l2[] = { "nutshell", "--list" };
    char *v1[] = { "nutshell", "-v" };
    char *v2[] = { "nutshell", "--version" };
    char *h1[] = { "nutshell", "-?" };
    char *h2[] = { "nutshell", "--help" };
    cli_parse(2, l1, &o); ASSERT_EQ((int)o.action, (int)CLI_LIST);
    cli_parse(2, l2, &o); ASSERT_EQ((int)o.action, (int)CLI_LIST);
    cli_parse(2, v1, &o); ASSERT_EQ((int)o.action, (int)CLI_VERSION);
    cli_parse(2, v2, &o); ASSERT_EQ((int)o.action, (int)CLI_VERSION);
    cli_parse(2, h1, &o); ASSERT_EQ((int)o.action, (int)CLI_HELP);
    cli_parse(2, h2, &o); ASSERT_EQ((int)o.action, (int)CLI_HELP);
    TEST_END();
}

/* ---- errors ---- */

int test_cli_missing_value(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell", "-sn" };
    CliOptions o;
    cli_parse(2, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_ERROR);
    ASSERT_NOT_NULL(strstr(o.error, "-sn"));
    TEST_END();
}

int test_cli_unknown_flag(void)
{
    TEST_BEGIN();
    CliOptions o;
    char *a1[] = { "nutshell", "-x" };
    char *a2[] = { "nutshell", "--frobnicate" };
    cli_parse(2, a1, &o);
    ASSERT_EQ((int)o.action, (int)CLI_ERROR);
    cli_parse(2, a2, &o);
    ASSERT_EQ((int)o.action, (int)CLI_ERROR);
    ASSERT_NOT_NULL(strstr(o.error, "--frobnicate"));
    TEST_END();
}

int test_cli_bare_argument(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell", "foo" };
    CliOptions o;
    cli_parse(2, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_ERROR);
    TEST_END();
}

int test_cli_mutual_exclusion(void)
{
    TEST_BEGIN();
    CliOptions o;
    char *a1[] = { "nutshell", "-sn", "a", "-h", "b" };
    char *a2[] = { "nutshell", "-v", "-l" };
    char *a3[] = { "nutshell", "-nc", "-sn", "a" };
    cli_parse(5, a1, &o); ASSERT_EQ((int)o.action, (int)CLI_ERROR);
    cli_parse(3, a2, &o); ASSERT_EQ((int)o.action, (int)CLI_ERROR);
    cli_parse(4, a3, &o); ASSERT_EQ((int)o.action, (int)CLI_ERROR);
    TEST_END();
}

int test_cli_trailing_junk(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell", "-sn", "a", "extra" };
    CliOptions o;
    cli_parse(4, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_ERROR);
    TEST_END();
}

/* ---- value handling ---- */

int test_cli_value_resembling_flag(void)
{
    TEST_BEGIN();
    char *argv[] = { "nutshell", "-sn", "-v" };
    CliOptions o;
    cli_parse(3, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_CONNECT_NAME);
    ASSERT_STR_EQ(o.arg, "-v");
    TEST_END();
}

int test_cli_overlong_value(void)
{
    TEST_BEGIN();
    char big[301];
    memset(big, 'A', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    char *argv[] = { "nutshell", "-sn", big };
    CliOptions o;
    cli_parse(3, argv, &o);
    ASSERT_EQ((int)o.action, (int)CLI_CONNECT_NAME);
    ASSERT_EQ((int)strlen(o.arg), 255);  /* truncated, NUL-terminated */
    TEST_END();
}

/* ---- usage text ---- */

int test_cli_usage_text_mentions_all_flags(void)
{
    TEST_BEGIN();
    const char *u = cli_usage_text();
    ASSERT_NOT_NULL(u);
    ASSERT_NOT_NULL(strstr(u, "--session-name"));
    ASSERT_NOT_NULL(strstr(u, "--host"));
    ASSERT_NOT_NULL(strstr(u, "--no-connect"));
    ASSERT_NOT_NULL(strstr(u, "--list"));
    ASSERT_NOT_NULL(strstr(u, "--version"));
    ASSERT_NOT_NULL(strstr(u, "--help"));
    ASSERT_NOT_NULL(strstr(u, "-sn"));
    ASSERT_NOT_NULL(strstr(u, "-nc"));
    ASSERT_NOT_NULL(strstr(u, "-?"));
    TEST_END();
}
```

Register in `tests/runner.c` — add a forward-declaration block after the `/* test_config.c */` block (search for `int test_config_roundtrip_ssh_user_idle(void);` around line 161 and insert after it):

```c
/* test_cli_args.c */
int test_cli_no_args(void);
int test_cli_session_name_short(void);
int test_cli_session_name_long(void);
int test_cli_host_short(void);
int test_cli_host_long(void);
int test_cli_no_connect(void);
int test_cli_list_version_help(void);
int test_cli_missing_value(void);
int test_cli_unknown_flag(void);
int test_cli_bare_argument(void);
int test_cli_mutual_exclusion(void);
int test_cli_trailing_junk(void);
int test_cli_value_resembling_flag(void);
int test_cli_overlong_value(void);
int test_cli_usage_text_mentions_all_flags(void);
```

And in `main()` (search for `failed += test_config_roundtrip_ssh_user_idle();` around line 1727, insert after it):

```c
    printf("\n--- CLI args ---\n");
    failed += test_cli_no_args();
    failed += test_cli_session_name_short();
    failed += test_cli_session_name_long();
    failed += test_cli_host_short();
    failed += test_cli_host_long();
    failed += test_cli_no_connect();
    failed += test_cli_list_version_help();
    failed += test_cli_missing_value();
    failed += test_cli_unknown_flag();
    failed += test_cli_bare_argument();
    failed += test_cli_mutual_exclusion();
    failed += test_cli_trailing_junk();
    failed += test_cli_value_resembling_flag();
    failed += test_cli_overlong_value();
    failed += test_cli_usage_text_mentions_all_flags();
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd /home/thomas/nutshell && make test`
Expected: FAIL to compile — `cli_args.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `src/core/cli_args.h`:

```c
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
    CLI_ERROR           /* bad command line — see .error                    */
} CliAction;

#define CLI_ARG_MAX ((size_t)256)

typedef struct {
    CliAction action;
    char arg[CLI_ARG_MAX];    /* session name or host for connect actions */
    char error[CLI_ARG_MAX];  /* human-readable message when CLI_ERROR    */
} CliOptions;

/* Parse argv (argv[0] ignored). Never fails hard: bad input yields
 * CLI_ERROR with a message in out->error. All options are mutually
 * exclusive. */
void cli_parse(int argc, char **argv, CliOptions *out);

/* Static usage text listing every flag. */
const char *cli_usage_text(void);

#endif
```

Create `src/core/cli_args.c`:

```c
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
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd /home/thomas/nutshell && make test`
Expected: all tests PASS, including the 15 new `--- CLI args ---` entries.

- [ ] **Step 5: Commit**

```bash
cd /home/thomas/nutshell
git add src/core/cli_args.h src/core/cli_args.c tests/test_cli_args.c tests/runner.c
git commit -m "feat(core): add pure CLI argument parser"
```

---

### Task 2: Profile lookup helpers

**Files:**
- Modify: `src/config/config.h` (declarations, after `config_profile_free` at line 57)
- Modify: `src/config/loader.c` (implementations at end of file)
- Modify: `tests/test_config.c` (append tests)
- Modify: `tests/runner.c` (register)

**Interfaces:**
- Consumes: `Config`, `Profile`, `Vector` (`vec_size`/`vec_get`) — all existing.
- Produces: `Profile *config_find_profile_by_name(const Config *cfg, const char *name);` and `Profile *config_find_profile_by_host(const Config *cfg, const char *host);` — Task 5's `window.c` handler calls both with these exact signatures.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_config.c`:

```c
/* ============================================================
 * Profile lookup (config_find_profile_by_name / _by_host)
 * ============================================================ */

static Config *make_lookup_cfg(void)
{
    Config *cfg = config_new_default();
    Profile *a = config_profile_new();
    snprintf(a->name, sizeof(a->name), "Automaton");
    snprintf(a->host, sizeof(a->host), "automaton.local");
    vec_push(&cfg->profiles, a);
    Profile *b = config_profile_new();
    /* unnamed profile — host only */
    snprintf(b->host, sizeof(b->host), "backup.example.com");
    vec_push(&cfg->profiles, b);
    Profile *c = config_profile_new();
    snprintf(c->name, sizeof(c->name), "automaton");  /* duplicate name, different case */
    snprintf(c->host, sizeof(c->host), "other.host");
    vec_push(&cfg->profiles, c);
    return cfg;
}

int test_find_profile_by_name_exact(void)
{
    TEST_BEGIN();
    Config *cfg = make_lookup_cfg();
    Profile *p = config_find_profile_by_name(cfg, "Automaton");
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p->host, "automaton.local");
    config_free(cfg);
    TEST_END();
}

int test_find_profile_by_name_case_insensitive(void)
{
    TEST_BEGIN();
    Config *cfg = make_lookup_cfg();
    Profile *p = config_find_profile_by_name(cfg, "AUTOMATON");
    ASSERT_NOT_NULL(p);
    /* first match wins: profile 'a', not the duplicate 'c' */
    ASSERT_STR_EQ(p->host, "automaton.local");
    config_free(cfg);
    TEST_END();
}

int test_find_profile_by_name_absent(void)
{
    TEST_BEGIN();
    Config *cfg = make_lookup_cfg();
    ASSERT_NULL(config_find_profile_by_name(cfg, "nosuch"));
    config_free(cfg);
    TEST_END();
}

int test_find_profile_null_and_empty(void)
{
    TEST_BEGIN();
    Config *cfg = make_lookup_cfg();
    ASSERT_NULL(config_find_profile_by_name(NULL, "x"));
    ASSERT_NULL(config_find_profile_by_name(cfg, NULL));
    ASSERT_NULL(config_find_profile_by_name(cfg, ""));
    ASSERT_NULL(config_find_profile_by_host(NULL, "x"));
    ASSERT_NULL(config_find_profile_by_host(cfg, NULL));
    ASSERT_NULL(config_find_profile_by_host(cfg, ""));
    config_free(cfg);
    TEST_END();
}

int test_find_profile_empty_name_never_matches(void)
{
    TEST_BEGIN();
    Config *cfg = make_lookup_cfg();
    /* profile 'b' has empty name — empty query must not match it */
    ASSERT_NULL(config_find_profile_by_name(cfg, ""));
    config_free(cfg);
    TEST_END();
}

int test_find_profile_by_host_case_insensitive(void)
{
    TEST_BEGIN();
    Config *cfg = make_lookup_cfg();
    Profile *p = config_find_profile_by_host(cfg, "BACKUP.example.COM");
    ASSERT_NOT_NULL(p);
    ASSERT_STR_EQ(p->host, "backup.example.com");
    config_free(cfg);
    TEST_END();
}

int test_find_profile_no_partial_match(void)
{
    TEST_BEGIN();
    Config *cfg = make_lookup_cfg();
    ASSERT_NULL(config_find_profile_by_name(cfg, "Auto"));       /* prefix */
    ASSERT_NULL(config_find_profile_by_host(cfg, "backup"));     /* prefix */
    config_free(cfg);
    TEST_END();
}
```

Register in `tests/runner.c` (forward decls after the `test_cli_args.c` block from Task 1; calls after the `--- CLI args ---` section):

```c
/* profile lookup (test_config.c) */
int test_find_profile_by_name_exact(void);
int test_find_profile_by_name_case_insensitive(void);
int test_find_profile_by_name_absent(void);
int test_find_profile_null_and_empty(void);
int test_find_profile_empty_name_never_matches(void);
int test_find_profile_by_host_case_insensitive(void);
int test_find_profile_no_partial_match(void);
```

```c
    printf("\n--- Profile lookup ---\n");
    failed += test_find_profile_by_name_exact();
    failed += test_find_profile_by_name_case_insensitive();
    failed += test_find_profile_by_name_absent();
    failed += test_find_profile_null_and_empty();
    failed += test_find_profile_empty_name_never_matches();
    failed += test_find_profile_by_host_case_insensitive();
    failed += test_find_profile_no_partial_match();
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd /home/thomas/nutshell && make test`
Expected: FAIL to compile — implicit declaration of `config_find_profile_by_name`.

- [ ] **Step 3: Write the implementation**

In `src/config/config.h`, after the `config_profile_free` declaration (line 57), add:

```c
/* Case-insensitive exact lookup. Empty fields never match; first match
 * wins on duplicates. Returns NULL when not found or on NULL/empty input. */
Profile *config_find_profile_by_name(const Config *cfg, const char *name);
Profile *config_find_profile_by_host(const Config *cfg, const char *host);
```

In `src/config/loader.c`, add `#include <ctype.h>` to the include block, and append at the end of the file:

```c
/* ---- Profile lookup ------------------------------------------------------- */

/* ASCII case-insensitive equality (config matching is exact, not prefix). */
static int str_ieq(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

Profile *config_find_profile_by_name(const Config *cfg, const char *name)
{
    if (!cfg || !name || name[0] == '\0') {
        return NULL;
    }
    size_t n = vec_size(&cfg->profiles);
    for (size_t i = 0u; i < n; i++) {
        Profile *p = (Profile *)vec_get(&cfg->profiles, i);
        if (p && p->name[0] != '\0' && str_ieq(p->name, name)) {
            return p;
        }
    }
    return NULL;
}

Profile *config_find_profile_by_host(const Config *cfg, const char *host)
{
    if (!cfg || !host || host[0] == '\0') {
        return NULL;
    }
    size_t n = vec_size(&cfg->profiles);
    for (size_t i = 0u; i < n; i++) {
        Profile *p = (Profile *)vec_get(&cfg->profiles, i);
        if (p && p->host[0] != '\0' && str_ieq(p->host, host)) {
            return p;
        }
    }
    return NULL;
}
```

Note: if `vec_size`/`vec_get` take non-const `Vector *`, cast: `vec_size((Vector *)&cfg->profiles)` is NOT needed — check `src/core/vector.h` first; `config_save` already calls them on a `const Config *`, so follow whatever it does.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd /home/thomas/nutshell && make test`
Expected: all PASS including the 7 new `--- Profile lookup ---` entries.

- [ ] **Step 5: Commit**

```bash
cd /home/thomas/nutshell
git add src/config/config.h src/config/loader.c tests/test_config.c tests/runner.c
git commit -m "feat(config): add case-insensitive profile lookup by name and host"
```

---

### Task 3: Auto-connect settings fields

**Files:**
- Modify: `src/config/config.h` (Settings struct, line 41 area)
- Modify: `src/config/loader.c` (`config_default_settings`, `settings_validate`, `config_load`, `config_save`)
- Modify: `tests/test_config.c`, `tests/runner.c`

**Interfaces:**
- Consumes: existing loader plumbing (`field_copy`, `json_obj_bool`, `json_obj_str`, `fprint_json_str`).
- Produces: `Settings.auto_connect` (int, 0/1, default 0) and `Settings.auto_connect_session` (char[CFG_STR_MAX], default empty). Tasks 4 and 5 read/write these exact field names.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_config.c`:

```c
/* ============================================================
 * Auto-connect settings
 * ============================================================ */

int test_config_default_auto_connect(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    ASSERT_EQ(s.auto_connect, 0);
    ASSERT_STR_EQ(s.auto_connect_session, "");
    TEST_END();
}

int test_config_validate_auto_connect_clamp(void)
{
    TEST_BEGIN();
    Settings s;
    config_default_settings(&s);
    s.auto_connect = 7;
    settings_validate(&s);
    ASSERT_EQ(s.auto_connect, 1);
    s.auto_connect = -3;
    settings_validate(&s);
    ASSERT_EQ(s.auto_connect, 1);
    s.auto_connect = 0;
    settings_validate(&s);
    ASSERT_EQ(s.auto_connect, 0);
    TEST_END();
}

int test_config_roundtrip_auto_connect(void)
{
    TEST_BEGIN();
    Config *cfg = config_new_default();
    cfg->settings.auto_connect = 1;
    snprintf(cfg->settings.auto_connect_session,
             sizeof(cfg->settings.auto_connect_session), "automaton");
    ASSERT_EQ(config_save(cfg, TMP_CFG), 0);
    config_free(cfg);

    Config *re = config_load(TMP_CFG);
    ASSERT_NOT_NULL(re);
    ASSERT_EQ(re->settings.auto_connect, 1);
    ASSERT_STR_EQ(re->settings.auto_connect_session, "automaton");
    config_free(re);
    remove(TMP_CFG);
    TEST_END();
}

int test_config_load_legacy_no_auto_connect(void)
{
    TEST_BEGIN();
    /* A config written before these fields existed must default them. */
    FILE *f = fopen(TMP_CFG, "w");
    ASSERT_NOT_NULL(f);
    fputs("{\"settings\": {\"font\": \"Consolas\"}, \"profiles\": []}", f);
    fclose(f);

    Config *cfg = config_load(TMP_CFG);
    ASSERT_NOT_NULL(cfg);
    ASSERT_EQ(cfg->settings.auto_connect, 0);
    ASSERT_STR_EQ(cfg->settings.auto_connect_session, "");
    config_free(cfg);
    remove(TMP_CFG);
    TEST_END();
}
```

Register in `tests/runner.c` (decls after the profile-lookup block; calls after the `--- Profile lookup ---` section):

```c
/* auto-connect settings (test_config.c) */
int test_config_default_auto_connect(void);
int test_config_validate_auto_connect_clamp(void);
int test_config_roundtrip_auto_connect(void);
int test_config_load_legacy_no_auto_connect(void);
```

```c
    printf("\n--- Auto-connect settings ---\n");
    failed += test_config_default_auto_connect();
    failed += test_config_validate_auto_connect_clamp();
    failed += test_config_roundtrip_auto_connect();
    failed += test_config_load_legacy_no_auto_connect();
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd /home/thomas/nutshell && make test`
Expected: FAIL to compile — `Settings` has no member `auto_connect`.

- [ ] **Step 3: Write the implementation**

`src/config/config.h` — in `Settings`, after `int markdown_render_enabled;` (line 41):

```c
    int  auto_connect;                       /* connect at startup: 0 = off (default) */
    char auto_connect_session[CFG_STR_MAX];  /* session name (or host) to auto-connect */
```

`src/config/loader.c`:

1. `config_default_settings()` — after `s->markdown_render_enabled = 1;`:

```c
    s->auto_connect = 0;
    /* auto_connect_session defaults to empty (already zeroed by memset) */
```

2. `settings_validate()` — after the `ssh_user_idle_timeout_mins` clamps:

```c
    if (s->auto_connect != 0) s->auto_connect = 1;
```

3. `config_load()` — after the `markdown_render_enabled` read (line ~279), before `settings_validate(s);`:

```c
        s->auto_connect = json_obj_bool(jset, "auto_connect", s->auto_connect);
        if ((sv = json_obj_str(jset, "auto_connect_session"))) {
            field_copy(s->auto_connect_session,
                       sizeof(s->auto_connect_session), sv);
        }
```

4. `config_save()` — the `markdown_render_enabled` line (currently the LAST settings key, written with `%s\n` and no trailing comma at line ~426) gains a trailing comma, and the new keys become last:

```c
    fprintf(f, "    \"markdown_render_enabled\": %s,\n",
            s->markdown_render_enabled ? "true" : "false");
    fprintf(f, "    \"auto_connect\": %s,\n",
            s->auto_connect ? "true" : "false");
    fputs("    \"auto_connect_session\": ", f);
    fprint_json_str(f, s->auto_connect_session);
    fputc('\n', f);
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd /home/thomas/nutshell && make test`
Expected: all PASS including the 4 new `--- Auto-connect settings ---` entries. Watch for regressions in existing round-trip tests (`test_config_roundtrip_all_fields`) — if one asserts on exact JSON output, update it to include the new keys.

- [ ] **Step 5: Commit**

```bash
cd /home/thomas/nutshell
git add src/config/config.h src/config/loader.c tests/test_config.c tests/runner.c
git commit -m "feat(config): persist auto-connect at startup settings"
```

---

### Task 4: Settings dialog "Startup" section

**Files:**
- Modify: `src/ui/resource.h` (new control IDs)
- Modify: `src/ui/settings.c` (WM_CREATE layout, tooltip table, WM_COMMAND save + checkbox toggle, dialog height)

**Interfaces:**
- Consumes: `Settings.auto_connect`, `Settings.auto_connect_session` (Task 3); existing helpers `make_label`, `make_combo`, `add_tooltip`, `S()` macro, `nd->cfg`, `nd->dpi`.
- Produces: UI only — no new exported symbols.

- [ ] **Step 1: Add control IDs**

`src/ui/resource.h` — after `#define IDC_AI_MD_RENDER 3066`:

```c
#define IDC_AUTOCONNECT_CHECK 3067 /* Settings: auto-connect at startup checkbox */
#define IDC_AUTOCONNECT_COMBO 3068 /* Settings: auto-connect session dropdown */
```

- [ ] **Step 2: Add the Startup section to WM_CREATE**

In `src/ui/settings.c`, locate the SSH "User Idle Timeout" row (ends `y += rh + S(5);  /* extra gap before the AI section */` at line ~522). Insert a Startup section between the idle-timeout row and that final gap line, mirroring the SSH section pattern (etched separator + heading + rows):

```c
        /* Startup section heading: etched line, then "Startup". */
        {
            int sep_y = y + MulDiv(8, nd->dpi, 96);
            int sep_h = MulDiv(2, nd->dpi, 96);
            HWND hSep2 = CreateWindow("STATIC", "",
                WS_VISIBLE | WS_CHILD | SS_ETCHEDHORZ,
                lx, sep_y, (ex + ew) - lx, sep_h,
                hwnd, NULL, NULL, NULL);
            (void)hSep2;
        }
        y += rh;

        {
            int st_h = MulDiv(20, nd->dpi, 96);
            HWND hStLabel = CreateWindow("STATIC", "Startup",
                WS_VISIBLE | WS_CHILD | SS_LEFT,
                lx, y, lw, st_h, hwnd, NULL, NULL, NULL);
            (void)hStLabel;
        }
        y += rh;

        /* Startup: auto-connect checkbox */
        {
            int ac_h = MulDiv(20, nd->dpi, 96);
            HWND hAc = CreateWindow("BUTTON", "Auto-connect at startup",
                WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
                ex, y, ew, ac_h, hwnd, (HMENU)IDC_AUTOCONNECT_CHECK, NULL, NULL);
            SendMessage(hAc, BM_SETCHECK,
                        nd->cfg->settings.auto_connect ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        y += rh;

        /* Startup: session to auto-connect */
        make_label(hwnd, "Session:", lx, y, lw, nd->dpi);
        {
            HWND hAcCombo = make_combo(hwnd, ex, y, ew, S(150),
                                       (HMENU)IDC_AUTOCONNECT_COMBO);
            const char *cur = nd->cfg->settings.auto_connect_session;
            int sel = -1;
            size_t np = vec_size(&nd->cfg->profiles);
            for (size_t pi = 0; pi < np; pi++) {
                const Profile *pr = (const Profile *)vec_get(&nd->cfg->profiles, pi);
                const char *label = (pr->name[0] != '\0') ? pr->name : pr->host;
                int idx = (int)SendMessageA(hAcCombo, CB_ADDSTRING, 0, (LPARAM)label);
                if (sel < 0 && cur[0] != '\0' && _stricmp(cur, label) == 0)
                    sel = idx;
            }
            /* Stored value no longer matches any session: keep it visible
             * (and selectable) so saving without touching it doesn't lose it. */
            if (sel < 0 && cur[0] != '\0')
                sel = (int)SendMessageA(hAcCombo, CB_ADDSTRING, 0, (LPARAM)cur);
            if (sel >= 0)
                SendMessage(hAcCombo, CB_SETCURSEL, (WPARAM)sel, 0);
            EnableWindow(hAcCombo,
                         nd->cfg->settings.auto_connect ? TRUE : FALSE);
        }
        y += rh + S(5);  /* gap before the AI section */
```

Then DELETE the now-duplicated `y += rh + S(5);  /* extra gap before the AI section */` line that previously ended the SSH idle row (the SSH row now ends with a plain `y += rh;` — adjust so exactly one gap remains before "AI API Key"). Check the top of `settings.c` includes `"config.h"` transitively via `settings_dlg.h` (it uses `nd->cfg->settings` already, so it does); `vec_size`/`vec_get` come via `config.h` → `vector.h`.

- [ ] **Step 3: Grow the dialog window**

In `settings_dlg_show` (line ~1200), the dialog height is `MulDiv(910, pdpi, 96)`. Four new rows ≈ `4 × 28 px`. Change `910` to `1022`. If 1022 is taller than the work area on the dev screen it's acceptable — Windows clamps top-level windows to the monitor; note it in the commit message for manual review.

- [ ] **Step 4: Tooltips**

In the `k_tooltips[]` table (line ~275), add before the closing `};`:

```c
    { IDC_AUTOCONNECT_CHECK,
      "Automatically connect to the selected session when Nutshell "
      "starts. Command-line options override this; start with -nc to "
      "skip auto-connect once." },
    { IDC_AUTOCONNECT_COMBO,
      "The saved session to auto-connect to at startup." },
```

- [ ] **Step 5: Checkbox toggles combo enablement**

In `SettingsWndProc`'s `WM_COMMAND` switch (the one containing `case IDOK:` / `case IDCANCEL:` at line ~1145), add a case:

```c
        case IDC_AUTOCONNECT_CHECK:
            EnableWindow(GetDlgItem(hwnd, IDC_AUTOCONNECT_COMBO),
                         IsDlgButtonChecked(hwnd, IDC_AUTOCONNECT_CHECK)
                             == BST_CHECKED ? TRUE : FALSE);
            break;
```

- [ ] **Step 6: Save handler**

In the `IDOK` save block, after the SSH user idle timeout read (line ~1133-1136), before `settings_validate(s);`:

```c
            /* Auto-connect at startup */
            s->auto_connect = (IsDlgButtonChecked(hwnd, IDC_AUTOCONNECT_CHECK)
                                == BST_CHECKED) ? 1 : 0;
            GetDlgItemText(hwnd, IDC_AUTOCONNECT_COMBO,
                           s->auto_connect_session,
                           (int)sizeof(s->auto_connect_session));
```

- [ ] **Step 7: Build (version bump first) and test**

Bump `src/ui/resource.h` `APP_VERSION` `"1.0.67"` → `"1.0.68"` and `APP_VERSION_BINARY` `1,0,67,0` → `1,0,68,0`; bump `README.md` `**Version**:` line to `v1.0.68`.

Run: `cd /home/thomas/nutshell && make clean && make release`
Expected: clean build, no warnings (`-Werror`).
Run: `cd /home/thomas/nutshell && make test`
Expected: all PASS (settings.c is not in the test build; this catches config regressions).

- [ ] **Step 8: Commit**

```bash
cd /home/thomas/nutshell
git add src/ui/resource.h src/ui/settings.c README.md
git commit -m "feat(ui): add auto-connect at startup section to Settings"
```

---

### Task 5: WinMain dispatch + startup connect

**Files:**
- Modify: `src/main.c` (full rewrite of WinMain + helpers)
- Modify: `src/ui/ui.h` (declare `ui_set_startup_action`)
- Modify: `src/ui/window.c` (startup statics, `WM_STARTUP_CONNECT`, post from WM_CREATE)
- Modify: `Makefile` (add `-lshell32` for `CommandLineToArgvW`)

**Interfaces:**
- Consumes: `cli_parse`/`cli_usage_text`/`CliOptions`/`CliAction` (Task 1), `config_find_profile_by_name`/`_by_host` (Task 2), `Settings.auto_connect`/`auto_connect_session` (Task 3), existing `on_session_connect(const Profile *)`, `WM_SHOW_SESSION_MANAGER`, `config_load`, `APP_VERSION`.
- Produces: `void ui_set_startup_action(CliAction action, const char *arg);` in `ui.h`.

- [ ] **Step 1: Makefile — link shell32**

In `LDFLAGS` (Makefile line ~11), add `-lshell32` after `-lshlwapi` (vcpkg libs must stay before Windows system libs; shell32 is a system lib so its position among the other `-l<win32>` entries is fine).

- [ ] **Step 2: ui.h — startup action hook**

Replace `src/ui/ui.h` content:

```c
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
```

- [ ] **Step 3: window.c — startup connect plumbing**

All edits in `src/ui/window.c`:

1. Next to `#define WM_CONN_DONE (WM_USER + 2)` (line ~52):

```c
#define WM_STARTUP_CONNECT      (WM_USER + 3)
```

2. Near the other globals (after `static char g_config_path[MAX_PATH];`, line ~102):

```c
static CliAction g_startup_action = CLI_RUN;
static char g_startup_arg[256];
```

(`cli_args.h` arrives via `ui.h`, which window.c already includes — verify with grep; if it includes `"ui.h"` the type is available.)

3. Implement the setter (near `ui_init`, before it):

```c
void ui_set_startup_action(CliAction action, const char *arg)
{
    g_startup_action = action;
    (void)snprintf(g_startup_arg, sizeof(g_startup_arg), "%s",
                   arg ? arg : "");
}
```

4. At the END of the `WM_CREATE` handler (immediately before its final statement — locate where the handler returns/breaks after all init; the acorn/GDI+ init block area), post the startup message so it runs after window creation completes:

```c
            PostMessage(hwnd, WM_STARTUP_CONNECT, 0, 0);
```

5. Add the handler next to `case WM_SHOW_SESSION_MANAGER:` (line ~2114):

```c
        case WM_STARTUP_CONNECT: {
            const Profile *pr = NULL;
            const char *wanted = NULL;
            if (g_startup_action == CLI_CONNECT_NAME) {
                wanted = g_startup_arg;
                pr = config_find_profile_by_name(g_config, wanted);
            } else if (g_startup_action == CLI_CONNECT_HOST) {
                wanted = g_startup_arg;
                pr = config_find_profile_by_host(g_config, wanted);
            } else if (g_startup_action == CLI_RUN
                       && g_config->settings.auto_connect
                       && g_config->settings.auto_connect_session[0] != '\0') {
                /* Settings dropdown shows host for unnamed sessions, so the
                 * stored string may be either: name first, then host. */
                wanted = g_config->settings.auto_connect_session;
                pr = config_find_profile_by_name(g_config, wanted);
                if (!pr) pr = config_find_profile_by_host(g_config, wanted);
            } else {
                return 0;  /* CLI_RUN_NO_CONNECT, or nothing to do */
            }
            if (pr) {
                on_session_connect(pr);
            } else {
                char nf_text[512];
                (void)snprintf(nf_text, sizeof(nf_text),
                               "Session \"%s\" not found.", wanted);
                MessageBoxA(hwnd, nf_text, "Session Not Found",
                            MB_OK | MB_ICONWARNING);
                PostMessage(hwnd, WM_SHOW_SESSION_MANAGER, 0, 0);
            }
            return 0;
        }
```

- [ ] **Step 4: main.c — parse and dispatch**

Replace `src/main.c` content:

```c
#include <winsock2.h>  /* must precede windows.h */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/cli_args.h"
#include "config/config.h"
#include "ui/ui.h"
#include "ui/icons.h"
#include "ui/resource.h"

/* Print to the parent console when launched from a terminal; fall back
 * to a MessageBox when there is none (double-click, Run dialog). */
static void cli_output(const char *text, const char *title, int is_error)
{
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (freopen("CONOUT$", "w", stdout) != NULL) {
            fputc('\n', stdout);
            fputs(text, stdout);
            fflush(stdout);
        }
        FreeConsole();
    } else {
        MessageBoxA(NULL, text, title,
                    (UINT)(MB_OK | (is_error ? MB_ICONERROR
                                             : MB_ICONINFORMATION)));
    }
}

/* Directory containing the running exe ("" on failure). */
static void exe_dir(char *buf, size_t n)
{
    buf[0] = '\0';
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, (DWORD)sizeof(path));
    if (len == 0 || len >= sizeof(path)) {
        return;
    }
    char *slash = strrchr(path, '\\');
    if (!slash) {
        return;
    }
    *slash = '\0';
    (void)snprintf(buf, n, "%s", path);
}

/* -l / --list: print saved sessions without starting the UI. */
static int run_list(void)
{
    char dir[MAX_PATH];
    char cfg_path[MAX_PATH];
    exe_dir(dir, sizeof(dir));
    if (dir[0] != '\0') {
        (void)snprintf(cfg_path, sizeof(cfg_path), "%s\\" CONFIG_FILENAME, dir);
    } else {
        (void)snprintf(cfg_path, sizeof(cfg_path), CONFIG_FILENAME);
    }

    if (GetFileAttributesA(cfg_path) == INVALID_FILE_ATTRIBUTES) {
        cli_output("No saved sessions.\n", "Nutshell Sessions", 0);
        return 0;
    }

    Config *cfg = config_load(cfg_path);
    if (!cfg) {
        cli_output("Could not parse " CONFIG_FILENAME ".\n",
                   "Nutshell Sessions", 1);
        return 2;
    }

    size_t n = vec_size(&cfg->profiles);
    if (n == 0) {
        cli_output("No saved sessions.\n", "Nutshell Sessions", 0);
        config_free(cfg);
        return 0;
    }

    /* name-or-host label, em-dash, host: one line per profile */
    size_t cap = n * 560u + 32u;
    char *out = malloc(cap);
    if (!out) {
        config_free(cfg);
        return 2;
    }
    size_t pos = 0;
    (void)snprintf(out, cap, "Saved sessions:\n");
    pos = strlen(out);
    for (size_t i = 0; i < n; i++) {
        const Profile *pr = (const Profile *)vec_get(&cfg->profiles, i);
        const char *label = (pr->name[0] != '\0') ? pr->name : pr->host;
        int wrote = snprintf(out + pos, cap - pos, "  %s — %s\n",
                             label, pr->host);
        if (wrote < 0 || (size_t)wrote >= cap - pos) {
            break;
        }
        pos += (size_t)wrote;
    }
    cli_output(out, "Nutshell Sessions", 0);
    free(out);
    config_free(cfg);
    return 0;
}

/* Convert the process command line to a UTF-8 argv. Returns argc; *argv_out
 * must be freed with free_utf8_argv. On failure returns 0 with *argv_out
 * NULL (treated as "no arguments"). */
static int build_utf8_argv(char ***argv_out)
{
    *argv_out = NULL;
    int argc = 0;
    LPWSTR *wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wargv || argc <= 0) {
        if (wargv) LocalFree(wargv);
        return 0;
    }
    char **argv = calloc((size_t)argc, sizeof(char *));
    if (!argv) {
        LocalFree(wargv);
        return 0;
    }
    for (int i = 0; i < argc; i++) {
        int need = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                                       NULL, 0, NULL, NULL);
        if (need <= 0) need = 1;
        argv[i] = calloc((size_t)need, 1u);
        if (argv[i]) {
            (void)WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1,
                                      argv[i], need, NULL, NULL);
        } else {
            argv[i] = calloc(1u, 1u);  /* empty string fallback */
        }
    }
    LocalFree(wargv);
    *argv_out = argv;
    return argc;
}

static void free_utf8_argv(int argc, char **argv)
{
    if (!argv) return;
    for (int i = 0; i < argc; i++) free(argv[i]);
    free(argv);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    char **argv = NULL;
    int argc = build_utf8_argv(&argv);
    CliOptions opts;
    cli_parse(argc, argv, &opts);
    free_utf8_argv(argc, argv);

    switch (opts.action) {
        case CLI_ERROR: {
            char err_text[600];
            (void)snprintf(err_text, sizeof(err_text), "%s\n\n%s",
                           opts.error, cli_usage_text());
            cli_output(err_text, "Nutshell — Usage", 1);
            return 2;
        }
        case CLI_HELP:
            cli_output(cli_usage_text(), "Nutshell — Usage", 0);
            return 0;
        case CLI_VERSION:
            cli_output("Nutshell " APP_VERSION "\n", "Nutshell", 0);
            return 0;
        case CLI_LIST:
            return run_list();
        default:
            break;  /* CLI_RUN / CLI_RUN_NO_CONNECT / connect actions */
    }

    ui_set_startup_action(opts.action, opts.arg);

    /* I-2: initialise WSA once for the process lifetime */
    WSADATA wsadata;
    WSAStartup(MAKEWORD(2, 2), &wsadata);

    ns_icons_init();
    ui_init(hInstance);
    ui_run();
    ns_icons_shutdown();

    WSACleanup();
    return 0;
}
```

Note: the `—` (em-dash) in `run_list` is UTF-8 in a narrow string; if `-Werror` objects or console output garbles, use `"  %s - %s\n"` (plain hyphen) instead — acceptable.

- [ ] **Step 5: Build (version bump first) and test**

Bump versions: `resource.h` → `1.0.69` / `1,0,69,0`; `README.md` → `v1.0.69`.

Run: `cd /home/thomas/nutshell && make clean && make release`
Expected: clean build. If `CommandLineToArgvW` gives a link error, confirm `-lshell32` landed in `LDFLAGS`.
Run: `cd /home/thomas/nutshell && make test`
Expected: all PASS (main.c is not in the test build).

- [ ] **Step 6: Commit**

```bash
cd /home/thomas/nutshell
git add src/main.c src/ui/ui.h src/ui/window.c Makefile src/ui/resource.h README.md
git commit -m "feat: command-line options and startup auto-connect dispatch"
```

---

### Task 6: Docs, final build, manual verification

**Files:**
- Modify: `README.md` (new "Command-Line Options" section)
- Modify: `src/ui/help_guide.c` (GETTING STARTED text)
- Modify: `src/ui/resource.h` (final version bump)

**Interfaces:** none — documentation and release only.

- [ ] **Step 1: README**

In `README.md`, add a new `### Command-Line Options` subsection inside `## User Guide`, after `### Getting Started` (line ~49):

```markdown
### Command-Line Options

Nutshell accepts one option at a time:

| Option | Description |
|---|---|
| `-sn, --session-name <name>` | Connect to the saved session with this name at startup |
| `-h, --host <host>` | Connect to the saved session with this host at startup |
| `-nc, --no-connect` | Start without auto-connecting (overrides the Settings option) |
| `-l, --list` | List saved sessions |
| `-v, --version` | Show version |
| `-?, --help` | Show usage |

Matching is case-insensitive and exact; if no session matches, Nutshell
shows an error and opens the Session Manager.

**One-click shortcut:** create a Windows shortcut with target
`nutshell.exe -sn "<session name>"` and pin it to the taskbar for
instant access to that session. Alternatively, enable **Auto-connect at
startup** in Settings to make a bare launch connect automatically
(`-nc` skips it if needed).

When run from a terminal, `-?`, `-v` and `-l` print to that terminal;
otherwise they appear in a message box.
```

- [ ] **Step 2: In-app User Guide**

In `src/ui/help_guide.c`, `GUIDE_TEXT` (line 24): after the GETTING STARTED block's fingerprint paragraph (ends `asked again unless the server's key changes.\r\n` `"\r\n"` at line ~50), insert:

```c
"QUICK LAUNCH\r\n"
"------------\r\n"
"\r\n"
"Start Nutshell straight into a session from the command line:\r\n"
"\r\n"
"  nutshell.exe -sn <name>   connect to the session with this name\r\n"
"  nutshell.exe -h <host>    connect to the session with this host\r\n"
"  nutshell.exe -nc          start without auto-connecting\r\n"
"  nutshell.exe -l           list saved sessions\r\n"
"  nutshell.exe -v           show version\r\n"
"  nutshell.exe -?           show help\r\n"
"\r\n"
"Pin a shortcut with \"nutshell.exe -sn <name>\" to the taskbar for\r\n"
"one-click access. Or enable \"Auto-connect at startup\" in Settings\r\n"
"to make every launch connect automatically.\r\n"
"\r\n"
```

- [ ] **Step 3: Final build (version bump first)**

Bump versions: `resource.h` → `1.0.70` / `1,0,70,0`; `README.md` → `v1.0.70`.

Run: `cd /home/thomas/nutshell && make clean && make release`
Expected: clean build.
Run: `cd /home/thomas/nutshell && make test`
Expected: all PASS.

- [ ] **Step 4: Commit**

```bash
cd /home/thomas/nutshell
git add README.md src/ui/help_guide.c src/ui/resource.h
git commit -m "docs: document command-line options and auto-connect"
```

- [ ] **Step 5: Manual verification checklist (on Windows; record results in the final report)**

- `nutshell.exe -?` from PowerShell → usage prints in the terminal.
- Double-click a shortcut with `-?` → MessageBox with usage.
- `-v` → version matches `APP_VERSION`; `-l` → lists sessions (terminal and MessageBox variants).
- Shortcut `nutshell.exe -sn <existing name>` → connects straight to the session.
- `-sn nosuch` → "Session not found" box, then Session Manager opens.
- `-sn a -h b` → mutual-exclusion error + usage, app does not start.
- Enable auto-connect in Settings, pick a session, restart bare → connects; restart with `-nc` → does not connect.
- Settings: unticking the checkbox greys the dropdown; stale stored session name still shown selected.
- Launch a second instance while one runs → independent new window.
