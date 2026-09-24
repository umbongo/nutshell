#ifdef _WIN32

/* ConPTY backend for a local shell session -- spec section 3.
 *
 * Deliberate constraints, all of them from the spec:
 *
 *  - The file does NOT raise _WIN32_WINNT. The MinGW headers this tree
 *    builds against sit at 0x0601 (Windows 7), so the three pseudo-console
 *    entry points are not declared and PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
 *    is not defined. They are resolved with GetProcAddress on kernel32.dll
 *    and the one constant is defined locally, which keeps the whole rest of
 *    the app on the same target it has always had. A Windows older than 10
 *    version 1809 simply fails the open with a sentence the user can act on.
 *
 *  - HPCON is not named anywhere here (it is guarded in some header sets);
 *    the handle is carried as a plain void *, which is what it is.
 *
 *  - The Terminal is touched only from the UI thread. The reader thread's
 *    sole job is ReadFile -> PtyRing; poll(), on the UI thread, drains the
 *    ring into term_process() and the log files.
 */

#include <windows.h>
#include "local_pty.h"
#include "pty_ring.h"
#include "string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE is guarded behind NTDDI_WIN10_RS5 in
 * processthreadsapi.h. The value is stable and documented; define it here
 * rather than move the whole app's target forward (spec section 3, and the
 * first critique finding). */
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif
#ifndef EXTENDED_STARTUPINFO_PRESENT
#define EXTENDED_STARTUPINFO_PRESENT 0x00080000
#endif

/* ---- kernel32 entry points, resolved once ------------------------------- */

typedef HRESULT (WINAPI *NsCreatePseudoConsoleFn)(COORD, HANDLE, HANDLE,
                                                  DWORD, void **);
typedef HRESULT (WINAPI *NsResizePseudoConsoleFn)(void *, COORD);
typedef void    (WINAPI *NsClosePseudoConsoleFn)(void *);

static NsCreatePseudoConsoleFn g_create_pcon;
static NsResizePseudoConsoleFn g_resize_pcon;
static NsClosePseudoConsoleFn  g_close_pcon;
static int                     g_conpty_probed;
static int                     g_conpty_ok;

/* GetProcAddress hands back a FARPROC. Casting a function pointer to void *
 * is not ISO C (-Wpedantic) and casting it straight to an incompatible
 * function type trips -Wcast-function-type (in -Wextra), so the address goes
 * through a union -- type punning a union member is defined in C11 and is
 * the one form both warnings accept. */
typedef union {
    FARPROC                 raw;
    NsCreatePseudoConsoleFn create;
    NsResizePseudoConsoleFn resize;
    NsClosePseudoConsoleFn  close;
} NsProcAddr;

static int conpty_available(void)
{
    if (g_conpty_probed) return g_conpty_ok;
    g_conpty_probed = 1;

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return 0;

    NsProcAddr a;
    a.raw = GetProcAddress(k32, "CreatePseudoConsole");
    g_create_pcon = a.create;
    a.raw = GetProcAddress(k32, "ResizePseudoConsole");
    g_resize_pcon = a.resize;
    a.raw = GetProcAddress(k32, "ClosePseudoConsole");
    g_close_pcon  = a.close;

    g_conpty_ok = (g_create_pcon && g_resize_pcon && g_close_pcon) ? 1 : 0;
    return g_conpty_ok;
}

/* ---- the transport ------------------------------------------------------ */

#define LOCAL_PTY_READ_CHUNK  4096
#define LOCAL_PTY_CLOSE_WAIT  500   /* ms, each of the two bounded waits */

struct LocalPty {
    void   *hpc;          /* HPCON                                          */
    HANDLE  in_write;     /* our end: bytes to the shell                    */
    HANDLE  out_read;     /* our end: bytes from the shell                  */
    HANDLE  proc;         /* the shell process                              */
    HANDLE  reader;       /* reader thread                                  */
    DWORD   reader_id;

    CRITICAL_SECTION cs;  /* guards ring, first[], first_len                 */
    PtyRing ring;

    int     opened;       /* the pseudo-console exists                       */
    int     cols, rows;   /* size currently applied                          */
    int     want_cols;    /* size requested; applied at open when it arrives */
    int     want_rows;    /*   before the pseudo-console exists              */

