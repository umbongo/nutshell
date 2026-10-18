/* src/core/dlog.h -- Nutshell's one runtime logger.
 *
 * Design: docs/superpowers/specs/2026-10-17-debug-logging-design.md. In short:
 * six categories, a level per category, one event per line in logfmt, a
 * 1 MB write queue drained by a writer thread, and a 4 MB in-memory recorder
 * that an issue mark (Ctrl+Shift+M) dumps to its own file.
 *
 * Portable: everything here compiles for the native tests. The things that
 * differ per OS (lock, clock, files, the writer thread) sit behind
 * DlogPlatform; dlog_sys.c supplies the Windows one and tests install a fake
 * (tests/fake_dlog.h).
 *
 * THREADS. dlog_emit()/DLOG() may be called from any thread once dlog_start()
 * has returned, and never block on I/O or on the writer. dlog_start(),
 * dlog_shutdown() and dlog_apply()/dlog_configure() are UI-thread calls (one
 * at a time). dlog_set_thread_tag() is per thread. dlog_issue_mark() is a
 * UI-thread call.
 */
#ifndef NUTSHELL_CORE_DLOG_H
#define NUTSHELL_CORE_DLOG_H

#include <stddef.h>
#include "dlog_types.h"
#include "dlog_files.h"
#include "dlog_snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Settings;

#define DLOG_LINE_MAX       4096          /* one line, LF included */
#define DLOG_QUEUE_BYTES    (1u << 20)    /* write queue: 1 MB */
#define DLOG_RECORDER_BYTES (4u << 20)    /* recorder ring: 4 MB */
#define DLOG_MAX_PATH       320
#define DLOG_REPEAT_MS      5000u         /* a run of identical lines is flushed this often */
#define DLOG_SECRETS_MAX    8
#define DLOG_WAIT_FOREVER   0xFFFFFFFFu   /* DlogPlatform.wait(): until wake() */

#if defined(__GNUC__)
/* gnu_printf, not printf: on MinGW "printf" means the MSVCRT dialect, which rejects %zu and %lld. */
#define DLOG_PRINTF(a, b) __attribute__((format(gnu_printf, a, b)))
#else
#define DLOG_PRINTF(a, b)
#endif

/* ---- The platform seam ---------------------------------------------------------- */

/* Everything OS-specific. Function pointers marked (opt) may be NULL. Paths are
 * the ANSI paths the rest of Nutshell uses. The tests' fake keeps files in
 * memory; see tests/fake_dlog.c. */
typedef struct DlogPlatform {
    void      (*lock)(void);                   /* one global lock, held for two memcpys */
    void      (*unlock)(void);
    int       (*try_lock)(void);               /* (opt) 1 = acquired; crash handler only */
    long long (*unix_ms)(void);                /* UTC milliseconds since 1970 */
    int       (*utc_offset_min)(void);         /* minutes east of UTC; cached by the platform */
    unsigned  (*mono_ms)(void);                /* monotonic milliseconds, wraps at 2^32 */
    void      (*sleep_ms)(unsigned ms);        /* (opt) */
    /* The writer thread. thread_start NULL = synchronous mode: no thread, the
     * caller pumps (dlog_pump()), as the tests do. */
    int       (*thread_start)(void (*fn)(void *), void *arg);   /* (opt) 1 = started */
    int       (*thread_join)(void);                              /* (opt) 1 = the writer has exited, 0 = gave up waiting */
    void      (*wait)(unsigned ms);                              /* (opt) writer sleeps until wake() or timeout; DLOG_WAIT_FOREVER = until wake() */
    void      (*wake)(void);                                     /* (opt) */
    /* Files. Errors are OS error codes (0 = success). */
    int       (*mkdir_p)(const char *dir);
    int       (*file_open)(const char *path, void **handle, unsigned long long *size);  /* append/create */
    int       (*file_write)(void *handle, const void *buf, size_t len);
    void      (*file_close)(void *handle);
    int       (*file_write_all)(const char *path, const void *buf, size_t len);        /* create/overwrite */
    long      (*file_read)(const char *path, unsigned long long off, void *buf, size_t cap); /* bytes, -1 error */
    int       (*list_dir)(const char *dir, DlogFileInfo *out, size_t cap);   /* count, -1 = no such dir */
    int       (*remove_file)(const char *path);                              /* 0 = deleted */
    const char *(*local_appdata)(void);                                      /* (opt) %LOCALAPPDATA% */
    void      (*err_text)(int err, char *buf, size_t cap);                   /* (opt) */
} DlogPlatform;

