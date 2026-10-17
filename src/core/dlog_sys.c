/* src/core/dlog_sys.c -- the Windows DlogPlatform: an SRW lock, the system
 * clock with a cached UTC offset, a writer thread on an event, ANSI file
 * calls, the crash filter. It lives in src/core (not src/ui) so `make test`
 * exercises the real lock, thread and files on Windows (tests/test_dlog_sys.c).
 * On any other host dlog_platform_system() is NULL and the logger refuses to
 * start with the system platform. */
#include "dlog.h"
#include "dlog_internal.h"

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include <string.h>

static SRWLOCK g_lock = SRWLOCK_INIT;
static HANDLE g_event;
static HANDLE g_thread;
static void (*g_thread_fn)(void *);
static void *g_thread_arg;

static void s_lock(void) { AcquireSRWLockExclusive(&g_lock); }
static void s_unlock(void) { ReleaseSRWLockExclusive(&g_lock); }
static int s_try_lock(void) { return TryAcquireSRWLockExclusive(&g_lock) ? 1 : 0; }

static long long s_unix_ms(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (long long)(t / 10000ULL) - 11644473600000LL;
}

/* The UTC offset changes at most twice a year: ask Windows once a minute, never
 * per line. A torn read of two ints is harmless (the offset only ever moves by
 * the DST step), so no lock. */
static int s_offset(void)
{
    static volatile LONG s_cached;
    static volatile LONG s_stamp;       /* GetTickCount of the last refresh; 0 = never */
    DWORD now = GetTickCount();
    DWORD stamp = (DWORD)s_stamp;
    if (stamp == 0 || (DWORD)(now - stamp) > 60000u) {
        TIME_ZONE_INFORMATION tz;
        DWORD r = GetTimeZoneInformation(&tz);
        LONG bias = tz.Bias;
        if (r == TIME_ZONE_ID_DAYLIGHT) bias += tz.DaylightBias;
        else if (r == TIME_ZONE_ID_STANDARD) bias += tz.StandardBias;
        s_cached = -bias;
        s_stamp = (LONG)(now ? now : 1u);
    }
    return (int)s_cached;
}

static unsigned s_mono(void) { return (unsigned)GetTickCount(); }
static void s_sleep(unsigned ms) { Sleep(ms); }

static DWORD WINAPI thread_entry(LPVOID unused)
{
    (void)unused;
    g_thread_fn(g_thread_arg);
    return 0;
}

static int s_thread_start(void (*fn)(void *), void *arg)
{
    if (g_thread) return 0;
    g_event = CreateEventA(NULL, FALSE, FALSE, NULL);       /* auto-reset */
    if (!g_event) return 0;
    g_thread_fn = fn;
    g_thread_arg = arg;
    g_thread = CreateThread(NULL, 0, thread_entry, NULL, 0, NULL);
    if (!g_thread) {
        CloseHandle(g_event);
        g_event = NULL;
        return 0;
    }
    return 1;
}

