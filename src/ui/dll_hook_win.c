/* src/ui/dll_hook_win.c -- the delay-load notification hook and the
 * --self-report writer.
 *
 * crypt32.dll and winhttp.dll are delay-imports (see the Makefile). The
 * helper that loads them, __delayLoadHelper2, comes from libmingwex.a's
 * delayimp.o (the installed libdelayimp.a is empty) and calls
 * __pfnDliNotifyHook2 before each LoadLibrary. libmingwex's delay-n.o
 * defines that variable as BSS; ours is an initialised definition, which
 * the linker prefers over a BSS common one. `used` keeps it through
 * -flto and --gc-sections. */
#include <winsock2.h>
#include <windows.h>
#include <delayimp.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include "dll_hook_win.h"
#include "dll_record.h"
#include "voice_net_win.h"
#include "voice_ledger.h"

static FARPROC WINAPI ns_dli_hook(unsigned notify, PDelayLoadInfo info)
{
    if (notify == dliNotePreLoadLibrary && info != NULL && info->szDll != NULL)
        dll_record_note(info->szDll);
    return NULL; /* let the helper do the load */
}

__attribute__((used)) PfnDliHook __pfnDliNotifyHook2 = ns_dli_hook;

#define SELF_REPORT_MAX_MODULES 256
#define SELF_REPORT_NAME_MAX    MAX_PATH

int ns_self_report_write(const char *path, int sapi_state)
{
    if (path == NULL || path[0] == '\0') return 0;

    static char names[SELF_REPORT_MAX_MODULES][SELF_REPORT_NAME_MAX];
    const char *mods[SELF_REPORT_MAX_MODULES];
    size_t n = 0;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                           GetCurrentProcessId());
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me;
        memset(&me, 0, sizeof(me));
        me.dwSize = sizeof(me);
        BOOL ok = Module32FirstW(snap, &me);
        while (ok && n < SELF_REPORT_MAX_MODULES) {
            int w = WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, names[n],
                                        SELF_REPORT_NAME_MAX, NULL, NULL);
            if (w > 0) { mods[n] = names[n]; n++; }
            me.dwSize = sizeof(me);
            ok = Module32NextW(snap, &me);
        }
        CloseHandle(snap);
    }

    static char text[(DLL_RECORD_MAX + SELF_REPORT_MAX_MODULES) * 96];
    size_t len = dll_record_format(text, sizeof(text), mods, n);
    /* The voice opener's count, after the DLL record: an idle launch shows 0. */
    int extra = snprintf(text + len, sizeof(text) - len, "voice-net-opens: %ld\n",
                         voice_net_open_count());
    if (extra > 0 && (size_t)extra < sizeof(text) - len) len += (size_t)extra;
    /* The resource ledger's sum (capture, sessions, speech requests, render
     * clients, ...): an idle launch holds none. */
    long voice_res = 0;
    for (int r = 0; r < (int)VRES_COUNT; r++) voice_res += voice_ledger_count((VoiceRes)r);
    extra = snprintf(text + len, sizeof(text) - len, "voice-resources: %ld\n", voice_res);
    if (extra > 0 && (size_t)extra < sizeof(text) - len) len += (size_t)extra;
    /* --self-report-sapi's control: -1 not asked (no line), 0 or 1 */
    if (sapi_state >= 0) {
        extra = snprintf(text + len, sizeof(text) - len, "sapi-control: %s\n",
                         sapi_state == 1 ? "held" : "unavailable");
        if (extra > 0 && (size_t)extra < sizeof(text) - len) len += (size_t)extra;
    }

    wchar_t wpath[MAX_PATH];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH) <= 0)
        return 0;
    HANDLE f = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD wrote = 0;
    BOOL ok = WriteFile(f, text, (DWORD)len, &wrote, NULL) && wrote == (DWORD)len;
    CloseHandle(f);
    return ok ? 1 : 0;
}
