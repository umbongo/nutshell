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
    /* Toggle: a chord during the stop press discards too */
    VoicePtt q; VoiceStep s;
    voice_ptt_init(&q, VOICE_MODE_TOGGLE, 0);
    st(&q, VE_KEY_DOWN, 0, 1, 0);
    st(&q, VE_KEY_UP, 50, 1, 0);
    st(&q, VE_KEY_DOWN, 1000, 1, 0);
    s = st(&q, VE_OTHER, 1010, 1, 0);
    ASSERT_EQ(s.action, VA_DISCARD);
    ASSERT_EQ(q.state, VS_KEY_SPENT);
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

int test_voice_cue_text(void)
{
    TEST_BEGIN();
    char o[96];
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
