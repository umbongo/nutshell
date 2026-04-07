/* redraw_log.h — REDRAW_DEBUG file logging helper.
 *
 * Include this in any .c file that uses REDRAW_DEBUG logging.
 * Writes to %TEMP%\nutshell_redraw.log, appending across calls.
 * Thread-safety not required: all callers are on the Windows message thread.
 */
#ifndef REDRAW_LOG_H
#define REDRAW_LOG_H

#ifdef REDRAW_DEBUG
#include <stdio.h>
#include <windows.h>

static FILE *_rdlog_file(void)
{
    static FILE *f = NULL;
    if (!f) {
        char tmp[MAX_PATH];
        GetTempPathA(MAX_PATH, tmp);
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%snutshell_redraw.log", tmp);
        f = fopen(path, "a");
    }
    return f;
}

#define REDRAW_LOG(fmt, ...) \
    do { \
        FILE *_f = _rdlog_file(); \
        if (_f) { fprintf(_f, fmt, ##__VA_ARGS__); fflush(_f); } \
    } while (0)

#else
#define REDRAW_LOG(fmt, ...) ((void)0)
#endif /* REDRAW_DEBUG */

#endif /* REDRAW_LOG_H */
