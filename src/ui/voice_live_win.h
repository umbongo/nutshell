#ifndef NUTSHELL_VOICE_LIVE_WIN_H
#define NUTSHELL_VOICE_LIVE_WIN_H

#include <stddef.h>
#include <stdint.h>
#include <windows.h>
#include "voice_live.h"

/*
 * voice_live_win -- the live transcription session: one WebSocket to the
 * provider, a receiver thread and a sender thread, fed by the audio thread
 * through a locked queue that runs the speech gate and (for 24 kHz providers)
 * the resampler. Parsed provider events go to the UI as WM_VOICE_LIVE.
 *
 * Threads: the receiver connects (voice_ws_connect, through the gate), waits
 * for session.created, hands the session message to the sender, posts
 * VLE_READY on session.updated, then posts every parsed event. The sender
 * sends the session message, waits for ready, then drains the feed (audio in
 * VOICE_LIVE_CHUNK_MS chunks, gate-closed and end markers in order). A close
 * is done by the sender (voice_ws_shutdown, then voice_ws_abort when that
 * fails) so the UI thread never blocks.
 *
 * Ledger (UI thread): VRES_LIVE_SESSION from start to close; VRES_LIVE_THREAD
 * once per thread from start to reap (or detach).
 */

#define WM_VOICE_LIVE (WM_APP + 0x42)   /* lParam = VoiceLiveMsg*, receiver frees with voice_live_msg_free */

/* ev.kind VLE_DONE is the "final" (ev.text NULL for OpenAI, the transcript of record for
 * Mistral); VLE_CLOSED means the link ended without a final (ev.text may hold a reason). */
typedef struct { unsigned token; VoiceLiveEvent ev; } VoiceLiveMsg;
void voice_live_msg_free(VoiceLiveMsg *m);

/* UI thread. Starts a session for recording `token` on target t with the key (copied, wiped
 * at the end). 0 with err when it cannot start. */
int  voice_live_start(HWND notify, const VoiceLiveTarget *t, const char *key, unsigned token,
                      char *err, size_t errcap);

/* Audio thread: ignored unless `token` is the running session's. Never blocks on the network. */
void voice_live_feed(unsigned token, const int16_t *s, size_t n);
/* After the last samples: gate flush, then the end message(s). */
void voice_live_feed_end(unsigned token);

/* UI thread: ask the session to end; returns at once. Releases VRES_LIVE_SESSION. Nothing
 * is posted for the session after this. */
void voice_live_close(unsigned token);

/* UI thread: join the closed sessions' threads (wait_ms 0: only those already done). With
 * wait_ms > 0 (voice off, shutdown) the current session is closed first; a session still
 * running after wait_ms gets voice_ws_abort and is detached (its threads free it). Releases
 * VRES_LIVE_THREAD per joined or detached thread. */
void voice_live_reap(DWORD wait_ms);

/* TEST-ONLY seam, not for product code: samples (and markers) queued for the current session. */
size_t voice_live_test_queued(void);

#endif /* NUTSHELL_VOICE_LIVE_WIN_H */
