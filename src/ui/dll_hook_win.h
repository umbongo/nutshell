#ifndef NUTSHELL_DLL_HOOK_WIN_H
#define NUTSHELL_DLL_HOOK_WIN_H

/* Writes the DLL record (dll_record.h: "requested: <dll>" lines) followed
 * by "module: <dll>" for every module loaded in this process (toolhelp
 * snapshot) to path, as UTF-8. A relative path is relative to the working
 * directory. Returns 1 on success, 0 when the file cannot be written. */
int ns_self_report_write(const char *path);

#endif
