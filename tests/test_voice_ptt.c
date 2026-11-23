#include "test_framework.h"
#include "voice_ptt.h"
#include "voice_config.h"
#include <string.h>

#define EXT 0x01000000L
#define REP 0x40000000L

static VoiceStep st(VoicePtt *p, VoiceEvent e, unsigned long now, int in_panel, int busy)
{
    return voice_ptt_step(p, e, now, in_panel, busy);
}

int test_voice_classify_right_ctrl(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_CONTROL, EXT, VOICE_PTT_RIGHT_CTRL), VI_PTT_DOWN);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_RCONTROL, EXT, VOICE_PTT_RIGHT_CTRL), VI_PTT_DOWN);
    ASSERT_EQ(voice_ptt_classify(WM_KEYUP, VK_CONTROL, EXT, VOICE_PTT_RIGHT_CTRL), VI_PTT_UP);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_CONTROL, EXT | REP, VOICE_PTT_RIGHT_CTRL), VI_PTT_REPEAT);
    ASSERT_EQ(voice_ptt_classify(WM_KEYUP, VK_CONTROL, EXT | REP, VOICE_PTT_RIGHT_CTRL), VI_PTT_UP);
    /* with Alt held Windows delivers the release as WM_SYSKEYUP */
    ASSERT_EQ(voice_ptt_classify(WM_SYSKEYUP, VK_CONTROL, EXT, VOICE_PTT_RIGHT_CTRL), VI_PTT_UP);
    ASSERT_EQ(voice_ptt_classify(WM_SYSKEYUP, VK_CONTROL, 0, VOICE_PTT_RIGHT_CTRL), VI_NONE);
    ASSERT_EQ(voice_ptt_classify(WM_SYSKEYUP, 'A', 0, VOICE_PTT_RIGHT_CTRL), VI_NONE);
    ASSERT_EQ(voice_ptt_classify(WM_SYSKEYUP, VK_F8, 0, VOICE_PTT_F8), VI_PTT_UP);
    TEST_END();
}

int test_voice_classify_left_ctrl_and_altgr_are_not_ptt(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_CONTROL, 0, VOICE_PTT_RIGHT_CTRL), VI_OTHER);
    ASSERT_EQ(voice_ptt_classify(WM_KEYUP, VK_CONTROL, 0, VOICE_PTT_RIGHT_CTRL), VI_NONE);
    ASSERT_EQ(voice_ptt_classify(WM_SYSKEYDOWN, VK_MENU, EXT, VOICE_PTT_RIGHT_CTRL), VI_OTHER);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_MENU, EXT, VOICE_PTT_RIGHT_CTRL), VI_OTHER);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, 'A', 0, VOICE_PTT_RIGHT_CTRL), VI_OTHER);
    TEST_END();
}

int test_voice_classify_f8_f9(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_F8, 0, VOICE_PTT_F8), VI_PTT_DOWN);
    ASSERT_EQ(voice_ptt_classify(WM_KEYUP, VK_F8, 0, VOICE_PTT_F8), VI_PTT_UP);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_F8, REP, VOICE_PTT_F8), VI_PTT_REPEAT);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_F9, 0, VOICE_PTT_F9), VI_PTT_DOWN);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_F8, 0, VOICE_PTT_RIGHT_CTRL), VI_OTHER);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_F9, 0, VOICE_PTT_F8), VI_OTHER);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_CONTROL, EXT, VOICE_PTT_F9), VI_OTHER);
    ASSERT_EQ(voice_ptt_classify(WM_KEYUP, VK_CONTROL, EXT, VOICE_PTT_F9), VI_NONE);
    TEST_END();
}

int test_voice_classify_mouse_and_wheel(void)
{
    TEST_BEGIN();
    unsigned downs[] = { WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN, WM_XBUTTONDOWN,
                         WM_NCLBUTTONDOWN, WM_NCRBUTTONDOWN, WM_NCMBUTTONDOWN, WM_NCXBUTTONDOWN,
                         WM_MOUSEWHEEL, WM_MOUSEHWHEEL };
    for (size_t i = 0; i < sizeof downs / sizeof downs[0]; i++)
        ASSERT_EQ(voice_ptt_classify(downs[i], 0, 0, VOICE_PTT_RIGHT_CTRL), VI_OTHER);
    ASSERT_EQ(voice_ptt_classify(WM_MOUSEMOVE, 0, 0, VOICE_PTT_RIGHT_CTRL), VI_NONE);
    ASSERT_EQ(voice_ptt_classify(WM_KEYUP, 'A', 0, VOICE_PTT_RIGHT_CTRL), VI_NONE);
    ASSERT_EQ(voice_ptt_classify(0x0001, 0, 0, VOICE_PTT_RIGHT_CTRL), VI_NONE);
    TEST_END();
}

int test_voice_classify_escape(void)
{
    TEST_BEGIN();
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_ESCAPE, 0, VOICE_PTT_RIGHT_CTRL), VI_ESCAPE);
    ASSERT_EQ(voice_ptt_classify(WM_KEYUP, VK_ESCAPE, 0, VOICE_PTT_RIGHT_CTRL), VI_NONE);
    TEST_END();
}

