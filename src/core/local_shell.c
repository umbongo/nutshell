/* Which shell a local session runs, and with what command line and
 * environment. Pure C11: no Win32 headers here. Every environment lookup,
 * file existence check and registry read goes through the LocalShellProbe
 * callbacks (any of which may be NULL, meaning "answers no").
 *
 * See docs/superpowers/specs/2026-09-22-local-shell-design.md sections 4
 * and 10 (2026-09-24: busybox removed, installed shells detected instead).
 */

#include "local_shell.h"
#include "resource.h"   /* APP_VERSION -- -Isrc/ui is on both build paths */

#include <stdio.h>
#include <string.h>

const char LOCAL_SHELL_NONE_MESSAGE[] =
    "No shell found. Install PowerShell, Git for Windows, or set a shell "
    "command in the profile.";

/* ---- probe wrappers: NULL probe or NULL callback both mean "no" --------- */

static int probe_exists(const LocalShellProbe *probe, const char *path)
{
    if (!probe || !probe->exists) return 0;
    return probe->exists(probe->ctx, path) ? 1 : 0;
}

static int probe_env(const LocalShellProbe *probe, const char *name,
                     char *out, size_t out_size)
{
    if (out && out_size > 0) out[0] = '\0';
    if (!probe || !probe->env) return 0;
    return probe->env(probe->ctx, name, out, out_size) ? 1 : 0;
}

static int probe_registry(const LocalShellProbe *probe, const char *key,
                          const char *value, char *out, size_t out_size)
{
    if (out && out_size > 0) out[0] = '\0';
    if (!probe || !probe->registry_string) return 0;
    return probe->registry_string(probe->ctx, key, value, out, out_size) ? 1 : 0;
}

/* ---- local_shell_quote --------------------------------------------------- */

size_t local_shell_quote(const char *path, char *out, size_t out_size)
{
    if (out && out_size > 0) out[0] = '\0';
    if (!path || !out || out_size == 0) return 0;

    int needs_quotes = strpbrk(path, " \t") != NULL;
    size_t len = strlen(path);
    size_t needed = needs_quotes ? len + 2 : len; /* +2 for the quote pair */

    if (needed >= out_size) { /* no room for `needed` chars plus the NUL */
        out[0] = '\0';
        return 0;
    }

    if (needs_quotes) {
        out[0] = '"';
        memcpy(out + 1, path, len);
        out[len + 1] = '"';
        out[len + 2] = '\0';
    } else {
        memcpy(out, path, len);
        out[len] = '\0';
    }
    return needed;
}

/* ---- helpers used only within this file ---------------------------------- */

/* Non-zero when `s` is NULL or made up only of spaces. */
static int shell_is_blank(const char *s)
{
    if (!s) return 1;
    for (; *s != '\0'; s++) {
        if (*s != ' ') return 0;
    }
    return 1;
}

/* Splits the first token off `s` (quoted with '"' or space-delimited),
 * strips one surrounding quote pair, and copies the unquoted token into
 * exe_out plus its directory (everything before the last '\\') into
 * dir_out. Either output is left empty ("") when it cannot be worked out
 * or would not fit. */
static void first_token_exe_and_dir(const char *s, char *exe_out, size_t exe_size,
                                    char *dir_out, size_t dir_size)
{
    if (exe_size > 0) exe_out[0] = '\0';
    if (dir_size > 0) dir_out[0] = '\0';
    if (!s || exe_size == 0) return;

    while (*s == ' ') s++;
    if (*s == '\0') return;

    const char *start;
    const char *end;
    if (*s == '"') {
        start = s + 1;
        const char *close = strchr(start, '"');
        end = close ? close : start + strlen(start);
    } else {
        start = s;
        const char *sp = strchr(start, ' ');
        end = sp ? sp : start + strlen(start);
    }

    size_t len = (size_t)(end - start);
    if (len == 0 || len >= exe_size) return;
    memcpy(exe_out, start, len);
    exe_out[len] = '\0';

    const char *last_slash = strrchr(exe_out, '\\');
    if (last_slash && dir_size > 0) {
        size_t dlen = (size_t)(last_slash - exe_out);
        if (dlen < dir_size) {
            memcpy(dir_out, exe_out, dlen);
            dir_out[dlen] = '\0';
        }
    }
}

