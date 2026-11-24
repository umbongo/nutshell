#ifndef NUTSHELL_VOICE_PTT_H
#define NUTSHELL_VOICE_PTT_H

#include <stddef.h>
#include <stdint.h>

/*
 * voice_ptt -- push-to-talk: the Win32 message classifier (numeric constants,
 * portable), the state machine (time injected), transcript delivery and the
 * session binding. No Win32 calls; the controller in src/ui feeds it.
 */

/* Win32 numbers, defined only where windows.h has not already. */
#ifndef WM_KEYDOWN
#define WM_KEYDOWN 0x0100
#endif
#ifndef WM_KEYUP
#define WM_KEYUP 0x0101
#endif
#ifndef WM_SYSKEYDOWN
#define WM_SYSKEYDOWN 0x0104
#endif
#ifndef WM_SYSKEYUP
#define WM_SYSKEYUP 0x0105
#endif
#ifndef WM_MOUSEMOVE
#define WM_MOUSEMOVE 0x0200
#endif
#ifndef WM_LBUTTONDOWN
#define WM_LBUTTONDOWN 0x0201
#endif
#ifndef WM_RBUTTONDOWN
#define WM_RBUTTONDOWN 0x0204
#endif
#ifndef WM_MBUTTONDOWN
#define WM_MBUTTONDOWN 0x0207
#endif
#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A
#endif
#ifndef WM_XBUTTONDOWN
#define WM_XBUTTONDOWN 0x020B
#endif
#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif
#ifndef WM_NCLBUTTONDOWN
#define WM_NCLBUTTONDOWN 0x00A1
#endif
#ifndef WM_NCRBUTTONDOWN
#define WM_NCRBUTTONDOWN 0x00A4
#endif
#ifndef WM_NCMBUTTONDOWN
#define WM_NCMBUTTONDOWN 0x00A7
#endif
#ifndef WM_NCXBUTTONDOWN
#define WM_NCXBUTTONDOWN 0x00AB
#endif
#ifndef VK_SHIFT
#define VK_SHIFT 0x10
#endif
#ifndef VK_LSHIFT
#define VK_LSHIFT 0xA0
#endif
#ifndef VK_RSHIFT
#define VK_RSHIFT 0xA1
#endif
#ifndef VK_LCONTROL
#define VK_LCONTROL 0xA2
#endif
#ifndef VK_CONTROL
#define VK_CONTROL 0x11
#endif
#ifndef VK_RCONTROL
#define VK_RCONTROL 0xA3
#endif
#ifndef VK_MENU
#define VK_MENU 0x12
#endif
#ifndef VK_ESCAPE
#define VK_ESCAPE 0x1B
#endif
#ifndef VK_F8
#define VK_F8 0x77
#endif
#ifndef VK_F9
#define VK_F9 0x78
#endif

typedef enum { VI_NONE = 0, VI_PTT_DOWN, VI_PTT_REPEAT, VI_PTT_UP, VI_ESCAPE, VI_OTHER } VoiceInput;

/* Right Shift = VK_RSHIFT, or VK_SHIFT with scan code (lParam bits 16-23) 0x36,
 * with lParam bit 24 (extended) CLEAR: a real Right Shift is never extended, and
 * the extended shift events the keyboard driver fakes around numpad keys
 * (Shift+numpad arrows with NumLock on) are not the key. Left Shift (scan 0x2A)
 * is never the key.
 * Right Ctrl = VK_CONTROL (or VK_RCONTROL) with lParam bit 24 (extended) set, on
 * WM_KEYDOWN/WM_KEYUP; bit 30 set on a key-down = repeat. F8/F9 = VK_F8/VK_F9 when
 * ptt_key selects them. Escape key-down = VI_ESCAPE. Any other WM_KEYDOWN or
 * WM_SYSKEYDOWN, mouse button downs (client and non-client) and both wheels =
 * VI_OTHER. Everything else = VI_NONE. */
VoiceInput voice_ptt_classify(unsigned msg, uintptr_t wparam, intptr_t lparam, int ptt_key);

/* Modifier keys held at a key-down, for voice_ptt_chord_filter (the controller
 * reads them with GetKeyState). */
#define VOICE_MOD_LSHIFT 0x01u
#define VOICE_MOD_RSHIFT 0x02u
#define VOICE_MOD_LCTRL  0x04u
#define VOICE_MOD_RCTRL  0x08u
#define VOICE_MOD_ALT    0x10u
#define VOICE_MOD_WIN    0x20u

/* A PTT key-down (not a repeat, not an up) while any modifier other than the
 * PTT key's own is already held is a chord (Ctrl+Shift, Win+Shift+S), not a
 * press: it becomes VI_OTHER. Every other input is returned unchanged. */