    /* What ConPTY emitted before we wrote anything (spec section 3, last
     * bullet: does it open with ESC[6n / ESC[c / ESC[>c?). */
    unsigned char first[LOCAL_PTY_FIRST_MAX];
    size_t  first_len;
    volatile LONG wrote_anything; /* set/read with Interlocked*, not under cs */

    int     env_inherited; /* build_env_block() failed: the child got our own
                             * environment verbatim, not spec->env. Cleared by
                             * local_pty_poll() once it has logged this once
                             * (diagnostics only).                            */
};

/* ---- log helpers -------------------------------------------------------- */

/* The same two helpers ssh_io.c has (they are static there, and ssh_io.c is
 * not linked into the Windows test harness), so a local session's session
 * log and debug log read exactly like an SSH one's. */

static void log_chunk(FILE *f, const char *data, size_t len)
{
    if (!f || len == 0u) return;
    char strip_buf[4096];
    while (len > 0u) {
        size_t slice = (len < sizeof(strip_buf) - 1u)
                         ? len : (sizeof(strip_buf) - 1u);
        size_t stripped = ansi_strip(strip_buf, sizeof(strip_buf), data, slice);
        if (stripped > 0u) fwrite(strip_buf, 1u, stripped, f);
        data += slice;
        len  -= slice;
    }
    fflush(f);
}

static void debug_log_chunk(FILE *f, const char *data, size_t len)
{
    if (!f || len == 0u) return;
    for (size_t i = 0u; i < len; i++) {
        unsigned char ch = (unsigned char)data[i];
        if      (ch == 0x1Bu) fputs("ESC", f);
        else if (ch >= 0x20u && ch < 0x7Fu) fputc((int)ch, f);
        else if (ch == '\r')  fputs("\\r", f);
        else if (ch == '\n')  fputs("\\n\n", f);
        else if (ch == '\t')  fputs("\\t", f);
        else fprintf(f, "\\x%02X", (unsigned int)ch);
    }
    fflush(f);
}

/* ---- UTF-8 -> UTF-16 ---------------------------------------------------- */

/* Everything crossing into CreateProcessW -- command line, environment,
 * working directory -- is UTF-8 on our side and UTF-16 on Windows's.
 * Returns a malloc'd wide string, or NULL. */
static WCHAR *utf8_to_wide(const char *s)
{
    if (!s) return NULL;
    int need = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (need <= 0) return NULL;
    WCHAR *w = (WCHAR *)malloc((size_t)need * sizeof(WCHAR));
    if (!w) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, w, need) <= 0) {
        free(w);
        return NULL;
    }
    return w;
}

/* ---- environment block -------------------------------------------------- */

/* Length of one "NAME=VALUE" entry's NAME part, or 0 when the entry has no
 * '=' or starts with one (the "=C:" drive-current-directory entries, which
 * must be copied through untouched and never matched against a name). */
static size_t env_name_len(const WCHAR *entry)
{
    if (!entry || entry[0] == L'\0' || entry[0] == L'=') return 0;
    size_t i = 0;
    while (entry[i] != L'\0' && entry[i] != L'=') i++;
    return (entry[i] == L'=') ? i : 0;
}

/* Case-insensitive compare of an entry's NAME against a plain wide name. */
static int env_name_is(const WCHAR *entry, size_t name_len, const WCHAR *name)
{
    size_t i = 0;
    for (; i < name_len; i++) {
        if (name[i] == L'\0') return 0;
        WCHAR a = entry[i], b = name[i];
        if (a >= L'a' && a <= L'z') a = (WCHAR)(a - L'a' + L'A');
        if (b >= L'a' && b <= L'z') b = (WCHAR)(b - L'a' + L'A');
        if (a != b) return 0;
    }
    return name[i] == L'\0';
}

/* The parent's environment with spec->env applied on top: an addition whose
 * name already exists replaces that entry, a new one is appended. Nothing is
 * ever removed (spec 4.3). Returns a malloc'd double-NUL-terminated block,
 * or NULL, in which case the caller inherits the parent block unchanged. */
