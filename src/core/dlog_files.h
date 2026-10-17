/* src/core/dlog_files.h -- the pure parts of the debug log's file handling:
 * timestamps, file names, folder resolution, rotation and retention
 * decisions, and the FORMAT.txt text. No I/O, no clock, no Windows; every
 * function is tested natively (tests/test_dlog_files.c). dlog.c does the
 * I/O through the DlogPlatform seam and asks these functions what to do.
 *
 * Names:  nutshell-YYYYMMDD-HHMMSS-p<pid>.log        the run's first file
 *         nutshell-YYYYMMDD-HHMMSS-p<pid>.<n>.log    rotation continues (n >= 2)
 *         nutshell-YYYYMMDD-HHMMSS-p<pid>.issue<n>.log / .png   issue marks
 */
#ifndef NUTSHELL_CORE_DLOG_FILES_H
#define NUTSHELL_CORE_DLOG_FILES_H

#include <stddef.h>

/* One directory entry, as the platform lists it. */
typedef struct {
    char               name[96];
    unsigned long long size;
    long long          mtime_ms;   /* unix milliseconds, UTC */
} DlogFileInfo;

/* ---- Time ------------------------------------------------------------------ */

#define DLOG_TS_LEN 29   /* 2026-10-17T14:03:22.418+01:00 */

typedef struct {
    int year, mon, day, hour, min, sec, ms;
} DlogTm;

/* Pure calendar arithmetic (no localtime(), no TZ database): the platform
 * supplies unix milliseconds and the UTC offset in minutes east of UTC. */
void      dlog_civil_from_unix_ms(long long unix_ms, int offset_min, DlogTm *tm);
long long dlog_unix_ms_from_civil(const DlogTm *tm, int offset_min);

/* "2026-10-17T14:03:22.418+01:00": exactly DLOG_TS_LEN characters, NUL after.
 * `out` must hold DLOG_TS_LEN + 1 bytes. */
void      dlog_format_timestamp(long long unix_ms, int offset_min, char *out);

/* Parse the timestamp at the start of `s` (which may continue after it).
 * Returns 1 and sets *unix_ms, or 0 when `s` does not start with one. */
int       dlog_parse_timestamp(const char *s, long long *unix_ms);

/* ---- Names ----------------------------------------------------------------- */

/* "nutshell-20261017-140310-p8124" */
size_t dlog_stem_name(char *out, size_t cap, long long unix_ms, int offset_min,
                      unsigned pid);
/* part <= 1: "<stem>.log"; part >= 2: "<stem>.<part>.log". 0 when it does not fit. */
size_t dlog_file_name(char *out, size_t cap, const char *stem, unsigned part);
/* "<stem>.issue<n>.<ext>"; ext is "log" or "png". 0 when it does not fit. */
size_t dlog_issue_file_name(char *out, size_t cap, const char *stem, unsigned n,
                            const char *ext);

typedef struct {
    char     stamp[16];   /* "20261017-140310" */
    unsigned pid;
    unsigned part;        /* main files: 1, 2, 3 ...; issue files: 0 */
    unsigned issue;       /* issue files: n >= 1; main files: 0 */
    int      is_png;
} DlogNameInfo;

/* 1 when `name` is one of our files (any of the shapes above), filling `info`
 * if non-NULL; 0 for anything else (FORMAT.txt, look-alikes, other cases). */
int  dlog_name_parse(const char *name, DlogNameInfo *info);
/* Chronological order of two parsed names: <0, 0, >0. The run's stamp, then the
 * pid, then main files by part before issue files by number. */
int  dlog_name_compare(const DlogNameInfo *a, const DlogNameInfo *b);

/* ---- Folder ---------------------------------------------------------------- */

/* Looks a %NAME% up; NULL when unset. */
typedef const char *(*DlogEnvLookup)(const char *name, void *ctx);

/* The log folder for a setting. Empty or NULL means "logs". %NAME% is expanded
 * first (an unknown name stays literal); an absolute setting ("C:\x", "C:/x",
 * "\\server\share", a rooted "\x" or "/x") is used as it is; anything else is
 * joined to exe_dir. A trailing separator is trimmed (a bare drive root keeps
 * its one). The separator of exe_dir is reused when it has one ('/' only when
 * it has no '\'), else '\'. Returns 1 and fills out, or 0 when the result
 * does not fit in cap, or the setting is relative and exe_dir is empty. */
int  dlog_resolve_dir(const char *setting, const char *exe_dir, char *out, size_t cap);
int  dlog_resolve_dir_ex(const char *setting, const char *exe_dir,
                         DlogEnvLookup lookup, void *ctx, char *out, size_t cap);
/* "<local_appdata>\Nutshell\logs" (it may end in a separator). 0 if it does not fit
 * or local_appdata is empty. */
int  dlog_fallback_dir(const char *local_appdata, char *out, size_t cap);
/* dir + separator + name (the separator dir already uses). 0 if it does not fit. */
int  dlog_join(char *out, size_t cap, const char *dir, const char *name);

/* For the Browse button: abs_dir inside exe_dir becomes the relative remainder
 * ("." for exe_dir itself), anything else is copied as is. Separators compare
 * equal either way and case is ignored (Windows paths). 1, or 0 on overflow or
 * NULL. Ported from the second implementation. */
int  dlog_relativize_dir(const char *abs_dir, const char *exe_dir, char *out, size_t cap);

/* ---- Rotation and retention -------------------------------------------------- */

#define DLOG_OFF_FILE_BYTES  (1ULL * 1024ULL * 1024ULL)   /* debugging off: 1 MB */

/* The per-file cap: off -> 1 MB; on -> max_file_mb clamped to 1..100. */
unsigned long long dlog_file_cap_bytes(int enabled, int max_file_mb);
/* The folder cap, max_total_mb clamped to 5..5000. */
unsigned long long dlog_total_cap_bytes(int max_total_mb);
/* A file that already holds data rotates once the next write would pass the
 * cap; an empty file never rotates (one line larger than the cap still goes
 * in). 1 to rotate. */
int  dlog_should_rotate(unsigned long long cur_size, unsigned long long add,
                        unsigned long long cap);

/* Retention. `state[i]` (may be NULL) is 0 = untouched, 1 = already deleted
 * (does not count), 2 = tried and failed (still counts, never picked again).
 * Returns the index of the oldest deletable file while the total of the files
 * still on disk exceeds total_cap, or -1 when done. Only files whose names
 * parse as ours are ever considered, and `current` (the file being written;
 * may be NULL) is never picked. */
int    dlog_retention_next(const DlogFileInfo *files, size_t n, const char *current,
                           unsigned long long total_cap, const unsigned char *state);
/* All victims assuming every deletion succeeds, oldest first. */
size_t dlog_retention_pick(const DlogFileInfo *files, size_t n, unsigned long long total_cap,
                           const char *current, size_t *victims, size_t victims_cap);
/* Sum of the sizes of our files (the "using 37 MB in 9 files" line). */
unsigned long long dlog_files_total(const DlogFileInfo *files, size_t n, size_t *count);

/* ---- FORMAT.txt ---------------------------------------------------------------- */

/* The text of FORMAT.txt for an AI (or a person) reading the folder cold.
 * The first line carries the version so it can be rewritten when it changes.
 * Returns the length (excluding NUL); output is cut if cap is too small. */
size_t dlog_format_text(char *out, size_t cap, const char *version);

#endif /* NUTSHELL_CORE_DLOG_FILES_H */
