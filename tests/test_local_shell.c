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

static LocalShellProbe fake_probe(FakeProbeData *d)
{
    LocalShellProbe p;
    memset(&p, 0, sizeof(p));
    p.exists = fake_exists;
    p.env = fake_env;
    p.registry_string = fake_registry;
    p.ctx = d;
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
    /* pwsh, Git bash and MSYS2 are all available too. */
    fake_add_env(&d, "ProgramFiles", "C:\\Program Files");
    fake_add_path(&d, "C:\\Program Files\\PowerShell\\7\\pwsh.exe");
    fake_set_registry(&d, "HKLM\\SOFTWARE\\GitForWindows", "InstallPath", "C:\\Git");
    fake_add_path(&d, "C:\\Git\\bin\\bash.exe");
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    LocalShellProbe p = fake_probe(&d);

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
    LocalShellProbe p = fake_probe(&d);

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
 * PowerShell 7 (step 2)
 * ========================================================================= */

int test_local_shell_pwsh_from_program_files(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "ProgramFiles", "C:\\Program Files");
    fake_add_path(&d, "C:\\Program Files\\PowerShell\\7\\pwsh.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);

    ASSERT_EQ((int)k, (int)SHELL_PWSH);
    ASSERT_STR_EQ(spec.exe, "C:\\Program Files\\PowerShell\\7\\pwsh.exe");
    ASSERT_STR_EQ(spec.dir, "C:\\Program Files\\PowerShell\\7");
    ASSERT_STR_EQ(spec.command,
                 "\"C:\\Program Files\\PowerShell\\7\\pwsh.exe\" -NoLogo");
    ASSERT_STR_EQ(local_shell_kind_name(spec.kind), "PowerShell");
    ASSERT_EQ(local_shell_kind_is_posix(spec.kind), 0);
    TEST_END();
}

int test_local_shell_pwsh_absent_falls_through(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "ProgramFiles", "C:\\Program Files");
    /* pwsh.exe does not exist there. */
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);
    ASSERT_EQ((int)k, (int)SHELL_MSYS2);
    TEST_END();
}

/* =========================================================================
 * Windows PowerShell (step 3)
 * ========================================================================= */

int test_local_shell_powershell_from_system_root(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "SystemRoot", "C:\\Windows");
    fake_add_path(&d,
        "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);

    ASSERT_EQ((int)k, (int)SHELL_POWERSHELL);
    ASSERT_STR_EQ(spec.exe,
        "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    ASSERT_STR_EQ(spec.dir,
        "C:\\Windows\\System32\\WindowsPowerShell\\v1.0");
    ASSERT_STR_EQ(spec.command,
        "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe -NoLogo");
    ASSERT_STR_EQ(local_shell_kind_name(spec.kind), "PowerShell");
    ASSERT_EQ(local_shell_kind_is_posix(spec.kind), 0);
    TEST_END();
}

int test_local_shell_pwsh_wins_over_powershell(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "ProgramFiles", "C:\\Program Files");
    fake_add_path(&d, "C:\\Program Files\\PowerShell\\7\\pwsh.exe");
    fake_add_env(&d, "SystemRoot", "C:\\Windows");
    fake_add_path(&d,
        "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);
    ASSERT_EQ((int)k, (int)SHELL_PWSH);
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
    LocalShellProbe p = fake_probe(&d);

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
    LocalShellProbe p = fake_probe(&d);

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
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);

    ASSERT_TRUE(k != SHELL_GITBASH);
    ASSERT_EQ((int)k, (int)SHELL_NONE);
    TEST_END();
}

/* =========================================================================
 * MSYS2 (step 5)
 * ========================================================================= */