/* The Windows platform (dlog_sys.c); NULL on other hosts. */
const DlogPlatform *dlog_platform_system(void);

/* ---- Lifecycle ------------------------------------------------------------------- */

typedef struct {
    const char *exe_dir;     /* a relative log folder resolves against this */
    const char *exe_path;    /* for the app.start line */
    const char *version;     /* "1.2.36" */
    unsigned    pid;
    const char *extra;       /* (opt) preformatted `key=value ...` appended to app.start */
    size_t      queue_bytes;     /* 0 = DLOG_QUEUE_BYTES (tests use small rings; min 8 KB) */
    size_t      recorder_bytes;  /* 0 = DLOG_RECORDER_BYTES (min 8 KB) */
} DlogStartInfo;

/* Start the logger with the system platform and a writer thread, debugging off
 * (errors and warnings only, 1 MB files). Lazy: no folder and no file exist
 * until the first line that passes the filter. The calling thread is tagged
 * "ui" and installs the crash filter. Returns 1, or 0 if the platform is
 * missing or the logger is already started. Call dlog_configure() once the
 * config is loaded. */
int  dlog_start(const DlogStartInfo *info);
/* Same with an explicit platform (tests). */
int  dlog_start_with(const DlogPlatform *platform, const DlogStartInfo *info);
/* Drain, flush pending repeats, close the file, wipe the secrets, free the
 * rings. Safe when not started. */
void dlog_shutdown(void);
int  dlog_is_started(void);

/* ---- Configuration -------------------------------------------------------------------- */

typedef struct {
    int           enabled;                       /* "Enable debugging" */
    unsigned char levels[DLOG_CAT_COUNT];        /* DlogLevel per category, used when enabled */
    int           include_content;               /* "Include terminal content" */
    int           screenshot;                    /* "Save a screenshot with each issue mark" */
    int           recorder;                      /* "Keep recent detail for issue marks" */
    unsigned char recorder_level;                /* DlogLevel the recorder captures (default Trace) */
    char          dir[256];                      /* the setting, as typed ("logs", "%TEMP%\x") */
    int           max_file_mb;                   /* 1-100 */
    int           max_total_mb;                  /* 5-5000 */
} DlogConfig;

void dlog_config_defaults(DlogConfig *c);
void dlog_config_from_settings(const struct Settings *s, DlogConfig *c);
/* Apply a configuration: thresholds change at once (atomically), a changed
 * folder closes the current file with a log.moved line and continues in the
 * new one, and a dlog.config line records the new levels if a file is open.
 * Writing the lines needs no restart. */
void dlog_apply(const DlogConfig *c);
/* dlog_config_from_settings + dlog_apply, and registers the AI API key as a
 * known secret to scrub from content. */
void dlog_configure(const struct Settings *s);

int  dlog_enabled(void);              /* "Enable debugging" is on */
int  dlog_content_enabled(void);      /* one relaxed read; the DLOG_CONTENT gate */
int  dlog_screenshot_enabled(void);

/* ---- Emitting ------------------------------------------------------------------------------- */

extern unsigned char g_dlog_capture[DLOG_CAT_COUNT];   /* highest event level that file OR recorder wants, per category */

static inline int dlog_capture_level(int cat)
{
    return (int)__atomic_load_n(&g_dlog_capture[cat], __ATOMIC_RELAXED);
}

