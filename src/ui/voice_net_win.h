#ifndef NUTSHELL_VOICE_NET_WIN_H
#define NUTSHELL_VOICE_NET_WIN_H

#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>

/*
 * voice_net_win -- the one way voice reaches the network. Every voice request
 * (the speech-to-text upload, the live WebSocket, later speech playback) gets
 * its WinHTTP session from voice_net_open(), which refuses while voice is off
 * and counts each session it hands out. The exe's --self-report prints the
 * count (`voice-net-opens: N`), so an idle launch can be shown to open none.
 */

typedef enum {
    VNK_STT_UPLOAD = 1,
    VNK_LIVE_WS    = 2,
    VNK_TTS        = 3    /* a seam for a later change: declared, unused */
} VoiceNetKind;

/* voice_gate_on() or NULL with ERROR_ACCESS_DENIED; counts the open;
 * ns_winhttp_open(L"Nutshell/1.0") (which also checks the AI gate). A failed
 * open is not counted. Close with WinHttpCloseHandle(). */
HINTERNET voice_winhttp_open(void);

/* The one voice entry point: gate check, a DLOG line naming the kind (never any
 * content), then voice_winhttp_open(). */
HINTERNET voice_net_open(VoiceNetKind k);

/* Sessions handed out so far (an interlocked read; any thread). */
long voice_net_open_count(void);

/* TEST-ONLY seam: zero the count. */
void voice_net_test_reset_count(void);

#endif /* NUTSHELL_VOICE_NET_WIN_H */
