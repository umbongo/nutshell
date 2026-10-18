/* src/ui/open_dir.h -- open a folder in Explorer, and only a folder.
 *
 * The debug log folder is a setting the user (or a hand-edited config) types.
 * ShellExecute "open" on a path that names a file would launch that file, so
 * the path is checked first: only an existing directory is opened; anything
 * else is refused with a note. Used by Help > Open Debug Log Folder and the
 * Settings > Debugging "Open" button.
 */
#ifndef NUTSHELL_UI_OPEN_DIR_H
#define NUTSHELL_UI_OPEN_DIR_H

#include <windows.h>
#include <shellapi.h>

/* Returns 1 when Explorer was asked to open `dir`, 0 when it was refused (a
 * message box says why). */
static inline int open_dir_in_explorer(HWND hwnd, const char *dir, const char *title)
{
    const DWORD attr = GetFileAttributesA(dir);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        MessageBoxA(hwnd, "That folder does not exist and could not be created.", title,
                    MB_OK | MB_ICONWARNING);
        return 0;
    }
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        MessageBoxA(hwnd, "That path is a file, not a folder, so it was not opened. "
                    "Choose a folder for the log.", title, MB_OK | MB_ICONWARNING);
        return 0;
    }
    (void)ShellExecuteA(hwnd, "open", dir, NULL, NULL, SW_SHOWNORMAL);
    return 1;
}

#endif /* NUTSHELL_UI_OPEN_DIR_H */