int test_voice_hold_delay_0_150_400(void)
{
    TEST_BEGIN();
    VoicePtt p; VoiceStep s;
    voice_ptt_init(&p, VOICE_MODE_HOLD, 0);
    s = st(&p, VE_KEY_DOWN, 1000, 1, 0);
    ASSERT_EQ(s.action, VA_OPEN);
    ASSERT_EQ(p.state, VS_REC_HOLD);

    voice_ptt_init(&p, VOICE_MODE_HOLD, 150);
    s = st(&p, VE_KEY_DOWN, 1000, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_ARMED);
    s = st(&p, VE_TICK, 1149, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_ARMED);
    s = st(&p, VE_TICK, 1150, 1, 0);
    ASSERT_EQ(s.action, VA_OPEN);
    ASSERT_EQ(p.state, VS_REC_HOLD);

    voice_ptt_init(&p, VOICE_MODE_HOLD, 400);
    st(&p, VE_KEY_DOWN, 5000, 1, 0);
    s = st(&p, VE_TICK, 5399, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    s = st(&p, VE_TICK, 5400, 1, 0);
    ASSERT_EQ(s.action, VA_OPEN);
    TEST_END();
}

int test_voice_chord_before_delay_never_opens(void)
{
    TEST_BEGIN();
    VoicePtt p; VoiceStep s;
    voice_ptt_init(&p, VOICE_MODE_HOLD, 150);
    st(&p, VE_KEY_DOWN, 1000, 1, 0);
    s = st(&p, VE_OTHER, 1050, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    s = st(&p, VE_TICK, 1200, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    s = st(&p, VE_KEY_UP, 1300, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_IDLE);
    /* Right Ctrl, Alt, then the Ctrl release arrives as SYSKEYUP: idle again */
    voice_ptt_init(&p, VOICE_MODE_HOLD, 150);
    st(&p, VE_KEY_DOWN, 2000, 1, 0);
    st(&p, VE_OTHER, 2020, 1, 0);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    VoiceInput in = voice_ptt_classify(WM_SYSKEYUP, VK_CONTROL, EXT, VOICE_PTT_RIGHT_CTRL);
    ASSERT_EQ(in, VI_PTT_UP);
    st(&p, VE_KEY_UP, 2100, 1, 0);
    ASSERT_EQ(p.state, VS_IDLE);
    st(&p, VE_KEY_DOWN, 3000, 1, 0);
    s = st(&p, VE_TICK, 3150, 1, 0);
    ASSERT_EQ(s.action, VA_OPEN);
    /* Escape while armed (Hold) or in a toggle press spends the key, no open */
    voice_ptt_init(&p, VOICE_MODE_HOLD, 150);
    st(&p, VE_KEY_DOWN, 0, 1, 0);
    s = st(&p, VE_ESCAPE, 20, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(s.consume, 0);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    s = st(&p, VE_TICK, 200, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    s = st(&p, VE_KEY_UP, 300, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_IDLE);
    voice_ptt_init(&p, VOICE_MODE_TOGGLE, 0);
    st(&p, VE_KEY_DOWN, 0, 1, 0);
    s = st(&p, VE_ESCAPE, 20, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(s.consume, 0);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    s = st(&p, VE_KEY_UP, 40, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_IDLE);
    TEST_END();
}

int test_voice_chord_while_recording_discards(void)
{
    TEST_BEGIN();
    /* key, mouse button and wheel all arrive as VE_OTHER; each discards */
    for (int i = 0; i < 3; i++) {
        VoicePtt p; VoiceStep s;
        voice_ptt_init(&p, VOICE_MODE_HOLD, 0);
        st(&p, VE_KEY_DOWN, 100, 1, 0);
        ASSERT_EQ(p.state, VS_REC_HOLD);
        s = st(&p, VE_OTHER, 400, 1, 0);
        ASSERT_EQ(s.action, VA_DISCARD);
        ASSERT_EQ(p.state, VS_KEY_SPENT);
        s = st(&p, VE_KEY_UP, 900, 1, 0);
        ASSERT_EQ(s.action, VA_NONE);
        ASSERT_EQ(p.state, VS_IDLE);
    }
    /* Toggle: a chord during the stop press spends the press but keeps the
     * recording (changed from "discards": a capital typed with Right Shift) */
    VoicePtt q; VoiceStep s;
    voice_ptt_init(&q, VOICE_MODE_TOGGLE, 0);
    st(&q, VE_KEY_DOWN, 0, 1, 0);
    st(&q, VE_KEY_UP, 50, 1, 0);
    st(&q, VE_KEY_DOWN, 1000, 1, 0);
    s = st(&q, VE_OTHER, 1010, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(q.state, VS_LATCHED_SPENT);
    ASSERT_EQ(voice_ptt_recording(&q), 1);
    TEST_END();
}

int test_voice_short_tap_discarded(void)
{
    TEST_BEGIN();
    VoicePtt p; VoiceStep s;
    voice_ptt_init(&p, VOICE_MODE_HOLD, 0);
    st(&p, VE_KEY_DOWN, 1000, 1, 0);
    s = st(&p, VE_KEY_UP, 1299, 1, 0);
    ASSERT_EQ(s.action, VA_DISCARD);
    ASSERT_EQ(p.state, VS_IDLE);
    st(&p, VE_KEY_DOWN, 2000, 1, 0);
    s = st(&p, VE_KEY_UP, 2300, 1, 0);
    ASSERT_EQ(s.action, VA_FINISH);
    ASSERT_EQ(p.state, VS_IDLE);
    TEST_END();
}

int test_voice_cap_finishes(void)
{
    TEST_BEGIN();
    VoicePtt p; VoiceStep s;
    voice_ptt_init(&p, VOICE_MODE_HOLD, 0);
    st(&p, VE_KEY_DOWN, 1000, 1, 0);
    s = st(&p, VE_TICK, 1000 + 59999, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    s = st(&p, VE_TICK, 1000 + 60000, 1, 0);
    ASSERT_EQ(s.action, VA_FINISH);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    s = st(&p, VE_KEY_UP, 70000, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_IDLE);
    /* a latched recording hits the cap and goes idle */
    st(&p, VE_MIC_CLICK, 100000, 1, 0);
    s = st(&p, VE_TICK, 160000, 1, 0);
    ASSERT_EQ(s.action, VA_FINISH);
    ASSERT_EQ(p.state, VS_IDLE);
    TEST_END();
}

int test_voice_escape_discards_and_consumes(void)
{
    TEST_BEGIN();
    VoicePtt p; VoiceStep s;
    voice_ptt_init(&p, VOICE_MODE_HOLD, 0);
    st(&p, VE_KEY_DOWN, 0, 1, 0);
    s = st(&p, VE_ESCAPE, 500, 1, 0);
    ASSERT_EQ(s.action, VA_DISCARD);
    ASSERT_EQ(s.consume, 1);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    /* latched (mic button): key is not down, so idle */
    voice_ptt_init(&p, VOICE_MODE_HOLD, 0);
    st(&p, VE_MIC_CLICK, 0, 1, 0);
    s = st(&p, VE_ESCAPE, 500, 1, 0);
    ASSERT_EQ(s.action, VA_DISCARD);
    ASSERT_EQ(s.consume, 1);
    ASSERT_EQ(p.state, VS_IDLE);
    /* stop press: key down */
    voice_ptt_init(&p, VOICE_MODE_TOGGLE, 0);
    st(&p, VE_KEY_DOWN, 0, 1, 0);
    st(&p, VE_KEY_UP, 10, 1, 0);
    st(&p, VE_KEY_DOWN, 900, 1, 0);
    s = st(&p, VE_ESCAPE, 910, 1, 0);
    ASSERT_EQ(s.action, VA_DISCARD);
    ASSERT_EQ(s.consume, 1);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    /* not recording: Escape keeps its meaning */
    voice_ptt_init(&p, VOICE_MODE_HOLD, 0);
    s = st(&p, VE_ESCAPE, 0, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(s.consume, 0);
    ASSERT_EQ(p.state, VS_IDLE);
    TEST_END();
}

int test_voice_not_in_panel_ignored(void)
{
    TEST_BEGIN();
    VoicePtt p; VoiceStep s;
    voice_ptt_init(&p, VOICE_MODE_HOLD, 0);
    s = st(&p, VE_KEY_DOWN, 0, 0, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_IDLE);
    voice_ptt_init(&p, VOICE_MODE_TOGGLE, 0);
    s = st(&p, VE_KEY_DOWN, 0, 0, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_IDLE);
    TEST_END();
}

int test_voice_busy_reports_at_start_only(void)
{
    TEST_BEGIN();
    VoicePtt p; VoiceStep s;
    /* busy at key-down, then a chord: no VA_BUSY */
    voice_ptt_init(&p, VOICE_MODE_HOLD, 150);
    s = st(&p, VE_KEY_DOWN, 0, 1, 1);
    ASSERT_EQ(s.action, VA_NONE);
    s = st(&p, VE_OTHER, 50, 1, 1);
    ASSERT_EQ(s.action, VA_NONE);
    s = st(&p, VE_TICK, 200, 1, 1);
    ASSERT_EQ(s.action, VA_NONE);
    st(&p, VE_KEY_UP, 300, 1, 1);
    /* busy when the delay elapses: VA_BUSY once */
    s = st(&p, VE_KEY_DOWN, 1000, 1, 1);
    ASSERT_EQ(s.action, VA_NONE);
    s = st(&p, VE_TICK, 1150, 1, 1);
    ASSERT_EQ(s.action, VA_BUSY);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    s = st(&p, VE_TICK, 1200, 1, 1);
    ASSERT_EQ(s.action, VA_NONE);
    s = st(&p, VE_KEY_UP, 1300, 1, 1);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_IDLE);
    /* delay 0 and busy: reported in the key-down call */
    voice_ptt_init(&p, VOICE_MODE_HOLD, 0);
    s = st(&p, VE_KEY_DOWN, 0, 1, 1);
    ASSERT_EQ(s.action, VA_BUSY);
    TEST_END();
}

int test_voice_toggle_mode(void)
{
    TEST_BEGIN();
    VoicePtt p; VoiceStep s;
    voice_ptt_init(&p, VOICE_MODE_TOGGLE, 150);
    s = st(&p, VE_KEY_DOWN, 0, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_TOGGLE_PRESS);
    s = st(&p, VE_TICK, 500, 1, 0);       /* the delay does not apply */
    ASSERT_EQ(s.action, VA_NONE);
    s = st(&p, VE_KEY_UP, 600, 1, 0);
    ASSERT_EQ(s.action, VA_OPEN);
    ASSERT_EQ(p.state, VS_REC_LATCHED);
    /* typing elsewhere while latched is allowed */
    s = st(&p, VE_OTHER, 700, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_REC_LATCHED);
    /* a stop press from outside the panel still counts */
    s = st(&p, VE_KEY_DOWN, 2000, 0, 0);
    ASSERT_EQ(p.state, VS_STOP_PRESS);
    s = st(&p, VE_KEY_UP, 2050, 0, 0);
    ASSERT_EQ(s.action, VA_FINISH);
    ASSERT_EQ(p.state, VS_IDLE);
    /* a press with OTHER between does not open */
    st(&p, VE_KEY_DOWN, 3000, 1, 0);
    s = st(&p, VE_OTHER, 3010, 1, 0);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    s = st(&p, VE_KEY_UP, 3020, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_IDLE);
    /* busy at the clean release */
    st(&p, VE_KEY_DOWN, 4000, 1, 1);
    s = st(&p, VE_KEY_UP, 4010, 1, 1);
    ASSERT_EQ(s.action, VA_BUSY);
    ASSERT_EQ(p.state, VS_IDLE);
    /* the next clean press-release opens again */
    st(&p, VE_KEY_DOWN, 5000, 1, 0);
    s = st(&p, VE_KEY_UP, 5010, 1, 0);
    ASSERT_EQ(s.action, VA_OPEN);
    TEST_END();
}

int test_voice_mic_click(void)
{
    TEST_BEGIN();
    VoicePtt p; VoiceStep s;
    voice_ptt_init(&p, VOICE_MODE_HOLD, 150);
    s = st(&p, VE_MIC_CLICK, 0, 1, 0);
    ASSERT_EQ(s.action, VA_OPEN);
    ASSERT_EQ(p.state, VS_REC_LATCHED);
    s = st(&p, VE_MIC_CLICK, 1000, 1, 0);
    ASSERT_EQ(s.action, VA_FINISH);
    ASSERT_EQ(p.state, VS_IDLE);
    s = st(&p, VE_MIC_CLICK, 2000, 1, 1);
    ASSERT_EQ(s.action, VA_BUSY);
    ASSERT_EQ(p.state, VS_IDLE);
    /* in any other state a click is an OTHER: it discards a held recording */
    voice_ptt_init(&p, VOICE_MODE_HOLD, 0);
    st(&p, VE_KEY_DOWN, 0, 1, 0);
    s = st(&p, VE_MIC_CLICK, 500, 1, 0);
    ASSERT_EQ(s.action, VA_DISCARD);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    TEST_END();
}

int test_voice_deactivate_and_modal_discard(void)
{
    TEST_BEGIN();
    VoicePtt p; VoiceStep s;
    voice_ptt_init(&p, VOICE_MODE_HOLD, 150);
    st(&p, VE_KEY_DOWN, 0, 1, 0);
    s = st(&p, VE_DEACTIVATE, 50, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_IDLE);
    voice_ptt_init(&p, VOICE_MODE_HOLD, 0);
    st(&p, VE_KEY_DOWN, 0, 1, 0);
    s = st(&p, VE_DEACTIVATE, 500, 1, 0);
    ASSERT_EQ(s.action, VA_DISCARD);
    ASSERT_EQ(p.state, VS_IDLE);
    st(&p, VE_MIC_CLICK, 1000, 1, 0);
    s = st(&p, VE_DEACTIVATE, 1500, 1, 0);
    ASSERT_EQ(s.action, VA_DISCARD);
    ASSERT_EQ(p.state, VS_IDLE);
    s = st(&p, VE_DEACTIVATE, 1600, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    TEST_END();
}

int test_voice_next_deadline(void)
{
    TEST_BEGIN();
    VoicePtt p;
    voice_ptt_init(&p, VOICE_MODE_HOLD, 150);
    ASSERT_EQ(voice_ptt_next_deadline(&p, 0), -1);
    st(&p, VE_KEY_DOWN, 0, 1, 0);
    ASSERT_EQ(voice_ptt_next_deadline(&p, 0), 150);
    ASSERT_EQ(voice_ptt_next_deadline(&p, 100), 50);
    ASSERT_EQ(voice_ptt_next_deadline(&p, 400), 0);
    st(&p, VE_TICK, 150, 1, 0);
    ASSERT_EQ(voice_ptt_next_deadline(&p, 150), 60000);
    ASSERT_EQ(voice_ptt_next_deadline(&p, 30150), 30000);
    st(&p, VE_KEY_UP, 31000, 1, 0);
    ASSERT_EQ(voice_ptt_next_deadline(&p, 31000), -1);
    TEST_END();
}

int test_voice_deliver(void)
{
    TEST_BEGIN();
    char out[64];
    ASSERT_EQ(voice_deliver("", "  \n", 0, out, sizeof out), VD_NOTHING);
    ASSERT_EQ(voice_deliver("draft", "", 0, out, sizeof out), VD_NOTHING);
    ASSERT_EQ(voice_deliver("", "hi", 0, out, sizeof out), VD_SEND);
    ASSERT_STR_EQ(out, "hi");
    ASSERT_EQ(voice_deliver("", "hi", 1, out, sizeof out), VD_FILL);
    ASSERT_STR_EQ(out, "hi");
    ASSERT_EQ(voice_deliver("draft", "hi", 0, out, sizeof out), VD_APPEND);
    ASSERT_STR_EQ(out, "draft hi");
    ASSERT_EQ(voice_deliver("draft ", "hi", 1, out, sizeof out), VD_APPEND);
    ASSERT_STR_EQ(out, "draft hi");
    ASSERT_EQ(voice_deliver("draft\n", "hi", 0, out, sizeof out), VD_APPEND);
    ASSERT_STR_EQ(out, "draft\nhi");
    char small[6];
    ASSERT_EQ(voice_deliver("draft", "hi", 0, small, sizeof small), VD_APPEND);
    ASSERT_STR_EQ(small, "draft");
    ASSERT_EQ(voice_deliver("", "hello world", 0, small, sizeof small), VD_SEND);
    ASSERT_STR_EQ(small, "hello");
    TEST_END();
}

int test_voice_binding_same(void)
{
    TEST_BEGIN();
    int s1, s2;
    VoiceBinding a = { &s1, 3, 7 };
    VoiceBinding b = { &s1, 3, 7 };
    ASSERT_EQ(voice_binding_same(&a, &b), 1);
    b.session = &s2;
    ASSERT_EQ(voice_binding_same(&a, &b), 0);
    b = a; b.session_gen = 4;
    ASSERT_EQ(voice_binding_same(&a, &b), 0);
    b = a; b.conv_gen = 8;
    ASSERT_EQ(voice_binding_same(&a, &b), 0);
    VoiceBinding n1 = { NULL, 1, 1 }, n2 = { NULL, 1, 1 };
    ASSERT_EQ(voice_binding_same(&n1, &n2), 0);
    TEST_END();
}

int test_voice_err_route(void)
{
    TEST_BEGIN();
    /* the current recording's error is honoured */
    ASSERT_EQ((int)voice_err_route(5, 5, 1), (int)VOICE_ERR_CAPTURE);
    /* a stale token is ignored, whatever is recording now */
    ASSERT_EQ((int)voice_err_route(4, 5, 1), (int)VOICE_ERR_STALE);
    ASSERT_EQ((int)voice_err_route(5, 6, 1), (int)VOICE_ERR_STALE);
    /* nothing recording: a leftover error is stale, even with equal tokens */
    ASSERT_EQ((int)voice_err_route(5, 5, 0), (int)VOICE_ERR_STALE);
    ASSERT_EQ((int)voice_err_route(5, 0, 1), (int)VOICE_ERR_STALE);
    /* token 0 is Settings' (the Test, enumeration), recording or not */
    ASSERT_EQ((int)voice_err_route(0, 5, 1), (int)VOICE_ERR_SETTINGS);
    ASSERT_EQ((int)voice_err_route(0, 0, 0), (int)VOICE_ERR_SETTINGS);
    /* tokens are compared in their low 24 bits (the error packs no more) */
    ASSERT_EQ((int)voice_err_route(0x01000005u & VOICE_TOKEN_MASK, 0x01000005u, 1), (int)VOICE_ERR_CAPTURE);
    ASSERT_EQ((int)voice_err_route(0x05u, 0x01000005u, 1), (int)VOICE_ERR_CAPTURE);
    ASSERT_EQ((int)voice_err_route(0x01000000u, 0x05u, 1), (int)VOICE_ERR_SETTINGS);   /* masks to 0 */
    TEST_END();
}

/* The audio thread ended the capture at the cap before the machine's own
 * finish: on_captured calls voice_ptt_capture_ended, and nothing the machine
 * does afterwards may finish or discard the audio already submitted. */
int test_voice_ptt_capture_ended_first(void)
{
    TEST_BEGIN();
    VoicePtt p; VoiceStep s;
    /* Hold: key still down */
    voice_ptt_init(&p, VOICE_MODE_HOLD, 0);
    ASSERT_EQ(st(&p, VE_KEY_DOWN, 1000, 1, 0).action, VA_OPEN);
    voice_ptt_capture_ended(&p, p.state == VS_REC_HOLD);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    ASSERT_EQ(voice_ptt_recording(&p), 0);
    ASSERT_TRUE(voice_ptt_next_deadline(&p, 1000 + VOICE_CAP_MS) < 0);   /* no cap timer */
    s = st(&p, VE_TICK, 1000 + VOICE_CAP_MS + 5, 1, 0);                   /* the cap tick */
    ASSERT_EQ(s.action, VA_NONE);
    s = st(&p, VE_ESCAPE, 1000 + VOICE_CAP_MS + 6, 1, 0);                 /* no discard either */
    ASSERT_EQ(s.action, VA_NONE);
    s = st(&p, VE_KEY_UP, 1000 + VOICE_CAP_MS + 10, 1, 0);                /* release: idle, no finish */
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_IDLE);
    s = st(&p, VE_KEY_DOWN, 1000 + VOICE_CAP_MS + 500, 1, 0);             /* a fresh press opens */
    ASSERT_EQ(s.action, VA_OPEN);

    /* Toggle (latched, key up): straight to idle */
    voice_ptt_init(&p, VOICE_MODE_TOGGLE, 0);
    ASSERT_EQ(st(&p, VE_KEY_DOWN, 1000, 1, 0).action, VA_NONE);
    ASSERT_EQ(st(&p, VE_KEY_UP, 1050, 1, 0).action, VA_OPEN);
    ASSERT_EQ(p.state, VS_REC_LATCHED);
    voice_ptt_capture_ended(&p, 0);
    ASSERT_EQ(p.state, VS_IDLE);
    s = st(&p, VE_TICK, 1050 + VOICE_CAP_MS + 5, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(st(&p, VE_KEY_DOWN, 90000, 1, 0).action, VA_NONE);          /* a new press... */
    ASSERT_EQ(st(&p, VE_KEY_UP, 90050, 1, 0).action, VA_OPEN);            /* ...opens normally */

    /* not recording: a no-op */
    voice_ptt_init(&p, VOICE_MODE_HOLD, 0);
    voice_ptt_capture_ended(&p, 1);
    ASSERT_EQ(p.state, VS_IDLE);
    TEST_END();
}

int test_voice_busy_text(void)
{
    TEST_BEGIN();
    ASSERT_STR_EQ(voice_busy_text(0, 1), "Connect a session first");
    ASSERT_STR_EQ(voice_busy_text(1, 1), "Connect a session first");
    ASSERT_STR_EQ(voice_busy_text(1, 0), "Still transcribing the last recording");
    ASSERT_STR_EQ(voice_busy_text(0, 0), "Assistant is busy");
    TEST_END();
}

int test_voice_cue_text(void)
{
    TEST_BEGIN();
    char o[128];
    voice_cue_text(VOICE_MODE_TOGGLE, VOICE_PTT_RIGHT_SHIFT, 0, o, sizeof o);
    ASSERT_STR_EQ(o, "Listening\xE2\x80\xA6 press Right Shift again to send");
    voice_cue_text(VOICE_MODE_HOLD, VOICE_PTT_RIGHT_SHIFT, 0, o, sizeof o);
    ASSERT_STR_EQ(o, "Listening\xE2\x80\xA6 release Right Shift to send");
    voice_cue_text(VOICE_MODE_HOLD, VOICE_PTT_RIGHT_SHIFT, 1, o, sizeof o);
    ASSERT_STR_EQ(o, "Listening\xE2\x80\xA6 press Right Shift again to send");
    voice_cue_text(VOICE_MODE_HOLD, VOICE_PTT_RIGHT_CTRL, 0, o, sizeof o);
    ASSERT_STR_EQ(o, "Listening\xE2\x80\xA6 release Right Ctrl to send");
    voice_cue_text(VOICE_MODE_TOGGLE, VOICE_PTT_RIGHT_CTRL, 0, o, sizeof o);
    ASSERT_STR_EQ(o, "Listening\xE2\x80\xA6 press Right Ctrl again to send");
    voice_cue_text(VOICE_MODE_HOLD, VOICE_PTT_RIGHT_CTRL, 1, o, sizeof o);
    ASSERT_STR_EQ(o, "Listening\xE2\x80\xA6 press Right Ctrl again to send");
    voice_cue_text(VOICE_MODE_HOLD, VOICE_PTT_F9, 0, o, sizeof o);
    ASSERT_STR_EQ(o, "Listening\xE2\x80\xA6 release F9 to send");
    voice_cue_text(VOICE_MODE_TOGGLE, VOICE_PTT_F9, 0, o, sizeof o);
    ASSERT_STR_EQ(o, "Listening\xE2\x80\xA6 press F9 again to send");
    TEST_END();
}

/* ---- Right Shift: classifier, chord filter and whole-sequence checks ---- */

#define SC(x) ((long)(x) << 16)
#define SC_RSHIFT 0x36
#define SC_LSHIFT 0x2A
#define SC_CTRL   0x1D
#define VK_INS 0x2D
#define VK_LEFT_ 0x25
#define VK_UP_ 0x26
#define VK_RIGHT_ 0x27
#define VK_DOWN_ 0x28
#define VK_LWIN_ 0x5B

typedef struct { unsigned msg; uintptr_t vk; intptr_t lp; unsigned mods; } KEv;

/* Feeds the sequence through classify + chord filter + step as the controller
 * does (a repeat or VI_NONE feeds nothing) and returns how many VA_OPEN came
 * out, ticking well past the hold delay at the end. */
static int drive(const KEv *seq, size_t n, int mode, int in_panel, int key)
{
    VoicePtt p; int opens = 0; unsigned long now = 1000;
    voice_ptt_init(&p, mode, 150);
    for (size_t i = 0; i < n; i++) {
        VoiceInput in = voice_ptt_classify(seq[i].msg, seq[i].vk, seq[i].lp, key);
        in = voice_ptt_chord_filter(in, key, seq[i].mods);
        VoiceEvent ev;
        switch (in) {
        case VI_PTT_DOWN: ev = VE_KEY_DOWN; break;
        case VI_PTT_UP: ev = VE_KEY_UP; break;
        case VI_ESCAPE: ev = VE_ESCAPE; break;
        case VI_OTHER: ev = VE_OTHER; break;
        default: continue;
        }
        now += (in == VI_PTT_UP) ? 400 : 10;     /* a held key is held past the delay */
        /* the controller's timer fires before the next event */
        if (voice_ptt_step(&p, VE_TICK, now, in_panel, 0).action == VA_OPEN) opens++;
        if (voice_ptt_step(&p, ev, now, in_panel, 0).action == VA_OPEN) opens++;
    }
    now += 1000;
    if (voice_ptt_step(&p, VE_TICK, now, in_panel, 0).action == VA_OPEN) opens++;
    return opens;
}

#define RS_DN  { WM_KEYDOWN, VK_SHIFT, SC(SC_RSHIFT) | 1, VOICE_MOD_RSHIFT }
#define RS_UP  { WM_KEYUP, VK_SHIFT, SC(SC_RSHIFT) | 1 | 0xC0000000L, 0 }

int test_voice_classify_right_shift(void)
{
    TEST_BEGIN();
    const int K = VOICE_PTT_RIGHT_SHIFT;
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_SHIFT, SC(SC_RSHIFT), K), VI_PTT_DOWN);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_RSHIFT, 0, K), VI_PTT_DOWN);
    ASSERT_EQ(voice_ptt_classify(WM_KEYUP, VK_SHIFT, SC(SC_RSHIFT), K), VI_PTT_UP);
    ASSERT_EQ(voice_ptt_classify(WM_KEYUP, VK_RSHIFT, 0, K), VI_PTT_UP);
    ASSERT_EQ(voice_ptt_classify(WM_SYSKEYUP, VK_SHIFT, SC(SC_RSHIFT), K), VI_PTT_UP);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_SHIFT, SC(SC_RSHIFT) | REP, K), VI_PTT_REPEAT);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_RSHIFT, REP, K), VI_PTT_REPEAT);
    /* Left Shift never */
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_SHIFT, SC(SC_LSHIFT), K), VI_OTHER);
    ASSERT_EQ(voice_ptt_classify(WM_KEYUP, VK_SHIFT, SC(SC_LSHIFT), K), VI_NONE);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_LSHIFT, 0, K), VI_OTHER);
    ASSERT_EQ(voice_ptt_classify(WM_KEYUP, VK_LSHIFT, 0, K), VI_NONE);
    /* the extended shift the driver fakes around numpad keys is not the key */
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_SHIFT, SC(SC_RSHIFT) | EXT, K), VI_OTHER);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_SHIFT, SC(SC_LSHIFT) | EXT, K), VI_OTHER);
    ASSERT_EQ(voice_ptt_classify(WM_KEYUP, VK_SHIFT, SC(SC_RSHIFT) | EXT, K), VI_NONE);
    ASSERT_EQ(voice_ptt_classify(WM_KEYUP, VK_SHIFT, SC(SC_LSHIFT) | EXT, K), VI_NONE);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_RSHIFT, EXT, K), VI_OTHER);
    /* Escape and a plain key keep their meaning */
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_ESCAPE, 0, K), VI_ESCAPE);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, 'A', SC(0x1E), K), VI_OTHER);
    /* Right Ctrl is not the key while Right Shift is selected */
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_CONTROL, EXT, K), VI_OTHER);
    ASSERT_EQ(voice_ptt_classify(WM_KEYUP, VK_CONTROL, EXT, K), VI_NONE);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_F8, 0, K), VI_OTHER);
    TEST_END();
}

