/* voice_net_win.c -- the one counted voice network opener. See the header. */

#include "voice_net_win.h"
#include "dlog.h"
#include "voice_gate.h"
#include "winhttp_open.h"

static volatile LONG g_opens;

HINTERNET voice_winhttp_open(void)
{
    if (!voice_gate_on()) {
        SetLastError(ERROR_ACCESS_DENIED);
        return NULL;
    }
    HINTERNET h = ns_winhttp_open(L"Nutshell/1.0");
    if (h) InterlockedIncrement(&g_opens);
    return h;
}

HINTERNET voice_net_open(VoiceNetKind k)
{
    if (!voice_gate_on()) {
        SetLastError(ERROR_ACCESS_DENIED);
        return NULL;
    }
    DLOG(DLOG_AI, DLOG_INFO, 0, "voice.net_open", "kind=%d", (int)k);
    return voice_winhttp_open();
}

long voice_net_open_count(void)
{
    return (long)InterlockedCompareExchange(&g_opens, 0, 0);
}

void voice_net_test_reset_count(void)
{
    InterlockedExchange(&g_opens, 0);
}
