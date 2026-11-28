#ifndef NUTSHELL_VOICE_READ_H
#define NUTSHELL_VOICE_READ_H

#include <stddef.h>

/*
 * The Read aloud reader's state machine. Pure: no Win32, no allocation. The
 * glue (src/ui/voice_read_win.c) feeds it presses, engine messages and stop
 * reasons, and executes the returned actions in this order: STOP, RELEASE,
 * START (feeding its result to voice_read_on_started), SPEAK, ERROR, VIEW.
 *
 * Transitions (the tests pin each down):
 *  - voice_read_shown = voice_active && mode == VOICE_MODE_DICTATION;
 *    voice_read_gate = shown && !recording.
 *  - Press, gate 0: nothing.
 *  - Press on the row being read (not idle, same id): STOP|VIEW, idle.
 *  - Press on another row while reading: STOP, then go on as from idle.
 *  - From idle with no text: nothing more; if a reading was stopped in the
 *    same press, VIEW and idle.
 *  - A held context of another engine: RELEASE, then as with none held.
 *  - Held and ready: SPEAK (serial = ++r->serial), speaking, VIEW. Held, not
 *    ready: starting (waits for VRM_READY), VIEW. None held: START (out.engine),
 *    starting, VIEW.
 *  - on_started(1) in starting with no context: ctx = 1. AI: ready, SPEAK,
 *    speaking. Windows: not ready, stays starting. on_started(0): ERROR
 *    (err_engine = engine, hr 0), VIEW, idle. Anywhere else: nothing.
 *  - VRM_READY: held and not ready becomes ready; in starting it also SPEAKs
 *    and moves to speaking. Otherwise nothing (a ready after the reading
 *    stopped speaks nothing).
 *  - VRM_DONE: in speaking with reply_id == serial, VIEW and idle.
 *  - VRM_ERROR with no context, or reply_id != 0 && != serial (an older
 *    reply's): nothing. Otherwise RELEASE; if not idle also ERROR, VIEW, idle.
 *  - on_stop: when not idle STOP|VIEW and idle. VRX_PANEL, VRX_SETTINGS and
 *    VRX_SAMPLE also RELEASE a held context. No stop is ever an ERROR.
 */

typedef enum { VRS_IDLE = 0, VRS_STARTING, VRS_SPEAKING } VoiceReadState;
typedef enum { VRX_DICTATION = 1, VRX_ESCAPE, VRX_CHAT, VRX_PANEL, VRX_SETTINGS, VRX_SAMPLE } VoiceReadStop;
typedef enum { VRM_READY = 1, VRM_DONE, VRM_ERROR } VoiceReadMsg;
enum { VRA_START = 1u << 0, VRA_SPEAK = 1u << 1, VRA_STOP = 1u << 2,
       VRA_RELEASE = 1u << 3, VRA_ERROR = 1u << 4, VRA_VIEW = 1u << 5 };

typedef struct {
    VoiceReadState state;
    int item_id;      /* the button row being read; 0 when idle */
    unsigned serial;  /* reply id of the latest speak; never reused; 0 before the first */
    int ctx;          /* 1 while an engine context is held (kept between readings) */
    int engine;       /* VOICE_SPEECH_* of the held context, or of the one starting */
    int ready;        /* the held context takes a speak now (AI at once, Windows after VRM_READY) */
} VoiceRead;

typedef struct { int item_id; int engine; int gate; int has_text; } VoiceReadPress;
typedef struct { unsigned actions; int engine; unsigned serial; int err_engine; long hr; } VoiceReadOut;

void voice_read_init(VoiceRead *r);
int  voice_read_shown(int voice_active, int mode);
int  voice_read_gate(int voice_active, int mode, int recording);
VoiceReadOut voice_read_on_press(VoiceRead *r, const VoiceReadPress *p);
VoiceReadOut voice_read_on_started(VoiceRead *r, int ok);
VoiceReadOut voice_read_on_engine(VoiceRead *r, VoiceReadMsg kind, unsigned reply_id, long hr);
VoiceReadOut voice_read_on_stop(VoiceRead *r, VoiceReadStop why);
void voice_read_error_text(int engine, long hr, const char *resolve_err, char *out, size_t cap);

#endif