int test_voice_classify_other_keys_ignore_right_shift(void)
{
    TEST_BEGIN();
    int keys[] = { VOICE_PTT_RIGHT_CTRL, VOICE_PTT_F8, VOICE_PTT_F9 };
    for (size_t i = 0; i < 3; i++) {
        ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_SHIFT, SC(SC_RSHIFT), keys[i]), VI_OTHER);
        ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_RSHIFT, 0, keys[i]), VI_OTHER);
        ASSERT_EQ(voice_ptt_classify(WM_KEYUP, VK_SHIFT, SC(SC_RSHIFT), keys[i]), VI_NONE);
    }
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_CONTROL, EXT, VOICE_PTT_RIGHT_CTRL), VI_PTT_DOWN);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_F8, 0, VOICE_PTT_F8), VI_PTT_DOWN);
    ASSERT_EQ(voice_ptt_classify(WM_KEYDOWN, VK_F9, 0, VOICE_PTT_F9), VI_PTT_DOWN);
    TEST_END();
}

int test_voice_chord_filter(void)
{
    TEST_BEGIN();
    const unsigned all[] = { VOICE_MOD_LSHIFT, VOICE_MOD_RSHIFT, VOICE_MOD_LCTRL,
                             VOICE_MOD_RCTRL, VOICE_MOD_ALT, VOICE_MOD_WIN };
    for (int key = 0; key < (int)VOICE_PTT_COUNT; key++) {
        ASSERT_EQ(voice_ptt_chord_filter(VI_PTT_DOWN, key, 0), VI_PTT_DOWN);
        for (size_t i = 0; i < 6; i++) {
            unsigned own = key == VOICE_PTT_RIGHT_SHIFT ? VOICE_MOD_RSHIFT
                         : key == VOICE_PTT_RIGHT_CTRL ? VOICE_MOD_RCTRL : 0u;
            VoiceInput want = (all[i] == own) ? VI_PTT_DOWN : VI_OTHER;
            ASSERT_EQ(voice_ptt_chord_filter(VI_PTT_DOWN, key, all[i]), want);
            /* anything else passes through unchanged */
            ASSERT_EQ(voice_ptt_chord_filter(VI_PTT_UP, key, all[i]), VI_PTT_UP);
            ASSERT_EQ(voice_ptt_chord_filter(VI_PTT_REPEAT, key, all[i]), VI_PTT_REPEAT);
            ASSERT_EQ(voice_ptt_chord_filter(VI_ESCAPE, key, all[i]), VI_ESCAPE);
            ASSERT_EQ(voice_ptt_chord_filter(VI_OTHER, key, all[i]), VI_OTHER);
            ASSERT_EQ(voice_ptt_chord_filter(VI_NONE, key, all[i]), VI_NONE);
        }
    }
    /* the key's own bit plus another modifier is still a chord */
    ASSERT_EQ(voice_ptt_chord_filter(VI_PTT_DOWN, VOICE_PTT_RIGHT_SHIFT,
                                     VOICE_MOD_RSHIFT | VOICE_MOD_LCTRL), VI_OTHER);
    ASSERT_EQ(voice_ptt_chord_filter(VI_PTT_DOWN, VOICE_PTT_RIGHT_SHIFT, VOICE_MOD_RSHIFT),
              VI_PTT_DOWN);
    TEST_END();
}

