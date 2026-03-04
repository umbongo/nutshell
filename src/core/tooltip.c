#include "tooltip.h"
#include <stdio.h>
#include <string.h>

void tooltip_format_duration(unsigned long secs, char *buf, size_t n)
{
    if (!buf || n == 0) return;

    unsigned long h = secs / 3600u;
    unsigned long m = (secs % 3600u) / 60u;
    unsigned long s = secs % 60u;

    if (h > 0) {
        (void)snprintf(buf, n, "%luh %lum %lus", h, m, s);
    } else if (m > 0) {
        (void)snprintf(buf, n, "%lum %lus", m, s);
    } else {
        (void)snprintf(buf, n, "%lus", s);
    }
}

void tooltip_build_text(TabStatus status,
                        const char *name,
                        const char *host,
                        const char *username,
                        unsigned long elapsed_secs,
                        const char *log_path,
                        char *buf, size_t n)
{
    if (!buf || n == 0) return;
    buf[0] = '\0';

    const char *nm = (name && name[0]) ? name : "(unnamed)";

    const char *state_str;
    switch (status) {
        case TAB_CONNECTING:   state_str = "Connecting";   break;
        case TAB_CONNECTED:    state_str = "Connected";    break;
        case TAB_DISCONNECTED: state_str = "Disconnected"; break;
        case TAB_IDLE: /* fall-through */
        default:               state_str = "Idle";         break;
    }

    if (status == TAB_DISCONNECTED || status == TAB_IDLE) {
        (void)snprintf(buf, n,
                       "Name:    %s\n"
                       "Status:  %s\n\n"
                       "[L] = toggle session logging",
                       nm, state_str);
        return;
    }

    /* Connected / Connecting */
    char dur[32];
    tooltip_format_duration(elapsed_secs, dur, sizeof(dur));

    const char *h = (host && host[0])         ? host     : "(unknown)";
    const char *u = (username && username[0]) ? username : "(unknown)";
    const char *log_str = (log_path && log_path[0]) ? "enabled" : "disabled";

    (void)snprintf(buf, n,
                   "Name:    %s\n"
                   "Host:    %s\n"
                   "User:    %s\n"
                   "Status:  %s (%s)\n"
                   "Logging: %s\n\n"
                   "[L] = toggle session logging",
                   nm, h, u, state_str, dur, log_str);
}
