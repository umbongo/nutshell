#ifndef CONGA_CORE_LOGGER_H
#define CONGA_CORE_LOGGER_H

typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

void log_init(const char *file_path, LogLevel min_level);
void log_close(void);
void log_write(LogLevel level, const char *msg);

#define LOG_DEBUG(msg) log_write(LOG_LEVEL_DEBUG, msg)
#define LOG_INFO(msg)  log_write(LOG_LEVEL_INFO, msg)
#define LOG_WARN(msg)  log_write(LOG_LEVEL_WARN, msg)
#define LOG_ERROR(msg) log_write(LOG_LEVEL_ERROR, msg)

#endif