int test_voice_right_shift_clean_press_opens(void)
{
    TEST_BEGIN();
    const KEv clean[] = { RS_DN, RS_UP };
    /* Toggle: opens on release; Hold: opens after the delay */
    ASSERT_EQ(drive(clean, 2, VOICE_MODE_TOGGLE, 1, VOICE_PTT_RIGHT_SHIFT), 1);
    ASSERT_EQ(drive(clean, 2, VOICE_MODE_HOLD, 1, VOICE_PTT_RIGHT_SHIFT), 1);
    /* terminal focus never opens */
    ASSERT_EQ(drive(clean, 2, VOICE_MODE_TOGGLE, 0, VOICE_PTT_RIGHT_SHIFT), 0);
    ASSERT_EQ(drive(clean, 2, VOICE_MODE_HOLD, 0, VOICE_PTT_RIGHT_SHIFT), 0);
    /* auto-repeats while held are ignored */
    const KEv rep[] = { RS_DN, { WM_KEYDOWN, VK_SHIFT, SC(SC_RSHIFT) | REP | 1, VOICE_MOD_RSHIFT },
                        { WM_KEYDOWN, VK_SHIFT, SC(SC_RSHIFT) | REP | 1, VOICE_MOD_RSHIFT }, RS_UP };
    ASSERT_EQ(drive(rep, 4, VOICE_MODE_TOGGLE, 1, VOICE_PTT_RIGHT_SHIFT), 1);
    /* VK_RSHIFT as delivered by some paths */
    const KEv vkr[] = { { WM_KEYDOWN, VK_RSHIFT, 0, VOICE_MOD_RSHIFT }, { WM_KEYUP, VK_RSHIFT, 0, 0 } };
    ASSERT_EQ(drive(vkr, 2, VOICE_MODE_TOGGLE, 1, VOICE_PTT_RIGHT_SHIFT), 1);
    ASSERT_EQ(drive(vkr, 2, VOICE_MODE_HOLD, 1, VOICE_PTT_RIGHT_SHIFT), 1);
    /* the same press with Right Ctrl selected is only a non-PTT key */
    ASSERT_EQ(drive(clean, 2, VOICE_MODE_TOGGLE, 1, VOICE_PTT_RIGHT_CTRL), 0);
    ASSERT_EQ(drive(clean, 2, VOICE_MODE_HOLD, 1, VOICE_PTT_F8), 0);
    TEST_END();
}

