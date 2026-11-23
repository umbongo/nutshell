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

/* Right Ctrl = VK_CONTROL (or VK_RCONTROL) with lParam bit 24 (extended) set, on
 * WM_KEYDOWN/WM_KEYUP; bit 30 set on a key-down = repeat. F8/F9 = VK_F8/VK_F9 when
 * ptt_key selects them. Escape key-down = VI_ESCAPE. Any other WM_KEYDOWN or
 * WM_SYSKEYDOWN, mouse button downs (client and non-client) and both wheels =
 * VI_OTHER. Everything else = VI_NONE. */
VoiceInput voice_ptt_classify(unsigned msg, uintptr_t wparam, intptr_t lparam, int ptt_key);

typedef enum { VS_IDLE = 0, VS_ARMED, VS_TOGGLE_PRESS, VS_REC_HOLD, VS_REC_LATCHED,
               VS_STOP_PRESS, VS_KEY_SPENT } VoiceState;
typedef enum { VA_NONE = 0, VA_OPEN, VA_FINISH, VA_DISCARD, VA_BUSY } VoiceAction;
typedef enum { VE_KEY_DOWN, VE_KEY_UP, VE_OTHER, VE_ESCAPE, VE_TICK, VE_MIC_CLICK,
               VE_DEACTIVATE } VoiceEvent;
typedef struct {
    VoiceState state; int mode; int delay_ms;
    unsigned long key_down_at, rec_start_at;
} VoicePtt;
typedef struct { VoiceAction action; int consume; } VoiceStep;

void voice_ptt_init(VoicePtt *p, int mode, int delay_ms);
/* in_panel: keyboard focus is in the AI panel; busy: the assistant is streaming,
 * running commands or has a card waiting. */
VoiceStep voice_ptt_step(VoicePtt *p, VoiceEvent ev, unsigned long now_ms,
                         int in_panel, int busy);
int voice_ptt_recording(const VoicePtt *p);  /* VS_REC_HOLD, VS_REC_LATCHED or VS_STOP_PRESS */
/* ms until the next VE_TICK matters (delay end or cap), or -1 when none. */
long voice_ptt_next_deadline(const VoicePtt *p, unsigned long now_ms);

typedef enum { VD_NOTHING = 0, VD_SEND, VD_FILL, VD_APPEND } VoiceDelivery;
/* Whitespace-only transcript -> VD_NOTHING. Empty input: review ? VD_FILL : VD_SEND,
 * out = transcript. Non-empty input -> VD_APPEND, out = input + (input ends in a space
 * or newline ? "" : " ") + transcript. Truncates to cap. */
VoiceDelivery voice_deliver(const char *input, const char *transcript, int review,
                            char *out, size_t cap);

typedef struct { const void *session; unsigned session_gen; unsigned conv_gen; } VoiceBinding;
int voice_binding_same(const VoiceBinding *a, const VoiceBinding *b);  /* all three equal, session non-NULL */

/* "Listening… release Right Ctrl to send" (hold, key started),
 * "Listening… press Right Ctrl again to send" (toggle, or a mic-button start),
 * with F8/F9 in place of Right Ctrl. The ellipsis is U+2026 in UTF-8. */
void voice_cue_text(int mode, int ptt_key, int latched, char *out, size_t cap);

#endif /* NUTSHELL_VOICE_PTT_H */
