#include "test_framework.h"
#include "local_shell.h"
#include "resource.h"   /* APP_VERSION */
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * Fake probe: a small configurable table standing in for the real
 * filesystem / environment / registry, per local_shell.h's contract.
 * ========================================================================= */

#define FAKE_MAX 8

typedef struct {
    const char *existing_paths[FAKE_MAX];
    size_t existing_count;

    const char *env_names[FAKE_MAX];
    const char *env_values[FAKE_MAX];
    size_t env_count;

    const char *reg_key;
    const char *reg_value;
    const char *reg_result; /* NULL means the registry lookup fails */
} FakeProbeData;

static void fake_reset(FakeProbeData *d)
{
    memset(d, 0, sizeof(*d));
}

static void fake_add_path(FakeProbeData *d, const char *path)
{
    d->existing_paths[d->existing_count] = path;
    d->existing_count++;
}

static void fake_add_env(FakeProbeData *d, const char *name, const char *value)
{
    d->env_names[d->env_count] = name;
    d->env_values[d->env_count] = value;
    d->env_count++;
}

static void fake_set_registry(FakeProbeData *d, const char *key, const char *value,
                              const char *result)
{
    d->reg_key = key;
    d->reg_value = value;
    d->reg_result = result;
}

static int fake_exists(void *ctx, const char *path)
{
    const FakeProbeData *d = (const FakeProbeData *)ctx;
    if (!path) return 0;
    for (size_t i = 0; i < d->existing_count; i++) {
        if (strcmp(d->existing_paths[i], path) == 0) return 1;
    }
    return 0;
}

static int fake_env(void *ctx, const char *name, char *out, size_t out_size)
{
    const FakeProbeData *d = (const FakeProbeData *)ctx;
    if (out && out_size > 0) out[0] = '\0';
    if (!name) return 0;
    for (size_t i = 0; i < d->env_count; i++) {
        if (strcmp(d->env_names[i], name) == 0) {
            if (!d->env_values[i] || d->env_values[i][0] == '\0') return 0;
            /* Mirror window.c's real probe_env: a value that does not fit
             * the caller's buffer is reported as "not found" (0), not
             * silently truncated -- that's what lets a too-long parent PATH
             * exercise the exact same fill_env() branch as an unset one. */
            if (strlen(d->env_values[i]) >= out_size) return 0;
            (void)snprintf(out, out_size, "%s", d->env_values[i]);
            return 1;
        }
    }
    return 0;
}

static int fake_registry(void *ctx, const char *key, const char *value,
                         char *out, size_t out_size)
{
    const FakeProbeData *d = (const FakeProbeData *)ctx;
    if (out && out_size > 0) out[0] = '\0';
    if (!d->reg_result) return 0;
    if (!d->reg_key || !key || strcmp(d->reg_key, key) != 0) return 0;
    if (!d->reg_value || !value || strcmp(d->reg_value, value) != 0) return 0;
    (void)snprintf(out, out_size, "%s", d->reg_result);
    return 1;
}

static LocalShellProbe fake_probe(FakeProbeData *d, const char *exe_dir)
{
    LocalShellProbe p;
    memset(&p, 0, sizeof(p));
    p.exists = fake_exists;
    p.env = fake_env;
    p.registry_string = fake_registry;
    p.ctx = d;
    p.exe_dir = exe_dir;
    return p;
}

/* =========================================================================
 * Custom shell (step 1) wins over everything else
 * ========================================================================= */

