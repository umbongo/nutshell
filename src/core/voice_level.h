#ifndef NUTSHELL_VOICE_LEVEL_H
#define NUTSHELL_VOICE_LEVEL_H

#include <stdint.h>

/* Level-meter maths for the Settings microphone check: dBFS, ballistics,
 * peak hold, a 1 s window and the zone a level falls in. Pure, no Win32. */

#define VOICE_METER_FLOOR_DB   (-60.0)
/* Zones, dBFS of the peak. A peak below -45 cannot pass the silence gate
 * (speech threshold RMS 250, about -42 dBFS), so nobody is speaking. */
#define VOICE_LEVEL_SILENT_DB  (-45.0)
#define VOICE_LEVEL_GOOD_LO_DB (-18.0)
#define VOICE_LEVEL_GOOD_HI_DB (-6.0)
#define VOICE_LEVEL_LOUD_DB    (-3.0)

#define VOICE_METER_RELEASE_DB_PER_S 20.0
#define VOICE_METER_HOLD_MS          1000u
#define VOICE_METER_SLOTS            20
#define VOICE_METER_SLOT_MS          50u

typedef enum { VLEVEL_SILENT, VLEVEL_QUIET, VLEVEL_GOOD, VLEVEL_HOT, VLEVEL_LOUD } VoiceLevelClass;

typedef struct {
    double   level_db;
    double   hold_db;
    uint32_t hold_age_ms;
    double   win_db[VOICE_METER_SLOTS];
    int      win_pos;
    uint32_t win_acc_ms;
} VoiceMeter;

/* peak 0..32768 -> dBFS, floored at VOICE_METER_FLOOR_DB. */
double voice_dbfs(int peak);
VoiceLevelClass voice_level_classify(double peak_db);
/* UTF-8; "Too quiet \xE2\x80\x94 raise the gain" uses an em dash. */
const char *voice_level_label(VoiceLevelClass c);

void voice_meter_reset(VoiceMeter *m);
/* Fast attack, 20 dB/s release, hold tick 1000 ms then 20 dB/s, and a 1 s
 * window of 20 x 50 ms slots each holding its max. */
void voice_meter_feed(VoiceMeter *m, int peak, uint32_t elapsed_ms);
double voice_meter_window_peak_db(const VoiceMeter *m);
/* (db - floor) / (0 - floor), clamped to [0,1]. */
double voice_meter_frac(double db);

#endif