/* Tag the calling thread for the file: "ui", "ssh", "ai", "shell", "set", "log"
 * (up to 7 chars; anything else is cut). Untagged threads print "?". */
void dlog_set_thread_tag(const char *tag);

/* Write one event. `fmt` is a printf-style list of key=value fields, e.g.
 * "rows=%d name=%s": a printf subset (d i u x X o c s p f g e, flags, width,
 * precision, hh h l ll z j t), checked by the compiler. A %s value is quoted
 * and escaped as needed; a field whose key looks sensitive (pass, secret,
 * token, key, auth, cookie) is written =[REDACTED]; never put quotes around a
 * %s in the format. Terminal, chat and command text must go through
 * DLOG_CONTENT instead. sid 0 prints "-", n prints "s<n>". `fmt` must not be
 * empty: use DLOG_EV for an event without fields. Never blocks: a line that
 * does not fit the queue is dropped and counted (errors keep a reserve). */
void dlog_emit(DlogCat cat, DlogEvLevel lvl, unsigned sid, const char *event,
               const char *fmt, ...) DLOG_PRINTF(5, 6);
/* Same with fields already formatted (a snapshot line, or none: ""). Not a
 * printf: the escape-safe subset is copied as is, keys are not redacted by
 * the formatter (the final-line scanner still runs). */
void dlog_emit_fields(DlogCat cat, DlogEvLevel lvl, unsigned sid, const char *event,
                      const char *fields);
/* Content: escaped (\e \r \n \t \\ \" \xNN), scrubbed of known secrets, written
 * as key="..." len=<total>. Dropped unless "Include terminal content" is on. */
void dlog_emit_content(DlogCat cat, DlogEvLevel lvl, unsigned sid, const char *event,
                       const char *key, const void *data, size_t len);

/* The call sites. `event` and `fmt` are string literals. The one-compare check
 * costs a byte read; arguments are not evaluated when nothing captures the
 * level. The source gate (tests/test_dlog_gate.c) rejects secret-looking
 * identifiers in any DLOG argument list. */
#define DLOG(cat, lvl, sid, ...) \
    do { if ((int)(lvl) <= dlog_capture_level((int)(cat))) \
             dlog_emit((cat), (lvl), (sid), __VA_ARGS__); } while (0)

/* An event with no fields (an empty printf format fails -Werror under
 * -Wformat-zero-length, so there is no DLOG(..., "")). */
#define DLOG_EV(cat, lvl, sid, event) \
    do { if ((int)(lvl) <= dlog_capture_level((int)(cat))) \
             dlog_emit_fields((cat), (lvl), (sid), (event), ""); } while (0)

/* Every content site, so a reviewer greps one macro. `data` need not be NUL
 * terminated. */
#define DLOG_CONTENT(cat, lvl, sid, event, key, data, len) \
    do { if ((int)(lvl) <= dlog_capture_level((int)(cat)) && dlog_content_enabled()) \
             dlog_emit_content((cat), (lvl), (sid), (event), (key), (data), (len)); } while (0)

/* ---- Formatting, redaction and escaping (pure; dlog_format.c) --------------------------------------- */

/* 1 when a field key must have its value redacted: it contains (any case) pass,
 * secret, token, key, auth or cookie, unless it is one of a short list of
 * known-safe names (auth_method, tokens_in, key_name ...). */
int  dlog_key_is_sensitive(const char *key);

/* A logfmt value: bare when non-empty and free of space, quote, '=', backslash,
 * control characters and invalid UTF-8; otherwise "double quoted" with \" and
 * \\ and \xNN escapes. NULL prints "-". NUL-terminated; returns the length. */
size_t dlog_fmt_value(char *out, size_t cap, const char *value);