static WCHAR *build_env_block(const LocalShellSpec *spec)
{
    WCHAR *parent = GetEnvironmentStringsW();
    if (!parent) return NULL;

    /* Wide copies of the additions, as complete "NAME=VALUE" entries. */
    WCHAR *add[LOCAL_SHELL_ENV_MAX];
    WCHAR *add_name[LOCAL_SHELL_ENV_MAX];
    int    add_count = 0;
    for (int i = 0; i < spec->env_count && i < LOCAL_SHELL_ENV_MAX; i++) {
        if (spec->env[i].name[0] == '\0') continue;
        size_t need = strlen(spec->env[i].name) + 1u
                    + strlen(spec->env[i].value) + 1u;
        char *pair = (char *)malloc(need);
        if (!pair) continue;
        (void)snprintf(pair, need, "%s=%s",
                       spec->env[i].name, spec->env[i].value);
        WCHAR *wpair = utf8_to_wide(pair);
        WCHAR *wname = utf8_to_wide(spec->env[i].name);
        free(pair);
        if (!wpair || !wname) { free(wpair); free(wname); continue; }
        add[add_count]      = wpair;
        add_name[add_count] = wname;
        add_count++;
    }

    /* Size: every parent entry we keep, plus every addition, plus the final
     * terminating NUL. */
    size_t total = 1u;
    for (const WCHAR *e = parent; *e != L'\0'; ) {
        size_t elen = wcslen(e);
        size_t nlen = env_name_len(e);
        int replaced = 0;
        for (int i = 0; i < add_count && nlen > 0; i++)
            if (env_name_is(e, nlen, add_name[i])) { replaced = 1; break; }
        if (!replaced) total += elen + 1u;
        e += elen + 1u;
    }
    for (int i = 0; i < add_count; i++) total += wcslen(add[i]) + 1u;

    WCHAR *block = (WCHAR *)malloc(total * sizeof(WCHAR));
    if (block) {
        size_t pos = 0;
        for (const WCHAR *e = parent; *e != L'\0'; ) {
            size_t elen = wcslen(e);
            size_t nlen = env_name_len(e);
            int replaced = 0;
            for (int i = 0; i < add_count && nlen > 0; i++)
                if (env_name_is(e, nlen, add_name[i])) { replaced = 1; break; }
            if (!replaced) {
                memcpy(block + pos, e, (elen + 1u) * sizeof(WCHAR));
                pos += elen + 1u;
            }
            e += elen + 1u;
        }
        for (int i = 0; i < add_count; i++) {
            size_t elen = wcslen(add[i]);
            memcpy(block + pos, add[i], (elen + 1u) * sizeof(WCHAR));
            pos += elen + 1u;
        }
        block[pos] = L'\0';
    }

    for (int i = 0; i < add_count; i++) { free(add[i]); free(add_name[i]); }
    FreeEnvironmentStringsW(parent);
    return block;
}

/* ---- reader thread ------------------------------------------------------ */

static DWORD WINAPI reader_thread(LPVOID param)
{
    LocalPty *p = (LocalPty *)param;
    char buf[LOCAL_PTY_READ_CHUNK];

    for (;;) {
        DWORD got = 0;
        BOOL ok = ReadFile(p->out_read, buf, (DWORD)sizeof(buf), &got, NULL);
        if (!ok || got == 0) break;   /* pipe closed, cancelled, or EOF */

        EnterCriticalSection(&p->cs);
        /* Record the opening bytes once, before anything has been written
         * to the shell -- that is the window in which a DSR/DA query would
         * appear (spec section 3, last bullet). */
        if (!InterlockedCompareExchange(&p->wrote_anything, 0, 0) &&
            p->first_len < LOCAL_PTY_FIRST_MAX) {
            size_t room = LOCAL_PTY_FIRST_MAX - p->first_len;
            size_t take = ((size_t)got < room) ? (size_t)got : room;
            memcpy(p->first + p->first_len, buf, take);
            p->first_len += take;
        }
        (void)pty_ring_push(&p->ring, buf, (size_t)got);
        LeaveCriticalSection(&p->cs);

        /* No stop check here: keep draining until ReadFile itself fails or
         * returns 0. Closing the handle (and CancelSynchronousIo as a
         * backstop) is what ends this loop -- see local_pty_close(). */
    }

    EnterCriticalSection(&p->cs);
    pty_ring_set_eof(&p->ring);
    LeaveCriticalSection(&p->cs);
    return 0;
}

/* ---- open --------------------------------------------------------------- */

