/* tests/fake_dlog.c -- see fake_dlog.h */
#include "fake_dlog.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

FakeDlog g_fk;

static int is_sep(char c) { return c == '\\' || c == '/'; }

void fk_free(void)
{
    for (int i = 0; i < g_fk.nfiles; i++) free(g_fk.files[i].data);
    memset(&g_fk, 0, sizeof(g_fk));
}

void fk_reset(void)
{
    fk_free();
    DlogTm tm = { 2026, 10, 17, 13, 3, 10, 0 };
    g_fk.unix_ms = dlog_unix_ms_from_civil(&tm, 0);
    g_fk.offset_min = 60;
    g_fk.mono = 1000;
    g_fk.try_lock_result = 1;
    (void)snprintf(g_fk.local_appdata, sizeof(g_fk.local_appdata), "C:\\Users\\me\\AppData\\Local");
}

FkFile *fk_find(const char *path)
{
    for (int i = 0; i < g_fk.nfiles; i++)
        if (strcmp(g_fk.files[i].path, path) == 0) return &g_fk.files[i];
    return NULL;
}

static FkFile *fk_create(const char *path)
{
    FkFile *f = fk_find(path);
    if (f) return f;
    if (g_fk.nfiles >= FK_MAX_FILES) return NULL;
    f = &g_fk.files[g_fk.nfiles++];
    memset(f, 0, sizeof(*f));
    (void)snprintf(f->path, sizeof(f->path), "%s", path);
    f->mtime = g_fk.unix_ms;
    return f;
}

static void fk_append(FkFile *f, const void *buf, size_t len)
{
    char *nd = (char *)realloc(f->data, f->len + len + 1);
    if (!nd) return;
    f->data = nd;
    memcpy(f->data + f->len, buf, len);
    f->len += len;
    f->data[f->len] = '\0';
    f->mtime = g_fk.unix_ms;
}

const char *fk_text(const char *path)
{
    FkFile *f = fk_find(path);
    if (!f) return NULL;
    if (!f->data) return "";
    return f->data;   /* always NUL terminated by fk_append */
}

void fk_put_file(const char *path, const char *text, unsigned long long pad_to, long long mtime)
{
    FkFile *f = fk_create(path);
    if (!f) return;
    free(f->data);
    f->data = NULL;
    f->len = 0;
    if (text) fk_append(f, text, strlen(text));
    while (f->len < pad_to) fk_append(f, "x", 1);
    f->mtime = mtime;
}

static int under(const char *path, const char *prefix)
{
    size_t n = strlen(prefix);
    return n > 0 && strncmp(path, prefix, n) == 0 && (path[n] == '\0' || is_sep(path[n]) || is_sep(prefix[n - 1]));
}

static int dir_known(const char *dir)
{
    for (int i = 0; i < g_fk.ndirs; i++)
        if (strcmp(g_fk.dirs[i], dir) == 0) return 1;
    return 0;
}

int fk_count_in(const char *dir)
{
    int n = 0;
    size_t dl = strlen(dir);
    for (int i = 0; i < g_fk.nfiles; i++) {
        const char *p = g_fk.files[i].path;
        if (strncmp(p, dir, dl) == 0 && is_sep(p[dl]) && !strpbrk(p + dl + 1, "\\/")) n++;
    }
    return n;
}

int fk_count_lines_containing(const char *text, const char *needle)
{
    int n = 0;
    if (!text) return 0;
    const char *p = text;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        char tmp[4096];
        if (l >= sizeof(tmp)) l = sizeof(tmp) - 1;
        memcpy(tmp, p, l);
        tmp[l] = '\0';
        if (strstr(tmp, needle)) n++;
        if (!e) break;
        p = e + 1;
    }
    return n;
}

void fk_advance(unsigned ms)
{
    g_fk.unix_ms += ms;
    g_fk.mono += ms;
}

/* ---- the platform ---------------------------------------------------------- */

static void p_lock(void)
{
    g_fk.lock_count++;
    g_fk.lock_depth++;
    if (g_fk.lock_depth > g_fk.max_lock_depth) g_fk.max_lock_depth = g_fk.lock_depth;
}
static void p_unlock(void) { g_fk.lock_depth--; }
static int  p_try_lock(void) { if (g_fk.try_lock_result) { p_lock(); return 1; } return 0; }
static long long p_unix_ms(void) { g_fk.clock_calls++; return g_fk.unix_ms; }
static int  p_offset(void) { return g_fk.offset_min; }
static unsigned p_mono(void) { return g_fk.mono; }
static void p_sleep(unsigned ms)
{
    g_fk.sleeps++;
    if (g_fk.advance_on_sleep) fk_advance(ms);
    if (g_fk.drain_on_sleep) (void)dlog_pump();
}
static void p_wake(void) { g_fk.wakes++; }