static void s_thread_join(void)
{
    if (g_thread) {
        WaitForSingleObject(g_thread, 5000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    if (g_event) {                      /* unpublish first: a late wake() must not see a closed handle */
        HANDLE ev = g_event;
        g_event = NULL;
        CloseHandle(ev);
    }
}

static void s_wait(unsigned ms) { if (g_event) WaitForSingleObject(g_event, ms); else Sleep(ms); }
static void s_wake(void) { if (g_event) SetEvent(g_event); }

static int s_mkdir_p(const char *dir)
{
    char tmp[DLOG_MAX_PATH];
    size_t n = strlen(dir);
    if (n == 0 || n >= sizeof(tmp)) return ERROR_BAD_PATHNAME;
    memcpy(tmp, dir, n + 1);
    for (size_t i = 1; i <= n; i++) {
        if (tmp[i] == '\\' || tmp[i] == '/' || tmp[i] == '\0') {
            char saved = tmp[i];
            tmp[i] = '\0';
            size_t l = strlen(tmp);
            int drive_only = (l == 2 && tmp[1] == ':');
            if (!drive_only && l > 1)
                (void)CreateDirectoryA(tmp, NULL);       /* exists / denied: the final check decides */
            tmp[i] = saved;
        }
    }
    DWORD attr = GetFileAttributesA(dir);
    if (attr == INVALID_FILE_ATTRIBUTES) return (int)GetLastError();
    return (attr & FILE_ATTRIBUTE_DIRECTORY) ? 0 : ERROR_DIRECTORY;
}

static int s_file_open(const char *path, void **handle, unsigned long long *size)
{
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return (int)GetLastError();
    LARGE_INTEGER li;
    li.QuadPart = 0;
    if (!GetFileSizeEx(h, &li)) li.QuadPart = 0;
    *handle = h;
    *size = (unsigned long long)li.QuadPart;
    return 0;
}

static int s_file_write(void *handle, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    while (len > 0) {
        DWORD chunk = (len > 0x40000000u) ? 0x40000000u : (DWORD)len;
        DWORD wrote = 0;
        if (!WriteFile((HANDLE)handle, p, chunk, &wrote, NULL)) return (int)GetLastError();
        if (wrote == 0) return ERROR_WRITE_FAULT;
        p += wrote;
        len -= wrote;
    }
    return 0;
}

static void s_file_close(void *handle) { CloseHandle((HANDLE)handle); }

static int s_file_write_all(const char *path, const void *buf, size_t len)
{
    HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return (int)GetLastError();
    int err = s_file_write(h, buf, len);
    CloseHandle(h);
    return err;
}

static long s_file_read(const char *path, unsigned long long off, void *buf, size_t cap)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)off;
    long result = -1;
    if (SetFilePointerEx(h, li, NULL, FILE_BEGIN)) {
        DWORD want = (cap > 0x10000000u) ? 0x10000000u : (DWORD)cap;
        DWORD got = 0;
        if (ReadFile(h, buf, want, &got, NULL)) result = (long)got;
    }
    CloseHandle(h);
    return result;
}

static int s_list_dir(const char *dir, DlogFileInfo *out, size_t cap)
{
    char pat[DLOG_MAX_PATH + 16];
    if (!dlog_join(pat, sizeof(pat), dir, "nutshell-*")) return -1;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND) {
            DWORD attr = GetFileAttributesA(dir);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return 0;
        }
        return -1;
    }
    int n = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if ((size_t)n >= cap) break;
        size_t l = strlen(fd.cFileName);
        if (l >= sizeof(out[n].name)) continue;
        memcpy(out[n].name, fd.cFileName, l + 1);
        out[n].size = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        unsigned long long t = ((unsigned long long)fd.ftLastWriteTime.dwHighDateTime << 32) |
                               fd.ftLastWriteTime.dwLowDateTime;
        out[n].mtime_ms = (long long)(t / 10000ULL) - 11644473600000LL;
        n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

static int s_remove(const char *path) { return DeleteFileA(path) ? 0 : (int)GetLastError(); }

static const char *s_local_appdata(void)
{
    static char buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", buf, (DWORD)sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return NULL;
    return buf;
}

static void s_err_text(int err, char *buf, size_t cap)
{
    if (cap == 0) return;
    DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)err,
                             MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, (DWORD)cap, NULL);
    if (n == 0) {
        (void)snprintf(buf, cap, "error %d", err);
        return;
    }
    while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == ' ' || buf[n - 1] == '.'))
        buf[--n] = '\0';
}

static const DlogPlatform k_system = {
    s_lock, s_unlock, s_try_lock, s_unix_ms, s_offset, s_mono, s_sleep,
    s_thread_start, s_thread_join, s_wait, s_wake,
    s_mkdir_p, s_file_open, s_file_write, s_file_close, s_file_write_all, s_file_read,
    s_list_dir, s_remove, s_local_appdata, s_err_text
};

const DlogPlatform *dlog_platform_system(void) { return &k_system; }

static LPTOP_LEVEL_EXCEPTION_FILTER g_prev_filter;

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep)
{
    unsigned long code = ep && ep->ExceptionRecord ? (unsigned long)ep->ExceptionRecord->ExceptionCode : 0ul;
    const void *addr = ep && ep->ExceptionRecord ? (const void *)ep->ExceptionRecord->ExceptionAddress : NULL;
    dlog_crash_note(code, addr);
    if (g_prev_filter) return g_prev_filter(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

void dlog_sys_install_crash_filter(void)
{
    static int installed;
    if (installed) return;
    installed = 1;
    g_prev_filter = SetUnhandledExceptionFilter(crash_filter);
}

#else /* not Windows */

const DlogPlatform *dlog_platform_system(void) { return NULL; }
void dlog_sys_install_crash_filter(void) {}

#endif
