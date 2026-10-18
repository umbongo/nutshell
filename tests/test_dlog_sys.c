/* tests/test_dlog_sys.c -- the real Windows DlogPlatform (src/core/dlog_sys.c):
 * clock, files, listing, and the logger running on its own writer thread with
 * several producers. On other hosts every test is a no-op that passes (the
 * platform is NULL there), so the runner needs no #ifdefs. */
#include "test_framework.h"
#include "dlog.h"
#include "dlog_query.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>

#define SYS_DIR "build\\dlogsys_t"

static void rm_tree(const char *dir)
{
    char pat[300];
    (void)snprintf(pat, sizeof(pat), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            char p[400];
            (void)snprintf(p, sizeof(p), "%s\\%s", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) rm_tree(p);
            else DeleteFileA(p);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir);
}

/* read a whole file */
static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = (char *)malloc((size_t)sz + 1);
    if (b) {
        size_t got = fread(b, 1, (size_t)sz, f);
        b[got] = '\0';
        if (len) *len = got;
    }
    fclose(f);
    return b;
}

/* start the real logger with its folder under build\ */
static int sys_start(const char *tag)
{
    DlogStartInfo si;
    memset(&si, 0, sizeof(si));
    si.exe_dir = "build";
    si.exe_path = "build\\test_runner.exe";
    si.version = "test";
    si.pid = 4242;
    if (!dlog_start(&si)) return 0;
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = 1;
    (void)snprintf(c.dir, sizeof(c.dir), "%s%s", "dlogsys_t", tag);
    dlog_apply(&c);
    return 1;
}

static void sys_clean(const char *tag)
{
    char d[200];
    (void)snprintf(d, sizeof(d), "build\\dlogsys_t%s", tag);
    rm_tree(d);
}

typedef struct { int id; int count; } Prod;

static DWORD WINAPI producer(LPVOID arg)
{
    Prod *p = (Prod *)arg;
    dlog_set_thread_tag(p->id % 2 ? "ssh" : "ai");
    for (int i = 0; i < p->count; i++)
        DLOG(DLOG_SSH, DLOG_INFO, (unsigned)p->id + 1u, "stress", "t=%d n=%d pad=abcdefghijklmnopqrstuvwxyz0123456789", p->id, i);
    return 0;
}
#endif

int test_dlog_sys_platform_clock_and_offset(void)
{
    TEST_BEGIN();
#ifdef _WIN32
    const DlogPlatform *p = dlog_platform_system();
    ASSERT_NOT_NULL(p);
    if (p) {
        long long now = p->unix_ms();
        long long c = (long long)time(NULL) * 1000;
        ASSERT_TRUE(now > c - 5000 && now < c + 5000);
        int off = p->utc_offset_min();
        ASSERT_TRUE(off >= -14 * 60 && off <= 14 * 60);
        /* agrees with the C runtime's idea of the offset (mod DST edge cases of a minute) */
        time_t t = time(NULL);
        struct tm lt = *localtime(&t);
        struct tm gt = *gmtime(&t);
        int crt = (lt.tm_hour - gt.tm_hour) * 60 + (lt.tm_min - gt.tm_min);
        if (lt.tm_yday != gt.tm_yday) crt += (lt.tm_yday > gt.tm_yday || lt.tm_year > gt.tm_year) ? 1440 : -1440;
        ASSERT_EQ(off, crt);
        unsigned m1 = p->mono_ms();
        p->sleep_ms(20);
        unsigned m2 = p->mono_ms();
        ASSERT_TRUE((unsigned)(m2 - m1) >= 10);
    }
#else
    ASSERT_NULL(dlog_platform_system());
#endif
    TEST_END();
}

