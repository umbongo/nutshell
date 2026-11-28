#include "log_open_error.h"

#include <stdio.h>
#include <string.h>

size_t log_open_error_text(const char *path, int err, char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
    const char *reason = (err != 0) ? strerror(err) : "unknown error";
    if (!reason || !reason[0]) reason = "unknown error";
    int n = snprintf(out, cap,
                     "Logging was not started. Nutshell could not open the log file\n"
                     "%s\n"
                     "for writing: %s.\n"
                     "Check that the folder exists and can be written to, or choose "
                     "another log folder in Settings.",
                     (path && path[0]) ? path : "(no path)", reason);
    if (n < 0) { out[0] = '\0'; return 0; }
    return strlen(out);
}