/* Literal format strings only: -Wformat-nonliteral is fatal in this tree. */
static void set_err(char *err, size_t err_size, const char *text)
{
    if (err && err_size > 0u) (void)snprintf(err, err_size, "%s", text);
}

static void set_err_code(char *err, size_t err_size, const char *text,
                         DWORD code)
{
    if (err && err_size > 0u)
        (void)snprintf(err, err_size, "%s (error %lu)", text,
                       (unsigned long)code);
}

LocalPty *local_pty_open(const LocalShellSpec *spec, int cols, int rows,
                         char *err, size_t err_size)
{
    if (err && err_size > 0u) err[0] = '\0';

    if (!spec || spec->kind == SHELL_NONE || spec->command[0] == '\0') {
        set_err(err, err_size,
                (spec && spec->error[0]) ? spec->error : LOCAL_SHELL_NONE_MESSAGE);
        return NULL;
    }
    /* M1: refuse a spec whose exe is empty as a second line of defence,
     * independent of local_shell_resolve_bare() (which now also refuses
     * this itself -- see local_shell.c). spec->command[0] != '\0' alone
     * does not catch this: a custom command whose first token was too long
     * for LOCAL_SHELL_PATH_MAX leaves spec->exe empty while spec->command
     * still holds the (unusable) full text. Without this check, wapp below
     * stays NULL and CreateProcess is handed a NULL lpApplicationName --
     * exactly the PATH/CWD-searching guess this file's own comment (see
     * below) says must never happen. */
    if (spec->exe[0] == '\0') {
        set_err(err, err_size,
                spec->error[0] ? spec->error :
                "Could not find an executable in the shell command.");
        return NULL;
    }
    if (!conpty_available()) {
        set_err(err, err_size,
                "Local shell needs Windows 10 version 1809 or later.");
        return NULL;
    }

    LocalPty *p = (LocalPty *)calloc(1u, sizeof(*p));
    if (!p) {
        set_err(err, err_size, "Out of memory starting the local shell.");
        return NULL;
    }
    if (pty_ring_init(&p->ring, 0u) != 0) {
        free(p);
        set_err(err, err_size, "Out of memory starting the local shell.");
        return NULL;
    }
    InitializeCriticalSection(&p->cs);

    /* A resize that arrived before the pseudo-console existed is simply the
     * size we create it at, so the grid can never be lost in that window
     * (spec section 3, "Resize"). */
    p->want_cols = (cols > 0) ? cols : 80;
    p->want_rows = (rows > 0) ? rows : 24;

    HANDLE in_read = NULL, out_write = NULL;
    WCHAR *wcmd = NULL, *wapp = NULL, *wcwd = NULL, *envblock = NULL;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    if (!CreatePipe(&in_read, &p->in_write, NULL, 0) ||
        !CreatePipe(&p->out_read, &out_write, NULL, 0)) {
        set_err_code(err, err_size, "Could not create the shell's pipes",
                     GetLastError());
        goto fail;
    }

    {
        COORD size;
        size.X = (SHORT)p->want_cols;
        size.Y = (SHORT)p->want_rows;
        HRESULT hr = g_create_pcon(size, in_read, out_write, 0, &p->hpc);
        if (FAILED(hr) || !p->hpc) {
            set_err_code(err, err_size, "Could not open a pseudo-console",
                         (DWORD)hr);
            goto fail;
        }
        p->cols = p->want_cols;
        p->rows = p->want_rows;
        p->opened = 1;
    }

    wcmd = utf8_to_wide(spec->command);
    if (!wcmd) {
        set_err(err, err_size, "Could not convert the shell command line.");
        goto fail;
    }

    /* lpApplicationName, explicitly, rather than leaving it NULL: with a
     * NULL lpApplicationName, CreateProcess parses lpCommandLine itself to
     * find the executable, trying progressively longer prefixes at each
     * unquoted space -- shortest first -- which is exactly what lets a
     * file planted at a shorter guess run instead of the one meant. Every
     * LocalShellSpec's exe (local_shell_resolve() for a detected shell,
     * local_shell_resolve_bare() for a custom one) is already an absolute,
     * unambiguous, single path by the time it reaches here, so handing it
     * straight to CreateProcess removes that search entirely; argv[0] in
     * wcmd stays quoted for the child's own GetCommandLineW parsing, but
     * Windows never uses it to choose which file to run once
     * lpApplicationName is set. */
    if (spec->exe[0] != '\0') {
        wapp = utf8_to_wide(spec->exe);
        if (!wapp) {
            set_err(err, err_size, "Could not convert the shell executable path.");
            goto fail;
        }
    }

    /* Working directory: %USERPROFILE%, or inherit ours when it is unset. */
    {
        DWORD n = GetEnvironmentVariableW(L"USERPROFILE", NULL, 0);
        if (n > 0u) {
            wcwd = (WCHAR *)malloc((size_t)n * sizeof(WCHAR));
            if (wcwd && GetEnvironmentVariableW(L"USERPROFILE", wcwd, n) == 0u) {
                free(wcwd);
                wcwd = NULL;
            }
        }
    }

    envblock = build_env_block(spec);
    if (!envblock) {
        /* GetEnvironmentStringsW or an allocation inside it failed: the
         * child inherits our own environment block verbatim (CreateProcessW
         * with a NULL environment does that), so spec->env's additions never
         * reach it. Not fatal -- worth one line in the debug log the first
         * time poll() runs, so it is recorded rather than silently dropped. */
        p->env_inherited = 1;
    }

    {
        SIZE_T attr_size = 0;
        InitializeProcThreadAttributeList(NULL, 1, 0, &attr_size);
        if (attr_size == 0u) {
            set_err_code(err, err_size,
                         "Could not size the process attribute list",
                         GetLastError());
            goto fail;
        }
        attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc((size_t)attr_size);
        if (!attrs) {
            set_err(err, err_size, "Out of memory starting the local shell.");
            goto fail;
        }
        if (!InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size) ||
            !UpdateProcThreadAttribute(attrs, 0,
                                       PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                       p->hpc, sizeof(p->hpc), NULL, NULL)) {
            set_err_code(err, err_size,
                         "Could not attach the pseudo-console",
                         GetLastError());
            goto fail;
        }
    }

    {
        STARTUPINFOEXW six;
        memset(&six, 0, sizeof(six));
        six.StartupInfo.cb = sizeof(six);
        six.lpAttributeList = attrs;

        /* CreateProcessW may write to lpCommandLine, so it gets its own
         * mutable copy even though wcmd is already ours. */
        BOOL started = CreateProcessW(wapp, wcmd, NULL, NULL, FALSE,
                                      EXTENDED_STARTUPINFO_PRESENT
                                        | CREATE_UNICODE_ENVIRONMENT,
                                      envblock, wcwd,
                                      &six.StartupInfo, &pi);
        if (!started) {
            set_err_code(err, err_size, "Could not start the shell",
                         GetLastError());
            goto fail;
        }
        p->proc = pi.hProcess;
        if (pi.hThread) CloseHandle(pi.hThread);
    }

    /* The child-side pipe ends belong to conhost now (spec section 3). */
    CloseHandle(in_read);   in_read   = NULL;
    CloseHandle(out_write); out_write = NULL;

    DeleteProcThreadAttributeList(attrs);
    free(attrs); attrs = NULL;
    free(wcmd);     wcmd = NULL;
    free(wapp);     wapp = NULL;
    free(wcwd);     wcwd = NULL;
    free(envblock); envblock = NULL;

    p->reader = CreateThread(NULL, 0, reader_thread, p, 0, &p->reader_id);
    if (!p->reader) {
        set_err_code(err, err_size, "Could not start the shell reader thread",
                     GetLastError());
        goto fail;
    }
    return p;