static int p_mkdir_p(const char *dir)
{
    if (g_fk.deny_prefix[0] && under(dir, g_fk.deny_prefix)) return 5;
    if (!dir_known(dir) && g_fk.ndirs < FK_MAX_DIRS)
        (void)snprintf(g_fk.dirs[g_fk.ndirs++], DLOG_MAX_PATH, "%s", dir);
    return 0;
}

static int p_file_open(const char *path, void **handle, unsigned long long *size)
{
    if (g_fk.deny_prefix[0] && under(path, g_fk.deny_prefix)) return 5;
    FkFile *f = fk_create(path);
    if (!f) return 4;
    f->open = 1;
    *handle = f;
    *size = f->len;
    return 0;
}

static int p_file_write(void *h, const void *buf, size_t len)
{
    g_fk.write_calls++;
    if (g_fk.fail_writes) return 112;
    fk_append((FkFile *)h, buf, len);
    return 0;
}

static void p_file_close(void *h) { ((FkFile *)h)->open = 0; }

static int p_write_all(const char *path, const void *buf, size_t len)
{
    if (g_fk.deny_prefix[0] && under(path, g_fk.deny_prefix)) return 5;
    FkFile *f = fk_create(path);
    if (!f) return 4;
    free(f->data);
    f->data = NULL;
    f->len = 0;
    fk_append(f, buf, len);
    return 0;
}

static long p_file_read(const char *path, unsigned long long off, void *buf, size_t cap)
{
    FkFile *f = fk_find(path);
    if (!f) return -1;
    if (off >= f->len) return 0;
    size_t n = f->len - (size_t)off;
    if (n > cap) n = cap;
    memcpy(buf, f->data + off, n);
    return (long)n;
}

static int p_list_dir(const char *dir, DlogFileInfo *out, size_t cap)
{
    int known = dir_known(dir);
    int n = 0;
    size_t dl = strlen(dir);
    for (int i = 0; i < g_fk.nfiles; i++) {
        const char *p = g_fk.files[i].path;
        if (strncmp(p, dir, dl) == 0 && is_sep(p[dl]) && !strpbrk(p + dl + 1, "\\/")) {
            known = 1;
            if ((size_t)n < cap) {
                (void)snprintf(out[n].name, sizeof(out[n].name), "%s", p + dl + 1);
                out[n].size = g_fk.files[i].len;
                out[n].mtime_ms = g_fk.files[i].mtime;
                n++;
            }
        }
    }
    return known ? n : -1;
}

static int p_remove(const char *path)
{
    for (int i = 0; i < g_fk.nfiles; i++) {
        if (strcmp(g_fk.files[i].path, path) != 0) continue;
        if (g_fk.files[i].undeletable || g_fk.files[i].open) return 32;
        /* Tombstone the slot, never compact the table: open handles are FkFile pointers into it.
         * (Moving the last slot into the hole left the logger holding a stale pointer to the
         * current file whose data buffer had been duplicated -- a use-after-free, 0xC0000374.) */
        free(g_fk.files[i].data);
        g_fk.files[i].data = NULL;
        g_fk.files[i].len = 0;
        g_fk.files[i].path[0] = '\0';
        g_fk.removed++;
        return 0;
    }
    return 2;
}

static const char *p_local(void) { return g_fk.local_appdata; }

static void p_err(int err, char *buf, size_t cap)
{
    if (err == 5) (void)snprintf(buf, cap, "Access is denied");
    else (void)snprintf(buf, cap, "error %d", err);
}

static const DlogPlatform k_fake = {
    p_lock, p_unlock, p_try_lock, p_unix_ms, p_offset, p_mono, p_sleep,
    NULL, NULL, NULL, p_wake,
    p_mkdir_p, p_file_open, p_file_write, p_file_close, p_write_all, p_file_read,
    p_list_dir, p_remove, p_local, p_err
};

const DlogPlatform *fk_platform(void) { return &k_fake; }

int fk_start(size_t queue_bytes, size_t recorder_bytes)
{
    DlogStartInfo si;
    memset(&si, 0, sizeof(si));
    si.exe_dir = "C:\\Tools\\nutshell";
    si.exe_path = "C:\\Tools\\nutshell\\nutshell.exe";
    si.version = "1.2.36";
    si.pid = 8124;
    si.queue_bytes = queue_bytes;
    si.recorder_bytes = recorder_bytes;
    return dlog_start_with(fk_platform(), &si);
}
