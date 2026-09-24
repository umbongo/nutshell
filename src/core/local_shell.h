#ifndef NUTSHELL_LOCAL_SHELL_H
#define NUTSHELL_LOCAL_SHELL_H

/* Which shell a local session runs, and with what command line and
 * environment -- all of it pure logic so it can be tested natively.
 *
 * No Win32 headers here or in local_shell.c: every environment lookup, file
 * existence check and registry read goes through the LocalShellProbe
 * callbacks, which src/ui/local_shell_probe.c fills with the real API calls
 * and tests/test_local_shell.c fills with a table.
 *
 * See docs/superpowers/specs/2026-09-22-local-shell-design.md sections 4
 * and 10 (2026-09-24: busybox removed, installed shells detected instead).
 */

#include <stddef.h>

typedef enum {
    SHELL_NONE       = 0,  /* nothing found -- see LocalShellSpec.error   */
    SHELL_CUSTOM     = 1,  /* the profile's own command line, verbatim    */
    SHELL_PWSH       = 2,  /* PowerShell 7+ (pwsh.exe)                    */
    SHELL_POWERSHELL = 3,  /* Windows PowerShell 5.1 (powershell.exe)     */
    SHELL_GITBASH    = 4,  /* Git for Windows bash                        */
    SHELL_MSYS2      = 5,  /* C:\msys64\usr\bin\bash.exe                  */
    SHELL_CMD        = 6   /* cmd.exe                                     */
} LocalShellKind;

#define LOCAL_SHELL_CMD_MAX       ((size_t)1024)
#define LOCAL_SHELL_PATH_MAX      ((size_t)512)
#define LOCAL_SHELL_ENV_MAX       6
#define LOCAL_SHELL_ENV_NAME_MAX  ((size_t)32)
#define LOCAL_SHELL_ENV_VALUE_MAX ((size_t)32768) /* Windows' own per-variable max */
#define LOCAL_SHELL_DISPLAY_MAX   ((size_t)48)

typedef struct LocalShellEnv {
    char name[LOCAL_SHELL_ENV_NAME_MAX];
    char value[LOCAL_SHELL_ENV_VALUE_MAX];
} LocalShellEnv;

typedef struct LocalShellSpec {
    LocalShellKind kind;
    char command[LOCAL_SHELL_CMD_MAX];  /* full command line, paths quoted   */
    char exe[LOCAL_SHELL_PATH_MAX];     /* the executable alone, unquoted    */
    char dir[LOCAL_SHELL_PATH_MAX];     /* directory holding exe ("" if n/a) */
    LocalShellEnv env[LOCAL_SHELL_ENV_MAX]; /* additions to the parent block */
    int  env_count;
    char error[256];                    /* filled only when kind == SHELL_NONE */
} LocalShellSpec;

/* The callbacks local_shell_resolve() is allowed to ask the world about.
 * Every one may be NULL, in which case it answers "no". */
typedef struct LocalShellProbe {
    /* Non-zero if `path` names an existing file. */
    int (*exists)(void *ctx, const char *path);
    /* Copy environment variable `name` into out; returns 1 when it was set
     * and non-empty, 0 otherwise (out is then left empty). */
    int (*env)(void *ctx, const char *name, char *out, size_t out_size);
    /* Read a registry string value; returns 1 on success. `key` is a full
     * path such as "HKLM\\SOFTWARE\\GitForWindows". */
    int (*registry_string)(void *ctx, const char *key, const char *value,
                           char *out, size_t out_size);
    void *ctx;
} LocalShellProbe;

/* Resolve the shell for a local session, in the fixed order of spec section 10:
 *
 *   1. profile_shell non-empty                    -> SHELL_CUSTOM, verbatim
 *   2. %ProgramFiles%\PowerShell\7\pwsh.exe        -> SHELL_PWSH
 *   3. %SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe
 *                                                  -> SHELL_POWERSHELL
 *   4. Git for Windows (registry InstallPath, else %ProgramFiles%\Git)
 *                                                  -> SHELL_GITBASH
 *   5. C:\msys64\usr\bin\bash.exe                  -> SHELL_MSYS2
 *   6. %SystemRoot%\System32\cmd.exe               -> SHELL_CMD
 *   7. nothing                                     -> SHELL_NONE
 *
 * Also fills out->env with the additions of spec section 10: TERM and NUTSHELL for
 * every kind; HOME, SHELL, and the shell's directory prepended to PATH (or
 * omitted entirely when the parent's own PATH can't be read, so the child
 * simply inherits it unchanged rather than losing it) only for the two bash
 * kinds (SHELL_GITBASH, SHELL_MSYS2); MSYSTEM only for SHELL_MSYS2. Nothing
 * is ever removed from the parent block.
 *
 * A profile_shell whose executable token is a bare name (no '\' or '/') is
 * NOT resolved to an absolute path here -- that is local_shell_resolve_bare()
 * below, run by the caller against the real filesystem right before
 * launching, never against a fake probe's idea of "found".
 *
 * Returns out->kind. Tolerates out == NULL by doing nothing. */
LocalShellKind local_shell_resolve(const char *profile_shell,
                                   const LocalShellProbe *probe,
                                   LocalShellSpec *out);

