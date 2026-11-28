#ifndef NUTSHELL_CORE_WINHTTP_OPEN_H
#define NUTSHELL_CORE_WINHTTP_OPEN_H

#include <stddef.h>

/* Failure code ns_winhttp_open() leaves in GetLastError() when it cannot
 * enforce TLS 1.2 or newer (a customer-defined code: bit 29 set). */
#define NS_ERR_TLS_UNAVAILABLE 0xE0004C01UL

/* The two protocol flags WinHTTP takes (same values as winhttp.h). */
#define NS_TLS_FLAG_TLS1_2 0x00000800UL
#define NS_TLS_FLAG_TLS1_3 0x00002000UL

/* The two operations the TLS decision needs, so it is testable without
 * WinHTTP (Wine never refuses the option). */
typedef struct {
    /* Apply the protocol set to the session; non-zero on success. */
    int  (*set_protocols)(void *session, unsigned long flags, void *user);
    /* Close the session handle. */
    void (*close_session)(void *session, void *user);
    void *user;
} NsTlsOps;

/* Enforce TLS 1.2 or newer on `session`: try 1.2 | 1.3, then 1.2 alone. On
 * success returns 1 and the session stays open. When both are refused it
 * closes the session exactly once (ops->close_session) and returns 0: no
 * path continues with the OS default protocol set. */
int ns_tls_enforce(void *session, const NsTlsOps *ops);

/* Plain-words text for a failed ns_winhttp_open(): the TLS message for
 * NS_ERR_TLS_UNAVAILABLE, "WinHttpOpen failed: <code>" otherwise. */
void ns_winhttp_failure_text(unsigned long code, char *buf, size_t buf_size);

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>

/* The one place a WinHTTP session is opened: automatic proxy settings
 * (falling back to the default proxy where the automatic type is not
 * supported) and TLS 1.2 or newer (TLS 1.2 alone where the 1.3 flag is
 * refused). `agent` is the User-Agent string; NULL means L"Nutshell/1.0".
 * Timeouts are left to the caller. While the AI gate (ai_gate.h) is off it
 * returns NULL with ERROR_ACCESS_DENIED. If TLS 1.2 cannot be enforced it closes
 * the session and returns NULL with NS_ERR_TLS_UNAVAILABLE (it never carries on
 * with weaker protocols). Returns NULL on failure (GetLastError()
 * holds the error); close with WinHttpCloseHandle(). */
HINTERNET ns_winhttp_open(const wchar_t *agent);

#endif /* _WIN32 */

#endif /* NUTSHELL_CORE_WINHTTP_OPEN_H */
