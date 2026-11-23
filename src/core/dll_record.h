#ifndef NUTSHELL_DLL_RECORD_H
#define NUTSHELL_DLL_RECORD_H

#include <stddef.h>

/* Record of every DLL Nutshell's own code asks Windows for: the delay-load
 * notification hook (src/ui/dll_hook_win.c) and the run-time LoadLibraryExW
 * sites note their names here. Used by the hidden --self-report switch to
 * prove that nothing of the AI assistant loads while it is off. */

#define DLL_RECORD_MAX  64
#define DLL_RECORD_NAME 64

/* Note a requested DLL. Case-insensitive and de-duplicated (the first
 * spelling is kept). NULL, empty and over-long names are ignored; when the
 * 64 entries are full further new names are dropped. Thread-safe. */
void dll_record_note(const char *name);

/* 1 when name (any case) has been noted, else 0. NULL gives 0. */
int dll_record_was_requested(const char *name);

/* Writes "requested: <dll>\n" for every noted name in first-noted order,
 * then "module: <dll>\n" for each of modules[0..n-1] (modules may be NULL
 * when n is 0). The result is NUL-terminated and never overruns cap; a
 * line that does not fit is left out whole. Returns the length written
 * (excluding the NUL); 0 when cap is 0. */
size_t dll_record_format(char *out, size_t cap, const char *const *modules,
                         size_t n);

/* Clears the record (tests). */
void dll_record_reset(void);

#endif