int test_voice_right_shift_chords_never_open(void)
{
    TEST_BEGIN();
    const int K = VOICE_PTT_RIGHT_SHIFT;
    const long ext = EXT;
    /* typing a capital: RShift down, 'A' down and up, RShift up */
    const KEv cap[] = { RS_DN, { WM_KEYDOWN, 'A', SC(0x1E), 0 }, { WM_KEYUP, 'A', SC(0x1E), 0 }, RS_UP };
    /* Shift+click */
    const KEv click[] = { RS_DN, { WM_LBUTTONDOWN, 0, 0, 0 }, RS_UP };
    /* Shift+wheel */
    const KEv wheel[] = { RS_DN, { WM_MOUSEWHEEL, 0, 0, 0 }, { WM_MOUSEHWHEEL, 0, 0, 0 }, RS_UP };
    /* Shift+Insert */
    const KEv ins[] = { RS_DN, { WM_KEYDOWN, VK_INS, SC(0x52) | ext, 0 },
                        { WM_KEYUP, VK_INS, SC(0x52) | ext, 0 }, RS_UP };
    /* Shift+arrows, including the auto-repeating arrow */
    const KEv arrows[] = { RS_DN,
        { WM_KEYDOWN, VK_LEFT_, SC(0x4B) | ext, 0 }, { WM_KEYDOWN, VK_LEFT_, SC(0x4B) | ext | REP, 0 },
        { WM_KEYDOWN, VK_LEFT_, SC(0x4B) | ext | REP, 0 }, { WM_KEYUP, VK_LEFT_, SC(0x4B) | ext, 0 },
        { WM_KEYDOWN, VK_RIGHT_, SC(0x4D) | ext, 0 }, { WM_KEYUP, VK_RIGHT_, SC(0x4D) | ext, 0 },
        { WM_KEYDOWN, VK_UP_, SC(0x48) | ext, 0 }, { WM_KEYUP, VK_UP_, SC(0x48) | ext, 0 },
        { WM_KEYDOWN, VK_DOWN_, SC(0x50) | ext, 0 }, { WM_KEYUP, VK_DOWN_, SC(0x50) | ext, 0 }, RS_UP };
    /* Ctrl+Shift+C / V, Shift pressed first: Ctrl then C arrive while Shift is down */
    const KEv sc_c[] = { RS_DN, { WM_KEYDOWN, VK_CONTROL, SC(SC_CTRL), 0 },
                         { WM_KEYDOWN, 'C', SC(0x2E), 0 }, { WM_KEYUP, 'C', SC(0x2E), 0 },
                         { WM_KEYUP, VK_CONTROL, SC(SC_CTRL), 0 }, RS_UP };
    const KEv sc_v[] = { RS_DN, { WM_KEYDOWN, VK_CONTROL, SC(SC_CTRL), 0 },
                         { WM_KEYDOWN, 'V', SC(0x2F), 0 }, { WM_KEYUP, 'V', SC(0x2F), 0 },
                         { WM_KEYUP, VK_CONTROL, SC(SC_CTRL), 0 }, RS_UP };
    /* Ctrl first: the PTT key-down sees Left Ctrl held (via the chord filter's mods) */
    const KEv cs_c[] = { { WM_KEYDOWN, VK_CONTROL, SC(SC_CTRL), 0 },
                         { WM_KEYDOWN, VK_SHIFT, SC(SC_RSHIFT), VOICE_MOD_LCTRL | VOICE_MOD_RSHIFT },
                         { WM_KEYDOWN, 'C', SC(0x2E), 0 }, { WM_KEYUP, 'C', SC(0x2E), 0 },
                         RS_UP, { WM_KEYUP, VK_CONTROL, SC(SC_CTRL), 0 } };
    /* Ctrl+Shift alone (the input-language hotkey), Ctrl first and Shift first */
    const KEv cs_alone[] = { { WM_KEYDOWN, VK_CONTROL, SC(SC_CTRL), 0 },
                             { WM_KEYDOWN, VK_SHIFT, SC(SC_RSHIFT), VOICE_MOD_LCTRL | VOICE_MOD_RSHIFT },
                             RS_UP, { WM_KEYUP, VK_CONTROL, SC(SC_CTRL), 0 } };
    const KEv sc_alone[] = { RS_DN, { WM_KEYDOWN, VK_CONTROL, SC(SC_CTRL), 0 },
                             { WM_KEYUP, VK_CONTROL, SC(SC_CTRL), 0 }, RS_UP };
    /* Right Ctrl + Right Shift, and Alt + Right Shift (Alt first) */
    const KEv rc_rs[] = { { WM_KEYDOWN, VK_CONTROL, SC(SC_CTRL) | ext, 0 },
                          { WM_KEYDOWN, VK_SHIFT, SC(SC_RSHIFT), VOICE_MOD_RCTRL | VOICE_MOD_RSHIFT },
                          RS_UP, { WM_KEYUP, VK_CONTROL, SC(SC_CTRL) | ext, 0 } };
    const KEv alt_rs[] = { { WM_SYSKEYDOWN, VK_MENU, SC(0x38), 0 },
                           { WM_SYSKEYDOWN, VK_SHIFT, SC(SC_RSHIFT), VOICE_MOD_ALT | VOICE_MOD_RSHIFT },
                           { WM_SYSKEYUP, VK_SHIFT, SC(SC_RSHIFT), 0 }, { WM_KEYUP, VK_MENU, SC(0x38), 0 } };
    /* Win+Shift (Win+Shift+S) */
    const KEv win_rs[] = { { WM_KEYDOWN, VK_LWIN_, SC(0x5B) | ext, 0 },
                           { WM_KEYDOWN, VK_SHIFT, SC(SC_RSHIFT), VOICE_MOD_WIN | VOICE_MOD_RSHIFT },
                           { WM_KEYDOWN, 'S', SC(0x1F), 0 }, { WM_KEYUP, 'S', SC(0x1F), 0 },
                           RS_UP, { WM_KEYUP, VK_LWIN_, SC(0x5B) | ext, 0 } };
    /* Left Shift held, then Right Shift (a Shift+Shift press) */
    const KEv ls_rs[] = { { WM_KEYDOWN, VK_SHIFT, SC(SC_LSHIFT), 0 },
                          { WM_KEYDOWN, VK_SHIFT, SC(SC_RSHIFT), VOICE_MOD_LSHIFT | VOICE_MOD_RSHIFT },
                          RS_UP, { WM_KEYUP, VK_SHIFT, SC(SC_LSHIFT), 0 } };
    /* Left Shift alone, and the faked extended shifts around numpad arrows */
    const KEv ls[] = { { WM_KEYDOWN, VK_SHIFT, SC(SC_LSHIFT), VOICE_MOD_LSHIFT },
                       { WM_KEYUP, VK_SHIFT, SC(SC_LSHIFT), 0 } };
    const KEv fake[] = { { WM_KEYDOWN, VK_SHIFT, SC(SC_RSHIFT) | ext, VOICE_MOD_RSHIFT },
                         { WM_KEYUP, VK_SHIFT, SC(SC_RSHIFT) | ext, 0 },
                         { WM_KEYDOWN, VK_SHIFT, SC(SC_LSHIFT) | ext, VOICE_MOD_LSHIFT },
                         { WM_KEYUP, VK_SHIFT, SC(SC_LSHIFT) | ext, 0 } };
#define CASE(a) { a, sizeof a / sizeof a[0] }
    struct { const KEv *s; size_t n; } cases[] = {
        CASE(cap), CASE(click), CASE(wheel), CASE(ins), CASE(arrows), CASE(sc_c), CASE(sc_v),
        CASE(cs_c), CASE(cs_alone), CASE(sc_alone), CASE(rc_rs), CASE(alt_rs), CASE(win_rs),
        CASE(ls_rs), CASE(ls), CASE(fake)
    };
#undef CASE
    for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
        for (int mode = 0; mode < (int)VOICE_MODE_COUNT; mode++) {
            for (int in_panel = 0; in_panel < 2; in_panel++) {
                if (drive(cases[c].s, cases[c].n, mode, in_panel, K) != 0)
                    printf("  chord case %d mode %d in_panel %d opened\n", (int)c, mode, in_panel);
                ASSERT_EQ(drive(cases[c].s, cases[c].n, mode, in_panel, K), 0);
            }
        }
    }
    TEST_END();
}