int test_local_shell_msys2_used_when_no_ps_or_gitbash(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    LocalShellProbe p = fake_probe(&d);

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
    fake_set_registry(&d, "HKLM\\SOFTWARE\\GitForWindows", "InstallPath", "C:\\Git");
    fake_add_path(&d, "C:\\Git\\bin\\bash.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);
    ASSERT_EQ((int)k, (int)SHELL_GITBASH);
    for (int i = 0; i < spec.env_count; i++) {
        ASSERT_TRUE(strcmp(spec.env[i].name, "MSYSTEM") != 0);
    }
    TEST_END();
}

/* =========================================================================
 * cmd.exe (step 6) -- the guaranteed-present fallback
 * ========================================================================= */

int test_local_shell_cmd_used_as_last_resort(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "SystemRoot", "C:\\Windows");
    fake_add_path(&d, "C:\\Windows\\System32\\cmd.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);

    ASSERT_EQ((int)k, (int)SHELL_CMD);
    ASSERT_STR_EQ(spec.exe, "C:\\Windows\\System32\\cmd.exe");
    ASSERT_STR_EQ(spec.dir, "C:\\Windows\\System32");
    ASSERT_STR_EQ(spec.command, "C:\\Windows\\System32\\cmd.exe");
    ASSERT_STR_EQ(local_shell_kind_name(spec.kind), "cmd");
    ASSERT_EQ(local_shell_kind_is_posix(spec.kind), 0);
    TEST_END();
}

/* =========================================================================
 * Nothing found (step 7)
 * ========================================================================= */

int test_local_shell_none_when_nothing_found(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    LocalShellProbe p = fake_probe(&d);

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
 * Environment additions -- TERM/NUTSHELL always; HOME/SHELL/PATH-prepend/
 * MSYSTEM only for the two bash kinds (spec 9.3)
 * ========================================================================= */

int test_local_shell_env_term_always_present(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    local_shell_resolve(NULL, &p, &spec);

    ASSERT_TRUE(spec.env_count >= 1);
    ASSERT_STR_EQ(spec.env[0].name, "TERM");
    ASSERT_STR_EQ(spec.env[0].value, "xterm-256color");
    TEST_END();
}

int test_local_shell_env_term_and_nutshell_only_for_powershell(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "ProgramFiles", "C:\\Program Files");
    fake_add_path(&d, "C:\\Program Files\\PowerShell\\7\\pwsh.exe");
    fake_add_env(&d, "USERPROFILE", "C:\\Users\\tom"); /* ignored: not bash */
    fake_add_env(&d, "PATH", "C:\\Windows");            /* ignored: not bash */
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);
    ASSERT_EQ((int)k, (int)SHELL_PWSH);
    ASSERT_EQ(spec.env_count, 2);
    ASSERT_STR_EQ(spec.env[0].name, "TERM");
    ASSERT_STR_EQ(spec.env[1].name, "NUTSHELL");
    for (int i = 0; i < spec.env_count; i++) {
        ASSERT_TRUE(strcmp(spec.env[i].name, "HOME") != 0);
        ASSERT_TRUE(strcmp(spec.env[i].name, "SHELL") != 0);
        ASSERT_TRUE(strcmp(spec.env[i].name, "PATH") != 0);
    }
    TEST_END();
}

int test_local_shell_env_term_and_nutshell_only_for_cmd(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "SystemRoot", "C:\\Windows");
    fake_add_path(&d, "C:\\Windows\\System32\\cmd.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);
    ASSERT_EQ((int)k, (int)SHELL_CMD);
    ASSERT_EQ(spec.env_count, 2);
    TEST_END();
}

