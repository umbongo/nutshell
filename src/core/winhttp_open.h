#ifndef NUTSHELL_CORE_WINHTTP_OPEN_H
#define NUTSHELL_CORE_WINHTTP_OPEN_H

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>

/* The one place a WinHTTP session is opened: automatic proxy settings
 * (falling back to the default proxy where the automatic type is not
 * supported) and TLS 1.2 or newer (TLS 1.2 alone where the 1.3 flag is
 * refused). `agent` is the User-Agent string; NULL means L"Nutshell/1.0".
 * Timeouts are left to the caller. Returns NULL on failure (GetLastError()
 * holds the WinHttpOpen error); close with WinHttpCloseHandle(). */
HINTERNET ns_winhttp_open(const wchar_t *agent);

#endif /* _WIN32 */

#endif /* NUTSHELL_CORE_WINHTTP_OPEN_H */