fail:
    if (attrs) { DeleteProcThreadAttributeList(attrs); free(attrs); }
    free(wcmd);
    free(wapp);
    free(wcwd);
    free(envblock);
    if (pi.hProcess) { TerminateProcess(pi.hProcess, 1); CloseHandle(pi.hProcess); }
    if (in_read)   CloseHandle(in_read);
    if (out_write) CloseHandle(out_write);
    if (p) {
        if (p->hpc && g_close_pcon) g_close_pcon(p->hpc);
        if (p->in_write) CloseHandle(p->in_write);
        if (p->out_read) CloseHandle(p->out_read);
        DeleteCriticalSection(&p->cs);
        pty_ring_free(&p->ring);
        free(p);
    }
    if (err && err_size > 0u && err[0] == '\0')
        set_err(err, err_size, "Could not start the local shell.");
    return NULL;
}

/* ---- poll --------------------------------------------------------------- */

int local_pty_poll(void *ctx, Terminal *term, FILE *log_file, FILE *debug_log)
{
    LocalPty *p = (LocalPty *)ctx;
    if (!p || !term) return -1;

    if (p->env_inherited && debug_log) {
        fputs("[nutshell: could not build the shell environment; "
              "parent block inherited]\n", debug_log);
        fflush(debug_log);
        p->env_inherited = 0;
    }

    char buf[LOCAL_PTY_READ_CHUNK];
    size_t total = 0u;
    int ring_done = 0;
    int dropped = 0;

    for (int loops = 0; loops < 16; loops++) {
        EnterCriticalSection(&p->cs);
        size_t got = pty_ring_drain(&p->ring, buf, sizeof(buf));
        if (pty_ring_full(&p->ring)) { dropped = 1; pty_ring_clear_full(&p->ring); }
        ring_done = (pty_ring_eof(&p->ring) && pty_ring_available(&p->ring) == 0u);
        LeaveCriticalSection(&p->cs);

        if (got > 0u) {
            term_process(term, buf, got);
            log_chunk(log_file, buf, got);
            debug_log_chunk(debug_log, buf, got);
            total += got;
        }
        if (got < sizeof(buf)) break;   /* ring drained for now */
    }

    if (dropped && debug_log) {
        fputs("\n[nutshell: local shell output overran the 64K ring; "
              "some bytes were dropped]\n", debug_log);
        fflush(debug_log);
    }

    if (total > 0u) return 1;

    /* EOF once the reader has seen the end AND the ring is empty. The
     * process handle being signalled is not by itself EOF -- a grandchild
     * can keep the pseudo-console's pipe (and so the reader) alive well
     * after the shell itself has exited (spec section 3, "Close"). The
     * fallback below only steps in once the READER thread itself is known
     * to have returned, which happens only after ReadFile has failed or
     * returned 0. */
    if (ring_done) return -2;
    if (p->reader && WaitForSingleObject(p->reader, 0) == WAIT_OBJECT_0) {
        EnterCriticalSection(&p->cs);
        int empty = (pty_ring_available(&p->ring) == 0u);
        LeaveCriticalSection(&p->cs);
        if (empty) return -2;
    }
    return 0;
}