/* Whatever follows the first token of `s` (quoted or space-delimited),
 * leading spaces trimmed -- "" when there is nothing after it. Used by
 * local_shell_resolve_bare() to keep the user's arguments when it rewrites
 * the executable to an absolute path. */
static void command_suffix(const char *s, char *out, size_t out_size)
{
    if (out_size > 0) out[0] = '\0';
    if (!s) return;

    while (*s == ' ') s++;
    if (*s == '\0') return;

    const char *rest;
    if (*s == '"') {
        const char *close = strchr(s + 1, '"');
        rest = close ? close + 1 : s + strlen(s);
    } else {
        const char *sp = strchr(s, ' ');
        rest = sp ? sp : s + strlen(s);
    }
    while (*rest == ' ') rest++;
    (void)snprintf(out, out_size, "%s", rest);
}

/* dir + "\\" + name -> out. Returns 1 on success, 0 if it would not fit. */
static int join_path(const char *dir, const char *name, char *out, size_t out_size)
{
    int n = snprintf(out, out_size, "%s\\%s", dir, name);
    return n >= 0 && (size_t)n < out_size;
}

/* Builds `"<exe_path>" <suffix>` (quoted only if exe_path has a space) into
 * out->command. An empty suffix ("") leaves no trailing space. */
static void build_command(LocalShellSpec *out, const char *exe_path, const char *suffix)
{
    char quoted[LOCAL_SHELL_PATH_MAX + 2];
    size_t qlen = local_shell_quote(exe_path, quoted, sizeof(quoted));
    if (qlen == 0) {
        /* Overflow (a pathologically long path): best effort, unquoted. */
        (void)snprintf(quoted, sizeof(quoted), "%s", exe_path);
    }
    if (suffix && suffix[0] != '\0') {
        (void)snprintf(out->command, sizeof(out->command), "%s %s", quoted, suffix);
    } else {
        (void)snprintf(out->command, sizeof(out->command), "%s", quoted);
    }
}

static void env_add(LocalShellSpec *out, const char *name, const char *value)
{
    if (out->env_count >= LOCAL_SHELL_ENV_MAX) return; /* never happens per spec */
    LocalShellEnv *e = &out->env[out->env_count];
    (void)snprintf(e->name, sizeof(e->name), "%s", name);
    (void)snprintf(e->value, sizeof(e->value), "%s", value);
    out->env_count++;
}

/* Spec section 10, in the fixed order the tests index by. HOME, SHELL, the PATH
 * prepend and MSYSTEM are bash-specific -- meaningless to PowerShell or
 * cmd.exe, and no longer assumed of a custom command either, now that a
 * bare custom executable is resolved against the same system directories
 * those two use (local_shell_resolve_bare()). TERM and NUTSHELL are always
 * added. */
