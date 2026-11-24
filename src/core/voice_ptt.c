#include "voice_ptt.h"
#include "voice_config.h"
#include <stdio.h>
#include <string.h>

#define LP_EXTENDED 0x01000000UL
#define LP_REPEAT   0x40000000UL
#define SCAN_RSHIFT 0x36UL

static int is_right_shift(unsigned vk, unsigned long lp)
{
    if (lp & LP_EXTENDED) return 0;
    if (vk == VK_RSHIFT) return 1;
    return vk == VK_SHIFT && ((lp >> 16) & 0xFFUL) == SCAN_RSHIFT;
}

VoiceInput voice_ptt_chord_filter(VoiceInput in, int ptt_key, unsigned mods)
{
    unsigned own = 0;
    if (ptt_key == VOICE_PTT_RIGHT_SHIFT) own = VOICE_MOD_RSHIFT;
    else if (ptt_key == VOICE_PTT_RIGHT_CTRL) own = VOICE_MOD_RCTRL;
    if (in == VI_PTT_DOWN && (mods & ~own)) return VI_OTHER;
    return in;
}

VoiceInput voice_ptt_classify(unsigned msg, uintptr_t wparam, intptr_t lparam, int ptt_key)
{
    unsigned long lp = (unsigned long)(uintptr_t)lparam;
    unsigned vk = (unsigned)(wparam & 0xFFFFu);
    switch (msg) {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        int down = (msg == WM_KEYDOWN);
        int is_ptt = 0;
        if (ptt_key == VOICE_PTT_F8) is_ptt = (vk == VK_F8);
        else if (ptt_key == VOICE_PTT_F9) is_ptt = (vk == VK_F9);
        else if (ptt_key == VOICE_PTT_RIGHT_SHIFT) is_ptt = is_right_shift(vk, lp);
        else if (ptt_key == VOICE_PTT_RIGHT_CTRL)
            is_ptt = ((vk == VK_CONTROL || vk == VK_RCONTROL) && (lp & LP_EXTENDED));
        if (is_ptt) {
            if (!down) return VI_PTT_UP;
            return (lp & LP_REPEAT) ? VI_PTT_REPEAT : VI_PTT_DOWN;
        }
        if (!down) return VI_NONE;
        return vk == VK_ESCAPE ? VI_ESCAPE : VI_OTHER;
    }
    case WM_SYSKEYDOWN:
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_XBUTTONDOWN:
    case WM_NCLBUTTONDOWN: case WM_NCRBUTTONDOWN: case WM_NCMBUTTONDOWN: case WM_NCXBUTTONDOWN:
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        return VI_OTHER;
    default:
        return VI_NONE;
    }
}

void voice_ptt_init(VoicePtt *p, int mode, int delay_ms)
{
    memset(p, 0, sizeof *p);
    p->state = VS_IDLE;
    p->mode = mode;
    p->delay_ms = delay_ms < 0 ? 0 : delay_ms;
}

int voice_ptt_recording(const VoicePtt *p)
{
    return p->state == VS_REC_HOLD || p->state == VS_REC_LATCHED || p->state == VS_STOP_PRESS
        || p->state == VS_LATCHED_SPENT;
}

static VoiceStep mk(VoiceAction a, int consume)
{
    VoiceStep s; s.action = a; s.consume = consume; return s;
}

/* The ARMED delay check, shared by KEY_DOWN (delay 0) and TICK. */
static VoiceStep armed_check(VoicePtt *p, unsigned long now, int busy)
{
    if ((long)(now - p->key_down_at) < (long)p->delay_ms) return mk(VA_NONE, 0);
    if (busy) { p->state = VS_KEY_SPENT; return mk(VA_BUSY, 0); }
    p->state = VS_REC_HOLD;
    p->rec_start_at = now;
    return mk(VA_OPEN, 0);
}

static int cap_reached(const VoicePtt *p, unsigned long now)
{
    return (long)(now - p->rec_start_at) >= VOICE_CAP_MS;
}