int test_local_shell_custom_wins_over_everything(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    /* Both busybox and Git bash are available too. */
    fake_add_path(&d, "C:\\nutshell\\busybox64.exe");
    fake_set_registry(&d, "HKLM\\SOFTWARE\\GitForWindows", "InstallPath", "C:\\Git");
    fake_add_path(&d, "C:\\Git\\bin\\bash.exe");
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    LocalShellProbe p = fake_probe(&d, "C:\\nutshell");

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve("mycustomshell.exe --flag", &p, &spec);

    ASSERT_EQ((int)k, (int)SHELL_CUSTOM);
    ASSERT_EQ((int)spec.kind, (int)SHELL_CUSTOM);
    ASSERT_STR_EQ(spec.command, "mycustomshell.exe --flag");
    ASSERT_STR_EQ(local_shell_kind_name(spec.kind), "custom");
    ASSERT_EQ(local_shell_kind_is_posix(spec.kind), 0);
    TEST_END();
}

int test_local_shell_custom_command_is_verbatim_not_requoted(void)
{
    TEST_BEGIN();
    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve("  \"C:\\tools\\my shell.exe\" -i  ",
                                           NULL, &spec);
    ASSERT_EQ((int)k, (int)SHELL_CUSTOM);
    ASSERT_STR_EQ(spec.command, "  \"C:\\tools\\my shell.exe\" -i  ");
    TEST_END();
}

int test_local_shell_custom_blank_profile_shell_falls_through(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    LocalShellProbe p = fake_probe(&d, NULL);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve("   ", &p, &spec);
    ASSERT_EQ((int)k, (int)SHELL_MSYS2); /* spaces-only is blank, not custom */
    TEST_END();
}

int test_local_shell_custom_exe_and_dir_from_quoted_token(void)
{
    TEST_BEGIN();
    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(
        "\"C:\\Program Files\\Custom\\shell.exe\" -i", NULL, &spec);
    ASSERT_EQ((int)k, (int)SHELL_CUSTOM);
    ASSERT_STR_EQ(spec.exe, "C:\\Program Files\\Custom\\shell.exe");
    ASSERT_STR_EQ(spec.dir, "C:\\Program Files\\Custom");
    TEST_END();
}

int test_local_shell_custom_exe_without_dir_when_no_backslash(void)
{
    TEST_BEGIN();
    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve("mycmd --flag", NULL, &spec);
    ASSERT_EQ((int)k, (int)SHELL_CUSTOM);
    ASSERT_STR_EQ(spec.exe, "mycmd");
    ASSERT_STR_EQ(spec.dir, "");
    TEST_END();
}

/* =========================================================================
 * Busybox sidecar (steps 2 and 3)
 * ========================================================================= */

int test_local_shell_busybox64_beside_exe_wins(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\nutshell\\busybox64.exe");
    fake_add_path(&d, "C:\\nutshell\\busybox.exe");
    fake_set_registry(&d, "HKLM\\SOFTWARE\\GitForWindows", "InstallPath", "C:\\Git");
    fake_add_path(&d, "C:\\Git\\bin\\bash.exe");
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    LocalShellProbe p = fake_probe(&d, "C:\\nutshell");

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve("", &p, &spec);

    ASSERT_EQ((int)k, (int)SHELL_BUSYBOX);
    ASSERT_STR_EQ(spec.exe, "C:\\nutshell\\busybox64.exe");
    ASSERT_STR_EQ(spec.dir, "C:\\nutshell");
    ASSERT_STR_EQ(spec.command, "C:\\nutshell\\busybox64.exe bash -l");
    ASSERT_STR_EQ(local_shell_kind_name(spec.kind), "busybox");
    ASSERT_EQ(local_shell_kind_is_posix(spec.kind), 1);
    TEST_END();
}

int test_local_shell_busybox_plain_when_64_absent(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\nutshell\\busybox.exe"); /* no busybox64.exe */
    LocalShellProbe p = fake_probe(&d, "C:\\nutshell");

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);

    ASSERT_EQ((int)k, (int)SHELL_BUSYBOX);
    ASSERT_STR_EQ(spec.exe, "C:\\nutshell\\busybox.exe");
    ASSERT_STR_EQ(spec.command, "C:\\nutshell\\busybox.exe bash -l");
    TEST_END();
}

