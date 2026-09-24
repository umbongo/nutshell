/* The Win32 half of LocalShellProbe. See local_shell_probe.h. */

#include <windows.h>
#include "local_shell_probe.h"
#include <string.h>

static int probe_exists(void *ctx, const char *path)
{
    (void)ctx;
    if (!path || !path[0]) return 0;
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES &&
            !(attr & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
}

static int probe_env(void *ctx, const char *name, char *out, size_t out_size)
{
    (void)ctx;
    if (!name || !out || out_size == 0u) return 0;
    out[0] = '\0';
    DWORD n = GetEnvironmentVariableA(name, out, (DWORD)out_size);
    if (n == 0u || n >= (DWORD)out_size) { out[0] = '\0'; return 0; }
    return out[0] ? 1 : 0;
}

/* key is "HKLM\\SOFTWARE\\..." -- only HKLM and HKCU are understood, which
 * is all local_shell.c's search asks for. Reads both the 64- and 32-bit
 * views so a 32-bit Git install is still found. */
static int probe_registry_string(void *ctx, const char *key, const char *value,
                                 char *out, size_t out_size)
{
    (void)ctx;
    if (!key || !value || !out || out_size == 0u) return 0;
    out[0] = '\0';

    HKEY root;
    const char *sub;
    if (strncmp(key, "HKLM\\", 5) == 0)      { root = HKEY_LOCAL_MACHINE; sub = key + 5; }
    else if (strncmp(key, "HKCU\\", 5) == 0) { root = HKEY_CURRENT_USER;  sub = key + 5; }
    else return 0;

    static const DWORD views[2] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    for (int i = 0; i < 2; i++) {
        HKEY h;
        if (RegOpenKeyExA(root, sub, 0,
                          KEY_QUERY_VALUE | views[i], &h) != ERROR_SUCCESS)
            continue;
        DWORD type = 0;
        DWORD len = (DWORD)out_size;
        LONG rc = RegQueryValueExA(h, value, NULL, &type,
                                   (LPBYTE)out, &len);
        RegCloseKey(h);
        if (rc == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
            if (len >= (DWORD)out_size) len = (DWORD)out_size - 1u;
            out[len] = '\0';
            /* RegQueryValueExA counts the NUL; trim any extra. */
            out[out_size - 1u] = '\0';
            if (out[0]) return 1;
        }
        out[0] = '\0';
    }
    return 0;
}

/* M2: GetFullPathNameA collapses "." and "..", doubled separators, and
 * forward slashes to back -- exactly the canonicalisation local_shell.c
 * needs to compare a hand-typed custom executable path against a detected
 * install's own path. `path` reaching here is always already absolute (see
 * local_shell.h's doc comment on this callback), so this never resolves
 * anything against the current directory. */
static int probe_normalize_path(void *ctx, const char *path, char *out, size_t out_size)
{
    (void)ctx;
    if (!path || !path[0] || !out || out_size == 0u) return 0;
    DWORD n = GetFullPathNameA(path, (DWORD)out_size, out, NULL);
    if (n == 0u || n >= (DWORD)out_size) { out[0] = '\0'; return 0; }
    return 1;
}

void local_shell_fill_probe(LocalShellProbe *probe)
{
    memset(probe, 0, sizeof(*probe));
    probe->exists          = probe_exists;
    probe->env             = probe_env;
    probe->registry_string = probe_registry_string;
    probe->normalize_path   = probe_normalize_path;
    probe->ctx             = NULL;
}