/* One complete line, exactly as the file gets it:
 *   <timestamp> <LEVEL> <cat> <sid> <thread> <event>[ <fields>]\n
 * `fields` is already logfmt (copied, never parsed beyond keeping quotes
 * balanced). Cut at DLOG_LINE_MAX / cap on a UTF-8 and escape boundary and
 * ended with " truncated=1". Returns the length including the LF (0 if cap is
 * too small for the prefix). Pure: the caller supplies the time. */
size_t dlog_format_line(char *buf, size_t cap, long long unix_ms, int offset_min,
                        int ev_level, int cat, unsigned sid, const char *thread,
                        const char *event, const char *fields);

/* Format one event body ("event key=value ...") exactly as dlog_emit would,
 * without prefix and LF, cut to cap (" truncated=1"). Returns the length,
 * excluding NUL. */
size_t dlog_format_body(char *out, size_t cap, const char *event, const char *fmt, ...) DLOG_PRINTF(4, 5);

/* The last line of defence, run on every line before it is queued: replaces
 * with [REDACTED] the value of a whole-key password/passphrase/pass/secret/
 * token/api_key/apikey/authorization/private_key=..., a "Bearer <token>", an
 * sk-<16+ chars> key, $dpapi$v1$ and $aes256gcm$v1$ blobs and PEM private key
 * blocks. `line` is len bytes (a trailing LF is kept) with room for cap bytes;
 * returns the new length. Ported from the second implementation. */
size_t dlog_redact_line(char *line, size_t len, size_t cap);

/* Known secrets, scrubbed from DLOG_CONTENT text (replaced by [REDACTED]).
 *
 *   dlog_secret_add(s)     register s: 3 to 255 bytes, at most DLOG_SECRETS_MAX at
 *                          once; returns 1 if it is now registered (also when it
 *                          already was: each add is counted), 0 if it is too short,
 *                          too long, NULL or the table is full.
 *   dlog_secret_remove(s)  undo ONE add of s (two connections with the same
 *                          password share a slot and each remove releases one of
 *                          them); the stored copy is wiped with secure_zero() when
 *                          the last add is undone.
 *   dlog_secret_clear()    forget everything (dlog_shutdown() calls it).
 *
 * The connect path registers a profile's password and key passphrase before it
 * authenticates and removes them when the attempt ends (pair every add with a
 * remove on every exit path); dlog_configure() registers the AI API key. Both
 * calls are thread-safe and callable before dlog_start(). A secret split
 * across two terminal reads of the same session is still caught: the scrub
 * keeps the tail that could be the start of a secret and joins it to the next
 * read. */
int  dlog_secret_add(const char *secret);
void dlog_secret_remove(const char *secret);
void dlog_secret_clear(void);
/* Copy src to dst replacing every known secret with [REDACTED]. Returns the
 * length written (NUL-terminated); stops when dst is full. Whole-buffer only:
 * dlog_emit_content() uses the stream-aware scrub. */
size_t dlog_scrub_copy(char *dst, size_t dst_cap, const char *src, size_t n);

/* Escape raw terminal bytes for a log line (see dlog_emit_content). The state
 * persists per stream so an OSC 52 clipboard payload split across reads is
 * still withheld and replaced by "[osc52 bytes=N]". *consumed (optional) is how
 * many input bytes made it into `out`; fewer than n when `out` filled up. When
 * it fills up the rest of the input is still run through the state machine
 * (nothing more is written), so an OSC 52 introducer in the unwritten tail is
 * seen and the next call withholds its payload. A partial OSC introducer at the
 * end of a chunk is held back for the next call. Returns the output length
 * (NUL-terminated if cap > 0).
 *
 * Limit: the state starts at "not inside a sequence", so turning "Include
 * terminal content" on while a terminal is in the middle of an OSC 52 payload
 * logs the rest of that one sequence (its introducer was never seen). */
typedef struct {
    int    state;
    size_t osc_bytes;
} DlogEscState;
void   dlog_esc_init(DlogEscState *st);
size_t dlog_escape_bytes(DlogEscState *st, const void *in, size_t n,
                         char *out, size_t cap, size_t *consumed);