int test_local_shell_busybox_runtime_dir_used_when_exe_dir_absent(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "LOCALAPPDATA", "C:\\Users\\tom\\AppData\\Local");
    fake_add_path(&d, "C:\\Users\\tom\\AppData\\Local\\Nutshell\\runtime\\busybox64.exe");
    LocalShellProbe p = fake_probe(&d, NULL); /* no sidecar directory */

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);

    ASSERT_EQ((int)k, (int)SHELL_BUSYBOX);
    ASSERT_STR_EQ(spec.dir, "C:\\Users\\tom\\AppData\\Local\\Nutshell\\runtime");
    TEST_END();
}

int test_local_shell_busybox_exe_dir_wins_over_runtime_dir(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "LOCALAPPDATA", "C:\\Users\\tom\\AppData\\Local");
    fake_add_path(&d, "C:\\Users\\tom\\AppData\\Local\\Nutshell\\runtime\\busybox64.exe");
    fake_add_path(&d, "C:\\nutshell\\busybox64.exe");
    LocalShellProbe p = fake_probe(&d, "C:\\nutshell");

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);

    ASSERT_EQ((int)k, (int)SHELL_BUSYBOX);
    ASSERT_STR_EQ(spec.dir, "C:\\nutshell");
    TEST_END();
}

int test_local_shell_busybox_runtime_dir_not_used_when_localappdata_unset(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    /* No LOCALAPPDATA entry at all. If the resolver mistakenly searched a
     * literal "\Nutshell\runtime" it would find this. */
    fake_add_path(&d, "\\Nutshell\\runtime\\busybox64.exe");
    LocalShellProbe p = fake_probe(&d, NULL);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);

    ASSERT_TRUE(k != SHELL_BUSYBOX);
    ASSERT_EQ((int)k, (int)SHELL_NONE);
    TEST_END();
}

/* =========================================================================
 * Git for Windows (step 4)
 * ========================================================================= */

int test_local_shell_gitbash_from_registry(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_set_registry(&d, "HKLM\\SOFTWARE\\GitForWindows", "InstallPath", "C:\\Git");
    fake_add_path(&d, "C:\\Git\\bin\\bash.exe");
    LocalShellProbe p = fake_probe(&d, NULL);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);

    ASSERT_EQ((int)k, (int)SHELL_GITBASH);
    ASSERT_STR_EQ(spec.exe, "C:\\Git\\bin\\bash.exe");
    ASSERT_STR_EQ(spec.dir, "C:\\Git\\bin");
    ASSERT_STR_EQ(spec.command, "C:\\Git\\bin\\bash.exe --login -i");
    ASSERT_STR_EQ(local_shell_kind_name(spec.kind), "Git bash");
    ASSERT_EQ(local_shell_kind_is_posix(spec.kind), 1);
    TEST_END();
}

int test_local_shell_gitbash_from_programfiles_when_registry_missing(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    /* No registry answer configured -> fake_registry always fails. */
    fake_add_env(&d, "ProgramFiles", "C:\\Program Files");
    fake_add_path(&d, "C:\\Program Files\\Git\\bin\\bash.exe");
    LocalShellProbe p = fake_probe(&d, NULL);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);

    ASSERT_EQ((int)k, (int)SHELL_GITBASH);
    ASSERT_STR_EQ(spec.exe, "C:\\Program Files\\Git\\bin\\bash.exe");
    ASSERT_STR_EQ(spec.dir, "C:\\Program Files\\Git\\bin");
    ASSERT_STR_EQ(spec.command, "\"C:\\Program Files\\Git\\bin\\bash.exe\" --login -i");
    TEST_END();
}

int test_local_shell_gitbash_not_accepted_when_bash_missing(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_set_registry(&d, "HKLM\\SOFTWARE\\GitForWindows", "InstallPath", "C:\\Git");
    /* bash.exe does NOT exist at C:\Git\bin\bash.exe. */
    LocalShellProbe p = fake_probe(&d, NULL);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);

    ASSERT_TRUE(k != SHELL_GITBASH);
    ASSERT_EQ((int)k, (int)SHELL_NONE);
    TEST_END();
}

