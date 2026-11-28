#ifndef NUTSHELL_VOICE_READ_WIN_H
#define NUTSHELL_VOICE_READ_WIN_H

#include <windows.h>
#include <stddef.h>
#include "config.h"
#include "voice_read.h"
#include "voice_ptt.h"
#include "voice_tts_win.h"

/*
 * voice_read_win -- the Read aloud reader's Win32 controller. UI thread only.
 * It drives the pure state machine in src/core/voice_read.c through an ops
 * table (the real one, in voice_win.c, reuses the speech router; the wintest
 * installs fakes), holds the text of the turn being read, and is the only
 * caller of the engine start op: voice_read_win_press refuses unless the gate
 * says yes, so nothing of speech exists before the first press or while Voice
 * is off.
 *
 * Late messages of a released context: a Windows-speech VTT_ERROR carries
 * reply id 0 and VTT_READY none, so the machine cannot tell an old context's
 * from the new one's. The controller does: it records the context's
 * generation when the start succeeds and drops every message of another one;
 * the held context's own VTT_EXITED counts as an engine error (reply id 0).
 */

#define VOICE_READ_TOKEN 0x7FFFFFF3u   /* beside VOICE_TTS_SAMPLE_TOKEN and VOICE_AIS_SAMPLE_TOKEN */

typedef struct {
    int   (*start)(unsigned token, int engine);       /* 1 when a context runs under token */
    void  (*speak)(unsigned token, unsigned id, const char *text, size_t from);
    void  (*stop)(unsigned token);
    void  (*quit)(unsigned token);                     /* stop, release and join */
    char *(*turn_text)(int item_id);                   /* malloc'd; NULL: the row carries no button now */
    void  (*view)(HWND panel, int on, int reading_id, int disabled);   /* panel NULL: the current one */
    void  (*note)(const char *text);                   /* an error row in the chat */
    void  (*binding)(VoiceBinding *out);
    int   (*resolve_error)(char *out, size_t cap);     /* 1 with AI speech's resolve reason */
    /* The generation (VoiceTtsMsg.gen) of the context just started under
     * token. REQUIRED: without it (NULL) a press is refused before start, and
     * 0 after a successful start quits that context and counts as a refused
     * start. Messages of any other generation are dropped. */
    unsigned (*context_gen)(unsigned token);
} VoiceReadOps;

void voice_read_win_set_ops(const VoiceReadOps *ops);       /* NULL detaches; resets the machine */
void voice_read_win_press(const Settings *s, int item_id, int recording);   /* the only start */
void voice_read_win_stop(VoiceReadStop why);
void voice_read_win_on_tts(const VoiceTtsMsg *m);           /* observes; never frees or consumes */
void voice_read_win_apply(const Settings *s, int running);  /* after every voice_feature_apply */
void voice_read_win_set_recording(int on);
void voice_read_win_chat_check(void);                       /* VRX_CHAT when the binding changed */
int  voice_read_win_active(void);                           /* 1 while a reading runs */
void voice_read_win_view_sync(HWND panel);

#endif