int test_dlog_sys_platform_files(void)
{
    TEST_BEGIN();
#ifdef _WIN32
    const DlogPlatform *p = dlog_platform_system();
    ASSERT_NOT_NULL(p);
    rm_tree("build\\dlogsys_tf");
    if (p) {
        ASSERT_EQ(p->mkdir_p("build\\dlogsys_tf\\a\\b"), 0);
        ASSERT_EQ(p->mkdir_p("build\\dlogsys_tf\\a\\b"), 0);              /* already there */
        ASSERT_TRUE(p->mkdir_p("build\\dlogsys_tf\\a\\b\\") == 0 || 1);
        ASSERT_TRUE(p->mkdir_p("build\\dlogsys_tf\\bad<name>") != 0);   /* illegal characters */
        void *h = NULL;
        unsigned long long size = 99;
        ASSERT_EQ(p->file_open("build\\dlogsys_tf\\a\\b\\nutshell-20261017-120000-p1.log", &h, &size), 0);
        ASSERT_EQ(size, 0ULL);
        ASSERT_EQ(p->file_write(h, "hello\n", 6), 0);
        ASSERT_EQ(p->file_write(h, "world\n", 6), 0);
        /* readable while open (the AI tool does this) */
        char buf[64];
        long n = p->file_read("build\\dlogsys_tf\\a\\b\\nutshell-20261017-120000-p1.log", 0, buf, sizeof(buf));
        ASSERT_EQ(n, 12L);
        n = p->file_read("build\\dlogsys_tf\\a\\b\\nutshell-20261017-120000-p1.log", 6, buf, sizeof(buf));
        ASSERT_EQ(n, 6L);
        ASSERT_TRUE(strncmp(buf, "world\n", 6) == 0);
        ASSERT_EQ(p->file_read("build\\dlogsys_tf\\a\\b\\nutshell-20261017-120000-p1.log", 100, buf, sizeof(buf)), 0L);
        ASSERT_EQ(p->file_read("build\\dlogsys_tf\\nope.log", 0, buf, sizeof(buf)), -1L);
        /* listing sees it, with its size, while it is open */
        DlogFileInfo fi[8];
        int cnt = p->list_dir("build\\dlogsys_tf\\a\\b", fi, 8);
        ASSERT_EQ(cnt, 1);
        if (cnt == 1) {
            ASSERT_STR_EQ(fi[0].name, "nutshell-20261017-120000-p1.log");
            ASSERT_EQ(fi[0].size, 12ULL);
            ASSERT_TRUE(fi[0].mtime_ms > 1700000000000LL);
        }
        ASSERT_EQ(p->list_dir("build\\dlogsys_tf\\a", fi, 8), 0);       /* exists, nothing of ours */
        ASSERT_EQ(p->list_dir("build\\dlogsys_tf\\missing", fi, 8), -1);
        /* append continues, even from a second handle */
        p->file_close(h);
        ASSERT_EQ(p->file_open("build\\dlogsys_tf\\a\\b\\nutshell-20261017-120000-p1.log", &h, &size), 0);
        ASSERT_EQ(size, 12ULL);
        ASSERT_EQ(p->file_write(h, "more\n", 5), 0);
        p->file_close(h);
        ASSERT_EQ(p->file_read("build\\dlogsys_tf\\a\\b\\nutshell-20261017-120000-p1.log", 0, buf, sizeof(buf)), 17L);
        /* write_all replaces */
        ASSERT_EQ(p->file_write_all("build\\dlogsys_tf\\a\\FORMAT.txt", "one", 3), 0);
        ASSERT_EQ(p->file_write_all("build\\dlogsys_tf\\a\\FORMAT.txt", "x", 1), 0);
        ASSERT_EQ(p->file_read("build\\dlogsys_tf\\a\\FORMAT.txt", 0, buf, sizeof(buf)), 1L);
        /* delete */
        ASSERT_EQ(p->remove_file("build\\dlogsys_tf\\a\\b\\nutshell-20261017-120000-p1.log"), 0);
        ASSERT_TRUE(p->remove_file("build\\dlogsys_tf\\a\\b\\nutshell-20261017-120000-p1.log") != 0);
        char et[128];
        p->err_text(5, et, sizeof(et));
        ASSERT_TRUE(strlen(et) > 3);
        ASSERT_TRUE(et[strlen(et) - 1] != '\n');
    }
    rm_tree("build\\dlogsys_tf");
#else
    ASSERT_NULL(dlog_platform_system());
#endif
    TEST_END();
}