int test_voice_right_shift_chord_then_clean_press_still_works(void)
{
    TEST_BEGIN();
    /* a chord spends only its own press: the next clean press opens */
    const KEv seq[] = { RS_DN, { WM_KEYDOWN, 'A', SC(0x1E), 0 }, { WM_KEYUP, 'A', SC(0x1E), 0 }, RS_UP,
                        RS_DN, RS_UP };
    ASSERT_EQ(drive(seq, 6, VOICE_MODE_TOGGLE, 1, VOICE_PTT_RIGHT_SHIFT), 1);
    ASSERT_EQ(drive(seq, 6, VOICE_MODE_HOLD, 1, VOICE_PTT_RIGHT_SHIFT), 1);
    /* Escape while the key is down spends the press as well */
    const KEv esc[] = { RS_DN, { WM_KEYDOWN, VK_ESCAPE, SC(0x01), 0 }, RS_UP };
    ASSERT_EQ(drive(esc, 3, VOICE_MODE_TOGGLE, 1, VOICE_PTT_RIGHT_SHIFT), 0);
    ASSERT_EQ(drive(esc, 3, VOICE_MODE_HOLD, 1, VOICE_PTT_RIGHT_SHIFT), 0);
    TEST_END();
}

/* Toggle: the stop press is still held when the 60 s cap arrives. The machine's
 * own tick finishes it (once); the later release only returns to idle. */
