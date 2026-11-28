#include "winhttp_open.h"
#include "ai_gate.h"
#include <stdio.h>

int ns_tls_enforce(void *session, const NsTlsOps *ops)
{
    if (!session || !ops || !ops->set_protocols || !ops->close_session)
        return 0;
    /* Windows 10 before 1903 refuses the 1.3 flag, so retry with 1.2 alone. */
    if (ops->set_protocols(session, NS_TLS_FLAG_TLS1_2 | NS_TLS_FLAG_TLS1_3,
                           ops->user))
        return 1;
    if (ops->set_protocols(session, NS_TLS_FLAG_TLS1_2, ops->user))
        return 1;
    ops->close_session(session, ops->user);
    return 0;
}

void ns_winhttp_failure_text(unsigned long code, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return;
    if (code == NS_ERR_TLS_UNAVAILABLE)
        snprintf(buf, buf_size,
                 "A secure connection could not be set up: TLS 1.2 or newer "
                 "is not available on this system, so nothing was sent");
    else
        snprintf(buf, buf_size, "WinHttpOpen failed: %lu", code);
}

#ifdef _WIN32

/* Older MinGW headers lack these; the values are the documented ones. */
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

static int tls_set_protocols(void *session, unsigned long flags, void *user)
{
    DWORD protocols = (DWORD)flags;
    (void)user;
    return WinHttpSetOption((HINTERNET)session, WINHTTP_OPTION_SECURE_PROTOCOLS,
                            &protocols, sizeof(protocols)) ? 1 : 0;
}

static void tls_close_session(void *session, void *user)
{
    (void)user;
    WinHttpCloseHandle((HINTERNET)session);
}

HINTERNET ns_winhttp_open(const wchar_t *agent)
{
    /* The one choke point for every AI request (chat, models list, web
     * tools): nothing leaves the process while the assistant is off. */
    if (!ai_gate_on()) {
        SetLastError(ERROR_ACCESS_DENIED);
        return NULL;
    }
    if (!agent) agent = L"Nutshell/1.0";

    /* The automatic type needs Windows 8.1 or later; fall back otherwise. */
    HINTERNET session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return NULL;

    /* TLS 1.2 or newer, or no session at all (ns_tls_enforce closes it). The
     * option only affects https connections. */
    NsTlsOps ops = { tls_set_protocols, tls_close_session, NULL };
    if (!ns_tls_enforce((void *)session, &ops)) {
        SetLastError((DWORD)NS_ERR_TLS_UNAVAILABLE);
        return NULL;
    }
    return session;
}

#else

typedef int ns_winhttp_open_unused;

#endif /* _WIN32 */
