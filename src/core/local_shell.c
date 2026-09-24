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
#include <stdlib.h>
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

/* M2: canonicalise `path` via the probe when it can, else just copy it
 * verbatim -- either way `out` always ends up filled, so callers never need
 * to branch on the return value; they only need two comparably-normalised
 * strings to compare. */
static int probe_normalize(const LocalShellProbe *probe, const char *path,
                           char *out, size_t out_size)
{
    if (out && out_size > 0) (void)snprintf(out, out_size, "%s", path ? path : "");
    if (!probe || !probe->normalize_path || !path) return 0;
    char tmp[LOCAL_SHELL_PATH_MAX];
    if (!probe->normalize_path(probe->ctx, path, tmp, sizeof(tmp))) return 0;
    (void)snprintf(out, out_size, "%s", tmp);
    return 1;
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

/* If a custom command's resolved executable is exactly the path one of the
 * automatic search's own steps would itself find on this machine right now,
 * adopt that shell's kind -- and so its env additions (fill_env()) and its
 * eligibility for the POSIX platform lock (local_shell_kind_is_posix()) --
 * while leaving the user's own command line and arguments untouched. Typing
 * the exact path of the detected Git bash by hand must behave identically
 * to picking "Git bash" from the profile editor's dropdown; without this, a
 * hand-typed match would stay SHELL_CUSTOM and silently lose the bash
 * HOME/SHELL/PATH additions and the Linux platform lock. Only called once
 * spec->exe is already an absolute, resolved path (the end of
 * local_shell_resolve_bare()'s job) -- comparing against a still-bare or
 * still-relative name would be meaningless. A detected LocalShellSpec is
 * ~200 KB (LOCAL_SHELL_ENV_MAX * LOCAL_SHELL_ENV_VALUE_MAX dominates) so it
 * is heap-allocated once and reused across the up-to-5 steps, not put on
 * the caller's stack (window.c's start_local_shell() runs on the UI
 * thread, same concern as local_shell_list_available()). Best effort: an
 * allocation failure just leaves the spec as SHELL_CUSTOM. */
/* M2: base name of `exe` -- after the last '\' or '/' -- with a trailing
 * ".exe" stripped, as a (pointer, length) pair rather than a copy. */
static const char *base_name_len(const char *exe, size_t *len_out)
{
    const char *base = exe;
    for (const char *c = exe; *c; c++)
        if (*c == '\\' || *c == '/') base = c + 1;
    size_t len = strlen(base);
    if (len > 4 && base_eq_ci(base + len - 4, 4, ".exe")) len -= 4;
    *len_out = len;
    return base;
}

/* M2: powershell.exe, pwsh.exe and cmd.exe are names Windows itself gives
 * to exactly one thing each -- unlike "bash.exe", which WSL's own launcher
 * also uses for something that is emphatically not Git bash or MSYS2, so
 * bash recognition stays exact-path-only (reclassify_if_known_shell()'s own
 * loop, above). A custom command whose executable is positively one of
 * these three, wherever it actually lives (a portable copy, a second
 * install, WindowsApps) -- not just at the one path the automatic search
 * itself would have found -- is reclassified the same as an exact path
 * match would be. Returns 1 (spec->kind set) or 0 (spec->kind untouched). */
static int reclassify_by_base_name(LocalShellSpec *spec)
{
    size_t len;
    const char *base = base_name_len(spec->exe, &len);

    if (base_eq_ci(base, len, "pwsh"))       { spec->kind = SHELL_PWSH;       return 1; }
    if (base_eq_ci(base, len, "powershell")) { spec->kind = SHELL_POWERSHELL; return 1; }
    if (base_eq_ci(base, len, "cmd"))        { spec->kind = SHELL_CMD;        return 1; }
    return 0;
}

static void reclassify_if_known_shell(LocalShellSpec *spec, const LocalShellProbe *probe)
{
    if (!spec || spec->kind != SHELL_CUSTOM || spec->exe[0] == '\0') return;

    char spec_norm[LOCAL_SHELL_PATH_MAX];
    (void)probe_normalize(probe, spec->exe, spec_norm, sizeof(spec_norm));

    LocalShellSpec *detected = (LocalShellSpec *)malloc(sizeof(*detected));
    if (detected) {
        size_t step_count = sizeof(SHELL_STEPS) / sizeof(SHELL_STEPS[0]);
        for (size_t i = 0; i < step_count; i++) {
            memset(detected, 0, sizeof(*detected));
            if (!SHELL_STEPS[i].try_fn(probe, detected)) continue;

            char detected_norm[LOCAL_SHELL_PATH_MAX];
            (void)probe_normalize(probe, detected->exe, detected_norm,
                                  sizeof(detected_norm));
            if (base_eq_ci(spec_norm, strlen(spec_norm), detected_norm)) {
                spec->kind = detected->kind;
                fill_env(spec, probe);
                free(detected);
                return;
            }
        }
        free(detected);
    }

    /* No exact (normalised) path match to a detected install -- M2's base
     * name fallback, powershell/pwsh/cmd only. */
    if (reclassify_by_base_name(spec)) {
        fill_env(spec, probe);
    }
}

/* ---- local_shell_resolve_bare(): a custom command's executable, resolved
 * to an absolute path safely ------------------------------------------------
 *
 * Three shapes of spec->exe reach here (spec->kind == SHELL_CUSTOM):
 *
 *   - bare (no '\' or '/'): searched against System32, the Windows
 *     directory and absolute PATH entries only -- never the exe's own
 *     directory or the current directory (see find_bare_exe() below).
 *   - has a separator and is relative: refused outright. A relative path
 *     resolves against whatever directory the shell happens to start in,
 *     the same hazard a bare name search avoids by never touching CWD.
 *   - has a separator and is absolute: safe to use once it names a single,
 *     unambiguous file. A quoted token already does; an UNQUOTED one that
 *     contains a space does not, because first_token_exe_and_dir() (which
 *     built spec->exe) stopped at the first space and so may hold only a
 *     prefix of the real path (e.g. "C:\Program" out of "C:\Program
 *     Files\...\shell.exe"). That case is re-resolved against the raw
 *     command line by trying every prefix ending at a space, LONGEST
 *     first, and accepting the first one that names a real file -- see
 *     resolve_unquoted_spaced() below for why longest-first and not
 *     CreateProcess's own shortest-first search. */

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

/* Non-zero when `cmd`'s first non-space character is a double quote. */
static int cmd_first_token_quoted(const char *cmd)
{
    if (!cmd) return 0;
    while (*cmd == ' ') cmd++;
    return *cmd == '"';
}

/* cmd[0..end) as a candidate executable path: tried as-is, and, when it has
 * no extension, with ".exe" appended -- the same two tries
 * try_dir_for_bare() makes against dir+name, here against a full path
 * lifted straight out of the raw command line. Copies whichever form
 * exists into exe_out and returns 1 on success. */
static int candidate_exe_exists(const LocalShellProbe *probe, const char *cmd,
                                size_t end, char *exe_out, size_t exe_size)
{
    if (end == 0 || end >= LOCAL_SHELL_PATH_MAX) return 0;

    char candidate[LOCAL_SHELL_PATH_MAX];
    memcpy(candidate, cmd, end);
    candidate[end] = '\0';

    if (probe_exists(probe, candidate)) {
        (void)snprintf(exe_out, exe_size, "%s", candidate);
        return 1;
    }
    if (!name_has_extension(candidate)) {
        char with_exe[LOCAL_SHELL_PATH_MAX];
        int n = snprintf(with_exe, sizeof(with_exe), "%s.exe", candidate);
        if (n > 0 && (size_t)n < sizeof(with_exe) && probe_exists(probe, with_exe)) {
            (void)snprintf(exe_out, exe_size, "%s", with_exe);
            return 1;
        }
    }
    return 0;
}

/* H4: an unquoted custom command line already known to start with an
 * absolute path and to contain at least one space: the executable and its
 * arguments cannot be told apart by punctuation alone. This tries only the
 * whole (trimmed) command as the executable -- covering a path whose own
 * name has embedded spaces and no separate arguments at all -- and, that
 * failing, exactly ONE split: the substring up to the LAST space in the
 * command, covering the common case of a single trailing argument (e.g.
 * "C:\Program Files\My Shell\shell.exe --arg", or an unquoted exe with no
 * embedded space of its own plus one argument, like "cmd.exe /k"). Either
 * candidate may also be tried with ".exe" appended, same as
 * candidate_exe_exists() always allows.
 *
 * Nothing shorter than that single split is EVER tried. The previous
 * version of this function kept shrinking the candidate leftward, one
 * space at a time, all the way back toward the first space in the whole
 * command if nothing longer existed -- which is exactly how a file
 * planted at that naive, shortest guess (e.g. "C:\Program.exe", sitting
 * where a first-space split alone would have landed, ahead of the intended
 * "C:\Program Files\...\shell.exe") could get run instead of the one the
 * user meant, the classic unquoted-path hazard this whole function exists
 * to avoid. Finding nothing at either of the two candidates is a refusal,
 * never a fall back to a shorter, riskier guess -- the caller shows the
 * user local_shell.h's suggestion to quote the path. A command with more
 * than one trailing argument (or a trailing argument that itself contains
 * a space) is refused the same way: quoting the executable path is the
 * only way to make it unambiguous. */
static int resolve_unquoted_spaced(const LocalShellProbe *probe, const char *cmd,
                                   char *exe_out, size_t exe_size,
                                   char *suffix_out, size_t suffix_size)
{
    size_t len = strlen(cmd);
    while (len > 0 && cmd[len - 1] == ' ') len--;
    if (len == 0) return 0;

    /* Candidate 1: the whole trimmed command, unsplit. */
    if (candidate_exe_exists(probe, cmd, len, exe_out, exe_size)) {
        if (suffix_size > 0) suffix_out[0] = '\0';
        return 1;
    }

    /* Candidate 2, and the last one tried: up to the single last space in
     * the command. */
    size_t i = len;
    while (i > 0 && cmd[i - 1] != ' ') i--;
    if (i == 0) return 0; /* no space at all: nothing left to try */
    size_t end = i - 1;
    while (end > 0 && cmd[end - 1] == ' ') end--; /* collapse repeated spaces */
    if (end == 0) return 0;

    if (candidate_exe_exists(probe, cmd, end, exe_out, exe_size)) {
        const char *rest = cmd + end;
        while (*rest == ' ') rest++;
        (void)snprintf(suffix_out, suffix_size, "%s", rest);
        return 1;
    }

    return 0;
}

/* Rewrites spec->exe/dir/command to `resolved_exe` plus `suffix`, shared by
 * both rewrite paths (a bare name found on the search, or an unquoted
 * absolute path whose real split point was found). */
static void adopt_resolved_exe(LocalShellSpec *spec, const char *resolved_exe,
                               const char *suffix)
{
    (void)snprintf(spec->exe, sizeof(spec->exe), "%s", resolved_exe);
    spec->dir[0] = '\0';
    const char *last_slash = strrchr(resolved_exe, '\\');
    if (last_slash) {
        size_t dlen = (size_t)(last_slash - resolved_exe);
        if (dlen < sizeof(spec->dir)) {
            memcpy(spec->dir, resolved_exe, dlen);
            spec->dir[dlen] = '\0';
        }
    }
    build_command(spec, resolved_exe, suffix);
}

int local_shell_resolve_bare(LocalShellSpec *spec, const LocalShellProbe *probe)
{
    if (!spec) return 1;
    if (spec->kind != SHELL_CUSTOM) return 1;
    if (spec->exe[0] == '\0') {
        /* M1: an empty exe is not "nothing to do" -- it means
         * first_token_exe_and_dir() could not work out an executable at
         * all (the command was blank after quote-stripping, or its first
         * token was too long to fit LOCAL_SHELL_PATH_MAX). Returning 1 here
         * used to hand the caller a spec that looked resolved but whose
         * exe was empty; local_pty_open() then fell back to a NULL
         * lpApplicationName, handing CreateProcess back exactly the
         * PATH/CWD-searching guess this whole function exists to remove.
         * Refuse instead. */
        (void)snprintf(spec->error, sizeof(spec->error), "%s",
            "Could not find an executable in the shell command (it may be "
            "empty, or its first token too long).");
        return 0;
    }

    if (strpbrk(spec->exe, "\\/") == NULL) {
        /* Bare name: search System32, the Windows directory, then each
         * absolute PATH entry -- never the exe's own directory or CWD. */
        char resolved[LOCAL_SHELL_PATH_MAX];
        if (!find_bare_exe(spec->exe, probe, resolved, sizeof(resolved))) {
            /* spec->exe (up to LOCAL_SHELL_PATH_MAX, 512) can exceed what's
             * left of spec->error (256) once the fixed wording is
             * accounted for -- a merely-cosmetic truncation of an already
             * pathological path, not a bug, so -Wformat-truncation's
             * warning is suppressed for this call (same rationale as
             * fill_env()'s PATH combine above). */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
            (void)snprintf(spec->error, sizeof(spec->error),
                "Could not find \"%s\" in System32, the Windows directory, "
                "or PATH.", spec->exe);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
            return 0;
        }
        char suffix[LOCAL_SHELL_CMD_MAX];
        command_suffix(spec->command, suffix, sizeof(suffix));
        adopt_resolved_exe(spec, resolved, suffix);
        reclassify_if_known_shell(spec, probe);
        return 1;
    }

    /* Has a separator: a relative path resolves against whatever directory
     * the shell happens to start in -- the same hazard a bare name search
     * avoids by never touching CWD -- so it is refused outright rather
     * than silently launched from an unexpected place. */
    if (!path_is_absolute(spec->exe, strlen(spec->exe))) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
        (void)snprintf(spec->error, sizeof(spec->error),
            "\"%s\" is a relative path. Use an absolute path, or a bare "
            "executable name (searched in System32, the Windows directory "
            "and PATH).", spec->exe);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        return 0;
    }

    if (cmd_first_token_quoted(spec->command)) {
        /* Quoted: the token is exact, spaces and all -- no ambiguity. */
        reclassify_if_known_shell(spec, probe);
        return 1;
    }

    const char *cmd = spec->command;
    while (*cmd == ' ') cmd++;

    if (strchr(cmd, ' ') == NULL) {
        /* Unquoted, absolute, and not one space anywhere in the whole
         * command: spec->exe already equals the entire command, so there
         * is nothing to split and nothing ambiguous about it. */
        reclassify_if_known_shell(spec, probe);
        return 1;
    }

    char resolved_exe[LOCAL_SHELL_PATH_MAX];
    char suffix[LOCAL_SHELL_CMD_MAX];
    if (!resolve_unquoted_spaced(probe, cmd, resolved_exe, sizeof(resolved_exe),
                                 suffix, sizeof(suffix))) {
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
        (void)snprintf(spec->error, sizeof(spec->error),
            "Could not tell where the executable path ends and the "
            "arguments begin in \"%s\" -- quote the executable path.", cmd);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        return 0;
    }

    adopt_resolved_exe(spec, resolved_exe, suffix);
    reclassify_if_known_shell(spec, probe);
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

/* A LocalShellSpec is ~200 KB (LOCAL_SHELL_ENV_MAX *
 * LOCAL_SHELL_ENV_VALUE_MAX dominates); this is called from
 * session_manager.c's WM_INITDIALOG, a dialog procedure, so it is
 * heap-allocated once and reused across the up-to-5 steps rather than
 * living on the dialog thread's stack. */
int local_shell_list_available(const LocalShellProbe *probe,
                               LocalShellChoice *out, int out_max)
{
    if (!out || out_max <= 0) return 0;

    LocalShellSpec *spec = (LocalShellSpec *)malloc(sizeof(*spec));
    if (!spec) return 0;

    int n = 0;
    size_t step_count = sizeof(SHELL_STEPS) / sizeof(SHELL_STEPS[0]);
    for (size_t i = 0; i < step_count && n < out_max; i++) {
        memset(spec, 0, sizeof(*spec));
        if (SHELL_STEPS[i].try_fn(probe, spec)) {
            out[n].kind = spec->kind;
            (void)snprintf(out[n].display, sizeof(out[n].display),
                           "%s", SHELL_STEPS[i].display);
            (void)snprintf(out[n].command, sizeof(out[n].command),
                           "%s", spec->command);
            n++;
        }
    }
    free(spec);
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

/* H3: fixes window.c's start_local_shell(), which used to test the local
 * `kind` variable captured from local_shell_resolve() -- BEFORE
 * local_shell_resolve_bare() had a chance to correct it (reclassify a
 * custom command to SHELL_GITBASH/MSYS2/PWSH/POWERSHELL/CMD, M2) -- rather
 * than spec->kind afterward. A custom command pointing at PowerShell or
 * cmd by an unreclassified path never got the strict lock at all: it kept
 * whatever `kind` had been before resolve_bare ran (always SHELL_CUSTOM for
 * a non-blank profile shell), which fell through to neither branch and
 * stayed on the looser auto-scan. */
LocalShellPlatformLock local_shell_platform_lock(const LocalShellSpec *spec)
{
    if (!spec) return LOCAL_SHELL_LOCK_NONE;

    if (local_shell_kind_is_posix(spec->kind)) {
        return LOCAL_SHELL_LOCK_LINUX;
    }
    if (spec->kind == SHELL_NONE) {
        return LOCAL_SHELL_LOCK_NONE;
    }
    /* SHELL_PWSH, SHELL_POWERSHELL, SHELL_CMD, or SHELL_CUSTOM (anything
     * that reached here as SHELL_CUSTOM was not reclassified to a known
     * POSIX shell by local_shell_resolve_bare() -- reclassify_by_base_name()
     * already turned a positively-identified PowerShell/cmd custom command
     * into one of the three kinds above, so nothing further needs checking
     * here): none of these has a Windows ruleset to loosen to, and an
     * unreclassified custom command could be anything, so every one of them
     * is locked strict rather than left on the auto-scan by default. */
    return LOCAL_SHELL_LOCK_STRICT;
}