/* =========================================================================
 * MSYS2 (step 5) — last resort, only kind that adds MSYSTEM
 * ========================================================================= */

int test_local_shell_msys2_last_resort(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    LocalShellProbe p = fake_probe(&d, NULL);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);

    ASSERT_EQ((int)k, (int)SHELL_MSYS2);
    ASSERT_STR_EQ(spec.exe, "C:\\msys64\\usr\\bin\\bash.exe");
    ASSERT_STR_EQ(spec.dir, "C:\\msys64\\usr\\bin");
    ASSERT_STR_EQ(spec.command, "C:\\msys64\\usr\\bin\\bash.exe --login -i");
    ASSERT_STR_EQ(local_shell_kind_name(spec.kind), "MSYS2");
    ASSERT_EQ(local_shell_kind_is_posix(spec.kind), 1);

    int found_msystem = 0;
    for (int i = 0; i < spec.env_count; i++) {
        if (strcmp(spec.env[i].name, "MSYSTEM") == 0) {
            ASSERT_STR_EQ(spec.env[i].value, "MSYS");
            found_msystem = 1;
        }
    }
    ASSERT_TRUE(found_msystem);
    TEST_END();
}

int test_local_shell_only_msys2_sets_msystem(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\nutshell\\busybox64.exe");
    LocalShellProbe p = fake_probe(&d, "C:\\nutshell");

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);
    ASSERT_EQ((int)k, (int)SHELL_BUSYBOX);
    for (int i = 0; i < spec.env_count; i++) {
        ASSERT_TRUE(strcmp(spec.env[i].name, "MSYSTEM") != 0);
    }
    TEST_END();
}

/* =========================================================================
 * Nothing found (step 6)
 * ========================================================================= */

int test_local_shell_none_when_nothing_found(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    LocalShellProbe p = fake_probe(&d, NULL);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);

    ASSERT_EQ((int)k, (int)SHELL_NONE);
    ASSERT_STR_EQ(spec.command, "");
    ASSERT_STR_EQ(spec.error, LOCAL_SHELL_NONE_MESSAGE);
    ASSERT_NULL(local_shell_kind_name(spec.kind));
    ASSERT_EQ(local_shell_kind_is_posix(spec.kind), 0);
    TEST_END();
}

/* =========================================================================
 * Environment additions (spec 4.3)
 * ========================================================================= */

int test_local_shell_env_term_always_present(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\nutshell\\busybox64.exe");
    LocalShellProbe p = fake_probe(&d, "C:\\nutshell");

    LocalShellSpec spec;
    local_shell_resolve(NULL, &p, &spec);

    ASSERT_TRUE(spec.env_count >= 1);
    ASSERT_STR_EQ(spec.env[0].name, "TERM");
    ASSERT_STR_EQ(spec.env[0].value, "xterm-256color");
    TEST_END();
}

int test_local_shell_env_home_present_when_userprofile_set(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\nutshell\\busybox64.exe");
    fake_add_env(&d, "USERPROFILE", "C:\\Users\\tom");
    LocalShellProbe p = fake_probe(&d, "C:\\nutshell");

    LocalShellSpec spec;
    local_shell_resolve(NULL, &p, &spec);

    ASSERT_STR_EQ(spec.env[1].name, "HOME");
    ASSERT_STR_EQ(spec.env[1].value, "C:\\Users\\tom");
    TEST_END();
}

int test_local_shell_env_home_absent_when_userprofile_unset(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\nutshell\\busybox64.exe");
    LocalShellProbe p = fake_probe(&d, "C:\\nutshell");

    LocalShellSpec spec;
    local_shell_resolve(NULL, &p, &spec);

    for (int i = 0; i < spec.env_count; i++) {
        ASSERT_TRUE(strcmp(spec.env[i].name, "HOME") != 0);
    }
    TEST_END();
}

