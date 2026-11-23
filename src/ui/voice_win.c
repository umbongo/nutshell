#include "voice_win.h"

#include "opt_feature.h"
#include "settings_dlg.h"
#include "voice_audio_win.h"
#include "voice_gate.h"
#include "voice_stt_win.h"
#include <stdlib.h>

static int g_audio_up;   /* the audio thread was started by voice_feature_apply */

void voice_feature_apply(HWND main)
{
    const Settings *s = ui_current_settings();
    int on = (s != NULL) ? opt_feature_active(s, OPT_VOICE) : 0;
    voice_gate_set_enabled(on);
    if (on) {
        /* Idempotent: it is called more than once at startup. */
        if (!g_audio_up && voice_audio_start(main)) g_audio_up = 1;
    } else {
        voice_stt_cancel_all();   /* an upload in flight is cancelled and joined */
        if (g_audio_up) {
            voice_audio_stop();
            g_audio_up = 0;
        }
    }
}

void voice_feature_shutdown(void)
{
    voice_gate_set_enabled(0);
    voice_stt_cancel_all();
    if (g_audio_up) {
        voice_audio_stop();
        g_audio_up = 0;
    }
}

int voice_feature_running(void)
{
    return g_audio_up;
}

void voice_on_audio_msg(WPARAM wp, LPARAM lp)
{
    /* An open Settings page takes the device list, levels and errors; what
     * nobody wants is freed here. */
    if (settings_voice_on_audio(wp, lp)) return;
    if (wp == VAM_DEVICES) voice_audio_free_devices((VoiceDeviceList *)lp);
    else if (wp == VAM_CAPTURED) voice_audio_free_capture((VoiceCapture *)lp);
}

void voice_on_stt(LPARAM lp)
{
    free((VoiceSttResult *)lp);
}
