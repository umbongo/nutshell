#ifndef NUTSHELL_TOOLTIP_H
#define NUTSHELL_TOOLTIP_H

#include "tab_manager.h"   /* TabStatus */
#include <stddef.h>

/* Format a duration in seconds as a human-readable string.
 * Examples:  0 → "0s", 59 → "59s", 90 → "1m 30s", 3661 → "1h 1m 1s".
 * Always null-terminates buf. */
void tooltip_format_duration(unsigned long secs, char *buf, size_t n);

/* Build the full tooltip string for a tab.
 *   status        — connection state (TAB_IDLE, TAB_CONNECTING, etc.)
 *   name          — session name (may be NULL or empty → "(unnamed)")
 *   host          — remote hostname (may be NULL or empty)
 *   username      — SSH username  (may be NULL or empty)
 *   elapsed_secs  — seconds since connect_time (0 when not connected)
 *   log_path      — path to session log file, or NULL if not logging
 *   buf           — output buffer
 *   n             — buf capacity (including NUL)
 *
 * Output format (connected/connecting):
 *   Session Name
 *   user@host
 *   Connected (1h 30m 45s)
 *   Logging: enabled
 */
void tooltip_build_text(TabStatus status,
                        const char *name,
                        const char *host,
                        const char *username,
                        unsigned long elapsed_secs,
                        const char *log_path,
                        char *buf, size_t n);

#endif
