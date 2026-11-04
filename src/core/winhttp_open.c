#include "winhttp_open.h"

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

HINTERNET ns_winhttp_open(const wchar_t *agent)
{
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

    /* TLS 1.2 or newer. Windows 10 before 1903 refuses the 1.3 flag, so
     * retry with 1.2 alone. The option only affects https connections. */
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
                    | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS,
                          &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS,
                         &protocols, sizeof(protocols));
    }
    return session;
}

#else

typedef int ns_winhttp_open_unused;

#endif /* _WIN32 */