VoiceInput voice_ptt_chord_filter(VoiceInput in, int ptt_key, unsigned mods);

typedef enum { VS_IDLE = 0, VS_ARMED, VS_TOGGLE_PRESS, VS_REC_HOLD, VS_REC_LATCHED,
               VS_STOP_PRESS, VS_KEY_SPENT, VS_LATCHED_SPENT } VoiceState;
/* VS_LATCHED_SPENT: a Toggle recording whose stop press was spent by a chord
 * (a capital typed with Right Shift): still recording, the key still down. */
typedef enum { VA_NONE = 0, VA_OPEN, VA_FINISH, VA_DISCARD, VA_BUSY } VoiceAction;
typedef enum { VE_KEY_DOWN, VE_KEY_UP, VE_OTHER, VE_ESCAPE, VE_TICK, VE_MIC_CLICK,
               VE_DEACTIVATE } VoiceEvent;
typedef struct {
    VoiceState state; int mode; int delay_ms;
    unsigned long key_down_at, rec_start_at;
    long cap_ms;
} VoicePtt;
typedef struct { VoiceAction action; int consume; } VoiceStep;

void voice_ptt_init(VoicePtt *p, int mode, int delay_ms);
/* The per-recording length cap; cap_ms <= 0 means VOICE_CAP_MS. */
void voice_ptt_set_cap(VoicePtt *p, long cap_ms);
/* in_panel: keyboard focus is in the AI panel; busy: the assistant is streaming,
 * running commands or has a card waiting. */
VoiceStep voice_ptt_step(VoicePtt *p, VoiceEvent ev, unsigned long now_ms,
                         int in_panel, int busy);
/* The audio thread ended the capture itself (the cap) before the machine's own
 * finish: take the machine out of recording without an action. key_held: the
 * key is still down, so its release only returns to idle (VS_KEY_SPENT); else
 * straight to idle. A no-op when the machine is not recording. */
void voice_ptt_capture_ended(VoicePtt *p, int key_held);
int voice_ptt_recording(const VoicePtt *p);  /* VS_REC_HOLD, VS_REC_LATCHED, VS_STOP_PRESS or VS_LATCHED_SPENT */
/* ms until the next VE_TICK matters (delay end or cap), or -1 when none. */
long voice_ptt_next_deadline(const VoicePtt *p, unsigned long now_ms);

typedef enum { VD_NOTHING = 0, VD_SEND, VD_FILL, VD_APPEND } VoiceDelivery;
/* Whitespace-only transcript -> VD_NOTHING. Empty input: auto_send ? VD_SEND : VD_FILL,
 * out = transcript. Non-empty input -> VD_APPEND, out = input + (input ends in whitespace
 * ? "" : " ") + transcript. Truncates to cap. */
VoiceDelivery voice_deliver(const char *input, const char *transcript, int auto_send,
                            char *out, size_t cap);

typedef struct { const void *session; unsigned session_gen; unsigned conv_gen; } VoiceBinding;
int voice_binding_same(const VoiceBinding *a, const VoiceBinding *b);  /* all three equal, session non-NULL */

/* Where an audio error goes. An error carries the token of the recording it
 * ends (0 = none: a Settings Test, an enumeration, the thread itself).
 * VOICE_ERR_CAPTURE: the current, still-open recording's. VOICE_ERR_SETTINGS:
 * token 0, for an open Settings page. VOICE_ERR_STALE: another recording's
 * (a discarded or finished one): ignored, it must not touch the current state. */
typedef enum { VOICE_ERR_STALE = 0, VOICE_ERR_CAPTURE, VOICE_ERR_SETTINGS } VoiceErrRoute;
#define VOICE_TOKEN_MASK 0xFFFFFFu   /* recording tokens are compared in their low 24 bits */
VoiceErrRoute voice_err_route(unsigned err_token, unsigned cur_token, int capturing);

/* The status line for a start refused at the start point: "Connect a session
 * first" with no active session, else the transcribing/busy text. */
const char *voice_busy_text(int pending, int no_session);

/* "Listening… release Right Shift to send" (hold, key started),
 * "Listening… press Right Shift again to send" (toggle, or a mic-button start),
 * with auto_send on; with it off "to finish" takes the place of "to send" (the
 * words wait in the box for review). The selected key's label (Right Ctrl, F8,
 * F9) takes the place of Right Shift. The ellipsis is U+2026 in UTF-8. */
void voice_cue_text(int mode, int ptt_key, int latched, int auto_send,
                    char *out, size_t cap);

#endif /* NUTSHELL_VOICE_PTT_H */