/* ---- write -------------------------------------------------------------- */

/* Enter is \r, which is what the key encoder already sends and what a
 * console child reads; the paste path in window.c converts line ends to \r
 * for a local session for the same reason. Ctrl+C arrives here as the byte
 * 0x03 and conhost turns it into the child's control event. */
int local_pty_write(void *ctx, const char *data, size_t len)
{
    LocalPty *p = (LocalPty *)ctx;
    if (!p || !p->in_write || !data) return -1;
    if (len == 0u) return 0;

    InterlockedExchange(&p->wrote_anything, 1);

    size_t off = 0u;
    while (off < len) {
        DWORD wrote = 0;
        DWORD chunk = (DWORD)((len - off > 0x10000u) ? 0x10000u : (len - off));
        if (!WriteFile(p->in_write, data + off, chunk, &wrote, NULL))
            return -1;
        if (wrote == 0u) return -1;
        off += (size_t)wrote;
    }
    return (int)len;
}

/* ---- resize ------------------------------------------------------------- */

int local_pty_resize(void *ctx, int cols, int rows)
{
    LocalPty *p = (LocalPty *)ctx;
    if (!p) return -1;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (cols > 0x7FFF) cols = 0x7FFF;
    if (rows > 0x7FFF) rows = 0x7FFF;

    p->want_cols = cols;
    p->want_rows = rows;

    /* Recorded and applied at open when the pseudo-console does not exist
     * yet (spec section 3, "Resize"). */
    if (!p->opened || !p->hpc || !g_resize_pcon) return 0;
    if (cols == p->cols && rows == p->rows) return 0;

    COORD size;
    size.X = (SHORT)cols;
    size.Y = (SHORT)rows;
    HRESULT hr = g_resize_pcon(p->hpc, size);
    if (FAILED(hr)) return -1;
    p->cols = cols;
    p->rows = rows;
    return 0;
}

/* ---- close -------------------------------------------------------------- */