int test_local_shell_env_home_present_when_userprofile_set(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    fake_add_env(&d, "USERPROFILE", "C:\\Users\\tom");
    LocalShellProbe p = fake_probe(&d);

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
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    LocalShellProbe p = fake_probe(&d);

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
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    LocalShellProbe p = fake_probe(&d);

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
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    LocalShellProbe p = fake_probe(&d);

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
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    fake_add_env(&d, "PATH", "C:\\Windows;C:\\Windows\\System32");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    local_shell_resolve(NULL, &p, &spec);

    int found = 0;
    for (int i = 0; i < spec.env_count; i++) {
        if (strcmp(spec.env[i].name, "PATH") == 0) {
            ASSERT_STR_EQ(spec.env[i].value,
                         "C:\\msys64\\usr\\bin;C:\\Windows;C:\\Windows\\System32");
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
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    /* No parent PATH set. */
    LocalShellProbe p = fake_probe(&d);

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
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    fake_add_env(&d, "PATH", huge_path);
    LocalShellProbe p = fake_probe(&d);

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
    /* A custom shell with a token that has no directory component -- and
     * no probe, so local_shell_resolve_bare() (run separately by the
     * caller) never gets the chance to give it one either. */
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
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    LocalShellKind k = local_shell_resolve(NULL, &p, &spec);
    ASSERT_EQ((int)k, (int)SHELL_MSYS2);
    ASSERT_EQ(spec.env_count, LOCAL_SHELL_ENV_MAX);
    ASSERT_TRUE(spec.env_count <= LOCAL_SHELL_ENV_MAX);
    TEST_END();
}

/* =========================================================================
 * local_shell_resolve_bare(): a bare custom executable resolved safely,
 * never against the exe's own directory or the current directory
 * ========================================================================= */

int test_local_shell_resolve_bare_noop_for_non_custom(void)
{
    TEST_BEGIN();
    LocalShellSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.kind = SHELL_MSYS2;
    ASSERT_EQ(local_shell_resolve_bare(&spec, NULL), 1);
    TEST_END();
}

int test_local_shell_resolve_bare_noop_when_already_has_path(void)
{
    TEST_BEGIN();
    /* Absolute, unquoted, and the exe itself has no embedded space (only a
     * space delimiting it from " /k") -- unambiguous once the executable is
     * confirmed to exist, so this is a no-op on the already-correct
     * spec->exe. A working probe is required now: telling an unambiguous
     * split from an ambiguous one (a longer path with an embedded space)
     * means checking what actually exists. */
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\Windows\\System32\\cmd.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    local_shell_resolve("C:\\Windows\\System32\\cmd.exe /k", &p, &spec);
    char before[LOCAL_SHELL_PATH_MAX];
    (void)snprintf(before, sizeof(before), "%s", spec.exe);
    ASSERT_EQ(local_shell_resolve_bare(&spec, &p), 1);
    ASSERT_STR_EQ(spec.exe, before);
    ASSERT_STR_EQ(spec.command, "C:\\Windows\\System32\\cmd.exe /k");
    TEST_END();
}

int test_local_shell_resolve_bare_rejects_relative_path_with_separator(void)
{
    TEST_BEGIN();
    /* Has a '/' but no drive letter or UNC prefix: a relative path resolves
     * against whatever directory the shell happens to start in -- the same
     * hazard the bare-name search avoids by never touching CWD -- so it is
     * refused outright, not silently launched from an unexpected place. */
    LocalShellSpec spec;
    local_shell_resolve("tools/shell.exe", NULL, &spec);
    ASSERT_EQ(local_shell_resolve_bare(&spec, NULL), 0);
    ASSERT_STR_EQ(spec.exe, "tools/shell.exe"); /* untouched */
    ASSERT_TRUE(spec.error[0] != '\0');
    TEST_END();
}

int test_local_shell_resolve_bare_rejects_relative_path_quoted(void)
{
    TEST_BEGIN();
    /* Quoting doesn't rescue a relative path -- it only removes the
     * space-splitting ambiguity, not the CWD hazard. */
    LocalShellSpec spec;
    local_shell_resolve("\"tools\\shell.exe\" -i", NULL, &spec);
    ASSERT_EQ(local_shell_resolve_bare(&spec, NULL), 0);
    ASSERT_STR_EQ(spec.exe, "tools\\shell.exe");
    TEST_END();
}

int test_local_shell_resolve_bare_noop_for_quoted_absolute_with_spaces(void)
{
    TEST_BEGIN();
    /* Quoted: the token is exact, spaces and all -- already unambiguous,
     * no probing needed. */
    LocalShellSpec spec;
    local_shell_resolve("\"C:\\Program Files\\My Shell\\shell.exe\" --arg",
                        NULL, &spec);
    ASSERT_EQ(local_shell_resolve_bare(&spec, NULL), 1);
    ASSERT_STR_EQ(spec.exe, "C:\\Program Files\\My Shell\\shell.exe");
    ASSERT_STR_EQ(spec.command,
                 "\"C:\\Program Files\\My Shell\\shell.exe\" --arg");
    TEST_END();
}

int test_local_shell_resolve_bare_unquoted_spaced_picks_longest_existing_prefix(void)
{
    TEST_BEGIN();
    /* Unquoted and absolute, with the real executable path itself
     * containing spaces: first_token_exe_and_dir() only got "C:\Program"
     * (up to the first space). The full path is the longest candidate and
     * is tried first; it exists, so it wins over any shorter guess. */
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\Program Files\\My Shell\\shell.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    local_shell_resolve("C:\\Program Files\\My Shell\\shell.exe --arg",
                        &p, &spec);
    ASSERT_STR_EQ(spec.exe, "C:\\Program"); /* the naive first-space parse */

    ASSERT_EQ(local_shell_resolve_bare(&spec, &p), 1);
    ASSERT_STR_EQ(spec.exe, "C:\\Program Files\\My Shell\\shell.exe");
    ASSERT_STR_EQ(spec.dir, "C:\\Program Files\\My Shell");
    ASSERT_STR_EQ(spec.command,
                 "\"C:\\Program Files\\My Shell\\shell.exe\" --arg");
    TEST_END();
}

int test_local_shell_resolve_bare_unquoted_spaced_appends_exe(void)
{
    TEST_BEGIN();
    /* The longest-prefix scan appends ".exe" the same way a bare-name
     * search does, when the candidate itself has no extension. */
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\Program Files\\My Shell\\shell.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    local_shell_resolve("C:\\Program Files\\My Shell\\shell --arg", &p, &spec);
    ASSERT_EQ(local_shell_resolve_bare(&spec, &p), 1);
    ASSERT_STR_EQ(spec.exe, "C:\\Program Files\\My Shell\\shell.exe");
    TEST_END();
}

int test_local_shell_resolve_bare_unquoted_spaced_falls_back_to_shorter_match(void)
{
    TEST_BEGIN();
    /* Only a shorter prefix exists on this machine (the "real" longer path
     * is absent) -- the scan takes whatever it can find, longest first;
     * this is the documented limit of what's possible without quotes. */
    FakeProbeData d;
    fake_reset(&d);
    fake_add_path(&d, "C:\\Program.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    local_shell_resolve("C:\\Program Files\\My Shell\\shell.exe --arg",
                        &p, &spec);
    ASSERT_EQ(local_shell_resolve_bare(&spec, &p), 1);
    ASSERT_STR_EQ(spec.exe, "C:\\Program.exe");
    ASSERT_STR_EQ(spec.command,
                 "C:\\Program.exe Files\\My Shell\\shell.exe --arg");
    TEST_END();
}

int test_local_shell_resolve_bare_unquoted_spaced_rejected_when_nothing_exists(void)
{
    TEST_BEGIN();
    /* Nothing along the chain of prefixes exists: refused, with a message
     * suggesting the fix (quote the path), rather than guessing. */
    FakeProbeData d;
    fake_reset(&d);
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    local_shell_resolve("C:\\Program Files\\My Shell\\shell.exe --arg",
                        &p, &spec);
    ASSERT_EQ(local_shell_resolve_bare(&spec, &p), 0);
    ASSERT_STR_EQ(spec.exe, "C:\\Program"); /* untouched */
    ASSERT_TRUE(strstr(spec.error, "quote") != NULL);
    TEST_END();
}

int test_local_shell_resolve_bare_unquoted_spaced_null_probe_rejected(void)
{
    TEST_BEGIN();
    /* A NULL probe can confirm nothing exists, so the ambiguous case must
     * refuse rather than guess -- the safe default, not a crash. */
    LocalShellSpec spec;
    local_shell_resolve("C:\\Program Files\\My Shell\\shell.exe --arg",
                        NULL, &spec);
    ASSERT_EQ(local_shell_resolve_bare(&spec, NULL), 0);
    TEST_END();
}

int test_local_shell_resolve_bare_found_in_system32(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "SystemRoot", "C:\\Windows");
    fake_add_path(&d, "C:\\Windows\\System32\\notepad.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    local_shell_resolve("notepad.exe -foo", NULL, &spec);
    ASSERT_EQ(local_shell_resolve_bare(&spec, &p), 1);
    ASSERT_STR_EQ(spec.exe, "C:\\Windows\\System32\\notepad.exe");
    ASSERT_STR_EQ(spec.dir, "C:\\Windows\\System32");
    ASSERT_STR_EQ(spec.command, "C:\\Windows\\System32\\notepad.exe -foo");
    TEST_END();
}

int test_local_shell_resolve_bare_appends_exe_when_no_extension(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "SystemRoot", "C:\\Windows");
    fake_add_path(&d, "C:\\Windows\\System32\\powershell.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    local_shell_resolve("powershell", NULL, &spec);
    ASSERT_EQ(local_shell_resolve_bare(&spec, &p), 1);
    ASSERT_STR_EQ(spec.exe, "C:\\Windows\\System32\\powershell.exe");
    ASSERT_STR_EQ(spec.command, "C:\\Windows\\System32\\powershell.exe");
    TEST_END();
}

int test_local_shell_resolve_bare_found_in_windows_dir(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "SystemRoot", "C:\\Windows");
    fake_add_path(&d, "C:\\Windows\\write.exe"); /* not in System32 */
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    local_shell_resolve("write.exe", NULL, &spec);
    ASSERT_EQ(local_shell_resolve_bare(&spec, &p), 1);
    ASSERT_STR_EQ(spec.exe, "C:\\Windows\\write.exe");
    ASSERT_STR_EQ(spec.dir, "C:\\Windows");
    TEST_END();
}

int test_local_shell_resolve_bare_found_via_absolute_path_entry(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "SystemRoot", "C:\\Windows"); /* not found there */
    fake_add_env(&d, "PATH",
                "C:\\tools;C:\\Program Files\\MyApp");
    fake_add_path(&d, "C:\\Program Files\\MyApp\\myapp.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    local_shell_resolve("myapp.exe", NULL, &spec);
    ASSERT_EQ(local_shell_resolve_bare(&spec, &p), 1);
    ASSERT_STR_EQ(spec.exe, "C:\\Program Files\\MyApp\\myapp.exe");
    ASSERT_STR_EQ(spec.dir, "C:\\Program Files\\MyApp");
    TEST_END();
}

int test_local_shell_resolve_bare_skips_relative_path_entries(void)
{
    TEST_BEGIN();
    /* A relative PATH entry ("." or "sub\dir") is never searched -- that
     * would resolve against the current directory, exactly the hazard this
     * function exists to prevent. The file "exists" there in the fake, but
     * must not be found because the entry that would find it is relative. */
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "SystemRoot", "C:\\Windows");
    fake_add_env(&d, "PATH", ".;tools");
    fake_add_path(&d, ".\\evil.exe");
    fake_add_path(&d, "tools\\evil.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    local_shell_resolve("evil.exe", NULL, &spec);
    ASSERT_EQ(local_shell_resolve_bare(&spec, &p), 0);
    ASSERT_STR_EQ(spec.exe, "evil.exe"); /* untouched */
    TEST_END();
}

int test_local_shell_resolve_bare_never_searches_exe_dir_or_cwd(void)
{
    TEST_BEGIN();
    /* The whole point: a bare name is not looked up the way CreateProcess
     * itself would (exe folder, then CWD, before System32). No probe
     * callback here can even express "beside nutshell.exe" or "the current
     * directory" any more -- there is no exe_dir field left to abuse, and
     * nothing here mentions LOCALAPPDATA either. A malicious
     * "C:\nutshell\powershell.exe" some other program dropped next to the
     * real one must never be picked. */
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "SystemRoot", "C:\\Windows");
    fake_add_path(&d, "C:\\nutshell\\powershell.exe"); /* beside the exe */
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    local_shell_resolve("powershell.exe", NULL, &spec);
    ASSERT_EQ(local_shell_resolve_bare(&spec, &p), 0);
    TEST_END();
}