int test_local_shell_env_shell_equals_exe(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\nutshell\\busybox64.exe");
    LocalShellProbe p = fake_probe(&d, "C:\\nutshell");

    LocalShellSpec spec;
    local_shell_resolve(NULL, &p, &spec);

    int found = 0;
    for (int i = 0; i < spec.env_count; i++) {
        if (strcmp(spec.env[i].name, "SHELL") == 0) {
            ASSERT_STR_EQ(spec.env[i].value, spec.exe);
            found = 1;
        }
    }
    ASSERT_TRUE(found);
    TEST_END();
}

int test_local_shell_env_nutshell_equals_app_version(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\nutshell\\busybox64.exe");
    LocalShellProbe p = fake_probe(&d, "C:\\nutshell");

    LocalShellSpec spec;
    local_shell_resolve(NULL, &p, &spec);

    int found = 0;
    for (int i = 0; i < spec.env_count; i++) {
        if (strcmp(spec.env[i].name, "NUTSHELL") == 0) {
            ASSERT_STR_EQ(spec.env[i].value, APP_VERSION);
            found = 1;
        }
    }
    ASSERT_TRUE(found);
    TEST_END();
}

int test_local_shell_env_path_prepends_dir_keeps_parent(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\nutshell\\busybox64.exe");
    fake_add_env(&d, "PATH", "C:\\Windows;C:\\Windows\\System32");
    LocalShellProbe p = fake_probe(&d, "C:\\nutshell");

    LocalShellSpec spec;
    local_shell_resolve(NULL, &p, &spec);

    int found = 0;
    for (int i = 0; i < spec.env_count; i++) {
        if (strcmp(spec.env[i].name, "PATH") == 0) {
            ASSERT_STR_EQ(spec.env[i].value,
                         "C:\\nutshell;C:\\Windows;C:\\Windows\\System32");
            found = 1;
        }
    }
    ASSERT_TRUE(found);
    TEST_END();
}

int test_local_shell_env_path_absent_when_parent_unavailable(void)
{
    TEST_BEGIN();
    /* When the PATH probe returns 0 (no parent PATH set, or no probe at
     * all), adding a PATH entry with just the shell's directory would
     * REPLACE the child's inherited PATH rather than extend it. So no PATH
     * entry at all is added -- the child inherits the parent block's PATH
     * unchanged (spec 4.3). */
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\nutshell\\busybox64.exe");
    /* No parent PATH set. */
    LocalShellProbe p = fake_probe(&d, "C:\\nutshell");

    LocalShellSpec spec;
    local_shell_resolve(NULL, &p, &spec);

    for (int i = 0; i < spec.env_count; i++) {
        ASSERT_TRUE(strcmp(spec.env[i].name, "PATH") != 0);
    }
    TEST_END();
}

int test_local_shell_env_path_absent_when_parent_does_not_fit(void)
{
    TEST_BEGIN();
    /* A parent PATH longer than LOCAL_SHELL_ENV_VALUE_MAX (32768, Windows'
     * own per-variable max): window.c's real probe_env reports "not found"
     * (0) rather than truncate, same as an unset PATH, so this must land in
     * the same no-PATH-entry branch as the unset case above. */
    static char huge_path[LOCAL_SHELL_ENV_VALUE_MAX + 100];
    size_t i = 0;
    for (; i + 1 < sizeof(huge_path); i++) huge_path[i] = 'A';
    huge_path[i] = '\0';

    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\nutshell\\busybox64.exe");
    fake_add_env(&d, "PATH", huge_path);
    LocalShellProbe p = fake_probe(&d, "C:\\nutshell");

    LocalShellSpec spec;
    local_shell_resolve(NULL, &p, &spec);

    for (int j = 0; j < spec.env_count; j++) {
        ASSERT_TRUE(strcmp(spec.env[j].name, "PATH") != 0);
    }
    TEST_END();
}