static void fill_env(LocalShellSpec *out, const LocalShellProbe *probe)
{
    out->env_count = 0;

    env_add(out, "TERM", "xterm-256color");

    int is_bash = (out->kind == SHELL_GITBASH || out->kind == SHELL_MSYS2);

    if (is_bash) {
        char userprofile[LOCAL_SHELL_PATH_MAX];
        if (probe_env(probe, "USERPROFILE", userprofile, sizeof(userprofile))) {
            env_add(out, "HOME", userprofile);
        }

        if (out->exe[0] != '\0') {
            env_add(out, "SHELL", out->exe);
        }
    }

    env_add(out, "NUTSHELL", APP_VERSION);

    if (is_bash && out->dir[0] != '\0') {
        char parent_path[LOCAL_SHELL_ENV_VALUE_MAX];
        int have_parent = probe_env(probe, "PATH", parent_path, sizeof(parent_path));
        /* When the parent PATH can't be read -- probe->env is NULL, PATH is
         * unset, or (a real possibility now that the buffer is 32768 bytes,
         * Windows' own per-variable max) it simply doesn't fit -- adding a
         * PATH entry here would REPLACE the child's inherited PATH with just
         * out->dir, losing everything else on it. Leaving PATH out of
         * spec->env entirely means the child inherits the parent block's
         * PATH unchanged, which is the safe fallback (spec 4.3). */
        if (have_parent) {
            char combined[LOCAL_SHELL_ENV_VALUE_MAX];
            /* out->dir is at most LOCAL_SHELL_PATH_MAX (512), far under
             * LOCAL_SHELL_ENV_VALUE_MAX, so the prepended directory always
             * survives in full; only a very long parent PATH's tail is ever
             * truncated by snprintf -- deliberately (spec 4.3), not a bug,
             * so the truncation warning is suppressed for this call. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
            (void)snprintf(combined, sizeof(combined), "%s;%s", out->dir, parent_path);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
            env_add(out, "PATH", combined);
        }
    }

    if (out->kind == SHELL_MSYS2) {
        env_add(out, "MSYSTEM", "MSYS");
    }
}

/* ---- the search steps of spec section 10 ----------------------------------------
 *
 * Each try_* has the same shape -- (probe, out) -> 1 on success, spec filled
 * in; 0 otherwise, spec untouched -- so local_shell_resolve() and
 * local_shell_list_available() can both walk them uniformly. */

static int try_pwsh(const LocalShellProbe *probe, LocalShellSpec *out)
{
    char program_files[LOCAL_SHELL_PATH_MAX];
    if (!probe_env(probe, "ProgramFiles", program_files, sizeof(program_files)))
        return 0;

    char ps_dir[LOCAL_SHELL_PATH_MAX];
    char exe_path[LOCAL_SHELL_PATH_MAX];
    if (!join_path(program_files, "PowerShell\\7", ps_dir, sizeof(ps_dir))) return 0;
    if (!join_path(ps_dir, "pwsh.exe", exe_path, sizeof(exe_path))) return 0;
    if (!probe_exists(probe, exe_path)) return 0;

    out->kind = SHELL_PWSH;
    (void)snprintf(out->exe, sizeof(out->exe), "%s", exe_path);
    (void)snprintf(out->dir, sizeof(out->dir), "%s", ps_dir);
    build_command(out, exe_path, "-NoLogo");
    return 1;
}

static int try_powershell(const LocalShellProbe *probe, LocalShellSpec *out)
{
    char system_root[LOCAL_SHELL_PATH_MAX];
    if (!probe_env(probe, "SystemRoot", system_root, sizeof(system_root)))
        return 0;

    char ps_dir[LOCAL_SHELL_PATH_MAX];
    char exe_path[LOCAL_SHELL_PATH_MAX];
    if (!join_path(system_root, "System32\\WindowsPowerShell\\v1.0",
                   ps_dir, sizeof(ps_dir)))
        return 0;
    if (!join_path(ps_dir, "powershell.exe", exe_path, sizeof(exe_path))) return 0;
    if (!probe_exists(probe, exe_path)) return 0;

    out->kind = SHELL_POWERSHELL;
    (void)snprintf(out->exe, sizeof(out->exe), "%s", exe_path);
    (void)snprintf(out->dir, sizeof(out->dir), "%s", ps_dir);
    build_command(out, exe_path, "-NoLogo");
    return 1;
}

static int try_gitbash(const LocalShellProbe *probe, LocalShellSpec *out)
{
    char install[LOCAL_SHELL_PATH_MAX];
    int have_install = probe_registry(probe, "HKLM\\SOFTWARE\\GitForWindows",
                                      "InstallPath", install, sizeof(install));
    if (!have_install) {
        char program_files[LOCAL_SHELL_PATH_MAX];
        if (probe_env(probe, "ProgramFiles", program_files, sizeof(program_files))) {
            have_install = join_path(program_files, "Git", install, sizeof(install));
        }
    }
    if (!have_install) return 0;

    char bash_path[LOCAL_SHELL_PATH_MAX];
    char bin_dir[LOCAL_SHELL_PATH_MAX];
    if (!join_path(install, "bin", bin_dir, sizeof(bin_dir))) return 0;
    if (!join_path(bin_dir, "bash.exe", bash_path, sizeof(bash_path))) return 0;

    if (!probe_exists(probe, bash_path)) return 0;

    out->kind = SHELL_GITBASH;
    (void)snprintf(out->exe, sizeof(out->exe), "%s", bash_path);
    (void)snprintf(out->dir, sizeof(out->dir), "%s", bin_dir);
    build_command(out, bash_path, "--login -i");
    return 1;
}

static int try_msys2(const LocalShellProbe *probe, LocalShellSpec *out)
{
    static const char path[] = "C:\\msys64\\usr\\bin\\bash.exe";
    static const char dir[]  = "C:\\msys64\\usr\\bin";

    if (!probe_exists(probe, path)) return 0;

    out->kind = SHELL_MSYS2;
    (void)snprintf(out->exe, sizeof(out->exe), "%s", path);
    (void)snprintf(out->dir, sizeof(out->dir), "%s", dir);
    build_command(out, path, "--login -i");
    return 1;
}

static int try_cmd(const LocalShellProbe *probe, LocalShellSpec *out)
{
    char system_root[LOCAL_SHELL_PATH_MAX];
    if (!probe_env(probe, "SystemRoot", system_root, sizeof(system_root)))
        return 0;

    char sys32[LOCAL_SHELL_PATH_MAX];
    char exe_path[LOCAL_SHELL_PATH_MAX];
    if (!join_path(system_root, "System32", sys32, sizeof(sys32))) return 0;
    if (!join_path(sys32, "cmd.exe", exe_path, sizeof(exe_path))) return 0;
    if (!probe_exists(probe, exe_path)) return 0;

    out->kind = SHELL_CMD;
    (void)snprintf(out->exe, sizeof(out->exe), "%s", exe_path);
    (void)snprintf(out->dir, sizeof(out->dir), "%s", sys32);
    build_command(out, exe_path, "");
    return 1;
}

/* ---- local_shell_resolve_bare(): bare custom executable, resolved safely - */

/* Non-zero when `s[0..len)` (not NUL-terminated beyond len) is an absolute
 * Windows path: a drive letter ("C:\\..." or "C:/...") or a UNC prefix
 * ("\\\\server\\..."). A relative PATH entry never matches -- it would
 * resolve against the current directory, exactly the hazard
 * local_shell_resolve_bare() exists to avoid. */
static int path_is_absolute(const char *s, size_t len)
{
    if (len >= 3 &&
        ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) &&
        s[1] == ':' && (s[2] == '\\' || s[2] == '/')) {
        return 1;
    }
    if (len >= 2 && s[0] == '\\' && s[1] == '\\') return 1;
    return 0;
}

static int name_has_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot != NULL;
}

/* Tries dir\name, and, when name has no extension, dir\name.exe -- the
 * second so a bare "powershell" resolves the same way CreateProcess itself
 * would have appended ".exe" to it. */
static int try_dir_for_bare(const LocalShellProbe *probe, const char *dir,
                            const char *name, char *out, size_t out_size)
{
    if (!dir || dir[0] == '\0') return 0;

    char full[LOCAL_SHELL_PATH_MAX];
    if (join_path(dir, name, full, sizeof(full)) && probe_exists(probe, full)) {
        (void)snprintf(out, out_size, "%s", full);
        return 1;
    }

    if (!name_has_extension(name)) {
        char with_exe[LOCAL_SHELL_PATH_MAX];
        int n = snprintf(with_exe, sizeof(with_exe), "%s.exe", name);
        if (n > 0 && (size_t)n < sizeof(with_exe) &&
            join_path(dir, with_exe, full, sizeof(full)) &&
            probe_exists(probe, full)) {
            (void)snprintf(out, out_size, "%s", full);
            return 1;
        }
    }
    return 0;
}

/* System32, then the Windows directory, then each absolute PATH entry --
 * never the exe's own directory, never the current directory. */
static int find_bare_exe(const char *name, const LocalShellProbe *probe,
                         char *out, size_t out_size)
{
    if (out_size > 0) out[0] = '\0';
    if (!name || name[0] == '\0') return 0;

    char system_root[LOCAL_SHELL_PATH_MAX];
    if (probe_env(probe, "SystemRoot", system_root, sizeof(system_root))) {
        char sys32[LOCAL_SHELL_PATH_MAX];
        if (join_path(system_root, "System32", sys32, sizeof(sys32)) &&
            try_dir_for_bare(probe, sys32, name, out, out_size)) {
            return 1;
        }
        if (try_dir_for_bare(probe, system_root, name, out, out_size)) {
            return 1;
        }
    }

    char path_val[LOCAL_SHELL_ENV_VALUE_MAX];
    if (probe_env(probe, "PATH", path_val, sizeof(path_val))) {
        const char *p = path_val;
        while (*p != '\0') {
            const char *semi = strchr(p, ';');
            size_t len = semi ? (size_t)(semi - p) : strlen(p);
            if (len > 0 && len < LOCAL_SHELL_PATH_MAX && path_is_absolute(p, len)) {
                char dir[LOCAL_SHELL_PATH_MAX];
                memcpy(dir, p, len);
                dir[len] = '\0';
                if (try_dir_for_bare(probe, dir, name, out, out_size)) {
                    return 1;
                }
            }
            p += len;
            if (*p == ';') p++;
        }
    }

    return 0;
}

int local_shell_resolve_bare(LocalShellSpec *spec, const LocalShellProbe *probe)
{
    if (!spec) return 1;
    if (spec->kind != SHELL_CUSTOM) return 1;
    if (spec->exe[0] == '\0') return 1;
    if (strpbrk(spec->exe, "\\/") != NULL) return 1; /* already has a path */

    char resolved[LOCAL_SHELL_PATH_MAX];
    if (!find_bare_exe(spec->exe, probe, resolved, sizeof(resolved))) {
        return 0;
    }

    char suffix[LOCAL_SHELL_CMD_MAX];
    command_suffix(spec->command, suffix, sizeof(suffix));

    (void)snprintf(spec->exe, sizeof(spec->exe), "%s", resolved);
    spec->dir[0] = '\0';
    const char *last_slash = strrchr(resolved, '\\');
    if (last_slash) {
        size_t dlen = (size_t)(last_slash - resolved);
        if (dlen < sizeof(spec->dir)) {
            memcpy(spec->dir, resolved, dlen);
            spec->dir[dlen] = '\0';
        }
    }
    build_command(spec, resolved, suffix);
    return 1;
}

/* ---- public API ------------------------------------------------------------ */

int local_shell_runtime_dir(const LocalShellProbe *probe, char *out, size_t out_size)
{
    if (out && out_size > 0) out[0] = '\0';
    if (!probe || !out || out_size == 0) return 0;

    char localappdata[LOCAL_SHELL_PATH_MAX];
    if (!probe_env(probe, "LOCALAPPDATA", localappdata, sizeof(localappdata))) {
        return 0;
    }

    int n = snprintf(out, out_size, "%s\\Nutshell\\runtime", localappdata);
    if (n < 0 || (size_t)n >= out_size) {
        out[0] = '\0';
        return 0;
    }
    return 1;
}

LocalShellKind local_shell_resolve(const char *profile_shell,
                                   const LocalShellProbe *probe,
                                   LocalShellSpec *out)
{
    if (!out) return SHELL_NONE;
    memset(out, 0, sizeof(*out));

    if (!shell_is_blank(profile_shell)) {
        out->kind = SHELL_CUSTOM;
        (void)snprintf(out->command, sizeof(out->command), "%s", profile_shell);
        first_token_exe_and_dir(profile_shell, out->exe, sizeof(out->exe),
                                out->dir, sizeof(out->dir));
        fill_env(out, probe);
        return out->kind;
    }

    /* Step 2: PowerShell 7. */
    if (try_pwsh(probe, out)) {
        fill_env(out, probe);
        return out->kind;
    }

    /* Step 3: Windows PowerShell. */
    if (try_powershell(probe, out)) {
        fill_env(out, probe);
        return out->kind;
    }

    /* Step 4: Git for Windows. */
    if (try_gitbash(probe, out)) {
        fill_env(out, probe);
        return out->kind;
    }

    /* Step 5: MSYS2. */
    if (try_msys2(probe, out)) {
        fill_env(out, probe);
        return out->kind;
    }

    /* Step 6: cmd.exe. */
    if (try_cmd(probe, out)) {
        fill_env(out, probe);
        return out->kind;
    }

    /* Step 7: nothing found. */
    out->kind = SHELL_NONE;
    (void)snprintf(out->error, sizeof(out->error), "%s", LOCAL_SHELL_NONE_MESSAGE);
    return out->kind;
}

typedef int (*ShellTryFn)(const LocalShellProbe *, LocalShellSpec *);

static const struct {
    ShellTryFn  try_fn;
    const char *display;
} SHELL_STEPS[] = {
    { try_pwsh,       "PowerShell 7" },
    { try_powershell, "Windows PowerShell" },
    { try_gitbash,    "Git for Windows bash" },
    { try_msys2,      "MSYS2 bash" },
    { try_cmd,        "Command Prompt" },
};

int local_shell_list_available(const LocalShellProbe *probe,
                               LocalShellChoice *out, int out_max)
{
    if (!out || out_max <= 0) return 0;

    int n = 0;
    size_t step_count = sizeof(SHELL_STEPS) / sizeof(SHELL_STEPS[0]);
    for (size_t i = 0; i < step_count && n < out_max; i++) {
        LocalShellSpec spec;
        memset(&spec, 0, sizeof(spec));
        if (SHELL_STEPS[i].try_fn(probe, &spec)) {
            out[n].kind = spec.kind;
            (void)snprintf(out[n].display, sizeof(out[n].display),
                           "%s", SHELL_STEPS[i].display);
            (void)snprintf(out[n].command, sizeof(out[n].command),
                           "%s", spec.command);
            n++;
        }
    }
    return n;
}

const char *local_shell_kind_name(LocalShellKind kind)
{
    switch (kind) {
        case SHELL_PWSH:
        case SHELL_POWERSHELL: return "PowerShell";
        case SHELL_GITBASH:    return "Git bash";
        case SHELL_MSYS2:      return "MSYS2";
        case SHELL_CMD:        return "cmd";
        case SHELL_CUSTOM:     return "custom";
        case SHELL_NONE:
        default:
            return NULL;
    }
}

/* Case-insensitive ASCII equality of s[0..len) and the whole of lit. */
static int base_eq_ci(const char *s, size_t len, const char *lit)
{
    size_t i = 0;
    for (; i < len && lit[i] != '\0'; i++) {
        int a = (unsigned char)s[i];
        int b = (unsigned char)lit[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return 0;
    }
    return i == len && lit[i] == '\0';
}

const char *local_shell_spec_name(const LocalShellSpec *spec)
{
    if (!spec) return NULL;
    if (spec->kind == SHELL_CUSTOM) {
        /* Base name of the executable: after the last '\' or '/'. */
        const char *base = spec->exe;
        for (const char *c = spec->exe; *c; c++)
            if (*c == '\\' || *c == '/') base = c + 1;
        size_t len = strlen(base);
        if (len > 4 && base_eq_ci(base + len - 4, 4, ".exe")) len -= 4;
        if (base_eq_ci(base, len, "powershell") || base_eq_ci(base, len, "pwsh"))
            return "PowerShell";
    }
    return local_shell_kind_name(spec->kind);
}

int local_shell_kind_is_posix(LocalShellKind kind)
{
    switch (kind) {
        case SHELL_GITBASH:
        case SHELL_MSYS2:
            return 1;
        case SHELL_CUSTOM:
        case SHELL_PWSH:
        case SHELL_POWERSHELL:
        case SHELL_CMD:
        case SHELL_NONE:
        default:
            return 0;
    }
}
