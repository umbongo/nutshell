/* tests/test_voice_ptt_win.c -- push-to-talk driven by real posted messages.
 * Built only by `make wintest`. No microphone is touched: the messages are
 * pulled off a real queue with GetMessage, classified by voice_ptt_classify
 * and fed to the state machine at injected times. */

#ifdef _WIN32

#include <windows.h>
#include <string.h>
#include "test_framework.h"
#include "voice_ptt.h"
#include "voice_config.h"

static LRESULT CALLBACK vp_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcA(h, m, w, l);
}

static HWND vp_window(void)
{
    static int registered;
    if (!registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = vp_wndproc;
        wc.hInstance = GetModuleHandleA(NULL);
        wc.lpszClassName = "NsVoicePttTest";
        RegisterClassA(&wc);
        registered = 1;
    }
    return CreateWindowExA(0, "NsVoicePttTest", "", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
}

/* Post one message, pull it back off the queue and classify it. */
static VoiceInput vp_pull_key(HWND w, UINT msg, WPARAM wp, LPARAM lp, int ptt_key)
{
    MSG m;
    PostMessageA(w, msg, wp, lp);
    if (!PeekMessageA(&m, w, 0, 0, PM_REMOVE)) return VI_NONE;
    return voice_ptt_classify(m.message, m.wParam, m.lParam, ptt_key);
}

static VoiceInput vp_pull(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    return vp_pull_key(w, msg, wp, lp, VOICE_PTT_RIGHT_CTRL);
}

static VoiceStep vp_feed(VoicePtt *p, VoiceInput in, unsigned long t)
{
    VoiceStep none = { VA_NONE, 0 };
    switch (in) {
    case VI_PTT_DOWN: return voice_ptt_step(p, VE_KEY_DOWN, t, 1, 0);
    case VI_PTT_UP:   return voice_ptt_step(p, VE_KEY_UP, t, 1, 0);
    case VI_OTHER:    return voice_ptt_step(p, VE_OTHER, t, 1, 0);
    default:          return none;
    }
}

int test_voice_ptt_posted_keys(void)
{
    TEST_BEGIN();
    HWND w = vp_window();
    ASSERT_NOT_NULL(w);
    VoicePtt p;
    VoiceStep st;

    /* Right Ctrl (scan 0x1D, extended) held for a second: OPEN, then FINISH. */
    voice_ptt_init(&p, VOICE_MODE_HOLD, 150);
    VoiceInput in = vp_pull(w, WM_KEYDOWN, VK_CONTROL, 0x011D0001);
    ASSERT_EQ(in, VI_PTT_DOWN);
    st = vp_feed(&p, in, 0);
    ASSERT_EQ(st.action, VA_NONE);
    st = voice_ptt_step(&p, VE_TICK, 150, 1, 0);
    ASSERT_EQ(st.action, VA_OPEN);
    in = vp_pull(w, WM_KEYUP, VK_CONTROL, (LPARAM)0xC11D0001);
    ASSERT_EQ(in, VI_PTT_UP);
    st = vp_feed(&p, in, 1000);
    ASSERT_EQ(st.action, VA_FINISH);

    /* Left Ctrl (no extended bit) is not the key: nothing opens. */
    voice_ptt_init(&p, VOICE_MODE_HOLD, 150);
    in = vp_pull(w, WM_KEYDOWN, VK_CONTROL, 0x001D0001);
    ASSERT_EQ(in, VI_OTHER);
    st = vp_feed(&p, in, 0);
    st = voice_ptt_step(&p, VE_TICK, 150, 1, 0);
    ASSERT_EQ(st.action, VA_NONE);

    /* A mouse click while the key is held discards the recording. */
    voice_ptt_init(&p, VOICE_MODE_HOLD, 150);
    st = vp_feed(&p, vp_pull(w, WM_KEYDOWN, VK_CONTROL, 0x011D0001), 0);
    st = voice_ptt_step(&p, VE_TICK, 150, 1, 0);
    ASSERT_EQ(st.action, VA_OPEN);
    in = vp_pull(w, WM_LBUTTONDOWN, 0, 0);
    ASSERT_EQ(in, VI_OTHER);
    st = vp_feed(&p, in, 400);
    ASSERT_EQ(st.action, VA_DISCARD);
    ASSERT_TRUE(!voice_ptt_recording(&p));

    DestroyWindow(w);
    TEST_END();
}

/* Right Shift through a real queue: VK_SHIFT with scan 0x36 and no extended bit
 * is the key; Left Shift (0x2A) and an extended 0x36 (a driver's fake shift)
 * are not. */
int test_voice_ptt_posted_right_shift(void)
{
    TEST_BEGIN();
    HWND w = vp_window();
    ASSERT_NOT_NULL(w);
    const int k = VOICE_PTT_RIGHT_SHIFT;
    ASSERT_EQ(vp_pull_key(w, WM_KEYDOWN, VK_SHIFT, 0x00360001, k), VI_PTT_DOWN);
    ASSERT_EQ(vp_pull_key(w, WM_KEYUP, VK_SHIFT, (LPARAM)0xC0360001, k), VI_PTT_UP);
    ASSERT_EQ(vp_pull_key(w, WM_KEYDOWN, VK_SHIFT, 0x00360001 | 0x40000000, k), VI_PTT_REPEAT);
    ASSERT_EQ(vp_pull_key(w, WM_KEYDOWN, VK_SHIFT, 0x002A0001, k), VI_OTHER);
    ASSERT_EQ(vp_pull_key(w, WM_KEYUP, VK_SHIFT, (LPARAM)0xC02A0001, k), VI_NONE);
    ASSERT_EQ(vp_pull_key(w, WM_KEYDOWN, VK_SHIFT, 0x01360001, k), VI_OTHER);
    DestroyWindow(w);
    TEST_END();
}

#endif /* _WIN32 */
