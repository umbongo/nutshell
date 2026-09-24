#ifndef NUTSHELL_LOCAL_SHELL_PROBE_H
#define NUTSHELL_LOCAL_SHELL_PROBE_H

/* The Win32 half of LocalShellProbe -- the three real API calls
 * (GetFileAttributesA, GetEnvironmentVariableA, RegQueryValueExA) that
 * local_shell_resolve(), local_shell_resolve_bare() and
 * local_shell_list_available() (all in src/core/local_shell.c, portable and
 * tested against a fake table) are allowed to ask the machine. Shared by
 * window.c (starting a session) and session_manager.c (the profile editor's
 * "Automatic" dropdown), so the two can never drift apart.
 */

#include "local_shell.h"

/* Fills probe with the real callbacks. */
void local_shell_fill_probe(LocalShellProbe *probe);

#endif /* NUTSHELL_LOCAL_SHELL_PROBE_H */
