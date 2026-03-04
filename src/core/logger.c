#include "logger.h"
#include <stdio.h>
#include <time.h>

static FILE    *_log_file  = NULL;
static LogLevel _log_level = LOG_LEVEL_INFO;

static const char *level_label(LogLevel level)
{
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO ";
        case LOG_LEVEL_WARN:  return "WARN ";
        case LOG_LEVEL_ERROR: return "ERROR";
        default:              return "?????";
    }
}

void log_init(const char *file_path, LogLevel min_level)
{
    _log_level = min_level;

    if (file_path && file_path[0] != '\0') {
        _log_file = fopen(file_path, "a");
        /* Non-fatal: if the file can't be opened we fall back to stderr only */
    }
}

void log_close(void)
{
    if (_log_file) {
        fclose(_log_file);
        _log_file = NULL;
    }
}

void log_write(LogLevel level, const char *msg)
{
    if (level < _log_level) {
        return;
    }

    time_t     now = time(NULL);
    const struct tm *t = localtime(&now);  /* not thread-safe; acceptable for Phase 1 */

    char timebuf[20];
    if (t) {
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", t);
    } else {
        timebuf[0] = '\0';
    }

    fprintf(stderr, "[%s] %s %s\n", timebuf, level_label(level), msg);

    if (_log_file) {
        fprintf(_log_file, "[%s] %s %s\n", timebuf, level_label(level), msg);
        fflush(_log_file);
    }
}