int test_local_shell_env_path_absent_when_dir_empty(void)
{
    TEST_BEGIN();
    /* A custom shell with a token that has no directory component. */
    LocalShellSpec spec;
    local_shell_resolve("mycmd", NULL, &spec);
    ASSERT_STR_EQ(spec.dir, "");

    for (int i = 0; i < spec.env_count; i++) {
        ASSERT_TRUE(strcmp(spec.env[i].name, "PATH") != 0);
    }
    TEST_END();
}

int test_local_shell_env_count_never_exceeds_max(void)
{
    TEST_BEGIN();
    ASSERT_EQ(LOCAL_SHELL_ENV_MAX, 6);

    /* Worst case: MSYS2 with USERPROFILE and PATH both set -- TERM, HOME,
     * SHELL, NUTSHELL, PATH, MSYSTEM = exactly 6. */
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    fake_add_env(&d, "USERPROFILE", "C:\\Users\\tom");
    fake_add_env(&d, "PATH", "C:\\Windows");
    LocalShellProbe p = fake_probe(&d, NULL);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);
    ASSERT_EQ((int)k, (int)SHELL_MSYS2);
    ASSERT_EQ(spec.env_count, LOCAL_SHELL_ENV_MAX);
    ASSERT_TRUE(spec.env_count <= LOCAL_SHELL_ENV_MAX);
    TEST_END();
}

/* =========================================================================
 * local_shell_runtime_dir
 * ========================================================================= */

int test_local_shell_runtime_dir_with_localappdata(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "LOCALAPPDATA", "C:\\Users\\tom\\AppData\\Local");
    LocalShellProbe p = fake_probe(&d, NULL);

    char out[LOCAL_SHELL_PATH_MAX];
    int ok = local_shell_runtime_dir(&p, out, sizeof(out));
    ASSERT_EQ(ok, 1);
    ASSERT_STR_EQ(out, "C:\\Users\\tom\\AppData\\Local\\Nutshell\\runtime");
    TEST_END();
}

int test_local_shell_runtime_dir_without_localappdata(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    LocalShellProbe p = fake_probe(&d, NULL);

    char out[LOCAL_SHELL_PATH_MAX];
    (void)snprintf(out, sizeof(out), "%s", "sentinel");
    int ok = local_shell_runtime_dir(&p, out, sizeof(out));
    ASSERT_EQ(ok, 0);
    ASSERT_STR_EQ(out, "");
    TEST_END();
}

int test_local_shell_runtime_dir_null_safety(void)
{
    TEST_BEGIN();
    char out[LOCAL_SHELL_PATH_MAX];
    (void)snprintf(out, sizeof(out), "%s", "sentinel");
    ASSERT_EQ(local_shell_runtime_dir(NULL, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "");

    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "LOCALAPPDATA", "C:\\x");
    LocalShellProbe p = fake_probe(&d, NULL);
    ASSERT_EQ(local_shell_runtime_dir(&p, NULL, 0), 0);
    TEST_END();
}

/* =========================================================================
 * local_shell_quote
 * ========================================================================= */

int test_local_shell_quote_no_space(void)
{
    TEST_BEGIN();
    char out[64];
    size_t n = local_shell_quote("C:\\Tools\\a.exe", out, sizeof(out));
    ASSERT_EQ((int)n, (int)strlen("C:\\Tools\\a.exe"));
    ASSERT_STR_EQ(out, "C:\\Tools\\a.exe");
    TEST_END();
}

int test_local_shell_quote_with_space(void)
{
    TEST_BEGIN();
    char out[64];
    size_t n = local_shell_quote("C:\\Program Files\\a.exe", out, sizeof(out));
    ASSERT_EQ((int)n, (int)strlen("\"C:\\Program Files\\a.exe\""));
    ASSERT_STR_EQ(out, "\"C:\\Program Files\\a.exe\"");
    TEST_END();
}

