/* Which shell a local session runs, and with what command line and
 * environment. Pure C11: no Win32 headers here. Every environment lookup,
 * file existence check and registry read goes through the LocalShellProbe
 * callbacks (any of which may be NULL, meaning "answers no").
 *
 * See docs/superpowers/specs/2026-09-22-local-shell-design.md section 4.
 */

#include "local_shell.h"
#include "resource.h"   /* APP_VERSION -- -Isrc/ui is on both build paths */

#include <stdio.h>
#include <string.h>

const char LOCAL_SHELL_NONE_MESSAGE[] =
    "No shell found. Put busybox64.exe next to nutshell.exe, install Git "
    "for Windows, or set a shell command in the profile.";

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

/* dir + "\\" + name -> out. Returns 1 on success, 0 if it would not fit. */
static int join_path(const char *dir, const char *name, char *out, size_t out_size)
{
    int n = snprintf(out, out_size, "%s\\%s", dir, name);
    return n >= 0 && (size_t)n < out_size;
}

/* Builds `"<exe_path>" <suffix>` (quoted only if exe_path has a space) into
 * out->command. */
static void build_command(LocalShellSpec *out, const char *exe_path, const char *suffix)
{
    char quoted[LOCAL_SHELL_PATH_MAX + 2];
    size_t qlen = local_shell_quote(exe_path, quoted, sizeof(quoted));
    if (qlen == 0) {
        /* Overflow (a pathologically long path): best effort, unquoted. */
        (void)snprintf(quoted, sizeof(quoted), "%s", exe_path);
    }
    (void)snprintf(out->command, sizeof(out->command), "%s %s", quoted, suffix);
}

static void env_add(LocalShellSpec *out, const char *name, const char *value)
{
    if (out->env_count >= LOCAL_SHELL_ENV_MAX) return; /* never happens per spec */
    LocalShellEnv *e = &out->env[out->env_count];
    (void)snprintf(e->name, sizeof(e->name), "%s", name);
    (void)snprintf(e->value, sizeof(e->value), "%s", value);
    out->env_count++;
}

/* Spec 4.3, in the fixed order the tests index by. */
static void fill_env(LocalShellSpec *out, const LocalShellProbe *probe)
{
    out->env_count = 0;

    env_add(out, "TERM", "xterm-256color");

    char userprofile[LOCAL_SHELL_PATH_MAX];
    if (probe_env(probe, "USERPROFILE", userprofile, sizeof(userprofile))) {
        env_add(out, "HOME", userprofile);
    }

    if (out->exe[0] != '\0') {
        env_add(out, "SHELL", out->exe);
    }

    env_add(out, "NUTSHELL", APP_VERSION);

    if (out->dir[0] != '\0') {
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

/* ---- the search steps of spec 4.2 ---------------------------------------- */

static int try_busybox(const LocalShellProbe *probe, const char *dir, LocalShellSpec *out)
{
    static const char *names[2] = { "busybox64.exe", "busybox.exe" };
    for (size_t i = 0; i < 2; i++) {
        char full[LOCAL_SHELL_PATH_MAX];
        if (!join_path(dir, names[i], full, sizeof(full))) continue;
        if (probe_exists(probe, full)) {
            out->kind = SHELL_BUSYBOX;
            (void)snprintf(out->exe, sizeof(out->exe), "%s", full);
            (void)snprintf(out->dir, sizeof(out->dir), "%s", dir);
            build_command(out, full, "bash -l");
            return 1;
        }
    }
    return 0;
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

/* ---- public API ----------------------------------------------------------- */

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

    /* Step 2: busybox64.exe / busybox.exe next to nutshell.exe. */
    if (probe && probe->exe_dir && probe->exe_dir[0] != '\0') {
        if (try_busybox(probe, probe->exe_dir, out)) {
            fill_env(out, probe);
            return out->kind;
        }
    }

    /* Step 3: the same two names in the runtime directory. */
    char runtime_dir[LOCAL_SHELL_PATH_MAX];
    if (local_shell_runtime_dir(probe, runtime_dir, sizeof(runtime_dir))) {
        if (try_busybox(probe, runtime_dir, out)) {
            fill_env(out, probe);
            return out->kind;
        }
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

    /* Step 6: nothing found. */
    out->kind = SHELL_NONE;
    (void)snprintf(out->error, sizeof(out->error), "%s", LOCAL_SHELL_NONE_MESSAGE);
    return out->kind;
}

const char *local_shell_kind_name(LocalShellKind kind)
{
    switch (kind) {
        case SHELL_BUSYBOX: return "busybox";
        case SHELL_GITBASH:  return "Git bash";
        case SHELL_MSYS2:    return "MSYS2";
        case SHELL_CUSTOM:   return "custom";
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
        case SHELL_BUSYBOX:
        case SHELL_GITBASH:
        case SHELL_MSYS2:
            return 1;
        case SHELL_CUSTOM:
        case SHELL_NONE:
        default:
            return 0;
    }
}