VoiceStep voice_ptt_step(VoicePtt *p, VoiceEvent ev, unsigned long now, int in_panel, int busy)
{
    int rec = voice_ptt_recording(p);

    if (ev == VE_DEACTIVATE) {
        p->state = VS_IDLE;
        return mk(rec ? VA_DISCARD : VA_NONE, 0);
    }
    if (ev == VE_ESCAPE) {
        if (rec) {
            int key_down = (p->state == VS_REC_HOLD || p->state == VS_STOP_PRESS
                            || p->state == VS_LATCHED_SPENT);
            p->state = key_down ? VS_KEY_SPENT : VS_IDLE;
            return mk(VA_DISCARD, 1);
        }
        /* Escape keeps its meaning; a chord with it still spends the key. */
        if (p->state == VS_ARMED || p->state == VS_TOGGLE_PRESS) p->state = VS_KEY_SPENT;
        return mk(VA_NONE, 0);
    }
    if (ev == VE_MIC_CLICK) {
        if (p->state == VS_IDLE) {
            if (busy) return mk(VA_BUSY, 0);
            p->state = VS_REC_LATCHED;
            p->rec_start_at = now;
            return mk(VA_OPEN, 0);
        }
        if (p->state == VS_REC_LATCHED) { p->state = VS_IDLE; return mk(VA_FINISH, 0); }
        ev = VE_OTHER;
    }

    switch (p->state) {
    case VS_IDLE:
        if (ev == VE_KEY_DOWN && in_panel) {
            p->key_down_at = now;
            if (p->mode == VOICE_MODE_TOGGLE) { p->state = VS_TOGGLE_PRESS; return mk(VA_NONE, 0); }
            p->state = VS_ARMED;
            return armed_check(p, now, busy);
        }
        return mk(VA_NONE, 0);
    case VS_ARMED:
        if (ev == VE_TICK) return armed_check(p, now, busy);
        if (ev == VE_KEY_UP) { p->state = VS_IDLE; return mk(VA_NONE, 0); }
        if (ev == VE_OTHER) { p->state = VS_KEY_SPENT; return mk(VA_NONE, 0); }
        return mk(VA_NONE, 0);
    case VS_TOGGLE_PRESS:
        if (ev == VE_OTHER) { p->state = VS_KEY_SPENT; return mk(VA_NONE, 0); }
        if (ev == VE_KEY_UP) {
            if (busy) { p->state = VS_IDLE; return mk(VA_BUSY, 0); }
            p->state = VS_REC_LATCHED;
            p->rec_start_at = now;
            return mk(VA_OPEN, 0);
        }
        return mk(VA_NONE, 0);
    case VS_REC_HOLD:
        if (ev == VE_KEY_UP) {
            int tap = (long)(now - p->key_down_at) < VOICE_SHORT_TAP_MS;
            p->state = VS_IDLE;
            return mk(tap ? VA_DISCARD : VA_FINISH, 0);
        }
        if (ev == VE_OTHER) { p->state = VS_KEY_SPENT; return mk(VA_DISCARD, 0); }
        if (ev == VE_TICK && cap_reached(p, now)) { p->state = VS_KEY_SPENT; return mk(VA_FINISH, 0); }
        return mk(VA_NONE, 0);
    case VS_REC_LATCHED:
        if (ev == VE_KEY_DOWN) { p->state = VS_STOP_PRESS; return mk(VA_NONE, 0); }
        if (ev == VE_TICK && cap_reached(p, now)) { p->state = VS_IDLE; return mk(VA_FINISH, 0); }
        return mk(VA_NONE, 0);
    case VS_STOP_PRESS:
        if (ev == VE_KEY_UP) { p->state = VS_IDLE; return mk(VA_FINISH, 0); }
        /* A chord spends the press but keeps the recording (a capital typed
         * with Right Shift must not throw the dictation away). */
        if (ev == VE_OTHER) { p->state = VS_LATCHED_SPENT; return mk(VA_NONE, 0); }
        if (ev == VE_TICK && cap_reached(p, now)) { p->state = VS_KEY_SPENT; return mk(VA_FINISH, 0); }
        return mk(VA_NONE, 0);
    case VS_LATCHED_SPENT:
        /* A real key-down here means the up was lost (both Shifts): the key
         * is pressed again, so this is the stopping press once more. */
        if (ev == VE_KEY_DOWN) { p->state = VS_STOP_PRESS; return mk(VA_NONE, 0); }
        if (ev == VE_KEY_UP) { p->state = VS_REC_LATCHED; return mk(VA_NONE, 0); }
        if (ev == VE_TICK && cap_reached(p, now)) { p->state = VS_KEY_SPENT; return mk(VA_FINISH, 0); }
        return mk(VA_NONE, 0);
    case VS_KEY_SPENT:
        if (ev == VE_KEY_UP) { p->state = VS_IDLE; return mk(VA_NONE, 0); }
        /* A real key-down here means the up was lost (Windows sends no key-up
         * for the first Shift released while both are down): a fresh press. */
        if (ev == VE_KEY_DOWN && in_panel) {
            p->key_down_at = now;
            if (p->mode == VOICE_MODE_TOGGLE) { p->state = VS_TOGGLE_PRESS; return mk(VA_NONE, 0); }
            p->state = VS_ARMED;
            return armed_check(p, now, busy);
        }
        if (ev == VE_KEY_DOWN) p->state = VS_IDLE;
        return mk(VA_NONE, 0);
    }
    return mk(VA_NONE, 0);
}

