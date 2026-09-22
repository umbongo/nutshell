#ifndef NUTSHELL_LOCAL_SHELL_H
#define NUTSHELL_LOCAL_SHELL_H

/* Which shell a local session runs, and with what command line and
 * environment -- all of it pure logic so it can be tested natively.
 *
 * No Win32 headers here or in local_shell.c: every environment lookup, file
 * existence check and registry read goes through the LocalShellProbe
 * callbacks, which src/ui/local_pty.c fills with the real API calls and
 * tests/test_local_shell.c fills with a table.
 *
 * See docs/superpowers/specs/2026-09-22-local-shell-design.md section 4.
 */

#include <stddef.h>

typedef enum {
    SHELL_NONE    = 0,  /* nothing found -- see LocalShellSpec.error */
    SHELL_CUSTOM  = 1,  /* the profile's own command line, verbatim  */
    SHELL_BUSYBOX = 2,  /* busybox64.exe / busybox.exe sidecar       */
    SHELL_GITBASH = 3,  /* Git for Windows bash                      */
    SHELL_MSYS2   = 4   /* C:\msys64\usr\bin\bash.exe                */
} LocalShellKind;

#define LOCAL_SHELL_CMD_MAX       ((size_t)1024)
#define LOCAL_SHELL_PATH_MAX      ((size_t)512)
#define LOCAL_SHELL_ENV_MAX       6
#define LOCAL_SHELL_ENV_NAME_MAX  ((size_t)32)
#define LOCAL_SHELL_ENV_VALUE_MAX ((size_t)32768) /* Windows' own per-variable max */

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
    /* Directory holding nutshell.exe, for the sidecar search. May be NULL. */
    const char *exe_dir;
} LocalShellProbe;

/* Resolve the shell for a local session, in the fixed order of spec 4.2:
 *
 *   1. profile_shell non-empty        -> SHELL_CUSTOM, used verbatim
 *   2. busybox64.exe / busybox.exe next to nutshell.exe -> SHELL_BUSYBOX
 *   3. the same in %LOCALAPPDATA%\Nutshell\runtime       -> SHELL_BUSYBOX
 *   4. Git for Windows (registry InstallPath, else %ProgramFiles%\Git)
 *                                                        -> SHELL_GITBASH
 *   5. C:\msys64\usr\bin\bash.exe                         -> SHELL_MSYS2
 *   6. nothing                                            -> SHELL_NONE
 *
 * Also fills out->env with the additions of spec 4.3: TERM, HOME, SHELL,
 * NUTSHELL, PATH (the shell's directory prepended to the parent's -- or
 * omitted entirely when the parent's own PATH can't be read, so the child
 * simply inherits it unchanged rather than losing it) and, for MSYS2 only,
 * MSYSTEM. Nothing is ever removed from the parent block.
 *
 * Returns out->kind. Tolerates out == NULL by doing nothing. */
LocalShellKind local_shell_resolve(const char *profile_shell,
                                   const LocalShellProbe *probe,
                                   LocalShellSpec *out);

/* %LOCALAPPDATA%\Nutshell\runtime (spec 4.1). Returns 1 and fills out when
 * LOCALAPPDATA is set, 0 and empties out when it is not -- the directory is
 * then simply absent from the search and nothing is written. */
int local_shell_runtime_dir(const LocalShellProbe *probe,
                            char *out, size_t out_size);

/* Copy `path` into out, wrapping it in double quotes when it contains a
 * space or a tab (and only then). Returns the length written, 0 on
 * overflow. */
size_t local_shell_quote(const char *path, char *out, size_t out_size);

/* The shell's name as the AI system prompt wants it: "busybox", "Git bash",
 * "MSYS2", "custom". NULL for SHELL_NONE. */
const char *local_shell_kind_name(LocalShellKind kind);

/* Non-zero when this kind is a known POSIX-ish shell whose platform the AI
 * panel may pin to Linux without a banner scan (spec section 6): busybox,
 * Git bash and MSYS2 yes, custom and none no. */
int local_shell_kind_is_posix(LocalShellKind kind);

/* Shown in the terminal when the search comes up empty (spec 4.2 step 6). */
extern const char LOCAL_SHELL_NONE_MESSAGE[];

#endif /* NUTSHELL_LOCAL_SHELL_H */
