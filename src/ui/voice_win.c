#include "voice_win.h"

#include "opt_feature.h"
#include "voice_gate.h"

void voice_feature_apply(HWND main)
{
    (void)main;
    const Settings *s = ui_current_settings();
    voice_gate_set_enabled(s != NULL ? opt_feature_active(s, OPT_VOICE) : 0);
}

int voice_feature_running(void)
{
    return 0;   /* nothing of voice runs yet: the controller only sets the gate */
}