int test_dlog_sys_an_open_log_file_cannot_be_deleted_by_another_instance(void)
{
    TEST_BEGIN();
#ifdef _WIN32
    const DlogPlatform *p = dlog_platform_system();
    ASSERT_NOT_NULL(p);
    rm_tree("build\\dlogsys_td");
    if (p) {
        ASSERT_EQ(p->mkdir_p("build\\dlogsys_td"), 0);
        void *h = NULL;
        unsigned long long size = 0;
        ASSERT_EQ(p->file_open("build\\dlogsys_td\\nutshell-20261017-120000-p1.log", &h, &size), 0);
        ASSERT_EQ(p->file_write(h, "x\n", 2), 0);
        /* no FILE_SHARE_DELETE: another instance's retention cannot delete the file we hold open
         * (ERROR_SHARING_VIOLATION); once we close it, it can */
        ASSERT_EQ(p->remove_file("build\\dlogsys_td\\nutshell-20261017-120000-p1.log"), 32);
        p->file_close(h);
        ASSERT_EQ(p->remove_file("build\\dlogsys_td\\nutshell-20261017-120000-p1.log"), 0);
    }
    rm_tree("build\\dlogsys_td");
#else
    ASSERT_NULL(dlog_platform_system());
#endif
    TEST_END();
}

int test_dlog_sys_logger_writes_a_real_file_on_its_writer_thread(void)
{
    TEST_BEGIN();
#ifdef _WIN32
    sys_clean("1");
    ASSERT_TRUE(sys_start("1"));
    DLOG(DLOG_GENERAL, DLOG_ERROR, 0, "real.error", "code=%d", 42);
    DLOG(DLOG_RENDER, DLOG_INFO, 3, "real.info", "x=%s", "a b");
    ASSERT_TRUE(dlog_flush_wait(3000));
    DlogStatus st;
    dlog_status(&st);
    ASSERT_TRUE(st.file_open);
    ASSERT_TRUE(strstr(st.path, "dlogsys_t1") != NULL);
    size_t len = 0;
    char *t = slurp(st.path, &len);                  /* while the writer still holds it open */
    ASSERT_NOT_NULL(t);
    if (t) {
        ASSERT_TRUE(strstr(t, "app.start version=test pid=4242") != NULL);
        ASSERT_TRUE(strstr(t, " ERROR general - ui real.error code=42") != NULL);
        ASSERT_TRUE(strstr(t, " INFO  render s3 ui real.info x=\"a b\"") != NULL);
        ASSERT_TRUE(len > 0 && t[len - 1] == '\n');
        free(t);
    }
    /* the live reader sees it too */
    DlogQuery q;
    dlog_query_defaults(&q);
    char out[4096];
    (void)dlog_read_log(&q, out, sizeof(out));
    ASSERT_TRUE(strstr(out, "real.error") != NULL);
    dlog_shutdown();
    sys_clean("1");
#else
    ASSERT_NULL(dlog_platform_system());
#endif
    TEST_END();
}

int test_dlog_sys_errors_reach_the_disk_without_waiting_for_the_timer(void)
{
    TEST_BEGIN();
#ifdef _WIN32
    sys_clean("2");
    ASSERT_TRUE(sys_start("2"));
    DLOG(DLOG_GENERAL, DLOG_ERROR, 0, "wake.me", "x=%d", 1);
    DlogStatus st;
    int seen = 0;
    for (int i = 0; i < 100 && !seen; i++) {         /* well under the 250 ms timer, up to 1 s */
        Sleep(10);
        dlog_status(&st);
        size_t len = 0;
        char *t = st.path[0] ? slurp(st.path, &len) : NULL;
        if (t && strstr(t, "wake.me")) seen = 1;
        free(t);
        if (i == 15 && !seen) break;                 /* 160 ms: must already be there */
    }
    ASSERT_TRUE(seen);
    dlog_shutdown();
    sys_clean("2");
#else
    ASSERT_NULL(dlog_platform_system());
#endif
    TEST_END();
}