int test_voice_toggle_cap_during_stop_press_tick(void)
{
    TEST_BEGIN();
    VoicePtt p; VoiceStep s;
    voice_ptt_init(&p, VOICE_MODE_TOGGLE, 0);
    st(&p, VE_KEY_DOWN, 1000, 1, 0);
    ASSERT_EQ(st(&p, VE_KEY_UP, 1050, 1, 0).action, VA_OPEN);
    ASSERT_EQ(p.state, VS_REC_LATCHED);
    ASSERT_EQ(st(&p, VE_KEY_DOWN, 1050 + VOICE_CAP_MS - 200, 1, 0).action, VA_NONE);
    ASSERT_EQ(p.state, VS_STOP_PRESS);
    /* just before the cap: nothing */
    s = st(&p, VE_TICK, 1050 + VOICE_CAP_MS - 1, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_STOP_PRESS);
    /* at the cap: one finish, key still down */
    s = st(&p, VE_TICK, 1050 + VOICE_CAP_MS, 1, 0);
    ASSERT_EQ(s.action, VA_FINISH);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    ASSERT_EQ(voice_ptt_recording(&p), 0);
    s = st(&p, VE_TICK, 1050 + VOICE_CAP_MS + 20, 1, 0);          /* no second finish */
    ASSERT_EQ(s.action, VA_NONE);
    s = st(&p, VE_KEY_UP, 1050 + VOICE_CAP_MS + 40, 1, 0);        /* release: idle, no finish */
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_IDLE);
    /* a fresh clean press works */
    ASSERT_EQ(st(&p, VE_KEY_DOWN, 1050 + VOICE_CAP_MS + 500, 1, 0).action, VA_NONE);
    s = st(&p, VE_KEY_UP, 1050 + VOICE_CAP_MS + 540, 1, 0);
    ASSERT_EQ(s.action, VA_OPEN);
    ASSERT_EQ(p.state, VS_REC_LATCHED);
    TEST_END();
}

/* The same cap, but the audio thread ends the capture first (voice_win.c passes
 * key_held = REC_HOLD or STOP_PRESS): no action, and nothing after it finishes. */
int test_voice_toggle_cap_during_stop_press_audio_first(void)
{
    TEST_BEGIN();
    VoicePtt p; VoiceStep s;
    voice_ptt_init(&p, VOICE_MODE_TOGGLE, 0);
    st(&p, VE_KEY_DOWN, 1000, 1, 0);
    ASSERT_EQ(st(&p, VE_KEY_UP, 1050, 1, 0).action, VA_OPEN);
    st(&p, VE_KEY_DOWN, 1050 + VOICE_CAP_MS - 100, 1, 0);
    ASSERT_EQ(p.state, VS_STOP_PRESS);
    voice_ptt_capture_ended(&p, p.state == VS_REC_HOLD || p.state == VS_STOP_PRESS);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    ASSERT_EQ(voice_ptt_recording(&p), 0);
    s = st(&p, VE_TICK, 1050 + VOICE_CAP_MS + 5, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    s = st(&p, VE_KEY_UP, 1050 + VOICE_CAP_MS + 30, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_IDLE);
    TEST_END();
}

/* Toggle: a capital typed with Right Shift during a recording keeps it. */
int test_voice_toggle_chord_in_stop_press_keeps_recording(void)
{
    TEST_BEGIN();
    VoicePtt p; VoiceStep s;
    voice_ptt_init(&p, VOICE_MODE_TOGGLE, 0);
    st(&p, VE_KEY_DOWN, 0, 1, 0);
    ASSERT_EQ(st(&p, VE_KEY_UP, 50, 1, 0).action, VA_OPEN);
    st(&p, VE_KEY_DOWN, 1000, 1, 0);
    st(&p, VE_OTHER, 1010, 1, 0);                 /* 'A' down */
    ASSERT_EQ(p.state, VS_LATCHED_SPENT);
    s = st(&p, VE_OTHER, 1020, 1, 0);             /* more typing */
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_LATCHED_SPENT);
    s = st(&p, VE_KEY_UP, 1100, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_REC_LATCHED);
    ASSERT_EQ(voice_ptt_recording(&p), 1);
    ASSERT_TRUE(voice_ptt_next_deadline(&p, 1100) > 0);
    /* a clean press and release now finishes */
    st(&p, VE_KEY_DOWN, 2000, 1, 0);
    s = st(&p, VE_KEY_UP, 2050, 1, 0);
    ASSERT_EQ(s.action, VA_FINISH);
    ASSERT_EQ(p.state, VS_IDLE);
    /* a mic click during the spent press is just another event */
    voice_ptt_init(&p, VOICE_MODE_TOGGLE, 0);
    st(&p, VE_MIC_CLICK, 0, 1, 0);
    st(&p, VE_KEY_DOWN, 100, 1, 0);
    s = st(&p, VE_MIC_CLICK, 110, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_LATCHED_SPENT);
    /* Hold mode is unchanged: a chord during the recording discards */
    voice_ptt_init(&p, VOICE_MODE_HOLD, 0);
    st(&p, VE_KEY_DOWN, 0, 1, 0);
    ASSERT_EQ(st(&p, VE_OTHER, 300, 1, 0).action, VA_DISCARD);
    TEST_END();
}