int test_local_shell_resolve_bare_fails_clearly_when_not_found(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    local_shell_resolve("nosuchtool.exe --flag", NULL, &spec);
    ASSERT_EQ(local_shell_resolve_bare(&spec, &p), 0);
    /* Untouched: still the bare, unresolved (and therefore unsafe to run)
     * form -- the caller is expected to refuse to launch it. */
    ASSERT_STR_EQ(spec.exe, "nosuchtool.exe");
    ASSERT_STR_EQ(spec.command, "nosuchtool.exe --flag");
    TEST_END();
}

int test_local_shell_resolve_bare_null_safety(void)
{
    TEST_BEGIN();
    ASSERT_EQ(local_shell_resolve_bare(NULL, NULL), 1);
    TEST_END();
}

/* =========================================================================
 * Reclassification: a custom command that resolves to exactly one of the
 * automatic search's own detected shells behaves as if "Automatic" had
 * found it -- same env additions, same platform-lock eligibility -- while
 * keeping the user's own command line untouched. Picking a shell from the
 * profile editor's dropdown just writes that shell's exact command line as
 * a custom one, so this is also what makes that path behave correctly.
 * ========================================================================= */

int test_local_shell_resolve_bare_reclassifies_quoted_path_to_gitbash(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_set_registry(&d, "HKLM\\SOFTWARE\\GitForWindows", "InstallPath", "C:\\Git");
    fake_add_path(&d, "C:\\Git\\bin\\bash.exe");
    fake_add_env(&d, "USERPROFILE", "C:\\Users\\tom");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    local_shell_resolve("\"C:\\Git\\bin\\bash.exe\" -i", &p, &spec);
    ASSERT_EQ((int)spec.kind, (int)SHELL_CUSTOM); /* not yet -- bare resolve does it */

    ASSERT_EQ(local_shell_resolve_bare(&spec, &p), 1);
    ASSERT_EQ((int)spec.kind, (int)SHELL_GITBASH);
    ASSERT_EQ(local_shell_kind_is_posix(spec.kind), 1);
    /* The user's own command line and arguments are untouched. */
    ASSERT_STR_EQ(spec.command, "\"C:\\Git\\bin\\bash.exe\" -i");

    /* And it now gets the bash env additions, same as automatic detection
     * would have given it. */
    int found_home = 0, found_shell = 0;
    for (int i = 0; i < spec.env_count; i++) {
        if (strcmp(spec.env[i].name, "HOME") == 0) {
            ASSERT_STR_EQ(spec.env[i].value, "C:\\Users\\tom");
            found_home = 1;
        }
        if (strcmp(spec.env[i].name, "SHELL") == 0) found_shell = 1;
    }
    ASSERT_TRUE(found_home);
    ASSERT_TRUE(found_shell);
    TEST_END();
}