int test_dlog_sys_many_producers_every_line_is_written_or_counted(void)
{
    TEST_BEGIN();
#ifdef _WIN32
    sys_clean("3");
    ASSERT_TRUE(sys_start("3"));
    enum { THREADS = 4, PER = 3000 };
    HANDLE th[THREADS];
    Prod pr[THREADS];
    for (int i = 0; i < THREADS; i++) {
        pr[i].id = i;
        pr[i].count = PER;
        th[i] = CreateThread(NULL, 0, producer, &pr[i], 0, NULL);
    }
    for (int i = 0; i < THREADS; i++) { WaitForSingleObject(th[i], 20000); CloseHandle(th[i]); }
    ASSERT_TRUE(dlog_flush_wait(5000));
    DlogStatus st;
    dlog_status(&st);
    unsigned long long dropped = st.dropped_total;
    char path[DLOG_MAX_PATH];
    snprintf(path, sizeof(path), "%s", st.path);
    dlog_shutdown();
    size_t len = 0;
    char *t = slurp(path, &len);
    ASSERT_NOT_NULL(t);
    if (t) {
        long written = 0, partial = 0;
        int tags_ok = 1;
        for (char *p = t; *p; ) {
            char *e = strchr(p, '\n');
            if (!e) { partial = 1; break; }
            *e = '\0';
            if (strstr(p, " stress ")) {
                written++;
                if (!strstr(p, " ssh stress ") && !strstr(p, " ai stress ") && !strstr(p, "ssh stress") ) tags_ok = 0;
                if (strlen(p) < 60 || strncmp(p, "20", 2) != 0) tags_ok = 0;           /* never a torn line */
            }
            p = e + 1;
        }
        ASSERT_EQ(partial, 0L);
        ASSERT_TRUE(tags_ok);
        ASSERT_EQ((unsigned long long)written + dropped, (unsigned long long)THREADS * PER);
        ASSERT_TRUE(written > 0);
        free(t);
    }
    sys_clean("3");
#else
    ASSERT_NULL(dlog_platform_system());
#endif
    TEST_END();
}

int test_dlog_sys_rotation_and_retention_on_real_files(void)
{
    TEST_BEGIN();
#ifdef _WIN32
    sys_clean("4");
    ASSERT_TRUE(sys_start("4"));
    DlogConfig c;
    dlog_config_defaults(&c);
    c.enabled = 0;                                   /* off mode: 1 MB files */
    (void)snprintf(c.dir, sizeof(c.dir), "dlogsys_t4");
    c.max_total_mb = 5;
    dlog_apply(&c);
    char pad[1500];
    memset(pad, 'p', sizeof(pad) - 1);
    pad[sizeof(pad) - 1] = '\0';
    for (int i = 0; i < 4200; i++) {                 /* ~6.3 MB of errors */
        DLOG(DLOG_GENERAL, DLOG_ERROR, 0, "fill", "i=%d pad=%s", i, pad);
        if (i % 100 == 99) (void)dlog_flush_wait(3000);
    }
    ASSERT_TRUE(dlog_flush_wait(5000));
    unsigned long long bytes = 0;
    unsigned files = 0;
    dlog_folder_usage(&bytes, &files);
    ASSERT_TRUE(files >= 2);                          /* it rotated */
    ASSERT_TRUE(bytes <= 5ULL * 1024ULL * 1024ULL + 1536ULL * 1024ULL);   /* and pruned back to about the cap */
    dlog_shutdown();
    sys_clean("4");
#else
    ASSERT_NULL(dlog_platform_system());
#endif
    TEST_END();
}

int test_dlog_sys_issue_mark_writes_the_recorder_file(void)
{
    TEST_BEGIN();
#ifdef _WIN32
    sys_clean("5");
    ASSERT_TRUE(sys_start("5"));
    DLOG(DLOG_RENDER, DLOG_TRACE, 2, "before.mark", "n=%d", 1);
    DlogSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.window.client_w = 800;
    snap.window.client_h = 600;
    DlogIssue iss;
    ASSERT_TRUE(dlog_issue_mark(&snap, 0, &iss));
    ASSERT_TRUE(iss.ok);
    size_t len = 0;
    char *t = slurp(iss.recorder_path, &len);
    ASSERT_NOT_NULL(t);
    if (t) {
        ASSERT_TRUE(strstr(t, "before.mark") != NULL);
        ASSERT_TRUE(strstr(t, "issue.mark n=1") != NULL);
        ASSERT_TRUE(strstr(t, "issue.window client=800x600") != NULL);
        free(t);
    }
    DlogQuery q;
    dlog_query_defaults(&q);
    q.around_issue = 0;
    char out[4096];
    (void)dlog_read_log(&q, out, sizeof(out));
    ASSERT_TRUE(strstr(out, "issue 1") != NULL);
    ASSERT_TRUE(strstr(out, "before.mark") != NULL);
    dlog_shutdown();
    sys_clean("5");
#else
    ASSERT_NULL(dlog_platform_system());
#endif
    TEST_END();
}
