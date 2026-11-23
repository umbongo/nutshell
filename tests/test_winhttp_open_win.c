/* tests/test_winhttp_open_win.c -- ns_winhttp_open() and the AI gate.
 * Built only by `make wintest`. No network: WinHttpOpen makes no request. */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include "test_framework.h"
#include "winhttp_open.h"
#include "ai_gate.h"

int test_winhttp_open_refuses_while_off(void)
{
    TEST_BEGIN();
    ai_gate_set_enabled(0);
    ai_gate_set_demo_override(0);
    SetLastError(0);
    HINTERNET h = ns_winhttp_open(NULL);
    DWORD err = GetLastError();
    ASSERT_NULL(h);
    ASSERT_EQ((int)err, (int)ERROR_ACCESS_DENIED);
    if (h) WinHttpCloseHandle(h);
    /* An agent string changes nothing. */
    h = ns_winhttp_open(L"Nutshell/test");
    ASSERT_NULL(h);
    TEST_END();
}

int test_winhttp_open_opens_while_on(void)
{
    TEST_BEGIN();
    ai_gate_set_enabled(1);
    HINTERNET h = ns_winhttp_open(NULL);
    ASSERT_NOT_NULL(h);
    if (h) WinHttpCloseHandle(h);
    ai_gate_set_enabled(0);
    /* The demo override opens it too, with enabled off. */
    ai_gate_set_demo_override(1);
    h = ns_winhttp_open(NULL);
    ASSERT_NOT_NULL(h);
    if (h) WinHttpCloseHandle(h);
    ai_gate_set_demo_override(0);
    ASSERT_NULL(ns_winhttp_open(NULL));
    TEST_END();
}

#endif /* _WIN32 */
