/* src/core/settings_debug.h -- the pure text and number rules behind the
 * Settings > Debugging page (spec 2026-10-17-debug-logging-design.md,
 * section 9). Portable: the Win32 page (src/ui/settings.c) feeds these and
 * shows what they return; tests/test_settings_debug.c covers them natively.
 * Row geometry lives in settings_layout.h (settings_debug_layout). */
#ifndef NUTSHELL_CORE_SETTINGS_DEBUG_H
#define NUTSHELL_CORE_SETTINGS_DEBUG_H

#include <stddef.h>
#include "dlog.h"

#define SETTINGS_DEBUG_FILE_MB_MIN    1
#define SETTINGS_DEBUG_FILE_MB_MAX    100
#define SETTINGS_DEBUG_TOTAL_MB_MIN   5
#define SETTINGS_DEBUG_TOTAL_MB_MAX   5000

/* The label a level shows in the combos: "Off", "Errors", "Info", "Debug",
 * "Trace" (the config words are lower case). NULL for an out-of-range level. */
const char *settings_debug_level_label(int level);

/* The category row label: "Rendering", "Terminal", "AI Assist",
 * "SSH connection", "Local shell", "General". NULL for an unknown category. */
const char *settings_debug_cat_label(int cat);

/* Read a numeric field: surrounding spaces ignored; digits only; clamped into
 * [lo, hi] (a number too large for int is hi). Empty or anything else keeps
 * `current` (itself clamped). Never negative. */
int settings_debug_parse_mb(const char *text, int lo, int hi, int current);

/* The "using 37 MB in 9 files" line. No files: "no log files yet"; under a
 * megabyte: "using under 1 MB in 2 files"; one file: "1 file". Returns the
 * length written (NUL-terminated, cut to cap). */
size_t settings_debug_usage_text(char *out, size_t cap,
                                 unsigned long long bytes, unsigned files);

/* The "Current log file" line from the logger's state: the path in use, with
 * the fallback reason after it if there is one; "logging to memory only ..."
 * when no folder is writable; "none yet, created at the first error" (the
 * first event when debugging is on) before any file exists. */
size_t settings_debug_current_text(char *out, size_t cap, const DlogStatus *st);

#endif /* NUTSHELL_CORE_SETTINGS_DEBUG_H */
