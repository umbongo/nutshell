#include "voice_ledger.h"

static int g_count[VRES_COUNT];
static int g_underflow;

void voice_ledger_reset(void)
{
    for (int i = 0; i < (int)VRES_COUNT; i++) g_count[i] = 0;
    g_underflow = 0;
}

void voice_ledger_acquire(VoiceRes r)
{
    if ((int)r < 0 || (int)r >= (int)VRES_COUNT) return;
    g_count[r]++;
}

void voice_ledger_release(VoiceRes r)
{
    if ((int)r < 0 || (int)r >= (int)VRES_COUNT) return;
    if (g_count[r] > 0) g_count[r]--;
    else g_underflow = 1;
}

int voice_ledger_count(VoiceRes r)
{
    if ((int)r < 0 || (int)r >= (int)VRES_COUNT) return 0;
    return g_count[r];
}

int voice_ledger_all_zero(void)
{
    for (int i = 0; i < (int)VRES_COUNT; i++)
        if (g_count[i] != 0) return 0;
    return 1;
}

int voice_ledger_underflow(void)
{
    return g_underflow;
}