int test_voice_toggle_latched_spent_escape_deactivate_cap(void)
{
    TEST_BEGIN();
    VoicePtt p; VoiceStep s;
    /* Escape discards; the key is down, so its release returns to idle */
    voice_ptt_init(&p, VOICE_MODE_TOGGLE, 0);
    st(&p, VE_KEY_DOWN, 0, 1, 0); st(&p, VE_KEY_UP, 10, 1, 0);
    st(&p, VE_KEY_DOWN, 500, 1, 0); st(&p, VE_OTHER, 510, 1, 0);
    s = st(&p, VE_ESCAPE, 520, 1, 0);
    ASSERT_EQ(s.action, VA_DISCARD);
    ASSERT_EQ(s.consume, 1);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    ASSERT_EQ(st(&p, VE_KEY_UP, 530, 1, 0).action, VA_NONE);
    ASSERT_EQ(p.state, VS_IDLE);
    /* deactivate discards */
    voice_ptt_init(&p, VOICE_MODE_TOGGLE, 0);
    st(&p, VE_KEY_DOWN, 0, 1, 0); st(&p, VE_KEY_UP, 10, 1, 0);
    st(&p, VE_KEY_DOWN, 500, 1, 0); st(&p, VE_OTHER, 510, 1, 0);
    s = st(&p, VE_DEACTIVATE, 520, 1, 0);
    ASSERT_EQ(s.action, VA_DISCARD);
    ASSERT_EQ(p.state, VS_IDLE);
    /* the cap finishes once, key still down */
    voice_ptt_init(&p, VOICE_MODE_TOGGLE, 0);
    st(&p, VE_KEY_DOWN, 1000, 1, 0); st(&p, VE_KEY_UP, 1050, 1, 0);
    st(&p, VE_KEY_DOWN, 1100, 1, 0); st(&p, VE_OTHER, 1110, 1, 0);
    ASSERT_EQ(st(&p, VE_TICK, 1050 + VOICE_CAP_MS - 1, 1, 0).action, VA_NONE);
    ASSERT_EQ(p.state, VS_LATCHED_SPENT);
    s = st(&p, VE_TICK, 1050 + VOICE_CAP_MS, 1, 0);
    ASSERT_EQ(s.action, VA_FINISH);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    ASSERT_EQ(st(&p, VE_KEY_UP, 1050 + VOICE_CAP_MS + 5, 1, 0).action, VA_NONE);
    ASSERT_EQ(p.state, VS_IDLE);
    /* the audio thread ends the capture first: key held, so key-spent */
    voice_ptt_init(&p, VOICE_MODE_TOGGLE, 0);
    st(&p, VE_KEY_DOWN, 0, 1, 0); st(&p, VE_KEY_UP, 10, 1, 0);
    st(&p, VE_KEY_DOWN, 500, 1, 0); st(&p, VE_OTHER, 510, 1, 0);
    voice_ptt_capture_ended(&p, p.state == VS_REC_HOLD || p.state == VS_STOP_PRESS
                                || p.state == VS_LATCHED_SPENT);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    ASSERT_EQ(voice_ptt_recording(&p), 0);
    TEST_END();
}

/* Both Shifts down then released: Windows sends no key-up for the first one
 * released, so the machine can sit in KEY_SPENT. The next real down is a press. */
int test_voice_key_spent_lost_up_new_down_is_a_press(void)
{
    TEST_BEGIN();
    VoicePtt p; VoiceStep s;
    voice_ptt_init(&p, VOICE_MODE_TOGGLE, 0);
    st(&p, VE_KEY_DOWN, 0, 1, 0);
    st(&p, VE_OTHER, 10, 1, 0);                   /* the left Shift down: a chord */
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    /* a repeat never reaches the machine (VI_PTT_REPEAT feeds nothing), and
     * other input changes nothing */
    s = st(&p, VE_OTHER, 20, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    s = st(&p, VE_TICK, 30, 1, 0);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    /* the up was lost: a fresh clean press opens */
    s = st(&p, VE_KEY_DOWN, 1000, 1, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_TOGGLE_PRESS);
    s = st(&p, VE_KEY_UP, 1050, 1, 0);
    ASSERT_EQ(s.action, VA_OPEN);
    ASSERT_EQ(p.state, VS_REC_LATCHED);
    /* Hold mode: the same recovery */
    voice_ptt_init(&p, VOICE_MODE_HOLD, 0);
    st(&p, VE_KEY_DOWN, 0, 1, 0);
    st(&p, VE_OTHER, 10, 1, 0);
    ASSERT_EQ(p.state, VS_KEY_SPENT);
    s = st(&p, VE_KEY_DOWN, 1000, 1, 0);
    ASSERT_EQ(s.action, VA_OPEN);
    ASSERT_EQ(p.state, VS_REC_HOLD);
    /* a down outside the panel just returns to idle */
    voice_ptt_init(&p, VOICE_MODE_TOGGLE, 0);
    st(&p, VE_KEY_DOWN, 0, 1, 0);
    st(&p, VE_OTHER, 10, 1, 0);
    s = st(&p, VE_KEY_DOWN, 100, 0, 0);
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_IDLE);
    /* the normal release still returns to idle */
    voice_ptt_init(&p, VOICE_MODE_TOGGLE, 0);
    st(&p, VE_KEY_DOWN, 0, 1, 0);
    st(&p, VE_OTHER, 10, 1, 0);
    st(&p, VE_KEY_UP, 20, 1, 0);
    ASSERT_EQ(p.state, VS_IDLE);
    TEST_END();
}

/* The lost key-up also happens in a spent stop press: RS down (stop press),
 * Left Shift down (chord), RS released first (Windows sends no key-up), Left
 * Shift up (nothing). The next clean press is the stopping press again. */
int test_voice_latched_spent_lost_up_new_down_is_stop_press(void)
{
    TEST_BEGIN();
    VoicePtt p; VoiceStep s;
    voice_ptt_init(&p, VOICE_MODE_TOGGLE, 0);
    st(&p, VE_KEY_DOWN, 0, 1, 0);
    ASSERT_EQ(st(&p, VE_KEY_UP, 50, 1, 0).action, VA_OPEN);
    st(&p, VE_KEY_DOWN, 1000, 1, 0);                  /* RS: stop press */
    st(&p, VE_OTHER, 1010, 1, 0);                     /* Left Shift down */
    ASSERT_EQ(p.state, VS_LATCHED_SPENT);
    s = st(&p, VE_KEY_DOWN, 2000, 1, 0);              /* the up was lost */
    ASSERT_EQ(s.action, VA_NONE);
    ASSERT_EQ(p.state, VS_STOP_PRESS);
    s = st(&p, VE_KEY_UP, 2050, 1, 0);
    ASSERT_EQ(s.action, VA_FINISH);
    ASSERT_EQ(p.state, VS_IDLE);
    TEST_END();
}