/* ---- Writing and flushing ------------------------------------------------------------------------- */

/* Drain the queue to the file now (synchronous mode, shutdown, tests). Returns
 * 0 if another thread is already pumping. */
int  dlog_pump(void);
/* Ask the writer to drain now, without waiting. */
void dlog_flush(void);
/* Drain and wait up to `ms` for the writer to catch up (the AI tool, issue
 * marks; never call it from a producer path). 1 when everything queued before
 * the call is on disk. */
int  dlog_flush_wait(unsigned ms);
/* Flush repeat runs older than DLOG_REPEAT_MS (the writer calls this every
 * 250 ms; dlog_pump() does too). */
void dlog_tick(void);
/* Crash handler: note the fatal error, then drain without blocking on a lock. */
void dlog_crash_note(unsigned long code, const void *addr);

/* ---- State and recorder ------------------------------------------------------------------------------ */

typedef struct {
    int                enabled;
    int                started;
    int                file_open;        /* the main log file is open */
    int                mem_only;         /* no folder was writable: events live in the recorder only */
    char               dir[DLOG_MAX_PATH];   /* folder in use, or the resolved setting before the first file */
    char               path[DLOG_MAX_PATH];  /* current file, "" before the first line / when memory-only */
    char               note[256];        /* why a fallback or memory-only was chosen, else "" */
    char               stem[64];         /* nutshell-YYYYMMDD-HHMMSS-p<pid> */
    unsigned long long dropped_total;    /* lines dropped because the queue was full */
    unsigned           issues;           /* issue marks so far */
} DlogStatus;
void dlog_status(DlogStatus *st);

/* Bytes and file count in the folder in use, by listing it (does I/O; for the
 * Settings "using 37 MB in 9 files" line). */
void dlog_folder_usage(unsigned long long *bytes, unsigned *files);

/* Copy the recorder, oldest line first (whole lines; when buf is smaller than
 * the recorder the newest lines are kept). Returns the bytes copied; with buf
 * NULL returns the bytes available. */
size_t dlog_recorder_copy(char *buf, size_t cap);
size_t dlog_recorder_size(void);

/* ---- Issue marks -------------------------------------------------------------------------------------- */

typedef struct {
    unsigned n;                           /* the issue number, 1, 2, ... per process */
    int      ok;                          /* the recorder file was written, or is queued to be (pending) */
    int      pending;                     /* ok, but the writer had not finished within the wait */
    int      dump_failed;                 /* a folder was wanted but the dump could not be queued (both job slots busy);
                                           * an issue.dump_failed WARN line was written */
    char     recorder_path[DLOG_MAX_PATH];    /* full path of nutshell-...issue<n>.log, "" when !ok */
    char     screenshot_path[DLOG_MAX_PATH];  /* full path to save the PNG to, "" if not wanted/possible */
} DlogIssue;

/* Mark an issue: writes issue.mark (ISSUE level, always), the snapshot lines
 * (ISSUE), snapshots the recorder and hands the dump to <stem>.issue<n>.log to
 * the writer thread (opening the folder, retention and the file write all
 * happen there, never on the caller), then waits for the writer up to 500 real
 * milliseconds. Call it on the UI thread BEFORE anything is painted, with the
 * snapshot filled in. want_screenshot = the "Save a screenshot" setting; the
 * caller saves the PNG to screenshot_path. snap may be NULL. Returns 1 if the
 * mark was recorded (ok reports the recorder file). */
int  dlog_issue_mark(const DlogSnapshot *snap, int want_screenshot, DlogIssue *out);

/* ---- Reading it back (dlog_query.c uses these) ------------------------------------------------------------- */

const DlogPlatform *dlog_platform(void);   /* the platform in use, NULL when not started */

#ifdef __cplusplus
}
#endif

#endif /* NUTSHELL_CORE_DLOG_H */
