#ifndef NUTSHELL_GDIP_PNG_H
#define NUTSHELL_GDIP_PNG_H

#ifdef _WIN32
#include <windows.h>

/* Save what is on screen over hwnd's client area as a PNG file, through the
 * GDI+ flat API (GDI+ must already be started, as window.c does in
 * WM_CREATE). The pixels are copied from the screen, not rendered through
 * WM_PRINT, so nothing in the window is repainted or healed by the capture:
 * a stale band shows in the picture as it shows to the user. `path` is the
 * ANSI path the debug log hands out (DlogIssue.screenshot_path). Returns 1
 * on success, 0 when the window is minimised or empty, a GDI call fails or
 * the file cannot be written. */
int gdip_png_save_client(HWND hwnd, const char *path);

#endif
#endif