int test_local_shell_resolve_bare_reclassifies_bare_name_to_cmd(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "SystemRoot", "C:\\Windows");
    fake_add_path(&d, "C:\\Windows\\System32\\cmd.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    local_shell_resolve("cmd.exe /k dir", &p, &spec);
    ASSERT_EQ(local_shell_resolve_bare(&spec, &p), 1);
    ASSERT_EQ((int)spec.kind, (int)SHELL_CMD);
    ASSERT_STR_EQ(spec.command, "C:\\Windows\\System32\\cmd.exe /k dir");
    ASSERT_EQ(local_shell_kind_is_posix(spec.kind), 0);
    TEST_END();
}

int test_local_shell_resolve_bare_unmatched_custom_stays_custom(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_set_registry(&d, "HKLM\\SOFTWARE\\GitForWindows", "InstallPath", "C:\\Git");
    fake_add_path(&d, "C:\\Git\\bin\\bash.exe");
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\zsh.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    /* zsh.exe at that path isn't anything the automatic search would ever
     * itself produce (it only looks for bash.exe there), so this must stay
     * SHELL_CUSTOM even though Git bash is also detected on this machine. */
    local_shell_resolve("\"C:\\msys64\\usr\\bin\\zsh.exe\" -l", &p, &spec);
    ASSERT_EQ(local_shell_resolve_bare(&spec, &p), 1);
    ASSERT_EQ((int)spec.kind, (int)SHELL_CUSTOM);
    TEST_END();
}