/* Security: CreateProcess with a NULL lpApplicationName searches, in order,
 * the directory the calling exe loaded from, the current directory, then
 * System32, the Windows directory and PATH -- so a bare custom shell name
 * like "powershell.exe" would run whatever file of that name happened to
 * sit next to nutshell.exe or in the working directory, ahead of the real
 * one. When spec->kind == SHELL_CUSTOM, this resolves spec->exe to a single
 * unambiguous absolute path -- the one local_pty.c then passes as
 * lpApplicationName, so CreateProcess never has to guess at all -- or
 * refuses outright:
 *
 *   - bare (no '\' or '/'): searched ourselves, *only* against
 *     %SystemRoot%\System32, %SystemRoot% and each PATH entry that is
 *     itself an absolute path (a relative PATH entry is skipped, not
 *     searched, for the same reason CWD is never searched) -- the bare
 *     name as given, and, when it has no extension, also with ".exe"
 *     appended. Not found anywhere in that search -> refused.
 *   - has a separator and is relative: always refused. A relative path
 *     resolves against whatever directory the shell happens to start in --
 *     the same hazard the bare-name search avoids by never touching CWD.
 *   - has a separator, is absolute, and the first token was quoted: already
 *     unambiguous (the quotes say exactly where the path ends) -- accepted
 *     as-is.
 *   - has a separator, is absolute, and was NOT quoted: unambiguous only
 *     when the whole command has no space in it at all. Otherwise the
 *     executable path may itself contain a space (e.g. "C:\Program
 *     Files\...\shell.exe") that the naive first-space split can't tell
 *     apart from the boundary between the path and its arguments,
 *     resolved by trying every prefix ending at a space, LONGEST first,
 *     and accepting the first that names a real file -- refused, with a
 *     message suggesting quotes, if none does. (Deliberately the reverse
 *     of CreateProcess's own shortest-first search, which is what lets a
 *     file planted at the shorter guess run instead of the intended one.)
 *
 * On any success that rewrites spec->exe (the bare-name and
 * ambiguous-unquoted-path cases), spec->dir and spec->command are rewritten
 * to match: the absolute, correctly quoted path plus whatever arguments
 * followed in the original command.
 *
 * Every success path then also checks spec->exe against what the automatic
 * search (local_shell_resolve()'s steps 2-6) would itself find on this
 * machine right now; an exact match reclassifies spec->kind to that shell
 * (and re-fills spec->env to match), so a hand-typed path to the detected
 * Git bash or MSYS2 gets the same bash HOME/SHELL/PATH additions and
 * Linux platform-lock eligibility (local_shell_kind_is_posix()) as picking
 * it from the profile editor's dropdown would -- the command line itself is
 * never rewritten by this step, only spec->kind and spec->env.
 *
 * Returns 1 when spec is safe to launch, 0 (spec->error filled, everything
 * else in spec left untouched) when it must be refused. A spec whose kind
 * isn't SHELL_CUSTOM, or NULL, is untouched and returns 1 (nothing of this
 * applies to it). */
int local_shell_resolve_bare(LocalShellSpec *spec, const LocalShellProbe *probe);

/* One shell found on this machine, for the profile editor's "Automatic"
 * dropdown: what it is called and the exact command line
 * local_shell_resolve() would use for it. */
typedef struct LocalShellChoice {
    LocalShellKind kind;
    char display[LOCAL_SHELL_DISPLAY_MAX]; /* "PowerShell 7", "cmd.exe", ... */
    char command[LOCAL_SHELL_CMD_MAX];     /* full, quoted command line     */
} LocalShellChoice;

/* The most local_shell_list_available() can return: pwsh, Windows
 * PowerShell, Git bash, MSYS2, cmd -- one row per automatic-search step
 * that isn't SHELL_CUSTOM or SHELL_NONE. */
#define LOCAL_SHELL_CHOICE_MAX 5

/* Every shell local_shell_resolve()'s automatic search (steps 2-6 above)
 * can find on this machine right now, in that same search order, filling
 * out[0..return value). Never includes SHELL_CUSTOM (there is nothing to
 * detect -- that's whatever the user types) or SHELL_NONE. Tolerates a NULL
 * probe or out (returns 0). */
int local_shell_list_available(const LocalShellProbe *probe,
                               LocalShellChoice *out, int out_max);

/* %LOCALAPPDATA%\Nutshell\runtime, retained for callers that still want it
 * (spec 4.1); no longer consulted by local_shell_resolve() now that the
 * busybox sidecar search is gone. Returns 1 and fills out when LOCALAPPDATA
 * is set, 0 and empties out when it is not. */
int local_shell_runtime_dir(const LocalShellProbe *probe,
                            char *out, size_t out_size);

/* Copy `path` into out, wrapping it in double quotes when it contains a
 * space or a tab (and only then). Returns the length written, 0 on
 * overflow. */
size_t local_shell_quote(const char *path, char *out, size_t out_size);

/* The shell's name as the AI system prompt wants it: "PowerShell", "Git
 * bash", "MSYS2", "cmd", "custom". NULL for SHELL_NONE. */
const char *local_shell_kind_name(LocalShellKind kind);

/* The name for a resolved spec: local_shell_kind_name(spec->kind), except
 * that a SHELL_CUSTOM whose executable's base name is powershell or pwsh
 * (with or without .exe, any letter case, any directory) is "PowerShell",
 * so the model is told which syntax to write. The kind stays SHELL_CUSTOM:
 * this is a name only, and the platform still goes through the banner scan.
 * NULL for a NULL spec or SHELL_NONE. */
const char *local_shell_spec_name(const LocalShellSpec *spec);

/* Non-zero when this kind is a known POSIX-ish shell whose platform the AI
 * panel may pin to Linux without a banner scan (spec section 6): Git bash
 * and MSYS2 yes, custom, PowerShell, cmd and none no. */
int local_shell_kind_is_posix(LocalShellKind kind);

/* Shown in the terminal when the search comes up empty (spec section 10 step 7).
 * Should be unreachable on a live Windows install -- cmd.exe is always
 * there -- but still needs a sentence for when %SystemRoot% itself can't be
 * read. */
extern const char LOCAL_SHELL_NONE_MESSAGE[];

#endif /* NUTSHELL_LOCAL_SHELL_H */
