/* tests/fake_dlog.h -- a deterministic DlogPlatform for the native tests: an
 * in-memory file system, a settable clock, no writer thread (tests pump). */
#ifndef FAKE_DLOG_H
#define FAKE_DLOG_H

#include "dlog.h"

#define FK_MAX_FILES 96
#define FK_MAX_DIRS  16

typedef struct {
    char   path[DLOG_MAX_PATH];
    char  *data;
    size_t len;
    long long mtime;
    int    open;          /* a handle is currently open on it */
    int    undeletable;   /* remove_file refuses (another instance holds it) */
} FkFile;

typedef struct {
    long long unix_ms;    /* the clock; tests advance it */
    int       offset_min;
    unsigned  mono;
    FkFile    files[FK_MAX_FILES];
    int       nfiles;
    char      dirs[FK_MAX_DIRS][DLOG_MAX_PATH];
    int       ndirs;
    char      deny_prefix[DLOG_MAX_PATH];   /* mkdir/open under this fail with error 5 */
    char      local_appdata[DLOG_MAX_PATH];
    int       wakes;
    int       sleeps;
    int       drain_on_sleep;     /* sleep_ms() runs dlog_pump(): a writer that keeps up */
    int       advance_on_sleep;   /* sleep_ms() advances the clocks by the slept ms */
    int       write_calls;
    int       fail_writes;        /* file_write returns an error */
    int       try_lock_result;    /* what try_lock() answers */
    int       lock_depth;         /* current lock nesting (must never go below 0) */
    int       max_lock_depth;
    int       removed;
    int       clock_calls;        /* unix_ms() calls: a skipped line must make none */
    int       lock_count;         /* lock() calls */
} FakeDlog;

extern FakeDlog g_fk;

/* Reset everything: empty file system, clock at 2026-10-17 13:03:10.000 UTC with
 * a +01:00 offset, mono 1000, local_appdata "C:\Users\me\AppData\Local". */
void fk_reset(void);
const DlogPlatform *fk_platform(void);

/* dlog_start_with(fk_platform(), ...) with the standard info (exe_dir
 * "C:\Tools\nutshell", pid 8124, version "1.2.36") and, if the sizes are
 * non-zero, small rings. Returns the result of dlog_start_with(). */
int fk_start(size_t queue_bytes, size_t recorder_bytes);

/* Free the fake's memory (after dlog_shutdown()). */
void fk_free(void);

FkFile *fk_find(const char *path);
/* The standard folder and stem for the standard info. */
#define FK_LOG_DIR  "C:\\Tools\\nutshell\\logs"
#define FK_STEM     "nutshell-20261017-140310-p8124"
#define FK_LOG_FILE FK_LOG_DIR "\\" FK_STEM ".log"
/* File contents as a NUL-terminated string (static buffer; ring of 4). NULL if absent. */
const char *fk_text(const char *path);
void fk_put_file(const char *path, const char *text, unsigned long long pad_to, long long mtime);
int  fk_count_in(const char *dir);                      /* files directly in dir */
int  fk_count_lines_containing(const char *text, const char *needle);
void fk_advance(unsigned ms);                           /* clocks forward */

#endif
