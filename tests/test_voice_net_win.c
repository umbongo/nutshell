/* tests/test_voice_net_win.c -- the one voice network opener. Built only by
 * `make wintest`. */

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <string.h>
#include "test_framework.h"
#include "voice_net_win.h"
#include "voice_gate.h"
#include "ai_gate.h"
#include "ai_http.h"

int test_voice_net_counts(void)
{
    TEST_BEGIN();
    voice_net_test_reset_count();
    /* gate off: no session, no count */
    ai_gate_set_enabled(0);
    voice_gate_set_enabled(0);
    SetLastError(0);
    HINTERNET h = voice_net_open(VNK_STT_UPLOAD);
    ASSERT_TRUE(h == NULL);
    ASSERT_EQ((int)GetLastError(), (int)ERROR_ACCESS_DENIED);
    ASSERT_TRUE(voice_winhttp_open() == NULL);
    ASSERT_EQ((int)voice_net_open_count(), 0);
    /* voice on but the assistant off: still closed (voice needs the AI gate) */
    voice_gate_set_enabled(1);
    ASSERT_TRUE(voice_net_open(VNK_LIVE_WS) == NULL);
    ASSERT_EQ((int)voice_net_open_count(), 0);
    /* both on: two opens, two counts */
    ai_gate_set_enabled(1);
    HINTERNET a = voice_net_open(VNK_STT_UPLOAD);
    HINTERNET b = voice_net_open(VNK_LIVE_WS);
    ASSERT_TRUE(a != NULL);
    ASSERT_TRUE(b != NULL);
    ASSERT_EQ((int)voice_net_open_count(), 2);
    if (a) WinHttpCloseHandle(a);
    if (b) WinHttpCloseHandle(b);
    /* the chat path never counts as a voice open */
    AiHttpResponse resp;
    (void)ai_http_post("http://127.0.0.1:1/", NULL, "{}", 2, &resp, NULL);
    ai_http_response_free(&resp);
    ASSERT_EQ((int)voice_net_open_count(), 2);
    voice_net_test_reset_count();
    ASSERT_EQ((int)voice_net_open_count(), 0);
    voice_gate_set_enabled(0);
    ai_gate_set_enabled(0);
    TEST_END();
}

#endif /* _WIN32 */