/* The order matters and is the spec's (section 3, "Close"):
 *
 *   (a) ClosePseudoConsole  ends conhost, which closes the pipe and unblocks
 *                           a well-behaved reader
 *   (b) CancelSynchronousIo unblocks a ReadFile that survived (a) -- conhost
 *                           gone but a grandchild still holding the pipe
 *   (c) wait 500 ms for the child process; TerminateProcess if still alive;
 *       close the process handle
 *   (d) wait 500 ms for the reader thread
 *
 * Only once the reader is confirmed to have returned (d) are in_write and
 * out_read closed, along with everything else. Closing a handle while
 * another thread may still be blocked in a syscall on it invites handle
 * reuse -- a later CreateFile/CreatePipe elsewhere in the process could be
 * handed that same handle value while the reader still thinks it owns it.
 * So when the reader has NOT returned within its 500 ms, this detaches
 * instead: close only the reader thread handle (harmless -- nothing waits
 * on it again), leave in_write and out_read open (the reader, wherever it
 * is stuck, still owns out_read; leaking two handles is cheaper than a
 * reuse bug), and leak this LocalPty itself, since the thread still holds
 * the pointer and freeing the ring or the critical section under it would
 * be a use-after-free. A bounded leak on a path that should never be taken
 * is the right trade; blocking the UI thread for ever is not.
 */
void local_pty_close(void *ctx)
{
    LocalPty *p = (LocalPty *)ctx;
    if (!p) return;

    /* (a) End conhost; unblocks a well-behaved reader on its own. */
    if (p->hpc && g_close_pcon) { g_close_pcon(p->hpc); p->hpc = NULL; }
    p->opened = 0;

    /* (b) Unblocks a ReadFile that (a) alone did not -- e.g. a grandchild
     * still holding the pseudo-console's write end of the pipe. */
    if (p->reader) CancelSynchronousIo(p->reader);

    /* (c) The shell itself, bounded. */
    if (p->proc) {
        if (WaitForSingleObject(p->proc, LOCAL_PTY_CLOSE_WAIT) != WAIT_OBJECT_0)
            TerminateProcess(p->proc, 1);
        CloseHandle(p->proc);
        p->proc = NULL;
    }

    /* (d) The reader thread, bounded. */
    if (p->reader) {
        if (WaitForSingleObject(p->reader, LOCAL_PTY_CLOSE_WAIT) != WAIT_OBJECT_0) {
            /* Detached: the thread outlives us. Close only the thread
             * handle; in_write/out_read stay open and this LocalPty is
             * leaked deliberately (see the block comment above). */
            CloseHandle(p->reader);
            p->reader = NULL;
            return;
        }
        CloseHandle(p->reader);
        p->reader = NULL;
    }

    if (p->in_write) { CloseHandle(p->in_write); p->in_write = NULL; }
    if (p->out_read) { CloseHandle(p->out_read); p->out_read = NULL; }

    DeleteCriticalSection(&p->cs);
    pty_ring_free(&p->ring);
    free(p);
}

/* ---- the vtable --------------------------------------------------------- */

SessionIo session_io_local(LocalPty *pty)
{
    SessionIo io;
    memset(&io, 0, sizeof(io));
    io.kind = SESSION_LOCAL;
    if (!pty) return io;          /* ctx stays NULL: no transport */
    io.poll   = local_pty_poll;
    io.write  = local_pty_write;
    io.resize = local_pty_resize;
    io.close  = local_pty_close;
    io.ctx    = pty;
    return io;
}

/* ---- diagnostics -------------------------------------------------------- */

int local_pty_child_exited(const LocalPty *pty)
{
    if (!pty || !pty->proc) return 1;
    return WaitForSingleObject(pty->proc, 0) == WAIT_OBJECT_0 ? 1 : 0;
}

size_t local_pty_first_bytes(const LocalPty *pty, char *out, size_t out_size)
{
    if (!pty || !out || out_size == 0u) return 0u;
    LocalPty *p = (LocalPty *)pty;   /* the lock is not a logical mutation */
    EnterCriticalSection(&p->cs);
    size_t n = (p->first_len < out_size) ? p->first_len : out_size;
    memcpy(out, p->first, n);
    LeaveCriticalSection(&p->cs);
    return n;
}

#endif /* _WIN32 */