int test_local_shell_quote_with_tab(void)
{
    TEST_BEGIN();
    char out[64];
    size_t n = local_shell_quote("C:\\Program\tFiles\\a.exe", out, sizeof(out));
    ASSERT_EQ((int)n, (int)strlen("\"C:\\Program\tFiles\\a.exe\""));
    ASSERT_STR_EQ(out, "\"C:\\Program\tFiles\\a.exe\"");
    TEST_END();
}

int test_local_shell_quote_overflow(void)
{
    TEST_BEGIN();
    char out[5];
    out[0] = 'X';
    size_t n = local_shell_quote("C:\\Program Files\\a.exe", out, sizeof(out));
    ASSERT_EQ((int)n, 0);
    ASSERT_STR_EQ(out, "");
    TEST_END();
}

int test_local_shell_quote_null_safety(void)
{
    TEST_BEGIN();
    char out[64];
    out[0] = 'X';
    ASSERT_EQ((int)local_shell_quote(NULL, out, sizeof(out)), 0);
    ASSERT_STR_EQ(out, "");

    ASSERT_EQ((int)local_shell_quote("C:\\a.exe", NULL, 64), 0);
    ASSERT_EQ((int)local_shell_quote("C:\\a.exe", out, 0), 0);
    TEST_END();
}

/* =========================================================================
 * NULL safety of local_shell_resolve
 * ========================================================================= */

int test_local_shell_resolve_null_out_is_safe(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    LocalShellProbe p = fake_probe(&d, NULL);
    LocalShellKind k = local_shell_resolve("something", &p, NULL);
    ASSERT_EQ((int)k, (int)SHELL_NONE);
    TEST_END();
}

int test_local_shell_resolve_null_probe_is_safe(void)
{
    TEST_BEGIN();
    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve("", NULL, &spec);
    ASSERT_EQ((int)k, (int)SHELL_NONE);
    ASSERT_STR_EQ(spec.error, LOCAL_SHELL_NONE_MESSAGE);
    TEST_END();
}

int test_local_shell_resolve_null_callbacks_is_safe(void)
{
    TEST_BEGIN();
    LocalShellProbe p;
    memset(&p, 0, sizeof(p));
    p.exe_dir = "C:\\nutshell"; /* present, but exists/env/registry are NULL */

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);
    ASSERT_EQ((int)k, (int)SHELL_NONE);
    TEST_END();
}

int test_local_shell_resolve_null_profile_shell_is_safe(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    LocalShellProbe p = fake_probe(&d, NULL);
    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);
    ASSERT_EQ((int)k, (int)SHELL_NONE);
    TEST_END();
}

/* =========================================================================
 * local_shell_kind_name / local_shell_kind_is_posix
 * ========================================================================= */

int test_local_shell_kind_name_and_is_posix(void)
{
    TEST_BEGIN();
    ASSERT_NULL(local_shell_kind_name(SHELL_NONE));
    ASSERT_STR_EQ(local_shell_kind_name(SHELL_CUSTOM), "custom");
    ASSERT_STR_EQ(local_shell_kind_name(SHELL_BUSYBOX), "busybox");
    ASSERT_STR_EQ(local_shell_kind_name(SHELL_GITBASH), "Git bash");
    ASSERT_STR_EQ(local_shell_kind_name(SHELL_MSYS2), "MSYS2");

    ASSERT_EQ(local_shell_kind_is_posix(SHELL_NONE), 0);
    ASSERT_EQ(local_shell_kind_is_posix(SHELL_CUSTOM), 0);
    ASSERT_EQ(local_shell_kind_is_posix(SHELL_BUSYBOX), 1);
    ASSERT_EQ(local_shell_kind_is_posix(SHELL_GITBASH), 1);
    ASSERT_EQ(local_shell_kind_is_posix(SHELL_MSYS2), 1);
    TEST_END();
}