/* =========================================================================
 * local_shell_list_available(): the profile editor's dropdown contents
 * ========================================================================= */

int test_local_shell_list_available_empty_machine(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    LocalShellProbe p = fake_probe(&d);

    LocalShellChoice choices[LOCAL_SHELL_CHOICE_MAX];
    int n = local_shell_list_available(&p, choices, LOCAL_SHELL_CHOICE_MAX);
    ASSERT_EQ(n, 0);
    TEST_END();
}

int test_local_shell_list_available_everything_installed(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "ProgramFiles", "C:\\Program Files");
    fake_add_path(&d, "C:\\Program Files\\PowerShell\\7\\pwsh.exe");
    fake_add_env(&d, "SystemRoot", "C:\\Windows");
    fake_add_path(&d,
        "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    fake_add_path(&d, "C:\\Windows\\System32\\cmd.exe");
    fake_set_registry(&d, "HKLM\\SOFTWARE\\GitForWindows", "InstallPath", "C:\\Git");
    fake_add_path(&d, "C:\\Git\\bin\\bash.exe");
    fake_add_path(&d, "C:\\msys64\\usr\\bin\\bash.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellChoice choices[LOCAL_SHELL_CHOICE_MAX];
    int n = local_shell_list_available(&p, choices, LOCAL_SHELL_CHOICE_MAX);
    ASSERT_EQ(n, LOCAL_SHELL_CHOICE_MAX);

    /* Same order as the automatic search. */
    ASSERT_EQ((int)choices[0].kind, (int)SHELL_PWSH);
    ASSERT_STR_EQ(choices[0].display, "PowerShell 7");
    ASSERT_STR_EQ(choices[0].command,
                 "\"C:\\Program Files\\PowerShell\\7\\pwsh.exe\" -NoLogo");
    ASSERT_EQ((int)choices[1].kind, (int)SHELL_POWERSHELL);
    ASSERT_STR_EQ(choices[1].display, "Windows PowerShell");
    ASSERT_EQ((int)choices[2].kind, (int)SHELL_GITBASH);
    ASSERT_STR_EQ(choices[2].display, "Git for Windows bash");
    ASSERT_EQ((int)choices[3].kind, (int)SHELL_MSYS2);
    ASSERT_STR_EQ(choices[3].display, "MSYS2 bash");
    ASSERT_EQ((int)choices[4].kind, (int)SHELL_CMD);
    ASSERT_STR_EQ(choices[4].display, "Command Prompt");
    TEST_END();
}

int test_local_shell_list_available_only_cmd(void)
{
    TEST_BEGIN();
    /* The realistic minimum on any live Windows install. */
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "SystemRoot", "C:\\Windows");
    fake_add_path(&d, "C:\\Windows\\System32\\cmd.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellChoice choices[LOCAL_SHELL_CHOICE_MAX];
    int n = local_shell_list_available(&p, choices, LOCAL_SHELL_CHOICE_MAX);
    ASSERT_EQ(n, 1);
    ASSERT_EQ((int)choices[0].kind, (int)SHELL_CMD);
    TEST_END();
}

int test_local_shell_list_available_respects_out_max(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "SystemRoot", "C:\\Windows");
    fake_add_path(&d,
        "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    fake_add_path(&d, "C:\\Windows\\System32\\cmd.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellChoice choices[1];
    int n = local_shell_list_available(&p, choices, 1);
    ASSERT_EQ(n, 1);
    ASSERT_EQ((int)choices[0].kind, (int)SHELL_POWERSHELL);
    TEST_END();
}

int test_local_shell_list_available_null_safety(void)
{
    TEST_BEGIN();
    ASSERT_EQ(local_shell_list_available(NULL, NULL, LOCAL_SHELL_CHOICE_MAX), 0);
    LocalShellChoice choices[LOCAL_SHELL_CHOICE_MAX];
    ASSERT_EQ(local_shell_list_available(NULL, choices, 0), 0);
    /* A NULL probe finds nothing, but must not crash. */
    ASSERT_EQ(local_shell_list_available(NULL, choices, LOCAL_SHELL_CHOICE_MAX), 0);
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
    LocalShellProbe p = fake_probe(&d);

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
    LocalShellProbe p = fake_probe(&d);

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
    LocalShellProbe p = fake_probe(&d);
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
    LocalShellProbe p = fake_probe(&d);
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
    LocalShellProbe p = fake_probe(&d);
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
    ASSERT_STR_EQ(local_shell_kind_name(SHELL_PWSH), "PowerShell");
    ASSERT_STR_EQ(local_shell_kind_name(SHELL_POWERSHELL), "PowerShell");
    ASSERT_STR_EQ(local_shell_kind_name(SHELL_GITBASH), "Git bash");
    ASSERT_STR_EQ(local_shell_kind_name(SHELL_MSYS2), "MSYS2");
    ASSERT_STR_EQ(local_shell_kind_name(SHELL_CMD), "cmd");

    ASSERT_EQ(local_shell_kind_is_posix(SHELL_NONE), 0);
    ASSERT_EQ(local_shell_kind_is_posix(SHELL_CUSTOM), 0);
    ASSERT_EQ(local_shell_kind_is_posix(SHELL_PWSH), 0);
    ASSERT_EQ(local_shell_kind_is_posix(SHELL_POWERSHELL), 0);
    ASSERT_EQ(local_shell_kind_is_posix(SHELL_GITBASH), 1);
    ASSERT_EQ(local_shell_kind_is_posix(SHELL_MSYS2), 1);
    ASSERT_EQ(local_shell_kind_is_posix(SHELL_CMD), 0);
    TEST_END();
}

/* =========================================================================
 * local_shell_spec_name(): the AI prompt's name for a resolved shell
 * ========================================================================= */

static const char *spec_name_of(const char *profile_shell, LocalShellSpec *spec)
{
    (void)local_shell_resolve(profile_shell, NULL, spec);
    return local_shell_spec_name(spec);
}

int test_local_shell_spec_name_powershell(void)
{
    TEST_BEGIN();
    LocalShellSpec spec;
    ASSERT_STR_EQ(spec_name_of("powershell.exe -NoLogo", &spec), "PowerShell");
    /* Still a custom command: only the name changes. */
    ASSERT_EQ((int)spec.kind, (int)SHELL_CUSTOM);
    ASSERT_EQ(local_shell_kind_is_posix(spec.kind), 0);
    ASSERT_STR_EQ(spec_name_of("pwsh.exe -NoLogo", &spec), "PowerShell");
    ASSERT_STR_EQ(spec_name_of("pwsh", &spec), "PowerShell");
    ASSERT_STR_EQ(spec_name_of("powershell", &spec), "PowerShell");
    ASSERT_STR_EQ(spec_name_of("PowerShell.EXE", &spec), "PowerShell");
    ASSERT_STR_EQ(spec_name_of("PWSH.exe -NoProfile", &spec), "PowerShell");
    TEST_END();
}

int test_local_shell_spec_name_powershell_with_path(void)
{
    TEST_BEGIN();
    LocalShellSpec spec;
    ASSERT_STR_EQ(spec_name_of(
        "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe -NoLogo",
        &spec), "PowerShell");
    ASSERT_STR_EQ(spec_name_of(
        "\"C:\\Program Files\\PowerShell\\7\\pwsh.exe\" -NoLogo", &spec),
        "PowerShell");
    ASSERT_STR_EQ(spec_name_of("C:/tools/pwsh.exe", &spec), "PowerShell");
    TEST_END();
}

int test_local_shell_spec_name_natively_detected_powershell(void)
{
    TEST_BEGIN();
    FakeProbeData d;
    fake_reset(&d);
    fake_add_env(&d, "ProgramFiles", "C:\\Program Files");
    fake_add_path(&d, "C:\\Program Files\\PowerShell\\7\\pwsh.exe");
    LocalShellProbe p = fake_probe(&d);

    LocalShellSpec spec;
    local_shell_resolve(NULL, &p, &spec);
    ASSERT_EQ((int)spec.kind, (int)SHELL_PWSH);
    ASSERT_STR_EQ(local_shell_spec_name(&spec), "PowerShell");
    TEST_END();
}

int test_local_shell_spec_name_other_custom_stays_custom(void)
{
    TEST_BEGIN();
    LocalShellSpec spec;
    ASSERT_STR_EQ(spec_name_of("cmd.exe", &spec), "custom");
    ASSERT_STR_EQ(spec_name_of("C:\\msys64\\usr\\bin\\zsh.exe -l", &spec), "custom");
    /* Near misses: the executable's base name must match exactly. */
    ASSERT_STR_EQ(spec_name_of("mypwsh.exe", &spec), "custom");
    ASSERT_STR_EQ(spec_name_of("pwsh2.exe", &spec), "custom");
    ASSERT_STR_EQ(spec_name_of("powershell_ise.exe", &spec), "custom");
    ASSERT_STR_EQ(spec_name_of("C:\\pwsh\\bash.exe", &spec), "custom");
    /* PowerShell as an argument, not the executable, is not PowerShell. */
    ASSERT_STR_EQ(spec_name_of("cmd.exe /k powershell.exe", &spec), "custom");
    TEST_END();
}

int test_local_shell_spec_name_non_custom_and_null(void)
{
    TEST_BEGIN();
    ASSERT_NULL(local_shell_spec_name(NULL));
    LocalShellSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.kind = SHELL_NONE;
    ASSERT_NULL(local_shell_spec_name(&spec));
    spec.kind = SHELL_GITBASH;
    /* A non-custom kind keeps its kind name, whatever exe says. */
    (void)snprintf(spec.exe, sizeof(spec.exe), "%s", "C:\\x\\pwsh.exe");
    ASSERT_STR_EQ(local_shell_spec_name(&spec), "Git bash");
    spec.kind = SHELL_CUSTOM;
    spec.exe[0] = '\0';
    ASSERT_STR_EQ(local_shell_spec_name(&spec), "custom");
    TEST_END();
}