long voice_ptt_next_deadline(const VoicePtt *p, unsigned long now)
{
    long left;
    if (p->state == VS_ARMED) {
        left = (long)p->delay_ms - (long)(now - p->key_down_at);
    } else if (voice_ptt_recording(p)) {
        left = (long)VOICE_CAP_MS - (long)(now - p->rec_start_at);
    } else {
        return -1;
    }
    return left < 0 ? 0 : left;
}

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

VoiceDelivery voice_deliver(const char *input, const char *transcript, int auto_send,
                            char *out, size_t cap)
{
    if (!out || cap == 0) return VD_NOTHING;
    out[0] = '\0';
    if (!transcript) return VD_NOTHING;
    const char *t = transcript;
    while (*t && is_space(*t)) t++;
    if (!*t) return VD_NOTHING;
    if (!input) input = "";
    size_t il = strlen(input);
    if (il == 0) {
        snprintf(out, cap, "%s", transcript);
        return auto_send ? VD_SEND : VD_FILL;
    }
    int sep = !is_space(input[il - 1]);
    snprintf(out, cap, "%s%s%s", input, sep ? " " : "", transcript);
    return VD_APPEND;
}

int voice_binding_same(const VoiceBinding *a, const VoiceBinding *b)
{
    return a && b && a->session && a->session == b->session
        && a->session_gen == b->session_gen && a->conv_gen == b->conv_gen;
}

void voice_ptt_capture_ended(VoicePtt *p, int key_held)
{
    if (!voice_ptt_recording(p)) return;
    p->state = key_held ? VS_KEY_SPENT : VS_IDLE;
}

VoiceErrRoute voice_err_route(unsigned err_token, unsigned cur_token, int capturing)
{
    err_token &= VOICE_TOKEN_MASK;
    cur_token &= VOICE_TOKEN_MASK;
    if (err_token == 0) return VOICE_ERR_SETTINGS;
    if (capturing && cur_token != 0 && err_token == cur_token) return VOICE_ERR_CAPTURE;
    return VOICE_ERR_STALE;
}

const char *voice_busy_text(int pending, int no_session)
{
    if (no_session) return "Connect a session first";
    return pending ? "Still transcribing the last recording" : "Assistant is busy";
}

void voice_cue_text(int mode, int ptt_key, int latched, int auto_send,
                    char *out, size_t cap)
{
    const char *label = voice_ptt_key_label(ptt_key);
    const char *verb = auto_send ? "send" : "finish";
    if (mode == VOICE_MODE_HOLD && !latched)
        snprintf(out, cap, "Listening\xE2\x80\xA6 release %s to %s", label, verb);
    else
        snprintf(out, cap, "Listening\xE2\x80\xA6 press %s again to %s", label, verb);
}
