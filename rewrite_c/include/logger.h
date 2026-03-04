#ifndef LOGGER_H
#define LOGGER_H

/*
 * Simple logger — writes to stderr and optionally to a file.
 *
 * Log calls take a plain pre-formatted string (not a format string).
 * Format your message with snprintf() first, then pass it to LOG_*.
 * This eliminates format-string vulnerabilities at log call sites.
 *
 * Example:
 *   char buf[256];
 *   snprintf(buf, sizeof(buf), "Connected to %s:%d", host, port);
 *   LOG_INFO(buf);
 *
 * log_init() may be called at most once. log_close() flushes and
 * closes the file (if any). Calling log_write() before log_init()
 * is safe — it writes to stderr at INFO level.
 */

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

/* Initialise the logger. Pass NULL or "" for file_path to disable
 * file output. Messages below min_level are discarded. */
void log_init(const char *file_path, LogLevel min_level);

/* Flush and close the log file (if open). */
void log_close(void);

/* Write a pre-formatted message at the given level. */
void log_write(LogLevel level, const char *msg);

#define LOG_DEBUG(msg) log_write(LOG_LEVEL_DEBUG, (msg))
#define LOG_INFO(msg)  log_write(LOG_LEVEL_INFO,  (msg))
#define LOG_WARN(msg)  log_write(LOG_LEVEL_WARN,  (msg))
#define LOG_ERROR(msg) log_write(LOG_LEVEL_ERROR, (msg))

#endif /* LOGGER_H */
